/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2025 Colin Ian King.
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
 */
#include "stress-ng.h"
#include "core-builtin.h"
#include "core-capabilities.h"
#include "core-mmap.h"

#if defined(__linux__)
typedef struct {
	char *name;
	bool timeout;
} stress_mem_info_t;

typedef struct {
	double online_duration;
	double online_count;
	double offline_duration;
	double offline_count;
} stress_memhotplug_metrics_t;
#endif

static const stress_help_t help[] = {
	{ NULL,	"memhotplug N",		"start N workers that exercise memory hotplug" },
	{ NULL, "memhotplug-mmap",	"enable random memory mapping/unmapping" },
	{ NULL,	"memhotplug-ops N",	"stop after N memory hotplug operations" },
	{ NULL,	NULL,			NULL }
};

static const stress_opt_t opts[] = {
	{ OPT_memhotplug_mmap, "memhotplug-mmap", TYPE_ID_BOOL, 0, 1, NULL },
	END_OPT,
};

#if defined(__linux__)
static const char sys_memory_path[] = "/sys/devices/system/memory";

static volatile bool do_jmp = false;
static sigjmp_buf jmp_env;
static uint64_t segv_count;
static void *mmap_ptr;
static size_t mmap_size;

/*
 *  stress_memhotplug_supported()
 *      check if we can run this as root
 */
static int stress_memhotplug_supported(const char *name)
{
	if (!stress_check_capability(SHIM_CAP_SYS_ADMIN)) {
		pr_inf_skip("%s stressor will be skipped, "
			"need to be running with CAP_SYS_ADMIN "
			"rights for this stressor\n", name);
		return -1;
	}
	return 0;
}

/*
 *  stress_memhotplug_munmap()
 *	unmap mmap'd region if it's valid
 */
static void stress_memhotplug_munmap(void)
{
	if ((mmap_ptr != MAP_FAILED) && (mmap_size > 0)) {
		(void)munmap(mmap_ptr, mmap_size);
		mmap_ptr = MAP_FAILED;
		mmap_size = 0;
	}
}

/*
 *  stress_memhotplug_mmap()
 *	exercise mmap to see if we can trip any activity
 *	that breaks mappings on hotplugged memory
 */
static void stress_memhotplug_mmap(void)
{
	const int flags = stress_mwc1() ? (MAP_ANONYMOUS | MAP_SHARED) :
				(MAP_ANONYMOUS | MAP_PRIVATE);
	mmap_size = 1024 * ((stress_mwc16() & 0x3ff) + 1);

	/*
	 *  need to catch any SEGVs and unmap in case the populate
	 *  or touching of pages are not mapped (being paranoid).
	 */
	do_jmp = true;
        if (sigsetjmp(jmp_env, 1)) {
		stress_memhotplug_munmap();
		return;
	}
	mmap_ptr = stress_mmap_populate(NULL, mmap_size, PROT_READ | PROT_WRITE, flags, -1, 0);
}


/*
 *  stress_memhotplug_removable()
 *	return true if memory can be hotplug removed
 */
static bool stress_memhotplug_removable(const char *name)
{
	char path[PATH_MAX];
	char buf[64];
	int val;

	(void)snprintf(path, sizeof(path), "%s/%s/removable",
		sys_memory_path, name);
	if (stress_system_read(path, buf, sizeof(buf)) < 0)
		return false;
	if (sscanf(buf, "%1d", &val) != 1)
		return false;
	if (val == 0)
		return false;
	return true;
}

static void stress_memhotplug_set_timer(const unsigned int secs)
{
	struct itimerval timer;

	(void)shim_memset(&timer, 0, sizeof(timer));
	timer.it_value.tv_sec = secs;
	timer.it_value.tv_usec = 0;
	timer.it_interval.tv_sec = secs;
	timer.it_interval.tv_usec = 0;
	(void)setitimer(ITIMER_PROF, &timer, NULL);
}

static void stress_memhotplug_mem_toggle(
	const bool memhotplug_mmap,
	stress_mem_info_t *mem_info,
	stress_memhotplug_metrics_t *metrics)
{
	char path[PATH_MAX];
	int fd;
	ssize_t n;
	double t;

	/*
	 *  Skip any hotplug memory regions that previously
	 *  timeout to avoid any repeated delays
	 */
	if (mem_info->timeout)
		return;

	if (!stress_memhotplug_removable(mem_info->name))
		return;

	(void)snprintf(path, sizeof(path), "%s/%s/state",
		sys_memory_path, mem_info->name);
	fd = open(path, O_RDWR | O_NONBLOCK);
	if (fd < 0)
		return;

	if (memhotplug_mmap)
		stress_memhotplug_mmap();

	stress_memhotplug_set_timer(5);
	t = stress_time_now();
	errno = 0;
	n = write(fd, "offline", 7);
	if (n < 0) {
		if (errno == EINTR)
			mem_info->timeout = true;
	} else {
		metrics->offline_duration = stress_time_now() - t;
		metrics->offline_count += 1.0;
	}

	if (memhotplug_mmap)
		stress_memhotplug_munmap();

	stress_memhotplug_set_timer(5);
	t = stress_time_now();
	errno = 0;
	n = write(fd, "online", 6);
	if (n >= 0) {
		metrics->online_duration = stress_time_now() - t;
		metrics->online_count += 1.0;
	}
	stress_memhotplug_set_timer(0);
	(void)close(fd);
}

static void stress_memhotplug_mem_online(stress_mem_info_t *mem_info)
{
	char path[PATH_MAX];
	int fd;
	ssize_t n;

	(void)snprintf(path, sizeof(path), "%s/%s/state",
		sys_memory_path, mem_info->name);
	fd = open(path, O_RDWR | O_NONBLOCK);
	if (fd < 0)
		return;

	stress_memhotplug_set_timer(5);
	errno = 0;
	n = write(fd, "online", 6);
	if (n < 0) {
		if (errno == EINTR)
			mem_info->timeout = true;
	}
	stress_memhotplug_set_timer(0);
	(void)close(fd);
}

static void stress_segv_handler(int signum)
{
	(void)signum;

	segv_count++;

	if (do_jmp) {
		siglongjmp(jmp_env, 1);         /* Ugly, bounce back */
		stress_no_return();
	}
}

/*
 *  stress_memhotplug()
 *	stress the linux memory hotplug subsystem
 */
static int stress_memhotplug(stress_args_t *args)
{
	DIR *dir;
	const struct dirent *d;
	stress_mem_info_t *mem_info;
	size_t i;
	NOCLOBBER size_t n = 0, max;
	stress_memhotplug_metrics_t metrics;
	struct sigaction old_action;
	double rate;
	bool memhotplug_mmap = false;

	if (stress_sighandler(args->name, SIGPROF, stress_sighandler_nop, NULL))
		return EXIT_NO_RESOURCE;
	if (stress_sighandler(args->name, SIGSEGV, stress_segv_handler, &old_action) < 0)
		return EXIT_NO_RESOURCE;

	(void)stress_get_setting("memhotplug-mmap", &memhotplug_mmap);

	dir = opendir(sys_memory_path);
	if (!dir) {
		if (stress_instance_zero(args))
			pr_inf_skip("%s: %s not accessible, skipping stressor\n",
				args->name, sys_memory_path);
		return EXIT_NOT_IMPLEMENTED;
	}

	/* Figure out number of potential hotplug memory regions */
	while ((d = readdir(dir)) != NULL) {
		if ((strncmp(d->d_name, "memory", 6) == 0) &&
		     stress_memhotplug_removable(d->d_name))
			n++;
	}
	if (n == 0) {
		if (stress_instance_zero(args))
			pr_inf_skip("%s: no hotplug memory entries found, skipping stressor\n",
				args->name);
		(void)closedir(dir);
		return EXIT_NOT_IMPLEMENTED;
	}
	rewinddir(dir);

	mem_info = (stress_mem_info_t *)calloc(n, sizeof(*mem_info));
	if (!mem_info) {
		pr_inf_skip("%s: failed to allocate %zu mem_info structs%s, skipping stressor\n",
			args->name, n, stress_get_memfree_str());
		(void)closedir(dir);
		return EXIT_NO_RESOURCE;
	}

	max = 0;
	while ((max < n) && ((d = readdir(dir)) != NULL)) {
		if ((strncmp(d->d_name, "memory", 6) == 0) &&
		     stress_memhotplug_removable(d->d_name)) {
			mem_info[max].name = shim_strdup(d->d_name);
			mem_info[max].timeout = false;
			max++;
		}
	}
	(void)closedir(dir);

	pr_dbg("%s: found %zu removable hotplug memory regions\n",
		args->name, max);

	metrics.offline_duration = 0.0;
	metrics.offline_count = 0.0;
	metrics.online_duration = 0.0;
	metrics.online_count = 0.0;

	mmap_ptr = MAP_FAILED;
	mmap_size = 0;
	segv_count = 0;

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

        if (sigsetjmp(jmp_env, 1))
		goto finish;
	do_jmp = true;

	do {
		bool ok = false;

		for (i = 0; LIKELY(stress_continue(args) && (i < max)); i++) {
			stress_memhotplug_mem_toggle(memhotplug_mmap, &mem_info[i], &metrics);
			if (!mem_info[i].timeout)
				ok = true;
			stress_bogo_inc(args);
		}
		if (!ok) {
			for (i = 0; i < max; i++)
				stress_memhotplug_mem_online(&mem_info[i]);
		}
	} while (stress_continue(args));

finish:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	do_jmp = false;
	(void)stress_sigrestore(args->name, SIGSEGV, &old_action);

	if (segv_count) {
		pr_dbg("%s: caught %" PRIu64 " unexpected SIGSEGVs\n",
			args->name, segv_count);
	}

	rate = (metrics.offline_count > 0.0) ? (double)metrics.offline_duration / metrics.offline_count : 0.0;
	if (rate > 0.0)
		stress_metrics_set(args, 0, "millisecs per offline action",
			rate * STRESS_DBL_MILLISECOND, STRESS_METRIC_HARMONIC_MEAN);
	rate = (metrics.online_count > 0.0) ? (double)metrics.online_duration / metrics.online_count : 0.0;
	if (rate > 0.0)
		stress_metrics_set(args, 1, "millisecs per online action",
			rate * STRESS_DBL_MILLISECOND, STRESS_METRIC_HARMONIC_MEAN);

	for (i = 0; i < max; i++)
		stress_memhotplug_mem_online(&mem_info[i]);
	for (i = 0; i < n; i++)
		free(mem_info[i].name);
	free(mem_info);

	return EXIT_SUCCESS;
}

const stressor_info_t stress_memhotplug_info = {
	.stressor = stress_memhotplug,
	.classifier = CLASS_OS,
	.opts = opts,
	.supported = stress_memhotplug_supported,
	.help = help
};
#else
const stressor_info_t stress_memhotplug_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_OS,
	.opts = opts,
	.help = help,
	.unimplemented_reason = "only supported on Linux"
};
#endif
