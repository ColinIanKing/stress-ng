/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * This code is a complete clean re-write of the stress tool by
 * Colin Ian King <colin.king@canonical.com> and attempts to be
 * backwardly compatible with the stress tool by Amos Waterland
 * <apw@rossby.metr.ou.edu> but has more stress tests and more
 * functionality.
 *
 */
#include "stress-ng.h"

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
		pr_inf("%s stressor will be skipped, "
			"need to be running with CAP_SYS_ADMIN "
			"rights for this stressor\n", name);
		return -1;
	}
	return 0;
#else
	pr_inf("%s: stressor will be skipped, uprobe not available\n", name);
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
static int stress_uprobe_write(const char *path, int flags, const char *str)
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

	(void)snprintf(path, sizeof(path), "/proc/%d/maps", (int)pid);
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
		 *  name *libc-*.so found?
		 */
		if ((n == 8) && strstr(libc_path, "/libc-") &&
		    strstr(libc_path, ".so") && !strncmp(perm, "r-xp", 4)) {
			addr = (void *)(intptr_t)(start - offset);
			break;
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

	libc_addr = stress_uprobe_libc_start(pid, libc_path);
	if (!libc_addr) {
		pr_inf("%s: cannot find start of libc text section, skipping stressor\n",
			args->name);
		return EXIT_NO_RESOURCE;
	}
	offset = ((char *)getpid - (char *)libc_addr);

	/* Make unique event name */
	(void)snprintf(event, sizeof(event), "stressngprobe%d%" PRIu32,
		getpid(), args->instance);

	ret = stress_uprobe_write("/sys/kernel/debug/tracing/current_tracer",
		O_WRONLY | O_CREAT | O_TRUNC, "nop\n");
	(void)ret;
	(void)snprintf(buf, sizeof(buf), "p:%s %s:%p\n", event, libc_path, (void *)offset);
	ret = stress_uprobe_write("/sys/kernel/debug/tracing/uprobe_events",
		O_WRONLY | O_CREAT | O_APPEND, buf);
	if (ret < 0) {
		pr_inf("%s: cannot set uprobe_event: errno=%d (%s), skipping stressor\n",
			args->name, errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}

	/* Enable tracing */
	(void)snprintf(buf, sizeof(buf), "/sys/kernel/debug/tracing/events/uprobes/%s/enable", event);
	ret = stress_uprobe_write(buf, O_WRONLY | O_CREAT | O_TRUNC, "1\n");
	if (ret < 0) {
		pr_inf("%s: cannot enable uprobe_event: errno=%d (%s), skipping stressor\n",
			args->name, errno, strerror(errno));
		rc = EXIT_NO_RESOURCE;
		goto clear_events;
	}
	ret = stress_uprobe_write("/sys/kernel/debug/tracing/trace",
		O_WRONLY | O_CREAT | O_TRUNC, "\n");
	if (ret < 0) {
		pr_inf("%s: cannot clear trace file, errno=%d (%s), skipping stressor\n",
			args->name, errno, strerror(errno));
		rc = EXIT_NO_RESOURCE;
		goto clear_events;
	}

	fd = open("/sys/kernel/debug/tracing/trace_pipe", O_RDONLY);
	if (fd < 0) {
		pr_inf("%s: cannot open trace file: errno=%d (%s), skipping stressor\n",
			args->name, errno, strerror(errno));
		rc = EXIT_NO_RESOURCE;
		goto clear_events;
	}

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		/*
		 *  Generate trace events on each stress_get_cpu call
		 */
		int i;
		fd_set rfds;
		struct timeval tv;

		/* Generate some events */
		for (i = 0; i < 64; i++) {
			getpid();
		}

		while (keep_stressing(args)) {
			char data[4096];
			ssize_t n;
			char *ptr;

			FD_ZERO(&rfds);
			FD_SET(fd, &rfds);

			tv.tv_sec = 0;
			tv.tv_usec = 1000;
			ret = select(fd + 1, &rfds, NULL, NULL, &tv);

			if (ret <= 0)
				break;

			n = read(fd, data, sizeof(data));
			if (n <= 0)
				break;

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
				inc_counter(args);
				if (!keep_stressing(args))
					goto terminate;
			} while (ptr < data + sizeof(data));
		}
	} while (keep_stressing(args));

terminate:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	(void)close(fd);
	/* Stop events */
	ret = stress_uprobe_write("/sys/kernel/debug/tracing/events/uprobes/enable",
		O_WRONLY, "0\n");
	(void)ret;

clear_events:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	/* Remove uprobe */
	snprintf(buf, sizeof(buf), "-:%s\n", event);
	ret = stress_uprobe_write("/sys/kernel/debug/tracing/uprobe_events",
		O_WRONLY | O_APPEND, buf);
	(void)ret;

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
	.stressor = stress_not_implemented,
	.class = CLASS_CPU,
	.supported = stress_uprobe_supported,
	.help = help
};
#endif
