/*
 * Loopback test application
 *
 * Copyright 2015 Google Inc.
 * Copyright 2015 Linaro Ltd.
 *
 * Provided under the three clause BSD license found in the LICENSE file.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/inotify.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <dirent.h>

#define MAX_SYSFS_PATH	0x200
#define CSV_MAX_LINE	0x1000
#define SYSFS_MAX_INT	0x20

struct dict {
	char *name;
	int type;
};

static struct dict dict[] = {
	{"ping", 2},
	{"transfer", 3},
	{"sink", 4}
};

struct loopback_names {
	char sysfs_entry[MAX_SYSFS_PATH];
	char dbgfs_entry[MAX_SYSFS_PATH];
	char *postfix;
	int module_node;
};
static struct loopback_names *lb_name = NULL;
static unsigned int lb_entries = 0;
static char *ctrl_path;
static char *dev = "dev";
static char *con = "con";

static int verbose;
static int debug;
static int raw_data_dump;

void abort()
{
	if (lb_name)
		free(lb_name);
	_exit(1);
}

void usage(void)
{
	fprintf(stderr, "Usage: loopback_test TEST [SIZE] ITERATIONS [SYSPATH] [DBGPATH]\n\n"
	"  Run TEST for a number of ITERATIONS with operation data SIZE bytes\n"
	"  TEST may be \'ping\' \'transfer\' or \'sink\'\n"
	"  SIZE indicates the size of transfer <= greybus max payload bytes\n"
	"  ITERATIONS indicates the number of times to execute TEST at SIZE bytes\n"
	"             Note if ITERATIONS is set to zero then this utility will\n"
	"             initiate an infinite (non terminating) test and exit\n"
	"             without logging any metrics data\n"
	"  SYSPATH indicates the sysfs path for the loopback greybus entries e.g.\n"
	"          /sys/bus/greybus/devices\n"
	"  DBGPATH indicates the debugfs path for the loopback greybus entries e.g.\n"
	"          /sys/kernel/debug/gb_loopback/\n"
	" Mandatory arguments\n"
	"   -t     must be one of the test names - sink, transfer or ping\n"
	"   -i     iteration count - the number of iterations to run the test over\n"
	" Optional arguments\n"
	"   -S     sysfs location - location for greybus 'endo' entires default /sys/bus/greybus/devices/\n"
	"   -D     debugfs location - location for loopback debugfs entries default /sys/kernel/debug/gb_loopback/\n"
	"   -s     size of data packet to send during test - defaults to zero\n"
	"   -m     mask - a bit mask of connections to include example: -m 8 = 4th connection -m 9 = 1st and 4th connection etc\n"
	"                 default is zero which means broadcast to all connections\n"
	"   -v     verbose output\n"
	"   -d     debug output\n"
	"   -r     raw data output - when specified the full list of latency values are included in the output CSV\n"
	"Examples:\n"
	"  Send 10000 transfers with a packet size of 128 bytes to all active connections\n"
	"  looptest -t transfer -s 128 -i 10000 -S /sys/bus/greybus/devices/ -D /sys/kernel/debug/gb_loopback/\n"
	"  looptest -t transfer -s 128 -i 10000 -m 0\n"
	"  Send 10000 transfers with a packet size of 128 bytes to connection 1 and 4\n"
	"  looptest -t transfer -s 128 -i 10000 -m 9\n"
	"  looptest -t ping -s 0 128 -i -S /sys/bus/greybus/devices/ -D /sys/kernel/debug/gb_loopback/\n"
	"  looptest -t sink -s 2030 -i 32768 -S /sys/bus/greybus/devices/ -D /sys/kernel/debug/gb_loopback/\n");
	abort();
}

int open_sysfs(const char *sys_pfx, const char *postfix, const char *node, int flags)
{
	extern int errno;
	int fd;
	char path[MAX_SYSFS_PATH];

	if (postfix)
		snprintf(path, sizeof(path), "%s%s_%s", sys_pfx, node, postfix);
	else
		snprintf(path, sizeof(path), "%s%s", sys_pfx, node);
	fd = open(path, flags);
	if (fd < 0) {
		fprintf(stderr, "unable to open %s\n", path);
		abort();
	}
	return fd;
}

int read_sysfs_int_fd(int fd, const char *sys_pfx, const char *postfix, const char *node)
{
	char buf[SYSFS_MAX_INT];

	if (read(fd, buf, sizeof(buf)) < 0) {
		if (postfix)
			fprintf(stderr, "unable to read from %s%s_%s %s\n", sys_pfx, node,
				postfix, strerror(errno));
		else
			fprintf(stderr, "unable to read from %s%s %s\n", sys_pfx, node,
				strerror(errno));
		close(fd);
		abort();
	}
	return atoi(buf);
}

float read_sysfs_float_fd(int fd, const char *sys_pfx, const char *postfix, const char *node)
{
	char buf[SYSFS_MAX_INT];

	if (read(fd, buf, sizeof(buf)) < 0) {
		if (postfix)
			fprintf(stderr, "unable to read from %s%s_%s %s\n", sys_pfx, node,
				postfix, strerror(errno));
		else
			fprintf(stderr, "unable to read from %s%s %s\n", sys_pfx, node,
				strerror(errno));
		close(fd);
		abort();
	}
	return atof(buf);
}

int read_sysfs_int(const char *sys_pfx, const char *postfix, const char *node)
{
	extern int errno;
	int fd, val;

	fd = open_sysfs(sys_pfx, postfix, node, O_RDONLY);
	val = read_sysfs_int_fd(fd, sys_pfx, postfix, node);
	close(fd);
	return val;
}

float read_sysfs_float(const char *sys_pfx, const char *postfix, const char *node)
{
	extern int errno;
	int fd;
	float val;

	fd = open_sysfs(sys_pfx, postfix, node, O_RDONLY);
	val = read_sysfs_float_fd(fd, sys_pfx, postfix, node);
	close(fd);
	return val;
}

void write_sysfs_val(const char *sys_pfx, const char *postfix, const char *node, int val)
{
	extern int errno;
	int fd, len;
	char buf[SYSFS_MAX_INT];

	fd = open_sysfs(sys_pfx, postfix, node, O_RDWR);
	len = snprintf(buf, sizeof(buf), "%d_%s", val, postfix);
	if (write(fd, buf, len) < 0) {
		fprintf(stderr, "unable to write to %s%s %s\n", sys_pfx, node,
			strerror(errno));
		close(fd);
		abort();
	}
	close(fd);
}

void log_csv_error(int len, int err)
{
	fprintf(stderr, "unable to write %d bytes to csv %s\n", len,
		strerror(err));
}

void __log_csv(const char *test_name, int size, int iteration_max,
	       int fd, struct tm *tm, const char *dbgfs_entry,
	       const char *sys_pfx, const char *postfix)
{
	char buf[CSV_MAX_LINE];
	extern int errno;
	int error, fd_dev, len;
	float request_avg, latency_avg, throughput_avg;
	int request_min, request_max, request_jitter;
	int latency_min, latency_max, latency_jitter;
	int throughput_min, throughput_max, throughput_jitter;
	unsigned int i;
	char rx_buf[SYSFS_MAX_INT];

	fd_dev = open(dbgfs_entry, O_RDONLY);
	if (fd_dev < 0) {
		fprintf(stderr, "unable to open specified device %s\n",
			dbgfs_entry);
		return;
	}

	/* gather data set */
	error = read_sysfs_int(sys_pfx, postfix, "error");
	request_min = read_sysfs_int(sys_pfx, postfix, "requests_per_second_min");
	request_max = read_sysfs_int(sys_pfx, postfix, "requests_per_second_max");
	request_avg = read_sysfs_float(sys_pfx, postfix, "requests_per_second_avg");
	latency_min = read_sysfs_int(sys_pfx, postfix, "latency_min");
	latency_max = read_sysfs_int(sys_pfx, postfix, "latency_max");
	latency_avg = read_sysfs_float(sys_pfx, postfix, "latency_avg");
	throughput_min = read_sysfs_int(sys_pfx, postfix, "throughput_min");
	throughput_max = read_sysfs_int(sys_pfx, postfix, "throughput_max");
	throughput_avg = read_sysfs_float(sys_pfx, postfix, "throughput_avg");

	/* derive jitter */
	request_jitter = request_max - request_min;
	latency_jitter = latency_max - latency_min;
	throughput_jitter = throughput_max - throughput_min;

	/* append calculated metrics to file */
	memset(buf, 0x00, sizeof(buf));
	len = snprintf(buf, sizeof(buf), "%u-%u-%u %u:%u:%u,",
		       tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
		       tm->tm_hour, tm->tm_min, tm->tm_sec);
	len += snprintf(&buf[len], sizeof(buf) - len,
			"%s,%s,%u,%u,%u,%u,%u,%f,%u,%u,%u,%f,%u,%u,%u,%f,%u",
			test_name, sys_pfx, size, iteration_max, error,
			request_min, request_max, request_avg, request_jitter,
			latency_min, latency_max, latency_avg, latency_jitter,
			throughput_min, throughput_max, throughput_avg,
			throughput_jitter);
	write(fd, buf, len);

	/* print basic metrics to stdout - requested feature add */
	printf("\n%s\n", buf);

	/* Write raw latency times to CSV  */
	for (i = 0; i < iteration_max && raw_data_dump; i++) {
		memset(&rx_buf, 0x00, sizeof(rx_buf));
		len = read(fd_dev, rx_buf, sizeof(rx_buf));
		if (len < 0) {
			fprintf(stderr, "error reading %s %s\n",
				dbgfs_entry, strerror(errno));
			break;
		}
		lseek(fd_dev, SEEK_SET, 0);
		len = snprintf(buf, sizeof(buf), ",%s", rx_buf);
		if (write(fd, buf, len) != len) {
			log_csv_error(0, errno);
			break;
		}
	}
	if (write(fd, "\n", 1) < 1)
		log_csv_error(1, errno);

	/* skip printing large set to stdout just close open handles */
	close(fd_dev);

}

void log_csv(const char *test_name, int size, int iteration_max,
	     const char *sys_pfx, uint32_t mask)
{
	int fd, j, i;
	struct tm tm;
	time_t t;
	mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
	char buf[MAX_SYSFS_PATH];

	t = time(NULL);
	tm = *localtime(&t);

	/*
	 * file name will test_name_size_iteration_max.csv
	 * every time the same test with the same parameters is run we will then
	 * append to the same CSV with datestamp - representing each test
	 * dataset.
	 */
	snprintf(buf, sizeof(buf), "%s_%d_%d.csv", test_name, size,
		 iteration_max);

	fd = open(buf, O_WRONLY|O_CREAT|O_APPEND, mode);
	if (fd < 0) {
		fprintf(stderr, "unable to open %s for appendation\n", buf);
		abort();
	}
	for (j = 0, i = 0; j < lb_entries; j++) {
		if (lb_name[j].module_node || mask & (1 << i) || (!mask))
			__log_csv(test_name, size, iteration_max, fd, &tm,
				  lb_name[j].dbgfs_entry,
				  lb_name[j].sysfs_entry,
				  lb_name[j].postfix);
		if (!lb_name[j].module_node)
			i++;
	}
	close(fd);
}

int construct_paths(const char *sys_pfx, const char *dbgfs_pfx)
{
	struct dirent **namelist;
	int i, n, ret, j;
	unsigned int module_id, interface_id, bundle_id, cport_id;

	n = scandir(dbgfs_pfx, &namelist, NULL, alphasort);
	if (n < 0) {
		perror("scandir");
		ret = -ENODEV;
		goto baddir;
	} else {
		/* Don't include '.' and '..' */
		lb_entries = n - 2;
		if (!lb_entries) {
			ret = -ENOMEM;
			goto done;
		}
		lb_name = malloc(lb_entries * sizeof(*lb_name));
		if (lb_name == NULL) {
			ret = -ENOMEM;
			goto done;
		}

		j = 0;
		for (i = 0; i < n; i++) {
			if (strstr(namelist[i]->d_name, "raw_latency_endo0")) {
				ret = sscanf(namelist[i]->d_name,
					     "raw_latency_endo0:%u:%u:%u:%u",
					     &module_id, &interface_id,
					     &bundle_id, &cport_id);
				if (ret == 4) {
					snprintf(lb_name[j].sysfs_entry, MAX_SYSFS_PATH,
						 "%sendo0:%u:%u:%u:%u/", sys_pfx,
						 module_id, interface_id,
						 bundle_id, cport_id);
					lb_name[j].postfix = con;
					lb_name[j].module_node = 0;
				} else {
					snprintf(lb_name[j].sysfs_entry, MAX_SYSFS_PATH,
						 "%sendo0/", sys_pfx);
					ctrl_path = lb_name[j].sysfs_entry;
					lb_name[j].postfix = dev;
					lb_name[j].module_node = 1;
				}
				snprintf(lb_name[j].dbgfs_entry, MAX_SYSFS_PATH,
					 "%s%s", dbgfs_pfx,
					 namelist[i]->d_name);
				if (debug)
					printf("add %s %s\n",
					       lb_name[j].dbgfs_entry,
					       lb_name[j].sysfs_entry);
				j++;

			}
		}
	}
	ret = 0;
done:
	for (i = 0; i < n; i++)
		free(namelist[n]);
	free(namelist);
baddir:
	return ret;
}

void loopback_run(const char *test_name, int size, int iteration_max,
		  const char *sys_prefix, const char *dbgfs_prefix,
		  uint32_t mask)
{
	char buf[MAX_SYSFS_PATH];
	char inotify_buf[0x800];
	char *sys_pfx = (char*)sys_prefix;
	extern int errno;
	fd_set fds;
	int test_id = 0;
	int i;
	int previous, err, iteration_count;
	int fd, wd, ret;
	struct timeval tv;

	if (construct_paths(sys_prefix, dbgfs_prefix)) {
		fprintf(stderr, "unable to construct sysfs/dbgfs path names\n");
		usage();
		return;
	}
	sys_pfx = ctrl_path;

	for (i = 0; i < sizeof(dict) / sizeof(struct dict); i++) {
		if (strstr(dict[i].name, test_name))
			test_id = dict[i].type;
	}
	if (!test_id) {
		fprintf(stderr, "invalid test %s\n", test_name);
		usage();
		return;
	}

	/* Terminate any currently running test */
	write_sysfs_val(sys_pfx, NULL, "type", 0);

	/* Set parameter for no wait between messages */
	write_sysfs_val(sys_pfx, NULL, "ms_wait", 0);

	/* Set operation size */
	write_sysfs_val(sys_pfx, NULL, "size", size);

	/* Set iterations */
	write_sysfs_val(sys_pfx, NULL, "iteration_max", iteration_max);

	/* Set mask of connections to include */
	write_sysfs_val(sys_pfx, NULL, "mask", mask);

	/* Initiate by setting loopback operation type */
	write_sysfs_val(sys_pfx, NULL, "type", test_id);
	sleep(1);

	if (iteration_max == 0) {
		printf("Infinite test initiated CSV won't be logged\n");
		return;
	}

	/* Setup for inotify on the sysfs entry */
	fd = inotify_init();
	if (fd < 0) {
		fprintf(stderr, "inotify_init fail %s\n", strerror(errno));
		abort();
	}
	snprintf(buf, sizeof(buf), "%s%s", sys_pfx, "iteration_count");
	wd = inotify_add_watch(fd, buf, IN_MODIFY);
	if (wd < 0) {
		fprintf(stderr, "inotify_add_watch %s fail %s\n",
			buf, strerror(errno));
		close(fd);
		abort();
	}

	previous = 0;
	err = 0;
	while (1) {
		/* Wait for change */
		tv.tv_sec = 1;
		tv.tv_usec = 0;
		FD_ZERO(&fds);
		FD_SET(fd, &fds);
		ret = select(fd + 1, &fds, NULL, NULL, &tv);

		if (ret > 0) {
			if (!FD_ISSET(fd, &fds)) {
				fprintf(stderr, "error - FD_ISSET fd=%d flase!\n",
					fd);
				break;
			}
			/* Read to clear the event */
			ret = read(fd, inotify_buf, sizeof(inotify_buf));
		}

		/* Grab the data */
		iteration_count = read_sysfs_int(sys_pfx, NULL, "iteration_count");

		/* Validate data value is different */
		if (previous == iteration_count) {
			err = 1;
			break;
		} else if (iteration_count == iteration_max) {
			break;
		}
		previous = iteration_count;
		if (verbose) {
			printf("%02d%% complete %d of %d\r",
				100 * iteration_count / iteration_max,
				iteration_count, iteration_max);
			fflush(stdout);
		}
	}
	inotify_rm_watch(fd, wd);
	close(fd);

	if (err)
		printf("\nError executing test\n");
	else
		log_csv(test_name, size, iteration_max, sys_pfx, mask);
}

int main(int argc, char *argv[])
{
	int o;
	char *test = NULL;
	int size = 0;
	uint32_t mask = 0;
	int iteration_count = 0;
	char *sysfs_prefix = "/sys/bus/greybus/devices/";
	char *debugfs_prefix = "/sys/kernel/debug/gb_loopback/";

	while ((o = getopt(argc, argv, "t:s:i:S:D:m:v::d::r::")) != -1) {
		switch (o) {
		case 't':
			test = optarg;
			break;
		case 's':
			size = atoi(optarg);
			break;
		case 'i':
			iteration_count = atoi(optarg);
			break;
		case 'S':
			sysfs_prefix = optarg;
			break;
		case 'D':
			debugfs_prefix = optarg;
			break;
		case 'm':
			mask = atol(optarg);
			break;
		case 'v':
			verbose = 1;
			break;
		case 'd':
			debug = 1;
			break;
		case 'r':
			raw_data_dump = 1;
			break;
		default:
			usage();
			return -EINVAL;
		}
	}

	if (test == NULL || iteration_count == 0)
		usage();

	loopback_run(test, size, iteration_count, sysfs_prefix, debugfs_prefix,
		     mask);
	if (lb_name)
		free(lb_name);
	return 0;
}
