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
#define MAX_TIMEOUT_COUNT 5
#define TIMEOUT_SEC 1

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
	int aggregate_output;
	int test_id;
	int device_count;
	int inotify_fd;
	int list_devices;
	char test_name[MAX_STR_LEN];
	char sysfs_prefix[MAX_SYSFS_PATH];
	char debugfs_prefix[MAX_SYSFS_PATH];
	struct loopback_device devices[MAX_NUM_DEVICES];
	struct loopback_results aggregate_results;
};
struct loopback_test t;

/* Helper macros to calculate the aggregate results for all devices */
static inline int device_enabled(struct loopback_test *t, int dev_idx);

#define GET_MAX(field)							\
static int get_##field##_aggregate(struct loopback_test *t)		\
{									\
	uint32_t max = 0;						\
	int i;								\
	for (i = 0; i < t->device_count; i++) {				\
		if (!device_enabled(t, i))				\
			continue;					\
		if (t->devices[i].results.field > max)			\
			max = t->devices[i].results.field;		\
	}								\
	return max;							\
}									\

#define GET_MIN(field)							\
static int get_##field##_aggregate(struct loopback_test *t)		\
{									\
	uint32_t min = ~0;						\
	int i;								\
	for (i = 0; i < t->device_count; i++) {				\
		if (!device_enabled(t, i))				\
			continue;					\
		if (t->devices[i].results.field < min)			\
			min = t->devices[i].results.field;		\
	}								\
	return min;							\
}									\

#define GET_AVG(field)							\
static int get_##field##_aggregate(struct loopback_test *t)		\
{									\
	uint32_t val = 0;						\
	uint32_t count = 0;						\
	int i;								\
	for (i = 0; i < t->device_count; i++) {				\
		if (!device_enabled(t, i))				\
			continue;					\
		count++;						\
		val += t->devices[i].results.field;			\
	}								\
	if (count)							\
		val /= count;						\
	return val;							\
}									\

GET_MAX(throughput_max);
GET_MAX(request_max);
GET_MAX(latency_max);
GET_MAX(apbridge_unipro_latency_max);
GET_MAX(gpbridge_firmware_latency_max);
GET_MIN(throughput_min);
GET_MIN(request_min);
GET_MIN(latency_min);
GET_MIN(apbridge_unipro_latency_min);
GET_MIN(gpbridge_firmware_latency_min);
GET_AVG(throughput_avg);
GET_AVG(request_avg);
GET_AVG(latency_avg);
GET_AVG(apbridge_unipro_latency_avg);
GET_AVG(gpbridge_firmware_latency_avg);

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
	"   -a     aggregate - show aggregation of all enabled devies\n"
	"   -l     list found loopback devices and exit.\n"
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

static void show_loopback_devices(struct loopback_test *t)
{
	int i;

	if (t->device_count == 0) {
		printf("No loopback devices.\n");
		return;
	}

	for (i = 0; i < t->device_count; i++)
		printf("device[%d] = %s\n", i, t->devices[i].name);

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

static int get_results(struct loopback_test *t)
{
	struct loopback_device *d;
	struct loopback_results *r;
	int i;

	for (i = 0; i < t->device_count; i++) {
		if (!device_enabled(t, i))
			continue;

		d = &t->devices[i];
		r = &d->results;

		r->error = read_sysfs_int(d->sysfs_entry, "error");
		r->request_min = read_sysfs_int(d->sysfs_entry, "requests_per_second_min");
		r->request_max = read_sysfs_int(d->sysfs_entry, "requests_per_second_max");
		r->request_avg = read_sysfs_float(d->sysfs_entry, "requests_per_second_avg");

		r->latency_min = read_sysfs_int(d->sysfs_entry, "latency_min");
		r->latency_max = read_sysfs_int(d->sysfs_entry, "latency_max");
		r->latency_avg = read_sysfs_float(d->sysfs_entry, "latency_avg");

		r->throughput_min = read_sysfs_int(d->sysfs_entry, "throughput_min");
		r->throughput_max = read_sysfs_int(d->sysfs_entry, "throughput_max");
		r->throughput_avg = read_sysfs_float(d->sysfs_entry, "throughput_avg");

		r->apbridge_unipro_latency_min =
			read_sysfs_int(d->sysfs_entry, "apbridge_unipro_latency_min");
		r->apbridge_unipro_latency_max =
			read_sysfs_int(d->sysfs_entry, "apbridge_unipro_latency_max");
		r->apbridge_unipro_latency_avg =
			read_sysfs_float(d->sysfs_entry, "apbridge_unipro_latency_avg");

		r->gpbridge_firmware_latency_min =
			read_sysfs_int(d->sysfs_entry, "gpbridge_firmware_latency_min");
		r->gpbridge_firmware_latency_max =
			read_sysfs_int(d->sysfs_entry, "gpbridge_firmware_latency_max");
		r->gpbridge_firmware_latency_avg =
			read_sysfs_float(d->sysfs_entry, "gpbridge_firmware_latency_avg");

		r->request_jitter = r->request_max - r->request_min;
		r->latency_jitter = r->latency_max - r->latency_min;
		r->throughput_jitter = r->throughput_max - r->throughput_min;
		r->apbridge_unipro_latency_jitter =
			r->apbridge_unipro_latency_max - r->apbridge_unipro_latency_min;
		r->gpbridge_firmware_latency_jitter =
			r->gpbridge_firmware_latency_max - r->gpbridge_firmware_latency_min;

	}

	/*calculate the aggregate results of all enabled devices */
	if (t->aggregate_output) {
		r = &t->aggregate_results;

		r->request_min = get_request_min_aggregate(t);
		r->request_max = get_request_max_aggregate(t);
		r->request_avg = get_request_avg_aggregate(t);

		r->latency_min = get_latency_min_aggregate(t);
		r->latency_max = get_latency_max_aggregate(t);
		r->latency_avg = get_latency_avg_aggregate(t);

		r->throughput_min = get_throughput_min_aggregate(t);
		r->throughput_max = get_throughput_max_aggregate(t);
		r->throughput_avg = get_throughput_avg_aggregate(t);

		r->apbridge_unipro_latency_min =
			get_apbridge_unipro_latency_min_aggregate(t);
		r->apbridge_unipro_latency_max =
			get_apbridge_unipro_latency_max_aggregate(t);
		r->apbridge_unipro_latency_avg =
			get_apbridge_unipro_latency_avg_aggregate(t);

		r->gpbridge_firmware_latency_min =
			get_gpbridge_firmware_latency_min_aggregate(t);
		r->gpbridge_firmware_latency_max =
			get_gpbridge_firmware_latency_max_aggregate(t);
		r->gpbridge_firmware_latency_avg =
			get_gpbridge_firmware_latency_avg_aggregate(t);

		r->request_jitter = r->request_max - r->request_min;
		r->latency_jitter = r->latency_max - r->latency_min;
		r->throughput_jitter = r->throughput_max - r->throughput_min;
		r->apbridge_unipro_latency_jitter =
			r->apbridge_unipro_latency_max - r->apbridge_unipro_latency_min;
		r->gpbridge_firmware_latency_jitter =
			r->gpbridge_firmware_latency_max - r->gpbridge_firmware_latency_min;

	}

	return 0;
}

void log_csv_error(int len, int err)
{
	fprintf(stderr, "unable to write %d bytes to csv %s\n", len,
		strerror(err));
}

int format_output(struct loopback_test *t,
			struct loopback_results *r,
			const char *dev_name,
			char *buf, int buf_len,
			struct tm *tm)
{
	int len = 0;

	memset(buf, 0x00, buf_len);
	len = snprintf(buf, buf_len, "%u-%u-%u %u:%u:%u",
		       tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
		       tm->tm_hour, tm->tm_min, tm->tm_sec);

	if (t->porcelain) {
		len += snprintf(&buf[len], buf_len - len,
			"\n test:\t\t\t%s\n path:\t\t\t%s\n size:\t\t\t%u\n iterations:\t\t%u\n errors:\t\t%u\n",
			t->test_name,
			dev_name,
			t->size,
			t->iteration_max,
			r->error);

		len += snprintf(&buf[len], buf_len - len,
			" requests per-sec:\tmin=%u, max=%u, average=%f, jitter=%u\n",
			r->request_min,
			r->request_max,
			r->request_avg,
			r->request_jitter);

		len += snprintf(&buf[len], buf_len - len,
			" ap-throughput B/s:\tmin=%u max=%u average=%f jitter=%u\n",
			r->throughput_min,
			r->throughput_max,
			r->throughput_avg,
			r->throughput_jitter);
		len += snprintf(&buf[len], buf_len - len,
			" ap-latency usec:\tmin=%u max=%u average=%f jitter=%u\n",
			r->latency_min,
			r->latency_max,
			r->latency_avg,
			r->latency_jitter);
		len += snprintf(&buf[len], buf_len - len,
			" apbridge-latency usec:\tmin=%u max=%u average=%f jitter=%u\n",
			r->apbridge_unipro_latency_min,
			r->apbridge_unipro_latency_max,
			r->apbridge_unipro_latency_avg,
			r->apbridge_unipro_latency_jitter);

		len += snprintf(&buf[len], buf_len - len,
			" gpbridge-latency usec:\tmin=%u max=%u average=%f jitter=%u\n",
			r->gpbridge_firmware_latency_min,
			r->gpbridge_firmware_latency_max,
			r->gpbridge_firmware_latency_avg,
			r->gpbridge_firmware_latency_jitter);

	} else {
		len += snprintf(&buf[len], buf_len- len, ",%s,%s,%u,%u,%u",
			t->test_name, dev_name, t->size, t->iteration_max,
			r->error);

		len += snprintf(&buf[len], buf_len - len, ",%u,%u,%f,%u",
			r->request_min,
			r->request_max,
			r->request_avg,
			r->request_jitter);

		len += snprintf(&buf[len], buf_len - len, ",%u,%u,%f,%u",
			r->latency_min,
			r->latency_max,
			r->latency_avg,
			r->latency_jitter);

		len += snprintf(&buf[len], buf_len - len, ",%u,%u,%f,%u",
			r->throughput_min,
			r->throughput_max,
			r->throughput_avg,
			r->throughput_jitter);

		len += snprintf(&buf[len], buf_len - len, ",%u,%u,%f,%u",
			r->apbridge_unipro_latency_min,
			r->apbridge_unipro_latency_max,
			r->apbridge_unipro_latency_avg,
			r->apbridge_unipro_latency_jitter);

		len += snprintf(&buf[len], buf_len - len, ",%u,%u,%f,%u",
			r->gpbridge_firmware_latency_min,
			r->gpbridge_firmware_latency_max,
			r->gpbridge_firmware_latency_avg,
			r->gpbridge_firmware_latency_jitter);
	}

	printf("\n%s\n", buf);

	return len;
}

static int log_results(struct loopback_test *t)
{
	int fd, i, len, ret;
	struct tm tm;
	time_t local_time;
	mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
	char file_name[MAX_SYSFS_PATH];
	char data[CSV_MAX_LINE];

	local_time = time(NULL);
	tm = *localtime(&local_time);

	/*
	* file name will test_name_size_iteration_max.csv
	* every time the same test with the same parameters is run we will then
	* append to the same CSV with datestamp - representing each test
	* dataset.
	*/
	if (!t->porcelain) {
		snprintf(file_name, sizeof(file_name), "%s_%d_%d.csv",
			t->test_name, t->size, t->iteration_max);

		fd = open(file_name, O_WRONLY | O_CREAT | O_APPEND, mode);
		if (fd < 0) {
			fprintf(stderr, "unable to open %s for appendation\n", file_name);
			abort();
		}

	}
	for (i = 0; i < t->device_count; i++) {
		if (!device_enabled(t, i))
			continue;

		len = format_output(t, &t->devices[i].results,
					t->devices[i].name,
					data, sizeof(data), &tm);
		if (!t->porcelain) {
			ret = write(fd, data, len);
			if (ret == -1)
				fprintf(stderr, "unable to write %d bytes to csv.\n", len);
		}

	}


	if (t->aggregate_output) {
		len = format_output(t, &t->aggregate_results, "aggregate",
					data, sizeof(data), &tm);
		if (!t->porcelain) {
			ret = write(fd, data, len);
			if (ret == -1)
				fprintf(stderr, "unable to write %d bytes to csv.\n", len);
		}
	}

	if (!t->porcelain)
		close(fd);

	return 0;
}

int is_loopback_device(const char *path, const char *node)
{
	char file[MAX_SYSFS_PATH];

	snprintf(file, MAX_SYSFS_PATH, "%s%s/iteration_count", path, node);
	if (access(file, F_OK) == 0)
		return 1;
	return 0;
}

int find_loopback_devices(struct loopback_test *t)
{
	struct dirent **namelist;
	int i, n, ret;
	unsigned int bus_id, interface_id, bundle_id;
	struct loopback_device *d;

	n = scandir(t->sysfs_prefix, &namelist, NULL, alphasort);
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
		ret = sscanf(namelist[i]->d_name, "%u-%u.%u",
				&bus_id, &interface_id, &bundle_id);
		if (ret != 3)
			continue;

		if (!is_loopback_device(t->sysfs_prefix, namelist[i]->d_name))
			continue;

		if (t->device_count == MAX_NUM_DEVICES) {
			fprintf(stderr, "max number of devices reached!\n");
			break;
		}

		d = &t->devices[t->device_count++];
		snprintf(d->name, MAX_STR_LEN, "%u-%u.%u",
				bus_id, interface_id, bundle_id);

		snprintf(d->sysfs_entry, MAX_SYSFS_PATH, "%s%s/",
			t->sysfs_prefix, d->name);

		snprintf(d->debugfs_entry, MAX_SYSFS_PATH, "%sraw_latency_%s",
			t->debugfs_prefix, d->name);

		if (t->debug)
			printf("add %s %s\n", d->sysfs_entry,
				d->debugfs_entry);
	}

	ret = 0;
done:
	for (i = 0; i < n; i++)
		free(namelist[n]);
	free(namelist);
baddir:
	return ret;
}


static int register_for_notification(struct loopback_test *t)
{
	char buf[MAX_SYSFS_PATH];
	int i;

	t->inotify_fd = inotify_init();
	if (t->inotify_fd < 0) {
		fprintf(stderr, "inotify_init fail %s\n", strerror(errno));
		abort();
	}

	for (i = 0; i < t->device_count; i++) {
		if (!device_enabled(t, i))
			continue;

		snprintf(buf, sizeof(buf), "%s%s", t->devices[i].sysfs_entry,
			"iteration_count");

		t->devices[i].inotify_wd = inotify_add_watch(t->inotify_fd,
							buf, IN_MODIFY);
		if (t->devices[i].inotify_wd < 0) {
			fprintf(stderr, "inotify_add_watch %s fail %s\n",
				buf, strerror(errno));
			close(t->inotify_fd);
			abort();
		}
	}

	return 0;
}

static int unregister_for_notification(struct loopback_test *t)
{
	int i;
	int ret = 0;

	for (i = 0; i < t->device_count; i++) {
		if (!device_enabled(t, i))
			continue;

		ret = inotify_rm_watch(t->inotify_fd, t->devices[i].inotify_wd);
		if (ret) {
			fprintf(stderr, "inotify_rm_watch error.\n");
			return ret;
		}
	}

	close(t->inotify_fd);
	return 0;
}

static int is_complete(struct loopback_test *t)
{
	uint32_t iteration_count = 0;
	int i;

	for (i = 0; i < t->device_count; i++) {
		if (!device_enabled(t, i))
			continue;

		iteration_count = read_sysfs_int(t->devices[i].sysfs_entry,
						 "iteration_count");

		/* at least one device did not finish yet */
		if (iteration_count != t->iteration_max)
			return 0;
	}

	return 1;
}

static int wait_for_complete(struct loopback_test *t)
{
	int remaining_timeouts = MAX_TIMEOUT_COUNT;
	char buf[MAX_SYSFS_PATH];
	struct timeval timeout;
	fd_set read_fds;
	int ret;

	while (1) {
		/* Wait for change */
		timeout.tv_sec = TIMEOUT_SEC;
		timeout.tv_usec = 0;
		FD_ZERO(&read_fds);
		FD_SET(t->inotify_fd, &read_fds);
		ret = select(FD_SETSIZE, &read_fds, NULL, NULL, &timeout);
		if (ret < 0) {
			fprintf(stderr, "Select error.\n");
			return -1;
		}

		/* timeout - test may be finished.*/
		if (!FD_ISSET(t->inotify_fd, &read_fds)) {
			remaining_timeouts--;

			if (is_complete(t))
				return 0;

			if (!remaining_timeouts) {
				fprintf(stderr, "Too many timeouts\n");
				return -1;
			}
		}

		/* read to clear the event */
		ret = read(t->inotify_fd, buf, sizeof(buf));
	}

	return 0;
}

static void prepare_devices(struct loopback_test *t)
{
	int i;

	/* Cancel any running tests */
	for (i = 0; i < t->device_count; i++)
		write_sysfs_val(t->devices[i].sysfs_entry, "type", 0);


	for (i = 0; i < t->device_count; i++) {
		if (!device_enabled(t, i))
			continue;

		write_sysfs_val(t->devices[i].sysfs_entry, "us_wait", 0);

		/* Set operation size */
		write_sysfs_val(t->devices[i].sysfs_entry, "size", t->size);

		/* Set iterations */
		write_sysfs_val(t->devices[i].sysfs_entry, "iteration_max",
				t->iteration_max);

	}
}

static int start(struct loopback_test *t)
{
	int i;

	/* the test starts by writing test_id to the type file. */
	for (i = 0; i < t->device_count; i++) {
		if (!device_enabled(t, i))
			continue;

		write_sysfs_val(t->devices[i].sysfs_entry, "type", t->test_id);
	}

	return 0;
}


void loopback_run(struct loopback_test *t)
{
	int i;
	int ret;

	for (i = 0; i < sizeof(dict) / sizeof(struct dict); i++) {
		if (strstr(dict[i].name, t->test_name))
			t->test_id = dict[i].type;
	}
	if (!t->test_id) {
		fprintf(stderr, "invalid test %s\n", t->test_name);
		usage();
		return;
	}

	prepare_devices(t);

	ret = register_for_notification(t);
	if (ret)
		goto err;

	start(t);

	sleep(1);

	wait_for_complete(t);

	unregister_for_notification(t);

	get_results(t);

	log_results(t);

	return;

err:
	printf("Error running test\n");
	return;
}

static int sanity_check(struct loopback_test *t)
{
	int i;

	if (t->device_count == 0) {
		fprintf(stderr, "No loopback devices found\n");
		return -1;
	}

	for (i = 0; i < MAX_NUM_DEVICES; i++) {
		if (!device_enabled(t, i))
			continue;

		if (t->mask && !strcmp(t->devices[i].name, "")) {
			fprintf(stderr, "Bad device mask %x\n", (1 << i));
			return -1;
		}

	}


	return 0;
}
int main(int argc, char *argv[])
{
	int o, ret;
	char *sysfs_prefix = "/sys/bus/greybus/devices/";
	char *debugfs_prefix = "/sys/kernel/debug/gb_loopback/";

	memset(&t, 0, sizeof(t));

	while ((o = getopt(argc, argv, "t:s:i:S:D:m:v::d::r::p::a::l::")) != -1) {
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
		case 'a':
			t.aggregate_output = 1;
			break;
		case 'l':
			t.list_devices = 1;
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

	ret = find_loopback_devices(&t);
	if (ret)
		return ret;
	ret = sanity_check(&t);
	if (ret)
		return ret;

	if (t.list_devices) {
		show_loopback_devices(&t);
		return 0;
	}

	loopback_run(&t);

	return 0;
}
