/*
 * Copyright (C) 2014-2021 Canonical, Ltd.
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
	{ NULL,	"procfs N",	"start N workers reading portions of /proc" },
	{ NULL,	"procfs-ops N",	"stop procfs workers after N bogo read operations" },
	{ NULL,	NULL,		NULL }
};

#if defined(HAVE_LIB_PTHREAD) && defined(__linux__)

#define PROC_BUF_SZ		(4096)
#define MAX_PROCFS_THREADS	(4)

typedef struct stress_ctxt {
	const stress_args_t *args;
	const char *path;
	bool writeable;
} stress_ctxt_t;

typedef struct {
	const char *filename;
	void (*stress_func)(const int fd);
} stress_proc_info_t;

#if !defined(NSIO)
#define	NSIO		0xb7
#endif
#if !defined(NS_GET_USERNS)
#define NS_GET_USERNS		_IO(NSIO, 0x1)
#endif
#if !defined(NS_GET_PARENT)
#define NS_GET_PARENT		_IO(NSIO, 0x2)
#endif
#if !defined(NS_GET_NSTYPE)
#define NS_GET_NSTYPE		_IO(NSIO, 0x3)
#endif
#if !defined(NS_GET_OWNER_UID)
#define NS_GET_OWNER_UID	_IO(NSIO, 0x4)
#endif

static sigset_t set;
static shim_pthread_spinlock_t lock;
static char proc_path[PATH_MAX];
static uint32_t mixup;

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

#if defined(HAVE_ASM_MTRR_H) &&		\
    defined(HAVE_MTRR_GENTRY) &&	\
    defined(MTRRIOC_GET_ENTRY)
/*
 *  stress_proc_mtrr()
 *	exercise /proc/mtrr ioctl MTRRIOC_GET_ENTRY
 */
static void stress_proc_mtrr(const int fd)
{
	struct mtrr_gentry gentry;

	(void)memset(&gentry, 0, sizeof(gentry));
	while (ioctl(fd, MTRRIOC_GET_ENTRY, &gentry) == 0) {
		gentry.regnum++;
	}
}
#endif

/*
 *  stress_proc_pci()
 *	exercise PCI PCIIOC_CONTROLLER
 */
#if defined(HAVE_LINUX_PCI_H) &&	\
    defined(PCIIOC_CONTROLLER)
static void stress_proc_pci(const int fd)
{
	int ret;

	ret = ioctl(fd, PCIIOC_CONTROLLER);
	(void)ret;
}

#endif

static stress_proc_info_t stress_proc_info[] = {
#if defined(HAVE_ASM_MTRR_H) &&		\
    defined(HAVE_MTRR_GENTRY) &&	\
    defined(MTRRIOC_GET_ENTRY)
	{ "/proc/mtrr",			stress_proc_mtrr },
#endif
#if defined(HAVE_LINUX_PCI_H) &&	\
    defined(PCIIOC_CONTROLLER)
	{ "/proc/bus/pci/00/00.0",	stress_proc_pci },	/* x86 */
	{ "/proc/bus/pci/0000:00/00.0",	stress_proc_pci },	/* RISC-V */
#endif
};

/*
 *  stress_proc_rw()
 *	read a proc file
 */
static inline void stress_proc_rw(
	const stress_ctxt_t *ctxt,
	int32_t loops)
{
	int fd;
	ssize_t ret;
	char buffer[PROC_BUF_SZ];
	char path[PATH_MAX];
	const double threshold = 0.2;
	const size_t page_size = ctxt->args->page_size;
	off_t pos;

	while (loops == -1 || loops > 0) {
		double t_start;
		bool timeout = false;
		uint8_t *ptr;
		struct stat statbuf;
		bool writeable = true;
		size_t len;
		ssize_t i;

		ret = shim_pthread_spin_lock(&lock);
		if (ret)
			return;
		(void)shim_strlcpy(path, proc_path, sizeof(path));
		(void)shim_pthread_spin_unlock(&lock);

		if (!*path || !keep_stressing_flag())
			break;

		if (!strncmp(path, "/proc/self", 10))
			writeable = false;
		if (!strncmp(path, "/proc", 5) && isdigit(path[5]))
			writeable = false;

		t_start = stress_time_now();

		if ((fd = open(path, O_RDONLY | O_NONBLOCK)) < 0)
			return;

		if (stress_time_now() - t_start > threshold) {
			timeout = true;
			(void)close(fd);
			goto next;
		}

		/*
		 *  Check if there any special features to exercise
		 */
		for (i = 0; i < (ssize_t)SIZEOF_ARRAY(stress_proc_info); i++) {
			if (!strcmp(path, stress_proc_info[i].filename)) {
				stress_proc_info[i].stress_func(fd);
				break;
			}
		}

		/*
		 *  fstat the file
		 */
		ret = fstat(fd, &statbuf);

#if defined(__linux__)
		/*
		 *  Linux name space symlinks can be exercised
		 *  with some special name space ioctls:
		 */
		if (statbuf.st_mode & S_IFLNK) {
			if (!strncmp(path, "/proc/self", 10) && (strstr(path, "/ns/"))) {
				int ns_fd;
				uid_t uid;

				ns_fd = ioctl(fd, NS_GET_USERNS);
				if (ns_fd >= 0)
					(void)close(ns_fd);
				ns_fd = ioctl(fd, NS_GET_PARENT);
				if (ns_fd >= 0)
					(void)close(ns_fd);
				ret = ioctl(fd, NS_GET_NSTYPE);
				(void)ret;
				/* The following returns -EINVAL */
				ret = ioctl(fd, NS_GET_OWNER_UID, &uid);
				(void)ret;
			}
		}
#endif

		/*
		 *  Multiple randomly sized reads
		 */
		for (i = 0; i < 4096 * PROC_BUF_SZ; i++) {
			ssize_t sz = 1 + (stress_mwc32() % sizeof(buffer));
			if (!keep_stressing_flag())
				break;
			ret = read(fd, buffer, sz);
			if (ret < 0)
				break;
			if (ret < sz)
				break;
			i += sz;

			if (stress_time_now() - t_start > threshold) {
				timeout = true;
				(void)close(fd);
				goto next;
			}
		}
		(void)close(fd);

		/* Multiple 1 char sized reads */
		if ((fd = open(path, O_RDONLY | O_NONBLOCK)) < 0)
			return;
		if (stress_time_now() - t_start > threshold) {
			timeout = true;
			(void)close(fd);
			goto next;
		}
		for (i = 0; ; i++) {
			if (!keep_stressing_flag())
				break;
			ret = read(fd, buffer, 1);
			if (ret < 1)
				break;
			if (stress_time_now() - t_start > threshold) {
				timeout = true;
				(void)close(fd);
				goto next;
			}
		}
		(void)close(fd);

		if ((fd = open(path, O_RDONLY | O_NONBLOCK)) < 0)
			return;
		if (stress_time_now() - t_start > threshold) {
			timeout = true;
			(void)close(fd);
			goto next;
		}
		/*
		 *  Zero sized reads
		 */
		ret = read(fd, buffer, 0);
		if (ret < 0)
			goto err;

		if (stress_time_now() - t_start > threshold) {
			timeout = true;
			(void)close(fd);
			goto next;
		}
		/*
		 *  Broken offset reads, see Linux commit
		 *  3bfa7e141b0bbb818b25e0daafb65aee92e49ac4
		 *  "fs/seq_file.c: seq_read(): add info message
		 *  about buggy .next functions"
		 */
		pos = lseek(fd, 0, SEEK_SET);
		if (pos < 0)
			goto mmap_test;
		ret = read(fd, buffer, sizeof(buffer));
		if (ret < 0)
			goto mmap_test;
		if (ret < (ssize_t)(sizeof(buffer) >> 1)) {
			char *bptr;

			for (bptr = buffer; *bptr && *bptr != '\n'; bptr++)
				;
			if (*bptr == '\n') {
				const off_t offset = 2 + (bptr - buffer);

				pos = lseek(fd, offset, SEEK_SET);
				if (pos == offset) {
					/* Causes incorrect 2nd read */
					ret = read(fd, buffer, sizeof(buffer));
					(void)ret;
				}
			}
		}

mmap_test:
		/*
		 *  mmap it
		 */
		ptr = mmap(NULL, page_size, PROT_READ,
			MAP_SHARED | MAP_ANONYMOUS, fd, 0);
		if (ptr != MAP_FAILED) {
			stress_uint8_put(*ptr);
			(void)munmap(ptr, page_size);
		}

		if (stress_time_now() - t_start > threshold) {
			timeout = true;
			(void)close(fd);
			goto next;
		}

#if defined(FIONREAD)
		{
			int nbytes;

			/*
			 *  ioctl(), bytes ready to read
			 */
			ret = ioctl(fd, FIONREAD, &nbytes);
			(void)ret;
		}
		if (stress_time_now() - t_start > threshold) {
			timeout = true;
			(void)close(fd);
			goto next;
		}
#endif

#if defined(HAVE_POLL_H)
		{
			struct pollfd fds[1];

			fds[0].fd = fd;
			fds[0].events = POLLIN;
			fds[0].revents = 0;

			ret = poll(fds, 1, 0);
			(void)ret;
		}
#endif

		/*
		 *  Seek and read
		 */
		pos = lseek(fd, 0, SEEK_SET);
		if (pos == (off_t)-1)
			goto err;
		pos = lseek(fd, 1, SEEK_CUR);
		if (pos == (off_t)-1)
			goto err;
		pos = lseek(fd, 0, SEEK_END);
		if (pos == (off_t)-1)
			goto err;
		pos = lseek(fd, 1, SEEK_SET);
		if (pos == (off_t)-1)
			goto err;

		if (stress_time_now() - t_start > threshold) {
			timeout = true;
			(void)close(fd);
			goto next;
		}

		ret = read(fd, buffer, 1);
		(void)ret;
err:
		(void)close(fd);
		if (stress_time_now() - t_start > threshold) {
			timeout = true;
			goto next;
		}

		if (writeable && ctxt->writeable) {
			/*
			 *  Zero sized writes
			 */
			if ((fd = open(path, O_WRONLY | O_NONBLOCK)) < 0)
				return;
			ret = write(fd, buffer, 0);
			(void)ret;
			(void)close(fd);
		}

		/*
		 *  Create /proc/ filename with - corruption to force
		 *  ENOENT procfs open failures
		 */
		len = strlen(path);

		/* /proc + ... */
		if (len > 5) {
			char *pptr = path + 5 + (stress_mwc16() % (len - 5));

			/* Skip over / */
			while (*pptr == '/')
				pptr++;

			if (*pptr) {
				*pptr = '-';

				/*
				 *  Expect ENOENT, but if it does open then
				 *  close it immediately
				 */
				fd = open(path, O_WRONLY | O_NONBLOCK);
				if (fd >= 0)
					(void)close(fd);
			}
		}
next:
		if (loops > 0) {
			if (timeout)
				break;
			loops--;
		}
	}
}

/*
 *  stress_proc_rw_thread
 *	keep exercising a procfs entry until
 *	controlling thread triggers an exit
 */
static void *stress_proc_rw_thread(void *ctxt_ptr)
{
	static void *nowt = NULL;
	stress_ctxt_t *ctxt = (stress_ctxt_t *)ctxt_ptr;

	/*
	 *  Block all signals, let controlling thread
	 *  handle these
	 */
	(void)sigprocmask(SIG_BLOCK, &set, NULL);

	while (keep_stressing_flag()) {
		stress_proc_rw(ctxt, -1);
		if (!*proc_path)
			break;
	}

	return &nowt;
}

/*
 *  stress_proc_dir()
 *	read directory
 */
static void stress_proc_dir(
	const stress_ctxt_t *ctxt,
	const char *path,
	const bool recurse,
	const int depth)
{
	struct dirent **dlist;
	const stress_args_t *args = ctxt->args;
	int32_t loops = args->instance < 8 ? args->instance + 1 : 8;
	int i, n, ret;
	char tmp[PATH_MAX];

	if (!keep_stressing_flag())
		return;

	/* Don't want to go too deep */
	if (depth > 20)
		return;

	mixup = stress_mwc32();
	dlist = NULL;
	n = scandir(path, &dlist, NULL, mixup_sort);
	if (n <= 0) {
		stress_dirent_list_free(dlist, n);
		return;
	}

	/* Non-directories files first */
	for (i = 0; (i < n) && keep_stressing_flag(); i++) {
		struct dirent *d = dlist[i];

		if (stress_is_dot_filename(d->d_name)) {
			free(d);
			dlist[i] = NULL;
			continue;
		}

		if ((d->d_type == DT_REG) || (d->d_type == DT_LNK)) {
			ret = shim_pthread_spin_lock(&lock);
			if (!ret) {
				(void)stress_mk_filename(tmp, sizeof(tmp), path, d->d_name);
				(void)shim_strlcpy(proc_path, tmp, sizeof(proc_path));
				(void)shim_pthread_spin_unlock(&lock);


				stress_proc_rw(ctxt, loops);
				inc_counter(args);
			}
			free(d);
			dlist[i] = NULL;
		}
	}

	if (!recurse) {
		stress_dirent_list_free(dlist, n);
		return;
	}

	/* Now recurse on directories */
	for (i = 0; i < n && keep_stressing_flag(); i++) {
		struct dirent *d = dlist[i];

		if (d && d->d_type == DT_DIR) {
			(void)stress_mk_filename(tmp, sizeof(tmp), path, d->d_name);

			free(d);
			dlist[i] = NULL;

			stress_proc_dir(ctxt, tmp, recurse, depth + 1);
			inc_counter(args);
		}
	}
	stress_dirent_list_free(dlist, n);
}

/*
 *  stress_random_pid()
 *	return /proc/$pid where pid is a random existing process ID
 */
static char *stress_random_pid(void)
{
	struct dirent **dlist = NULL;
	static char path[PATH_MAX];
	int i, n;
	unsigned int j;

	(void)shim_strlcpy(path, "/proc/self", sizeof(path));

	n = scandir("/proc", &dlist, NULL, mixup_sort);
	if (!n) {
		stress_dirent_list_free(dlist, n);
		return path;
	}

	/*
	 *  try 32 random probes before giving up
	 */
	for (i = 0, j = 0; i < 32; i++) {
		char *name;
		j += stress_mwc32();
		j %= n;

		name = dlist[j]->d_name;

		if (isdigit(name[0])) {
			(void)stress_mk_filename(path, sizeof(path), "/proc", name);
			break;
		}
	}

	stress_dirent_list_free(dlist, n);
	return path;
}

/*
 *  stress_dirent_proc_prune()
 *	remove . and .. and pid files from directory list
 */
static int stress_dirent_proc_prune(struct dirent **dlist, const int n)
{
	int i, j;

	for (i = 0, j = 0; i < n; i++) {
		if (dlist[i]) {
			if (stress_is_dot_filename(dlist[i]->d_name) ||
			    isdigit((int)dlist[i]->d_name[0])) {
				free(dlist[i]);
				dlist[i] = NULL;
			} else {
				dlist[j] = dlist[i];
				j++;
			}
		}
	}
	return j;
}

/*
 *  stress_procfs
 *	stress reading all of /proc
 */
static int stress_procfs(const stress_args_t *args)
{
	int i, n;
	pthread_t pthreads[MAX_PROCFS_THREADS];
	int rc, ret[MAX_PROCFS_THREADS];
	stress_ctxt_t ctxt;
	struct dirent **dlist = NULL;

	n = scandir("/proc", &dlist, NULL, alphasort);
	if (n <= 0) {
		pr_inf("%s: no /sys entries found, skipping stressor\n", args->name);
		return EXIT_NO_RESOURCE;
	}
	n = stress_dirent_proc_prune(dlist, n);

	(void)sigfillset(&set);

	shim_strlcpy(proc_path, "/proc/self", sizeof(proc_path));

	ctxt.args = args;
	ctxt.writeable = (geteuid() != 0);

	rc = shim_pthread_spin_init(&lock, PTHREAD_PROCESS_PRIVATE);
	if (rc) {
		pr_inf("%s: pthread_spin_init failed, errno=%d (%s)\n",
			args->name, rc, strerror(rc));
		return EXIT_NO_RESOURCE;
	}

	(void)memset(ret, 0, sizeof(ret));

	for (i = 0; i < MAX_PROCFS_THREADS; i++) {
		ret[i] = pthread_create(&pthreads[i], NULL,
				stress_proc_rw_thread, &ctxt);
	}

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		size_t j = args->instance % n;

		for (i = 0; i < n; i++) {
			char procfspath[PATH_MAX];
			struct dirent *d = dlist[i];

			if (!keep_stressing(args))
				break;

			stress_mk_filename(procfspath, sizeof(procfspath), "/proc", d->d_name);
			if ((d->d_type == DT_REG) || (d->d_type == DT_LNK)) {
				if (!shim_pthread_spin_lock(&lock)) {
					(void)shim_strlcpy(proc_path, procfspath, sizeof(proc_path));
					(void)shim_pthread_spin_unlock(&lock);

					stress_proc_rw(&ctxt, 8);
					inc_counter(args);
				}
			} else if (d->d_type == DT_DIR) {
				stress_proc_dir(&ctxt, procfspath, true, 0);
			}

			j = (j + args->num_instances) % n;
			inc_counter(args);
		}

		if (!keep_stressing(args))
			break;

		stress_proc_dir(&ctxt, stress_random_pid(), true, 0);

		inc_counter(args);
	} while (keep_stressing(args));

	rc = shim_pthread_spin_lock(&lock);
	if (rc) {
		pr_dbg("%s: failed to lock spin lock for sysfs_path\n", args->name);
	} else {
		shim_strlcpy(proc_path, "", sizeof(proc_path));
		rc = shim_pthread_spin_unlock(&lock);
		(void)rc;
	}

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	for (i = 0; i < MAX_PROCFS_THREADS; i++) {
		if (ret[i] == 0)
			(void)pthread_join(pthreads[i], NULL);
	}
	(void)shim_pthread_spin_destroy(&lock);

	stress_dirent_list_free(dlist, n);

	return EXIT_SUCCESS;
}

stressor_info_t stress_procfs_info = {
	.stressor = stress_procfs,
	.class = CLASS_FILESYSTEM | CLASS_OS,
	.help = help
};
#else
stressor_info_t stress_procfs_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_FILESYSTEM | CLASS_OS,
	.help = help
};
#endif
