// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
 *
 */
#include "stress-ng.h"
#include "core-capabilities.h"

static const stress_help_t help[] = {
	{ NULL,	"uprobe N",	"start N workers that generate uprobe events" },
	{ NULL,	"uprobe-ops N",	"stop after N uprobe events" },
	{ NULL,	NULL,		NULL }
};

/*
 *  stress_uprobe_supported()
 *      check if we can run this with CAP_SYS_ADMIN capability
 */
static int stress_uprobe_supported(const char *name)
{
#if defined(__linux__)
	if (!stress_check_capability(SHIM_CAP_SYS_ADMIN)) {
		pr_inf_skip("%s stressor will be skipped, "
			"need to be running with CAP_SYS_ADMIN "
			"rights for this stressor\n", name);
		return -1;
	}
	return 0;
#else
	pr_inf_skip("%s: stressor will be skipped, uprobe not available\n", name);
	return -1;
#endif
}

#if defined(__linux__)
#define X_STR_(x) #x
#define X_STR(x) X_STR_(x)

/*
 *  stress_uprobe_write
 *     write to a uprobe sysfs file a string, open using flags setting in flags
 */
static int stress_uprobe_write(const char *path, const int flags, const char *str)
{
	int fd, rc = 0;

	fd = open(path, flags, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
	if (fd < 0)
		return -errno;
	errno = 0;
	if (write(fd, str, strlen(str)) < 0)
		rc = -errno;

	(void)close(fd);

	return rc;
}

/*
 *  stress_uprobe_libc_start()
 *	find start address of libc text segment by scanning /proc/$PID/maps
 */
static void *stress_uprobe_libc_start(const pid_t pid, char *libc_path)
{
	char path[PATH_MAX], perm[5], buf[1024];
	FILE *fp;
	uint64_t start, end, offset, dev_major, dev_minor, inode;
	void *addr = NULL;

	(void)snprintf(path, sizeof(path), "/proc/%" PRIdMAX "/maps", (intmax_t)pid);
	fp = fopen(path, "r");
	if (!fp)
		return addr;

	while (fgets(buf, sizeof(buf), fp)) {
		int n;

		n = sscanf(buf, "%" SCNx64 "-%" SCNx64 "%4s %" SCNx64 " %" SCNx64
			":%" SCNx64 " %" SCNu64 "%" X_STR(PATH_MAX) "s\n",
			&start, &end, perm, &offset, &dev_major, &dev_minor,
			&inode, libc_path);

		/*
		 *  name /libc-*.so or /libc.so found?
		 */
		if ((n == 8) && !strncmp(perm, "r-xp", 4) &&
		    strstr(libc_path, ".so")) {
			if (strstr(libc_path, "/libc-") ||
			    strstr(libc_path, "/libc.so")) {
				addr = (void *)(intptr_t)(start - offset);
				break;
			}
		}
	}
	(void)fclose(fp);

	return addr;
}

/*
 *  stress_uprobe()
 *	stress uprobe events
 */
static int stress_uprobe(const stress_args_t *args)
{
	char buf[PATH_MAX + 256], libc_path[PATH_MAX];
	int ret;
	char event[128];
	ptrdiff_t offset;
	void *libc_addr;
	int rc = EXIT_SUCCESS;
	int fd;
	pid_t pid = getpid();
	double t_start, duration = 0.0, bytes = 0.0, rate;

	libc_addr = stress_uprobe_libc_start(pid, libc_path);
	if (!libc_addr) {
		if (args->instance == 0)
			pr_inf_skip("%s: cannot find start of libc text section, skipping stressor\n",
				args->name);
		return EXIT_NO_RESOURCE;
	}
	offset = ((char *)getpid - (char *)libc_addr);

	/* Make unique event name */
	(void)snprintf(event, sizeof(event), "stressngprobe%d%" PRIu32,
		getpid(), args->instance);

	VOID_RET(int, stress_uprobe_write("/sys/kernel/debug/tracing/current_tracer",
		O_WRONLY | O_CREAT | O_TRUNC, "nop\n"));
	(void)snprintf(buf, sizeof(buf), "p:%s %s:%p\n", event, libc_path, (void *)offset);
	ret = stress_uprobe_write("/sys/kernel/debug/tracing/uprobe_events",
		O_WRONLY | O_CREAT | O_APPEND, buf);
	if (ret < 0) {
		pr_inf_skip("%s: cannot set uprobe_event: errno=%d (%s), skipping stressor\n",
			args->name, errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}

	/* Enable tracing */
	(void)snprintf(buf, sizeof(buf), "/sys/kernel/debug/tracing/events/uprobes/%s/enable", event);
	ret = stress_uprobe_write(buf, O_WRONLY | O_CREAT | O_TRUNC, "1\n");
	if (ret < 0) {
		pr_inf_skip("%s: cannot enable uprobe_event: errno=%d (%s), skipping stressor\n",
			args->name, errno, strerror(errno));
		rc = EXIT_NO_RESOURCE;
		goto clear_events;
	}
	ret = stress_uprobe_write("/sys/kernel/debug/tracing/trace",
		O_WRONLY | O_CREAT | O_TRUNC, "\n");
	if (ret < 0) {
		pr_inf_skip("%s: cannot clear trace file, errno=%d (%s), skipping stressor\n",
			args->name, errno, strerror(errno));
		rc = EXIT_NO_RESOURCE;
		goto clear_events;
	}

	fd = open("/sys/kernel/debug/tracing/trace_pipe", O_RDONLY);
	if (fd < 0) {
		pr_inf_skip("%s: cannot open trace file: errno=%d (%s), skipping stressor\n",
			args->name, errno, strerror(errno));
		rc = EXIT_NO_RESOURCE;
		goto clear_events;
	}

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	t_start = stress_time_now();
	do {
		/*
		 *  Generate trace events on each stress_get_cpu call
		 */
		int i;
		fd_set rfds;
#if defined(HAVE_SELECT)
		struct timeval tv;
#endif

		/* Generate some events */
		for (i = 0; i < 64; i++) {
			getpid();
		}

		while (stress_continue(args)) {
			char data[4096];
			ssize_t n;
			char *ptr;

			FD_ZERO(&rfds);
			FD_SET(fd, &rfds);

#if defined(HAVE_SELECT)
			tv.tv_sec = 0;
			tv.tv_usec = 1000;
			ret = select(fd + 1, &rfds, NULL, NULL, &tv);

			if (ret <= 0)
				break;
#endif

			n = read(fd, data, sizeof(data));
			if (n <= 0)
				break;
			bytes += (double)n;

			/*
			 *  Quick and dirty ubprobe event parsing,
			 *  this will undercount when text crosses
			 *  a read boundary, however, setting the read
			 *  size to be ~4K means we should always fill
			 *  the buffer and not get any misses.
			 */
			ptr = data;
			do {
				ptr = strstr(ptr, event);
				if (!ptr)
					break;
				ptr++;
				stress_bogo_inc(args);
				if (!stress_continue(args))
					goto terminate;
			} while (ptr < data + sizeof(data));
		}
	} while (stress_continue(args));
	duration = stress_time_now() - t_start;
	rate = (duration > 0.0) ? bytes / duration : 0.0;
	stress_metrics_set(args, 0, "MB trace data per second", rate / (double)MB);

terminate:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	(void)close(fd);
	/* Stop events */
	VOID_RET(int, stress_uprobe_write("/sys/kernel/debug/tracing/events/uprobes/enable",
		O_WRONLY, "0\n"));

clear_events:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	/* Remove uprobe */
	(void)snprintf(buf, sizeof(buf), "-:%s\n", event);
	VOID_RET(int, stress_uprobe_write("/sys/kernel/debug/tracing/uprobe_events",
		O_WRONLY | O_APPEND, buf));

	return rc;
}

stressor_info_t stress_uprobe_info = {
	.stressor = stress_uprobe,
	.class = CLASS_CPU,
	.supported = stress_uprobe_supported,
	.help = help
};
#else
stressor_info_t stress_uprobe_info = {
	.stressor = stress_unimplemented,
	.class = CLASS_CPU,
	.supported = stress_uprobe_supported,
	.help = help,
	.unimplemented_reason = "only supported on Linux"
};
#endif
