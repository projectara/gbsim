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

#define MAX_NUM_DEVICES 50
#define MAX_SYSFS_PATH	0x200
#define CSV_MAX_LINE	0x1000
#define SYSFS_MAX_INT	0x20
#define MAX_STR_LEN	255

struct dict {
	char *name;
	int type;
};

static struct dict dict[] = {
	{"ping", 2},
	{"transfer", 3},
	{"sink", 4}
};

struct loopback_results {
	float latency_avg;
	uint32_t latency_max;
	uint32_t latency_min;
	uint32_t latency_jitter;

	float request_avg;
	uint32_t request_max;
	uint32_t request_min;
	uint32_t request_jitter;

	float throughput_avg;
	uint32_t throughput_max;
	uint32_t throughput_min;
	uint32_t throughput_jitter;

	float apbridge_unipro_latency_avg;
	uint32_t apbridge_unipro_latency_max;
	uint32_t apbridge_unipro_latency_min;
	uint32_t apbridge_unipro_latency_jitter;

	float gpbridge_firmware_latency_avg;
	uint32_t gpbridge_firmware_latency_max;
	uint32_t gpbridge_firmware_latency_min;
	uint32_t gpbridge_firmware_latency_jitter;

	uint32_t error;
};

struct loopback_device {
	char name[MAX_SYSFS_PATH];
	char sysfs_entry[MAX_SYSFS_PATH];
	char debugfs_entry[MAX_SYSFS_PATH];
	int inotify_wd;
	struct loopback_results results;
};

struct loopback_test {
	int verbose;
	int debug;
	int raw_data_dump;
	int porcelain;
	int mask;
	int size;
	int iteration_max;
	int test_id;
	int device_count;
	char test_name[MAX_STR_LEN];
	char sysfs_prefix[MAX_SYSFS_PATH];
	char debugfs_prefix[MAX_SYSFS_PATH];
	struct loopback_device devices[MAX_NUM_DEVICES];
};
struct loopback_test t;


void abort()
{
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
	"   -p     porcelain - when specified printout is in a user-friendly non-CSV format. This option suppresses writing to CSV file\n"
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

static inline int device_enabled(struct loopback_test *t, int dev_idx)
{
	if (!t->mask || (t->mask & (1 << dev_idx)))
		return 1;

	return 0;
}

int open_sysfs(const char *sys_pfx, const char *node, int flags)
{
	int fd;
	char path[MAX_SYSFS_PATH];

	snprintf(path, sizeof(path), "%s%s", sys_pfx, node);
	fd = open(path, flags);
	if (fd < 0) {
		fprintf(stderr, "unable to open %s\n", path);
		abort();
	}
	return fd;
}

int read_sysfs_int_fd(int fd, const char *sys_pfx, const char *node)
{
	char buf[SYSFS_MAX_INT];

	if (read(fd, buf, sizeof(buf)) < 0) {
		fprintf(stderr, "unable to read from %s%s %s\n", sys_pfx, node,
			strerror(errno));
		close(fd);
		abort();
	}
	return atoi(buf);
}

float read_sysfs_float_fd(int fd, const char *sys_pfx, const char *node)
{
	char buf[SYSFS_MAX_INT];

	if (read(fd, buf, sizeof(buf)) < 0) {

		fprintf(stderr, "unable to read from %s%s %s\n", sys_pfx, node,
			strerror(errno));
		close(fd);
		abort();
	}
	return atof(buf);
}

int read_sysfs_int(const char *sys_pfx, const char *node)
{
	int fd, val;

	fd = open_sysfs(sys_pfx, node, O_RDONLY);
	val = read_sysfs_int_fd(fd, sys_pfx, node);
	close(fd);
	return val;
}

float read_sysfs_float(const char *sys_pfx, const char *node)
{
	int fd;
	float val;

	fd = open_sysfs(sys_pfx, node, O_RDONLY);
	val = read_sysfs_float_fd(fd, sys_pfx, node);
	close(fd);
	return val;
}

void write_sysfs_val(const char *sys_pfx, const char *node, int val)
{
	int fd, len;
	char buf[SYSFS_MAX_INT];

	fd = open_sysfs(sys_pfx, node, O_RDWR);
	len = snprintf(buf, sizeof(buf), "%d", val);
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

void __log_csv(struct loopback_test *t, struct loopback_device *d,
	       int fd, struct tm *tm, const char *dbgfs_entry)
{
	char buf[CSV_MAX_LINE];
	int error, fd_dev, len;
	float request_avg, latency_avg, throughput_avg;
	float apbridge_unipro_latency_avg, gpbridge_firmware_latency_avg;
	int request_min, request_max, request_jitter;
	int latency_min, latency_max, latency_jitter;
	int throughput_min, throughput_max, throughput_jitter;
	int apbridge_unipro_latency_min, apbridge_unipro_latency_max;
	int apbridge_unipro_latency_jitter;
	int gpbridge_firmware_latency_min, gpbridge_firmware_latency_max;
	int gpbridge_firmware_latency_jitter;
	unsigned int i;
	char rx_buf[SYSFS_MAX_INT];

	fd_dev = open(dbgfs_entry, O_RDONLY);
	if (fd_dev < 0) {
		fprintf(stderr, "unable to open specified device %s\n",
			dbgfs_entry);
		return;
	}

	/* gather data set */
	error = read_sysfs_int(d->sysfs_entry, "error");
	request_min = read_sysfs_int(d->sysfs_entry, "requests_per_second_min");
	request_max = read_sysfs_int(d->sysfs_entry, "requests_per_second_max");
	request_avg = read_sysfs_float(d->sysfs_entry, "requests_per_second_avg");
	latency_min = read_sysfs_int(d->sysfs_entry, "latency_min");
	latency_max = read_sysfs_int(d->sysfs_entry, "latency_max");
	latency_avg = read_sysfs_float(d->sysfs_entry, "latency_avg");
	throughput_min = read_sysfs_int(d->sysfs_entry, "throughput_min");
	throughput_max = read_sysfs_int(d->sysfs_entry, "throughput_max");
	throughput_avg = read_sysfs_float(d->sysfs_entry, "throughput_avg");
	apbridge_unipro_latency_min = read_sysfs_int(d->sysfs_entry, "apbridge_unipro_latency_min");
	apbridge_unipro_latency_max = read_sysfs_int(d->sysfs_entry, "apbridge_unipro_latency_max");
	apbridge_unipro_latency_avg = read_sysfs_float(d->sysfs_entry, "apbridge_unipro_latency_avg");
	gpbridge_firmware_latency_min = read_sysfs_int(d->sysfs_entry, "gpbridge_firmware_latency_min");
	gpbridge_firmware_latency_max = read_sysfs_int(d->sysfs_entry, "gpbridge_firmware_latency_max");
	gpbridge_firmware_latency_avg = read_sysfs_float(d->sysfs_entry, "gpbridge_firmware_latency_avg");

	/* derive jitter */
	request_jitter = request_max - request_min;
	latency_jitter = latency_max - latency_min;
	throughput_jitter = throughput_max - throughput_min;
	apbridge_unipro_latency_jitter = apbridge_unipro_latency_max - apbridge_unipro_latency_min;
	gpbridge_firmware_latency_jitter = gpbridge_firmware_latency_max - gpbridge_firmware_latency_min;

	/* append calculated metrics to file */
	memset(buf, 0x00, sizeof(buf));
	len = snprintf(buf, sizeof(buf), "%u-%u-%u %u:%u:%u",
		       tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
		       tm->tm_hour, tm->tm_min, tm->tm_sec);
	if (t->porcelain) {
		len += snprintf(&buf[len], sizeof(buf) - len,
			"\n test:\t\t\t%s\n path:\t\t\t%s\n size:\t\t\t%u\n iterations:\t\t%u\n errors:\t\t%u\n",
			t->test_name, d->sysfs_entry, t->size, t->iteration_max, error);
		len += snprintf(&buf[len], sizeof(buf) - len,
			" requests per-sec:\tmin=%u, max=%u, average=%f, jitter=%u\n",
			request_min, request_max, request_avg, request_jitter);
		len += snprintf(&buf[len], sizeof(buf) - len,
			" ap-throughput B/s:\tmin=%u max=%u average=%f jitter=%u\n",
			throughput_min, throughput_max, throughput_avg, throughput_jitter);
		len += snprintf(&buf[len], sizeof(buf) - len,
			" ap-latency usec:\tmin=%u max=%u average=%f jitter=%u\n",
			latency_min, latency_max, latency_avg, latency_jitter);
		len += snprintf(&buf[len], sizeof(buf) - len,
			" apbridge-latency usec:\tmin=%u max=%u average=%f jitter=%u\n",
			apbridge_unipro_latency_min,
			apbridge_unipro_latency_max, apbridge_unipro_latency_avg,
			apbridge_unipro_latency_jitter);
		len += snprintf(&buf[len], sizeof(buf) - len,
			" gpbridge-latency usec:\tmin=%u max=%u average=%f jitter=%u\n",
			gpbridge_firmware_latency_min,
			gpbridge_firmware_latency_max, gpbridge_firmware_latency_avg,
			gpbridge_firmware_latency_jitter);
	} else {
		len += snprintf(&buf[len], sizeof(buf) - len,
			",%s,%s,%u,%u,%u,%u,%u,%f,%u,%u,%u,%f,%u,%u,%u,%f,%u,%u,%u,%f,%u,%u,%u,%f,%u",
			t->test_name, d->sysfs_entry, t->size, t->iteration_max, error,
			request_min, request_max, request_avg, request_jitter,
			latency_min, latency_max, latency_avg, latency_jitter,
			throughput_min, throughput_max, throughput_avg,
			throughput_jitter, apbridge_unipro_latency_min,
			apbridge_unipro_latency_max, apbridge_unipro_latency_avg,
			apbridge_unipro_latency_jitter, gpbridge_firmware_latency_min,
			gpbridge_firmware_latency_max, gpbridge_firmware_latency_avg,
			gpbridge_firmware_latency_jitter);
		write(fd, buf, len);
	}

	/* print basic metrics to stdout - requested feature add */
	printf("\n%s\n", buf);

	/* Write raw latency times to CSV  */
	for (i = 0; i < t->iteration_max && t->raw_data_dump && !t->porcelain; i++) {
		memset(&rx_buf, 0x00, sizeof(rx_buf));
		len = read(fd_dev, rx_buf, sizeof(rx_buf));
		if (len < 0) {
			fprintf(stderr, "error reading %s %s\n",
				t->debugfs_prefix, strerror(errno));
			break;
		}
		lseek(fd_dev, SEEK_SET, 0);
		len = snprintf(buf, sizeof(buf), ",%s", rx_buf);
		if (write(fd, buf, len) != len) {
			log_csv_error(0, errno);
			break;
		}
	}
	if (!t->porcelain) {
		if (write(fd, "\n", 1) < 1)
			log_csv_error(1, errno);
	}

	/* skip printing large set to stdout just close open handles */
	close(fd_dev);

}

void log_csv(struct loopback_test *t)
{
	int fd, i;
	struct tm tm;
	time_t cur_time;
	mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
	char buf[MAX_SYSFS_PATH];

	cur_time = time(NULL);
	tm = *localtime(&cur_time);

	/*
	 * file name will test_name_size_iteration_max.csv
	 * every time the same test with the same parameters is run we will then
	 * append to the same CSV with datestamp - representing each test
	 * dataset.
	 */
	snprintf(buf, sizeof(buf), "%s_%d_%d.csv", t->test_name, t->size,
		 t->iteration_max);

	if (!t->porcelain) {
		fd = open(buf, O_WRONLY | O_CREAT | O_APPEND, mode);
		if (fd < 0) {
			fprintf(stderr, "unable to open %s for appendation\n", buf);
			abort();
		}
	}
	for (i = 0; i < t->device_count; i++) {
		if (!device_enabled(t, i))
			continue;

		__log_csv(t, &t->devices[i], fd, &tm, t->devices[i].debugfs_entry);
	}
	if (!t->porcelain)
		close(fd);
}

int construct_paths(struct loopback_test *t)
{
	struct dirent **namelist;
	int i, n, ret;
	unsigned int bus_id, interface_id, bundle_id;
	struct loopback_device *d;

	n = scandir(t->debugfs_prefix, &namelist, NULL, alphasort);
	if (n < 0) {
		perror("scandir");
		ret = -ENODEV;
		goto baddir;
	}

	/* Don't include '.' and '..' */
	if (n <= 2) {
		ret = -ENOMEM;
		goto done;
	}

	for (i = 0; i < n; i++) {
		if (!strstr(namelist[i]->d_name, "raw_latency_"))
			continue;
		ret = sscanf(namelist[i]->d_name,
				"raw_latency_%u-%u.%u",
				&bus_id, &interface_id,
				&bundle_id);
		if (ret == 3) {
			d = &t->devices[t->device_count++];

			snprintf(d->sysfs_entry, MAX_SYSFS_PATH,
					"%s%u-%u.%u/", t->sysfs_prefix,
					bus_id, interface_id, bundle_id);

			snprintf(d->debugfs_entry, MAX_SYSFS_PATH,
				"%s%s", t->debugfs_prefix,
				namelist[i]->d_name);

			if (t->debug)
				printf("add %s %s\n", d->sysfs_entry,
					d->debugfs_entry);
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

void loopback_run(struct loopback_test *t)
{
	char buf[MAX_SYSFS_PATH];
	char inotify_buf[0x800];
	char *sys_pfx = (char *)t->sysfs_prefix;
	fd_set fds;
	int i;
	int previous, err, iteration_count;
	int fd, wd, ret;
	struct timeval tv;

	if (construct_paths(t)) {
		fprintf(stderr, "unable to construct sysfs/dbgfs path names\n");
		usage();
		return;
	}
	sys_pfx = t->devices[0].sysfs_entry;

	for (i = 0; i < sizeof(dict) / sizeof(struct dict); i++) {
		if (strstr(dict[i].name, t->test_name))
			t->test_id = dict[i].type;
	}
	if (!t->test_id) {
		fprintf(stderr, "invalid test %s\n", t->test_name);
		usage();
		return;
	}

	/* Terminate any currently running test */
	write_sysfs_val(sys_pfx, "type", 0);

	/* Set parameter for no wait between messages */
	write_sysfs_val(sys_pfx, "us_wait", 0);

	/* Set operation size */
	write_sysfs_val(sys_pfx, "size", t->size);

	/* Set iterations */
	write_sysfs_val(sys_pfx, "iteration_max", t->iteration_max);

	/* Set mask of connections to include */
	write_sysfs_val(sys_pfx, "mask", t->mask);

	/* Initiate by setting loopback operation type */
	write_sysfs_val(sys_pfx, "type", t->test_id);
	sleep(1);

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
		iteration_count = read_sysfs_int(sys_pfx, "iteration_count");

		/* Validate data value is different */
		if (previous == iteration_count) {
			err = 1;
			break;
		} else if (iteration_count == t->iteration_max) {
			break;
		}
		previous = iteration_count;
		if (t->verbose) {
			printf("%02d%% complete %d of %d\r",
				100 * iteration_count / t->iteration_max,
				iteration_count, t->iteration_max);
			fflush(stdout);
		}
	}
	inotify_rm_watch(fd, wd);
	close(fd);

	if (err)
		printf("\nError executing test\n");
	else
		log_csv(t);
}

int main(int argc, char *argv[])
{
	int o;
	char *sysfs_prefix = "/sys/bus/greybus/devices/";
	char *debugfs_prefix = "/sys/kernel/debug/gb_loopback/";

	memset(&t, 0, sizeof(t));

	while ((o = getopt(argc, argv, "t:s:i:S:D:m:v::d::r::p::")) != -1) {
		switch (o) {
		case 't':
			snprintf(t.test_name, MAX_STR_LEN, "%s", optarg);
			break;
		case 's':
			t.size = atoi(optarg);
			break;
		case 'i':
			t.iteration_max = atoi(optarg);
			break;
		case 'S':
			snprintf(t.sysfs_prefix, MAX_SYSFS_PATH, "%s", optarg);
			break;
		case 'D':
			snprintf(t.debugfs_prefix, MAX_SYSFS_PATH, "%s", optarg);
			break;
		case 'm':
			t.mask = atol(optarg);
			break;
		case 'v':
			t.verbose = 1;
			break;
		case 'd':
			t.debug = 1;
			break;
		case 'r':
			t.raw_data_dump = 1;
			break;
		case 'p':
			t.porcelain = 1;
			break;
		default:
			usage();
			return -EINVAL;
		}
	}

	if (t.test_name == NULL || t.iteration_max == 0)
		usage();

	if (!strcmp(t.sysfs_prefix, ""))
		snprintf(t.sysfs_prefix, MAX_SYSFS_PATH, "%s", sysfs_prefix);

	if (!strcmp(t.debugfs_prefix, ""))
		snprintf(t.debugfs_prefix, MAX_SYSFS_PATH, "%s", debugfs_prefix);

	loopback_run(&t);

	return 0;
}
