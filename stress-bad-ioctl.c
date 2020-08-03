/*
 * Copyright (C) 2013-2020 Canonical, Ltd.
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
	{ NULL,	"bad-ioctl N",		"start N stressors that perform illegal read ioctls on devices" },
	{ NULL,	"bad-ioctl-ops  N",	"stop after N bad ioctl bogo operations" },
	{ NULL,	NULL,		NULL }
};

#if defined(HAVE_LIB_PTHREAD) &&	\
    defined(__linux__) &&		\
    defined(_IOR)

#define MAX_DEV_THREADS		(4)

static sigset_t set;
static shim_pthread_spinlock_t lock;
static char *dev_path;
static uint32_t mixup;

typedef struct stress_bad_ioctl_func {
	const char *devpath;
	const size_t devpath_len;
	void (*func)(const char *name, const int fd, const char *devpath);
} stress_bad_ioctl_func_t;

static stress_hash_table_t *dev_hash_table;
static sigjmp_buf jmp_env;

static int stress_bad_ioctl_supported(const char *name)
{
        if (stress_check_capability(SHIM_CAP_IS_ROOT) ||
	    (geteuid() == 0)) {
                pr_inf("%s stressor will be skipped, "
                        "need to be running without root privilege "
                        "for this stressor\n", name);
                return -1;
        }
	return 0;
}

static void MLOCKED_TEXT stress_segv_handler(int signum)
{
	(void)signum;

	siglongjmp(jmp_env, 1);
}

/*
 *  stress_bad_ioctl_rw()
 *	exercise a dev entry
 */
static inline void stress_bad_ioctl_rw(
	const stress_args_t *args,
	const bool is_main_process)
{
	int fd, ret;
	char path[PATH_MAX];
	const double threshold = 0.25;
	const size_t page_size = args->page_size;
	NOCLOBBER uint16_t i = 0;
	uint8_t *buf;
	uint8_t *buf8;
	uint16_t *buf16;
	uint32_t *buf32;
	uint64_t *buf64;
	uint32_t *ptr, *buf_end;

	typedef struct {
			uint8_t page[4096];
	} stress_4k_page_t;

	buf = mmap(NULL, page_size, PROT_READ | PROT_WRITE,
		MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (buf == MAP_FAILED)
		return;

	buf8 = (uint8_t *)(buf + page_size - sizeof(uint8_t));
	buf16 = (uint16_t *)(buf + page_size - sizeof(uint16_t));
	buf32 = (uint32_t *)(buf + page_size - sizeof(uint32_t));
	buf64 = (uint64_t *)(buf + page_size - sizeof(uint64_t));
	buf_end	= (uint32_t *)(buf + page_size);

	for (ptr = (uint32_t *)buf; ptr < buf_end; ptr++) {
		*ptr = stress_mwc32();
	}

	do {
		double t_start;
		uint8_t type = (i >> 8) & 0xff;
		uint8_t nr = i & 0xff;
		uint64_t rnd = stress_mwc32();

		ret = shim_pthread_spin_lock(&lock);
		if (ret)
			return;
		(void)shim_strlcpy(path, dev_path, sizeof(path));
		(void)shim_pthread_spin_unlock(&lock);

		if (!*path || !keep_stressing_flag())
			break;

		t_start = stress_time_now();

		for (ptr = (uint32_t *)buf; ptr < buf_end; ptr++) {
			*ptr ^= rnd;
		}

		if ((fd = open(path, O_RDONLY | O_NONBLOCK)) < 0)
			break;

		ret = sigsetjmp(jmp_env, 1);
		if (ret != 0)
			goto next;

		(void)memset(buf, 0, page_size);

		ret = ioctl(fd, _IOR(type, nr, uint64_t), buf64);
		(void)ret;
		if (stress_time_now() - t_start > threshold)
			break;

		ret = ioctl(fd, _IOR(type, nr, uint32_t), buf32);
		(void)ret;
		if (stress_time_now() - t_start > threshold)
			break;

		ret = ioctl(fd, _IOR(type, nr, uint16_t), buf16);
		(void)ret;
		if (stress_time_now() - t_start > threshold)
			break;

		ret = ioctl(fd, _IOR(type, nr, uint8_t), buf8);
		(void)ret;
		if (stress_time_now() - t_start > threshold)
			break;

		ret = ioctl(fd, _IOR(type, nr, stress_4k_page_t), buf);
		(void)ret;
		if (stress_time_now() - t_start > threshold)
			break;

		ret = ioctl(fd, _IOR(type, nr, uint8_t), NULL);
		(void)ret;
		if (stress_time_now() - t_start > threshold)
			break;

		ret = ioctl(fd, _IOR(type, nr, uint8_t), args->mapped->page_none);
		(void)ret;
		if (stress_time_now() - t_start > threshold)
			break;

		ret = ioctl(fd, _IOR(type, nr, uint8_t), args->mapped->page_ro);
		(void)ret;
		if (stress_time_now() - t_start > threshold)
			break;

		(void)close(fd);
next:
		i++;
	} while (is_main_process);

	(void)munmap(buf, page_size);
}

/*
 *  stress_bad_ioctl_thread
 *	keep exercising a /dev entry until
 *	controlling thread triggers an exit
 */
static void *stress_bad_ioctl_thread(void *arg)
{
	static void *nowt = NULL;
	const stress_pthread_args_t *pa = (stress_pthread_args_t *)arg;
	const stress_args_t *args = pa->args;

	/*
	 *  Block all signals, let controlling thread
	 *  handle these
	 */
	(void)sigprocmask(SIG_BLOCK, &set, NULL);

	while (keep_stressing_flag())
		stress_bad_ioctl_rw(args, true);

	return &nowt;
}

/*
 *  stress_bad_ioctl_dir()
 *	read directory
 */
static void stress_bad_ioctl_dir(
	const stress_args_t *args,
	const char *path,
	const bool recurse,
	const int depth)
{
	struct dirent **dlist;
	const mode_t flags = S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;
	int i, n;

	if (!keep_stressing_flag())
		return;

	/* Don't want to go too deep */
	if (depth > 20)
		return;

	dlist = NULL;
	n = scandir(path, &dlist, NULL, alphasort);
	if (n <= 0)
		goto done;

	for (i = 0; i < n; i++) {
		int ret;
		struct stat buf;
		char filename[PATH_MAX];
		char tmp[PATH_MAX];
		struct dirent *d = dlist[i];
		size_t len;

		if (!keep_stressing())
			break;
		if (stress_is_dot_filename(d->d_name))
			continue;

		len = strlen(d->d_name);

		/*
		 *  Exercise no more than 3 of the same device
		 *  driver, e.g. ttyS0..ttyS2
		 */
		if (len > 1) {
			int dev_n;
			char *ptr = d->d_name + len - 1;

			while (ptr > d->d_name && isdigit((int)*ptr))
				ptr--;
			ptr++;
			dev_n = atoi(ptr);
			if (dev_n > 2)
				continue;
		}

		(void)snprintf(tmp, sizeof(tmp), "%s/%s", path, d->d_name);
		switch (d->d_type) {
		case DT_DIR:
			if (!recurse)
				continue;
			if (stress_hash_get(dev_hash_table, tmp))
				continue;
			ret = stat(tmp, &buf);
			if (ret < 0) {
				stress_hash_add(dev_hash_table, tmp);
				continue;
			}
			if ((buf.st_mode & flags) == 0) {
				stress_hash_add(dev_hash_table, tmp);
				continue;
			}
			inc_counter(args);
			stress_bad_ioctl_dir(args, tmp, recurse, depth + 1);
			break;
		case DT_BLK:
		case DT_CHR:
			if (stress_hash_get(dev_hash_table, tmp))
				continue;
			if (strstr(tmp, "watchdog")) {
				stress_hash_add(dev_hash_table, tmp);
				continue;
			}
			if (stress_try_open(args, tmp, O_RDONLY | O_NONBLOCK, 1500000000)) {
				stress_hash_add(dev_hash_table, tmp);
				continue;
			}
			ret = shim_pthread_spin_lock(&lock);
			if (!ret) {
				(void)shim_strlcpy(filename, tmp, sizeof(filename));
				dev_path = filename;
				(void)shim_pthread_spin_unlock(&lock);
				stress_bad_ioctl_rw(args, false);
				inc_counter(args);
			}
			break;
		default:
			break;
		}
	}
done:
	stress_dirent_list_free(dlist, n);
}

/*
 *  stress_bad_ioctl
 *	stress read-only ioctls on all of /dev
 */
static int stress_bad_ioctl(const stress_args_t *args)
{
	pthread_t pthreads[MAX_DEV_THREADS];
	int ret[MAX_DEV_THREADS], rc = EXIT_SUCCESS;
	stress_pthread_args_t pa;

	dev_path = "/dev/null";
	pa.args = args;
	pa.data = NULL;

	(void)memset(ret, 0, sizeof(ret));

	do {
		pid_t pid;

again:
		if (!keep_stressing())
			break;
		pid = fork();
		if (pid < 0) {
			if ((errno == EAGAIN) || (errno == ENOMEM))
				goto again;
		} else if (pid > 0) {
			int status, wret;

			(void)setpgid(pid, g_pgrp);
			/* Parent, wait for child */
			wret = shim_waitpid(pid, &status, 0);
			if (wret < 0) {
				if (errno != EINTR)
					pr_dbg("%s: waitpid(): errno=%d (%s)\n",
						args->name, errno, strerror(errno));
				(void)kill(pid, SIGTERM);
				(void)kill(pid, SIGKILL);
				(void)shim_waitpid(pid, &status, 0);
			} else {
				if (WIFEXITED(status) &&
				    WEXITSTATUS(status) != 0) {
					rc = EXIT_FAILURE;
					break;
				}
			}
		} else if (pid == 0) {
			size_t i;
			int r, rc;

			rc = sigsetjmp(jmp_env, 1);
			if (rc != 0) {
				pr_err("%s: caught an unexpected segmentation fault\n", args->name);
				_exit(EXIT_FAILURE);
			}

			if (stress_sighandler(args->name, SIGSEGV, stress_segv_handler, NULL) < 0)
				_exit(EXIT_NO_RESOURCE);

			dev_hash_table = stress_hash_create(251);
			if (!dev_hash_table) {
				pr_err("%s: cannot create device hash table: %d (%s))\n",
					args->name, errno, strerror(errno));
				_exit(EXIT_NO_RESOURCE);
			}

			(void)setpgid(0, g_pgrp);
			stress_parent_died_alarm();
			(void)sched_settings_apply(true);
			rc = shim_pthread_spin_init(&lock, SHIM_PTHREAD_PROCESS_SHARED);
			if (rc) {
				pr_inf("%s: pthread_spin_init failed, errno=%d (%s)\n",
					args->name, rc, strerror(rc));
				_exit(EXIT_NO_RESOURCE);
			}

			/* Make sure this is killable by OOM killer */
			stress_set_oom_adjustment(args->name, true);
			mixup = stress_mwc32();

			for (i = 0; i < MAX_DEV_THREADS; i++) {
				ret[i] = pthread_create(&pthreads[i], NULL,
						stress_bad_ioctl_thread, (void *)&pa);
			}

			do {
				stress_bad_ioctl_dir(args, "/dev", true, 0);
			} while (keep_stressing());

			r = shim_pthread_spin_lock(&lock);
			if (r) {
				pr_dbg("%s: failed to lock spin lock for dev_path\n", args->name);
			} else {
				dev_path = "";
				r = shim_pthread_spin_unlock(&lock);
				(void)r;
			}

			for (i = 0; i < MAX_DEV_THREADS; i++) {
				if (ret[i] == 0)
					(void)pthread_join(pthreads[i], NULL);
			}
			stress_hash_delete(dev_hash_table);
			_exit(EXIT_SUCCESS);
		}
	} while (keep_stressing());

	(void)shim_pthread_spin_destroy(&lock);

	return rc;
}
stressor_info_t stress_bad_ioctl_info = {
	.stressor = stress_bad_ioctl,
	.supported = stress_bad_ioctl_supported,
	.class = CLASS_DEV | CLASS_OS | CLASS_PATHOLOGICAL,
	.help = help
};
#else
stressor_info_t stress_bad_ioctl_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_DEV | CLASS_OS | CLASS_PATHOLOGICAL,
	.help = help
};
#endif
