/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2025 Colin Ian King
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
#include "core-hash.h"
#include "core-killpid.h"
#include "core-mmap.h"
#include "core-pthread.h"
#include "core-prime.h"
#include "core-put.h"
#include "core-try-open.h"

#include <sys/ioctl.h>

#if defined(HAVE_LINUX_FS_H)
#include <linux/fs.h>
#else
UNEXPECTED
#endif

#if defined(HAVE_SYS_UTSNAME_H)
#include <sys/utsname.h>
#else
UNEXPECTED
#endif

#if defined(HAVE_POLL_H)
#include <poll.h>
#else
UNEXPECTED
#endif

static const stress_help_t help[] = {
	{ NULL,	"sysfs N",	"start N workers reading files from /sys" },
	{ NULL,	"sysfs-ops N",	"stop after sysfs bogo operations" },
	{ NULL,	NULL,		NULL }
};

#if defined(HAVE_LIB_PTHREAD) &&	\
    defined(__linux__)

#define SYS_BUF_SZ		(4096)
#define MAX_SYSFS_THREADS	(4)	/* threads stressing sysfs */
#define DRAIN_DELAY_US		(50000)	/* backoff in (us) microsecs */
#define DURATION_PER_SYSFS_FILE	(100000)	/* max duration per file in microsecs */
#define OPS_PER_SYSFS_FILE	(28)	/* max iterations per sysfs file */

static sigset_t set;
static shim_pthread_spinlock_t lock;
static shim_pthread_spinlock_t open_lock;
static shim_pthread_spinlock_t hash_lock;
static volatile bool drain_kmsg = false;
static volatile uint32_t counter = 0;
static const char signum_path[] = "/sys/kernel/notes";
static uint32_t os_release;
static stress_hash_table_t *sysfs_hash_table;
static uint64_t hash_items = 0;

typedef struct {
	stress_args_t *args;	/* stressor args */
	int kmsgfd;			/* /dev/kmsg file descriptor */
	bool sys_admin;			/* true if sys admin capable */
	char sysfs_path[PATH_MAX];	/* path to exercise */
	uint64_t sysfs_files_opened;	/* count of sysfs files opened */
} stress_ctxt_t;

typedef struct {
	const char *path;
	void (*const sysfs_func)(const char *path);
} stress_sysfs_wr_func_t;

static void stress_sysfs_sys_power_disk(const char *path)
{
	(void)stress_system_write(path, "test_resume", 11);
}

static const stress_sysfs_wr_func_t stress_sysfs_wr_funcs[] = {
#if defined(__linux__)
	{ "/sys/power/disk",	stress_sysfs_sys_power_disk },
#endif
};

static inline uint32_t path_sum(const char *path)
{
	return stress_hash_x17(path);
}

static int mixup_sort(const struct dirent **d1, const struct dirent **d2)
{
	const uint32_t s1 = path_sum((*d1)->d_name);
	const uint32_t s2 = path_sum((*d2)->d_name);

	if (s1 == s2)
		return 0;
	return (s1 < s2) ? -1 : 1;
}

/*
 *  stress_kmsg_drain()
 *	drain message buffer, return true if we need
 *	to drain lots of backed up messages because
 *	the stressor is spamming the kmsg log
 */
static bool stress_kmsg_drain(const int fd)
{
	int count = 0;

	if (UNLIKELY(fd == -1))
		return false;

	for (;;) {
		ssize_t ret;
		char buffer[1024];

		ret = read(fd, buffer, sizeof(buffer));
		if (ret <= 0)
			break;

		count += ret;
	}
	return count > 256;
}

/*
 *  stress_sys_add_bad()
 *	add a path onto the bad (omit) hash table
 */
static inline void stress_sys_add_bad(const char *path)
{
	if (shim_pthread_spin_lock(&hash_lock))
		return;	/* Can't lock! */
	if (!stress_hash_add(sysfs_hash_table, path))
		hash_items++;
	(void)shim_pthread_spin_unlock(&hash_lock);
}

/*
 *  stress_sys_bad()
 *	find str in hash table, if non-null it's bad
 */
static inline stress_hash_t *stress_sys_bad(stress_hash_table_t *hash_table, const char *str)
{
	stress_hash_t *hash;

	if (shim_pthread_spin_lock(&hash_lock))
		return NULL;	/* Can't lock! */
	hash = stress_hash_get(hash_table, str);
	(void)shim_pthread_spin_unlock(&hash_lock);

	return hash;
}

/*
 *  stress_sys_rw()
 *	read a sys file
 */
static inline bool stress_sys_rw(stress_ctxt_t *ctxt)
{
	int fd;
	ssize_t i = 0, ret;
	char buffer[SYS_BUF_SZ];
	char path[PATH_MAX];
	stress_args_t *args = ctxt->args;
	const double threshold = 0.2;
	size_t page_size = ctxt->args->page_size;

	while (stress_continue_flag()) {
		double t_start;
		uint8_t *ptr;
		fd_set rfds;
		struct timeval tv;
		ssize_t rret;

		ret = shim_pthread_spin_lock(&lock);
		if (UNLIKELY(ret))
			return false;
		(void)shim_strscpy(path, ctxt->sysfs_path, sizeof(path));
		counter++;
		(void)shim_pthread_spin_unlock(&lock);
		if (counter > OPS_PER_SYSFS_FILE)
			(void)shim_sched_yield();

		if (UNLIKELY(!*path || !stress_continue_flag()))
			break;

		t_start = stress_time_now();
		ret = stress_try_open(args, path, O_RDONLY | O_NONBLOCK, 150000000);
		if (ret == STRESS_TRY_OPEN_FAIL) {
			stress_sys_add_bad(path);
			goto next;
		}
		if ((fd = open(path, O_RDONLY | O_NONBLOCK)) < 0) {
			stress_sys_add_bad(path);
			goto next;
		}
		ret = shim_pthread_spin_lock(&open_lock);
		if (UNLIKELY(ret)) {
			(void)close(fd);
			return false;
		}
		ctxt->sysfs_files_opened++;
		(void)shim_pthread_spin_unlock(&open_lock);

		if (UNLIKELY(stress_time_now() - t_start > threshold)) {
			(void)close(fd);
			goto next;
		}

		/*
		 *  Multiple randomly sized reads
		 */
		while (i < (4096 * SYS_BUF_SZ)) {
			const ssize_t sz = 1 + stress_mwc32modn(sizeof(buffer) - 1);

			if (UNLIKELY(!stress_continue_flag()))
				break;
			rret = read(fd, buffer, (size_t)sz);
			if (UNLIKELY(rret < 0))
				break;
			if (rret < sz)
				break;
			i += sz;

			if (stress_kmsg_drain(ctxt->kmsgfd)) {
				drain_kmsg = true;
				(void)close(fd);
				goto drain;
			}
			if (UNLIKELY(stress_time_now() - t_start > threshold)) {
				(void)close(fd);
				goto next;
			}
		}

		/* file stat should be OK if we've just opened it */
		if (g_opt_flags & OPT_FLAGS_VERIFY) {
			struct stat statbuf;

			if (UNLIKELY(shim_fstat(fd, &statbuf) < 0))
				pr_fail("%s: stat failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
		}
		(void)close(fd);

		if (UNLIKELY(stress_time_now() - t_start > threshold))
			goto next;
		if ((fd = open(path, O_RDONLY | O_NONBLOCK)) < 0)
			goto next;
		/*
		 *  Zero sized reads
		 */
		rret = read(fd, buffer, 0);
		if (UNLIKELY(rret < 0))
			goto err;
		if (UNLIKELY(stress_time_now() - t_start > threshold))
			goto next;
		if (stress_kmsg_drain(ctxt->kmsgfd)) {
			drain_kmsg = true;
			(void)close(fd);
			goto drain;
		}
		/*
		 *  mmap it
		 */
		ptr = (uint8_t *)mmap(NULL, page_size, PROT_READ,
			MAP_SHARED | MAP_ANONYMOUS, fd, 0);
		if (ptr != MAP_FAILED) {
			stress_uint8_put(*ptr);
			(void)munmap((void *)ptr, page_size);
		}
		if (UNLIKELY(stress_time_now() - t_start > threshold))
			goto next;

		/*
		 *  select on sys file
		 */
		tv.tv_sec = 0;
		tv.tv_usec = 0;
		FD_ZERO(&rfds);
		VOID_RET(int, select(fd + 1, &rfds, NULL, NULL, &tv));
		if (UNLIKELY(stress_time_now() - t_start > threshold))
			goto next;

#if defined(HAVE_POLL_H) &&	\
    defined(HAVE_POLL)
		{
			struct pollfd fds[1];

			fds[0].fd = fd;
			fds[0].events = POLLIN;
			fds[0].revents = 0;

			VOID_RET(int, poll(fds, 1, 1));
			if (UNLIKELY(stress_time_now() - t_start > threshold))
				goto next;
		}
#else
		UNEXPECTED
#endif

#if defined(HAVE_PPOLL)
		{
			struct timespec ts;
			sigset_t sigmask;
			struct pollfd fds[1];

			fds[0].fd = fd;
			fds[0].events = POLLIN;
			fds[0].revents = 0;

			ts.tv_sec = 0;
			ts.tv_nsec = 1000;

			(void)sigemptyset(&sigmask);
			VOID_RET(int, shim_ppoll(fds, 1, &ts, &sigmask));
		}
#else
		UNEXPECTED
#endif

		/*
		 *  lseek
		 */
		VOID_RET(off_t, lseek(fd, (off_t)0, SEEK_SET));

		/*
		 *  simple ioctls
		 */
#if defined(FIGETBSZ)
		{
			int isz;

			VOID_RET(int, ioctl(fd, FIGETBSZ, &isz));
		}
#else
		UNEXPECTED
#endif
#if defined(FIONREAD)
		{
			int isz;

			VOID_RET(int, ioctl(fd, FIONREAD , &isz));
		}
#else
		UNEXPECTED
#endif

		if (stress_kmsg_drain(ctxt->kmsgfd)) {
			drain_kmsg = true;
			(void)close(fd);
			goto drain;
		}

		if (stress_kmsg_drain(ctxt->kmsgfd)) {
			drain_kmsg = true;
			(void)close(fd);
			goto drain;
		}
err:
		(void)close(fd);
		if (UNLIKELY(stress_time_now() - t_start > threshold))
			goto next;

		/*
		 *  We only attempt writes if we are not
		 *  root
		 */
		if (!ctxt->sys_admin) {
			/*
			 *  Zero sized writes
			 */
			if ((fd = open(path, O_WRONLY | O_NONBLOCK)) < 0)
				goto next;
			VOID_RET(ssize_t, write(fd, buffer, 0));
			(void)close(fd);

			if (UNLIKELY(stress_time_now() - t_start > threshold))
				goto next;
		} else {
			/*
			 * Special case for some sysfs entries
			 */
			size_t j;

			for (j = 0; j < SIZEOF_ARRAY(stress_sysfs_wr_funcs); j++) {
				if (strcmp(stress_sysfs_wr_funcs[j].path, path) == 0) {
					stress_sysfs_wr_funcs[j].sysfs_func(path);
				}
			}

#if 0
			/*
			 * Special case where we are root and file
			 * is a sysfd ROM file
			 */
			const char *rom = strstr(path, "rom");

			if (rom && (rom[3] == '\0')) {
				if ((fd = open(path, O_RDWR | O_NONBLOCK)) < 0)
					goto next;
				/* Enable ROM read */
				ret = write(fd, "1", 1);
				if (ret < 0) {
					(void)close(fd);
					goto next;
				}
				/*
				 *  Accessing ROM memory may be slow,
				 *  so just do one read for now.
				 */
				VOID_RET(ssize_t, read(fd, buffer, sizeof(buffer)));

				/* Disable ROM read */
				ret = write(fd, "0", 1);
				(void)close(fd);
				if (UNLIKELY(ret < 0)) {
					goto next;
				}
			}
#endif
		}
next:
		if (stress_kmsg_drain(ctxt->kmsgfd)) {
			drain_kmsg = true;
			goto drain;
		}

		if (drain_kmsg) {
drain:
			(void)shim_usleep(DRAIN_DELAY_US);
		}
	}
	return false;
}

/*
 *  stress_sys_rw_thread
 *	keep exercising a sysfs entry until
 *	controlling thread triggers an exit
 */
static void *stress_sys_rw_thread(void *ctxt_ptr)
{
	stress_ctxt_t *ctxt = (stress_ctxt_t *)ctxt_ptr;
	stress_args_t *args = ctxt->args;

	/*
	 *  Block all signals, let controlling thread
	 *  handle these
	 */
	(void)sigprocmask(SIG_BLOCK, &set, NULL);

	while (stress_continue(args))
		stress_sys_rw(ctxt);

	return &g_nowt;
}

static const char * const sys_skip_paths[] = {
	"/sys/class/zram-control/hot_add",	/* reading this will add a new zram dev */
	"/sys/kernel/debug",			/* don't read debug interfaces */
};

/*
 *  stress_sys_skip()
 *	skip paths that are known to cause issues
 */
static bool stress_sys_skip(const char *path)
{
	size_t i;

	for (i = 0; i < SIZEOF_ARRAY(sys_skip_paths); i++) {
		const char *skip_path = sys_skip_paths[i];
		const size_t len = strlen(skip_path);

		if (!strncmp(path, skip_path, len))
			return true;
	}
	/*
	 *  Can OOPS on Azure when reading
	 *  "/sys/devices/LNXSYSTM:00/LNXSYBUS:00/PNP0A03:00/device:07/" \
	 *  "VMBUS:01/99221fa0-24ad-11e2-be98-001aa01bbf6e/channels/4/read_avail"
	 */
	if (UNLIKELY(strstr(path, "PNP0A03") && strstr(path, "VMBUS")))
		return true;
	/*
	 *  Has been known to cause issues on s390x
	 *
	if (UNLIKELY(strstr(path, "virtio0/block") && strstr(path, "cache_type")))
		return true;
	 */

	/*
	 *  The tpm driver for pre Linux 4.10 is racey so skip
	 */
	if (UNLIKELY((os_release < 410) && (strstr(path, "/sys/kernel/security/tpm0"))))
		return true;

	return false;
}

/*
 *  stress_sys_dir()
 *	read directory
 */
static void stress_sys_dir(
	stress_ctxt_t *ctxt,
	const char *path,
	const bool recurse,
	const int depth)
{
	struct dirent **dlist = NULL;
	stress_args_t *args = ctxt->args;
	mode_t flags = S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;
	int i, n;

	if (UNLIKELY(!stress_continue_flag()))
		return;

	/* Don't want to go too deep */
	if (depth > 20)
		return;

	/* Don't want to reset any GCOV metrics */
	if (UNLIKELY(!strcmp(path, "/sys/kernel/debug/gcov")))
		return;

	n = scandir(path, &dlist, NULL, mixup_sort);
	if (UNLIKELY(n <= 0)) {
		stress_dirent_list_free(dlist, n);
		return;
	}

	if (ctxt->sys_admin)
		flags |= S_IRUSR | S_IWUSR;

	/* Non-directories first */
	for (i = 0; LIKELY((i < n) && stress_continue(args)); i++) {
		int ret;
		struct stat buf;
		char tmp[PATH_MAX];
		const struct dirent *d = dlist[i];
		double time_start, time_end, time_out;

		if (stress_is_dot_filename(d->d_name))
			goto dt_reg_free;

		(void)stress_mk_filename(tmp, sizeof(tmp), path, d->d_name);
		/* Is it in the hash of bad paths? */
		if (stress_sys_bad(sysfs_hash_table, tmp))
			goto dt_reg_free;

		if (stress_sys_skip(tmp))
			goto dt_reg_free;

		if (shim_dirent_type(path, d) != SHIM_DT_REG)
			continue;

		ret = shim_stat(tmp, &buf);
		if (ret < 0)
			goto dt_reg_free;

		if ((buf.st_mode & flags) == 0)
			goto dt_reg_free;

		ret = shim_pthread_spin_lock(&lock);
		if (UNLIKELY(ret))
			goto dt_reg_free;

		(void)shim_strscpy(ctxt->sysfs_path, tmp, sizeof(ctxt->sysfs_path));
		counter = 0;
		(void)shim_pthread_spin_unlock(&lock);

		drain_kmsg = false;
		time_start = stress_time_now();
		time_end = time_start + ((double)DURATION_PER_SYSFS_FILE * ONE_MILLIONTH);
		time_out = time_start + 1.0;
		/*
		 *  wait for a timeout, or until woken up
		 *  by pthread(s) once maximum iteration count
		 *  has been reached
		 */
		do {
			(void)shim_usleep_interruptible(1000);
			/* Cater for very long delays */
			if (UNLIKELY((counter == 0) && (stress_time_now() > time_out)))
				break;
			/* Cater for slower delays */
			if (UNLIKELY((counter > 0) && (stress_time_now() > time_end)))
				break;
		} while ((counter < OPS_PER_SYSFS_FILE) && stress_continue(args));

		stress_bogo_inc(args);
dt_reg_free:
		free(dlist[i]);
		dlist[i] = NULL;
	}

	if (!recurse) {
		stress_dirent_list_free(dlist, n);
		return;
	}

	/* Now directories.. */
	for (i = 0; LIKELY((i < n) && stress_continue(args)); i++) {
		const struct dirent *d = dlist[i];
		struct stat buf;
		int ret;
		char tmp[PATH_MAX];

		if (UNLIKELY(!d))
			continue;
		if (shim_dirent_type(path, d) != SHIM_DT_DIR)
			goto dt_dir_free;

		(void)stress_mk_filename(tmp, sizeof(tmp), path, d->d_name);
		ret = shim_stat(tmp, &buf);
		if (ret < 0)
			goto dt_dir_free;
		if ((buf.st_mode & flags) == 0)
			goto dt_dir_free;

		stress_bogo_inc(args);
		stress_sys_dir(ctxt, tmp, recurse, depth + 1);
dt_dir_free:
		free(dlist[i]);
		dlist[i] = NULL;
	}
	stress_dirent_list_free(dlist, n);
}

static bool stress_sysfs_bad_signal(const int status)
{
	size_t i;

	static const int bad_signals[] = {
#if defined(SIGBUS)
		SIGBUS,
#endif
#if defined(SIGILL)
		SIGILL,
#endif
#if defined(SIGSEGV)
		SIGSEGV,
#endif
#if defined(SIGTRAP)
		SIGTRAP,
#endif
#if defined(SIGSYS)
		SIGSYS,
#endif
	};

	if (!WIFSIGNALED(status))
		return false;

	for (i = 0; i < SIZEOF_ARRAY(bad_signals); i++) {
		if (WTERMSIG(status) == bad_signals[i])
			return true;
	}

	return false;
}

/*
 *  stress_sysfs
 *	stress reading all of /sys
 */
static int stress_sysfs(stress_args_t *args)
{
	int i, n, rc = EXIT_SUCCESS;
	pthread_t pthreads[MAX_SYSFS_THREADS];
	int ret, pthreads_ret[MAX_SYSFS_THREADS];
	stress_ctxt_t *ctxt;
	struct dirent **dlist = NULL;
	double t, duration, rate;

	ctxt = (stress_ctxt_t *)stress_mmap_populate(NULL, sizeof(*ctxt),
				     PROT_READ | PROT_WRITE,
				     MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (ctxt == MAP_FAILED) {
		pr_inf_skip("%s: cannot mmap shared context region, skipping stressor\n", args->name);
		return EXIT_NO_RESOURCE;
	}
	stress_set_vma_anon_name(ctxt, sizeof(*ctxt), "sysfs-pthread-context");

	n = scandir("/sys", &dlist, NULL, alphasort);
	if (n <= 0)
		goto exit_no_sysfs_entries;
	n = stress_dirent_list_prune(dlist, n);
	if (n <= 0)
		goto exit_no_sysfs_entries;

	os_release = 0;
#if defined(HAVE_UNAME) &&	\
    defined(HAVE_SYS_UTSNAME_H)
	{
		static struct utsname utsbuf;

		ret = uname(&utsbuf);
		if (ret == 0) {
			uint16_t major, minor;

			if (sscanf(utsbuf.release, "%5" SCNu16 ".%5" SCNu16, &major, &minor) == 2)
				os_release = (major * 100) + minor;
		}
	}
#else
	UNEXPECTED
#endif
	sysfs_hash_table = stress_hash_create(1021);
	if (!sysfs_hash_table) {
		pr_err("%s: cannot create sysfs hash table, errno=%d (%s))\n",
			args->name, errno, strerror(errno));
		rc = EXIT_NO_RESOURCE;
		goto exit_free;
	}

	(void)shim_memset(ctxt, 0, sizeof(*ctxt));
	(void)shim_strscpy(ctxt->sysfs_path, signum_path, sizeof(ctxt->sysfs_path));

	ctxt->args = args;
	ctxt->kmsgfd = open("/dev/kmsg", O_RDONLY | O_NONBLOCK);
	ctxt->sys_admin = stress_check_capability(SHIM_CAP_SYS_ADMIN);
	(void)stress_kmsg_drain(ctxt->kmsgfd);

	ret = shim_pthread_spin_init(&lock, PTHREAD_PROCESS_PRIVATE);
	if (ret) {
		pr_inf("%s: pthread_spin_init on lock failed, errno=%d (%s)\n",
			args->name, ret, strerror(ret));
		rc = EXIT_NO_RESOURCE;
		goto exit_delete_hash;
	}
	ret = shim_pthread_spin_init(&open_lock, PTHREAD_PROCESS_PRIVATE);
	if (ret) {
		pr_inf("%s: pthread_spin_init on open_lock failed, errno=%d (%s)\n",
			args->name, ret, strerror(ret));
		rc = EXIT_NO_RESOURCE;
		goto exit_destroy_lock;
	}
	ret = shim_pthread_spin_init(&hash_lock, PTHREAD_PROCESS_PRIVATE);
	if (ret) {
		pr_inf("%s: pthread_spin_init on hash_lock failed, errno=%d (%s)\n",
			args->name, ret, strerror(ret));
		rc = EXIT_NO_RESOURCE;
		goto exit_destroy_open_lock;
	}

	(void)shim_memset(pthreads_ret, 0, sizeof(pthreads_ret));

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	/*
	 *  Main stressing loop: the pthread stressors are
	 *  all wrapped by a controlling child process that
	 *  maybe killed by the kernel when accessing problematic
	 *  sysfs files. When this occurs the sysfs file being
	 *  exercised is dumped out by a debug message when using
	 *  the -v option.
         *
         *  The parent waits for any child deaths and will restart
	 *  if the child was terminated prematurely by an unexpected
	 *  signal from the kernel.
	 */
	t = stress_time_now();
	do {
		pid_t pid;

again:
		pid = fork();
		if (pid < 0) {
			if (stress_redo_fork(args, errno))
				goto again;
			if (UNLIKELY(!stress_continue(args))) {
				rc = EXIT_SUCCESS;
				goto finish;
			}
		} else if (pid > 0) {
			pid_t wret;
			int status;

			/* Parent, wait for child */
			wret = waitpid(pid, &status, 0);
			if (wret < 0) {
				if (errno != EINTR)
					pr_dbg("%s: waitpid() on PID %" PRIdMAX " failed, errno=%d (%s)\n",
						args->name, (intmax_t)pid, errno, strerror(errno));
				/* Ring ring, time to die */
				stress_kill_and_wait(args, pid, SIGALRM, true);
			} else {
				if (stress_sysfs_bad_signal(status)) {
					pr_inf("%s: killed by %s exercising '%s'\n",
						args->name,
						stress_strsignal(WTERMSIG(status)),
						ctxt->sysfs_path);
					stress_sys_add_bad(ctxt->sysfs_path);
					rc = EXIT_FAILURE;
				}
				if (WIFEXITED(status) &&
				    WEXITSTATUS(status) != 0) {
					rc = EXIT_FAILURE;
					break;
				}
			}
		} else {
			/* Child, spawn threads for sysfs stressing */

			stress_set_proc_state(args->name, STRESS_STATE_RUN);
			stress_parent_died_alarm();

			for (i = 0; i < MAX_SYSFS_THREADS; i++) {
				pthreads_ret[i] = pthread_create(&pthreads[i], NULL,
								stress_sys_rw_thread, ctxt);
			}

			do {
				int j = (int)args->instance % n;
				const int inc = (int)stress_get_next_prime64((((uint64_t)(args->instance + 1) * 50U) + 1200));

				for (i = 0; i < n; i++) {
					char sysfspath[PATH_MAX];

					if (UNLIKELY(!stress_continue(args)))
						break;

					if (UNLIKELY(stress_is_dot_filename(dlist[j]->d_name)))
						continue;

					stress_mk_filename(sysfspath, sizeof(sysfspath),
							"/sys", dlist[j]->d_name);

					stress_sys_dir(ctxt, sysfspath, true, 0);
					j = (j + inc) % n;
				}
			} while (stress_continue(args));

			ret = shim_pthread_spin_lock(&lock);
			if (ret) {
				pr_dbg("%s: failed to lock spin lock for sysfs_path\n", args->name);
			} else {
				(void)shim_strscpy(ctxt->sysfs_path, "", sizeof(ctxt->sysfs_path));
				VOID_RET(int, shim_pthread_spin_unlock(&lock));
			}

			/* Forcefully kill threads */
			for (i = 0; i < MAX_SYSFS_THREADS; i++) {
				if (pthreads_ret[i] == 0) {
					stress_force_killed_bogo(args);
					(void)pthread_kill(pthreads[i], SIGKILL);
				}
			}

			for (i = 0; i < MAX_SYSFS_THREADS; i++) {
				if (pthreads_ret[i] == 0)
					(void)pthread_join(pthreads[i], NULL);
			}

			_exit(EXIT_SUCCESS);
		}
	} while (stress_continue(args));
	duration = stress_time_now() - t;

	pr_dbg("%s: skipped %" PRIu64 " out of %" PRIu64 " sysfs files accessed\n",
		args->name, hash_items, stress_bogo_get(args));

	rate = (duration > 0.0) ? (double)ctxt->sysfs_files_opened / duration : 0.0;
	stress_metrics_set(args, 0, "sysfs files exercised per sec",
		rate, STRESS_METRIC_HARMONIC_MEAN);

finish:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	(void)shim_pthread_spin_destroy(&hash_lock);
exit_destroy_open_lock:
	(void)shim_pthread_spin_destroy(&open_lock);
exit_destroy_lock:
	(void)shim_pthread_spin_destroy(&lock);
exit_delete_hash:
	stress_hash_delete(sysfs_hash_table);
	if (ctxt->kmsgfd != -1)
		(void)close(ctxt->kmsgfd);

exit_free:
	stress_dirent_list_free(dlist, n);
	(void)munmap((void *)ctxt, sizeof(*ctxt));

	return rc;

exit_no_sysfs_entries:
	if (stress_instance_zero(args))
		pr_inf_skip("%s: no /sys entries found, skipping stressor\n", args->name);
	rc = EXIT_NO_RESOURCE;
	goto exit_free;
}

const stressor_info_t stress_sysfs_info = {
	.stressor = stress_sysfs,
	.classifier = CLASS_OS,
	.verify = VERIFY_OPTIONAL,
	.help = help
};
#else
const stressor_info_t stress_sysfs_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_OS,
	.verify = VERIFY_OPTIONAL,
	.help = help,
	.unimplemented_reason = "not Linux or built without pthread support"
};
#endif
