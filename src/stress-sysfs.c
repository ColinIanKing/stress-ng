/*
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
	{ NULL,	"sysfs N",	"start N workers reading files from /sys" },
	{ NULL,	"sysfs-ops N",	"stop after sysfs bogo operations" },
	{ NULL,	NULL,		NULL }
};

#if defined(HAVE_LIB_PTHREAD) && defined(__linux__)

#define SYS_BUF_SZ		(4096)
#define MAX_SYSFS_THREADS	(4)	/* threads stressing sysfs */
#define DRAIN_DELAY_US		(50000)	/* backoff in (us) microsecs */
#define DURATION_PER_SYSFS_FILE	(100000)	/* max duration per file in microsecs */
#define OPS_PER_SYSFS_FILE	(64)	/* max iterations per sysfs file */

static sigset_t set;
static shim_pthread_spinlock_t lock;
static char sysfs_path[PATH_MAX];
static uint32_t mixup;
static volatile bool drain_kmsg = false;
static volatile uint32_t counter = 0;
static char signum_path[] = "/sys/kernel/notes";
static uint32_t os_release;
static stress_hash_table_t *sysfs_hash_table;

typedef struct stress_ctxt {
	const stress_args_t *args;		/* stressor args */
	int kmsgfd;			/* /dev/kmsg file descriptor */
	bool sys_admin;			/* true if sys admin capable */
} stress_ctxt_t;

static uint32_t path_sum(const char *path)
{
	const char *ptr = path;
	register uint32_t sum = mixup;

	while (*ptr) {
		sum <<= 1;
		sum += *(ptr++);
	}

	return sum;
}

static int mixup_sort(const struct dirent **d1, const struct dirent **d2)
{
	uint32_t s1, s2;

	s1 = path_sum((*d1)->d_name);
	s2 = path_sum((*d2)->d_name);

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

	if (fd == -1)
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
static void stress_sys_add_bad(const char *path)
{
	if (shim_pthread_spin_lock(&lock))
		return;	/* Can't lock! */

	stress_hash_add(sysfs_hash_table, path);
	(void)shim_pthread_spin_unlock(&lock);
}

/*
 *  stress_sys_rw()
 *	read a proc file
 */
static inline bool stress_sys_rw(const stress_ctxt_t *ctxt)
{
	int fd;
	ssize_t i = 0, ret;
	char buffer[SYS_BUF_SZ];
	char path[PATH_MAX];
	const stress_args_t *args = ctxt->args;
	const double threshold = 0.2;
	size_t page_size = ctxt->args->page_size;

	while (keep_stressing_flag()) {
		double t_start;
		uint8_t *ptr;
		fd_set rfds;
		struct timeval tv;
		off_t lret;

		ret = shim_pthread_spin_lock(&lock);
		if (ret)
			return false;
		(void)shim_strlcpy(path, sysfs_path, sizeof(path));
		counter++;
		(void)shim_pthread_spin_unlock(&lock);
		if (counter > OPS_PER_SYSFS_FILE)
			shim_sched_yield();

		if (!*path || !keep_stressing_flag())
			break;

		t_start = stress_time_now();
		ret = stress_try_open(args, path, O_RDONLY | O_NONBLOCK, 1500000000);
		if (ret == STRESS_TRY_OPEN_FAIL) {
			stress_sys_add_bad(path);
			goto next;
		}
		if ((fd = open(path, O_RDONLY | O_NONBLOCK)) < 0) {
			stress_sys_add_bad(path);
			goto next;
		}
		if (stress_time_now() - t_start > threshold) {
			(void)close(fd);
			goto next;
		}

		/*
		 *  Multiple randomly sized reads
		 */
		while (i < (4096 * SYS_BUF_SZ)) {
			ssize_t sz = 1 + (stress_mwc32() % (sizeof(buffer) - 1));
			if (!keep_stressing_flag())
				break;
			ret = read(fd, buffer, sz);
			if (ret < 0)
				break;
			if (ret < sz)
				break;
			i += sz;

			if (stress_kmsg_drain(ctxt->kmsgfd)) {
				drain_kmsg = true;
				(void)close(fd);
				goto drain;
			}
			if (stress_time_now() - t_start > threshold) {
				(void)close(fd);
				goto next;
			}
		}

		/* file stat should be OK if we've just opened it */
		if (g_opt_flags & OPT_FLAGS_VERIFY) {
			struct stat statbuf;

			if (fstat(fd, &statbuf) < 0)
				pr_fail("%s: stat failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
		}
		(void)close(fd);

		if (stress_time_now() - t_start > threshold)
			goto next;
		if ((fd = open(path, O_RDONLY | O_NONBLOCK)) < 0)
			goto next;
		/*
		 *  Zero sized reads
		 */
		ret = read(fd, buffer, 0);
		if (ret < 0)
			goto err;
		if (stress_time_now() - t_start > threshold)
			goto next;
		if (stress_kmsg_drain(ctxt->kmsgfd)) {
			drain_kmsg = true;
			(void)close(fd);
			goto drain;
		}

		/*
		 *  mmap it
		 */
		ptr = mmap(NULL, page_size, PROT_READ,
			MAP_SHARED | MAP_ANONYMOUS, fd, 0);
		if (ptr != MAP_FAILED) {
			stress_uint8_put(*ptr);
			(void)munmap(ptr, page_size);
		}
		if (stress_time_now() - t_start > threshold)
			goto next;

		/*
		 *  select on proc file
		 */
		tv.tv_sec = 0;
		tv.tv_usec = 0;
		FD_ZERO(&rfds);
		ret = select(fd + 1, &rfds, NULL, NULL, &tv);
		(void)ret;
		if (stress_time_now() - t_start > threshold)
			goto next;

#if defined(HAVE_POLL_H)
		{
			struct pollfd fds[1];

			fds[0].fd = fd;
			fds[0].events = POLLIN;
			fds[0].revents = 0;

			ret = poll(fds, 1, 1);
			(void)ret;
			if (stress_time_now() - t_start > threshold)
				goto next;
		}
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
			ret = ppoll(fds, 1, &ts, &sigmask);
			(void)ts;
		}
#endif

		/*
		 *  lseek
		 */
		lret = lseek(fd, (off_t)0, SEEK_SET);
		(void)lret;

		/*
		 *  simple ioctls
		 */
#if defined(FIGETBSZ)
		{
			int isz;

			ret = ioctl(fd, FIGETBSZ, &isz);
			(void)ret;
		}
#endif
#if defined(FIONREAD)
		{
			int isz;

			ret = ioctl(fd, FIONREAD , &isz);
			(void)ret;
		}
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
		if (stress_time_now() - t_start > threshold)
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
			ret = write(fd, buffer, 0);
			(void)ret;
			(void)close(fd);

			if (stress_time_now() - t_start > threshold)
				goto next;
		} else {
#if 0
			/*
			 * Special case where we are root and file
			 * is a sysfd ROM file
			 */
			const char *rom = strstr(path, "rom");

			if (rom && rom[3] == '\0') {
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
				ret = read(fd, buffer, sizeof(buffer));
				(void)ret;

				/* Disable ROM read */
				ret = write(fd, "0", 1);
				if (ret < 0) {
					(void)close(fd);
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
	static void *nowt = NULL;
	stress_ctxt_t *ctxt = (stress_ctxt_t *)ctxt_ptr;
	const stress_args_t *args = ctxt->args;

	/*
	 *  Block all signals, let controlling thread
	 *  handle these
	 */
	(void)sigprocmask(SIG_BLOCK, &set, NULL);

	while (keep_stressing(args))
		stress_sys_rw(ctxt);

	return &nowt;
}

/*
 *  stress_sys_skip()
 *	skip paths that are known to cause issues
 */
static bool stress_sys_skip(const char *path)
{
	/*
	 *  Skip over debug interfaces
	 */
	if (!strncmp(path, "/sys/kernel/debug", 17))
		return true;
	/*
	 *  Can OOPS on Azure when reading
	 *  "/sys/devices/LNXSYSTM:00/LNXSYBUS:00/PNP0A03:00/device:07/" \
	 *  "VMBUS:01/99221fa0-24ad-11e2-be98-001aa01bbf6e/channels/4/read_avail"
	 */
	if (strstr(path, "PNP0A03") && strstr(path, "VMBUS"))
		return true;
	/*
	 *  Has been known to cause issues on s390x
	 *
	if (strstr(path, "virtio0/block") && strstr(path, "cache_type"))
		return true;
	 */

	/*
	 *  The tpm driver for pre Linux 4.10 is racey so skip
	 */
	if ((os_release < 410) && (strstr(path, "/sys/kernel/security/tpm0")))
		return true;

	return false;
}

/*
 *  stress_sys_dir()
 *	read directory
 */
static void stress_sys_dir(
	const stress_ctxt_t *ctxt,
	const char *path,
	const bool recurse,
	const int depth)
{
	struct dirent **dlist = NULL;
	const stress_args_t *args = ctxt->args;
	mode_t flags = S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;
	int i, n;

	if (!keep_stressing_flag())
		return;

	/* Don't want to go too deep */
	if (depth > 20)
		return;

	/* Don't want to reset any GCOV metrics */
	if (!strcmp(path, "/sys/kernel/debug/gcov"))
		return;

	mixup = stress_mwc32();
	n = scandir(path, &dlist, NULL, mixup_sort);
	if (n <= 0) {
		stress_dirent_list_free(dlist, n);
		return;
	}

	if (ctxt->sys_admin)
		flags |= S_IRUSR | S_IWUSR;

	/* Non-directories first */
	for (i = 0; (i < n) && keep_stressing(args); i++) {
		int ret;
		struct stat buf;
		char tmp[PATH_MAX];
		struct dirent *d = dlist[i];
		double time_start, time_end, time_out;

		if (stress_is_dot_filename(d->d_name))
			goto dt_reg_free;

		(void)stress_mk_filename(tmp, sizeof(tmp), path, d->d_name);
		/* Is it in the hash of bad paths? */
		if (stress_hash_get(sysfs_hash_table, tmp))
			goto dt_reg_free;

		if (stress_sys_skip(tmp))
			goto dt_reg_free;

		if (d->d_type != DT_REG)
			continue;

		ret = stat(tmp, &buf);
		if (ret < 0)
			goto dt_reg_free;

		if ((buf.st_mode & flags) == 0)
			goto dt_reg_free;

		ret = shim_pthread_spin_lock(&lock);
		if (ret)
			goto dt_reg_free;

		(void)shim_strlcpy(sysfs_path, tmp, sizeof(sysfs_path));
		counter = 0;
		(void)shim_pthread_spin_unlock(&lock);
		drain_kmsg = false;
		time_start = stress_time_now();
		time_end = time_start + ((double)DURATION_PER_SYSFS_FILE / 1000000.0);
		time_out = time_start + 1.0;
		/*
		 *  wait for a timeout, or until woken up
		 *  by pthread(s) once maximum iteration count
		 *  has been reached
		 */
		do {
			shim_usleep_interruptible(50);
			/* Cater for very long delays */
			if ((counter == 0) && (stress_time_now() > time_out))
				break;
			/* Cater for slower delays */
			if ((counter > 0) && (stress_time_now() > time_end))
				break;
		} while ((counter < OPS_PER_SYSFS_FILE) && keep_stressing(args));

		inc_counter(args);
dt_reg_free:
		free(dlist[i]);
		dlist[i] = NULL;
	}

	if (!recurse) {
		stress_dirent_list_free(dlist, n);
		return;
	}

	/* Now directories.. */
	for (i = 0; (i < n) && keep_stressing(args); i++) {
		struct dirent *d = dlist[i];
		struct stat buf;
		int ret;
		char tmp[PATH_MAX];

		if (!d)
			continue;
		if (d->d_type != DT_DIR)
			goto dt_dir_free;

		(void)stress_mk_filename(tmp, sizeof(tmp), path, d->d_name);
		ret = stat(tmp, &buf);
		if (ret < 0)
			goto dt_dir_free;
		if ((buf.st_mode & flags) == 0)
			goto dt_dir_free;

		inc_counter(args);
		stress_sys_dir(ctxt, tmp, recurse, depth + 1);
dt_dir_free:
		free(dlist[i]);
		dlist[i] = NULL;
	}
	stress_dirent_list_free(dlist, n);
}


/*
 *  stress_sysfs
 *	stress reading all of /sys
 */
static int stress_sysfs(const stress_args_t *args)
{
	int i, n;
	pthread_t pthreads[MAX_SYSFS_THREADS];
	int rc, ret[MAX_SYSFS_THREADS];
	stress_ctxt_t ctxt;
	struct dirent **dlist = NULL;

	n = scandir("/sys", &dlist, NULL, alphasort);
	if (n <= 0) {
		pr_inf("%s: no /sys entries found, skipping stressor\n", args->name);
		stress_dirent_list_free(dlist, n);
		return EXIT_NO_RESOURCE;
	}
	n = stress_dirent_list_prune(dlist, n);

	os_release = 0;
#if defined(HAVE_UNAME) && defined(HAVE_SYS_UTSNAME_H)
	{
		static struct utsname utsbuf;

		rc = uname(&utsbuf);
		if (rc == 0) {
			uint16_t major, minor;

			if (sscanf(utsbuf.release, "%5" SCNd16 ".%5" SCNd16, &major, &minor) == 2)
				os_release = (major * 100) + minor;
		}
	}
#endif
	sysfs_hash_table = stress_hash_create(1021);
	if (!sysfs_hash_table) {
		pr_err("%s: cannot create sysfs hash table: %d (%s))\n",
			args->name, errno, strerror(errno));
		stress_dirent_list_free(dlist, n);
		return EXIT_NO_RESOURCE;
	}

	(void)memset(&ctxt, 0, sizeof(ctxt));
	shim_strlcpy(sysfs_path, signum_path, sizeof(sysfs_path));

	ctxt.args = args;
	ctxt.kmsgfd = open("/dev/kmsg", O_RDONLY | O_NONBLOCK);
	ctxt.sys_admin = stress_check_capability(SHIM_CAP_SYS_ADMIN);
	(void)stress_kmsg_drain(ctxt.kmsgfd);

	rc = shim_pthread_spin_init(&lock, PTHREAD_PROCESS_PRIVATE);
	if (rc) {
		pr_inf("%s: pthread_spin_init failed, errno=%d (%s)\n",
			args->name, rc, strerror(rc));
		if (ctxt.kmsgfd != -1)
			(void)close(ctxt.kmsgfd);
		stress_hash_delete(sysfs_hash_table);
		stress_dirent_list_free(dlist, n);
		return EXIT_NO_RESOURCE;
	}

	(void)memset(ret, 0, sizeof(ret));

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	for (i = 0; i < MAX_SYSFS_THREADS; i++) {
		ret[i] = pthread_create(&pthreads[i], NULL,
				stress_sys_rw_thread, &ctxt);
	}

	do {
		int j = (args->instance) % n;

		for (i = 0; i < n; i++) {
			char sysfspath[PATH_MAX];

			if (!keep_stressing(args))
				break;

			if (stress_is_dot_filename(dlist[j]->d_name))
				continue;

			stress_mk_filename(sysfspath, sizeof(sysfspath),
				"/sys", dlist[j]->d_name);

			stress_sys_dir(&ctxt, sysfspath, true, 0);

			j = (j + args->num_instances) % n;
		}
	} while (keep_stressing(args));

	rc = shim_pthread_spin_lock(&lock);
	if (rc) {
		pr_dbg("%s: failed to lock spin lock for sysfs_path\n", args->name);
	} else {
		shim_strlcpy(sysfs_path, "", sizeof(sysfs_path));
		rc = shim_pthread_spin_unlock(&lock);
		(void)rc;
	}

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	/* Forcefully kill threads */
	for (i = 0; i < MAX_SYSFS_THREADS; i++) {
		if (ret[i] == 0)
			(void)pthread_kill(pthreads[i], SIGHUP);
	}

	for (i = 0; i < MAX_SYSFS_THREADS; i++) {
		if (ret[i] == 0)
			(void)pthread_join(pthreads[i], NULL);
	}
	stress_hash_delete(sysfs_hash_table);
	if (ctxt.kmsgfd != -1)
		(void)close(ctxt.kmsgfd);
	(void)shim_pthread_spin_destroy(&lock);

	stress_dirent_list_free(dlist, n);

	return EXIT_SUCCESS;
}

stressor_info_t stress_sysfs_info = {
	.stressor = stress_sysfs,
	.class = CLASS_OS,
	.help = help
};
#else
stressor_info_t stress_sysfs_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_OS,
	.help = help
};
#endif
