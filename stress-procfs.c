/*
 * Copyright (C) 2014-2021 Canonical, Ltd.
 * Copyright (C) 2021-2025 Colin Ian King.
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
#include "core-arch.h"
#include "core-bitops.h"
#include "core-builtin.h"
#include "core-capabilities.h"
#include "core-hash.h"
#include "core-mmap.h"
#include "core-pthread.h"
#include "core-put.h"

#include <ctype.h>
#include <sys/ioctl.h>

#if defined(HAVE_ASM_MTRR_H)
#include <asm/mtrr.h>
#endif

#if defined(HAVE_LINUX_PCI_H)
#include <linux/pci.h>
#endif

#if defined(HAVE_POLL_H)
#include <poll.h>
#endif

static const stress_help_t help[] = {
	{ NULL,	"procfs N",	"start N workers reading portions of /proc" },
	{ NULL,	"procfs-ops N",	"stop procfs workers after N bogo read operations" },
	{ NULL,	NULL,		NULL }
};

#define PROCFS_FLAG_READ	(0x01)
#define PROCFS_FLAG_WRITE	(0x02)
#define PROCFS_FLAG_TIMEOUT	(0x10)
#define PROCFS_FLAG_READ_WRITE	(PROCFS_FLAG_READ | PROCFS_FLAG_WRITE)

#if defined(HAVE_LIB_PTHREAD) &&	\
    (defined(__linux__) || defined(__CYGWIN__))

#define PROC_BUF_SZ		(4096)
#define MAX_PROCFS_THREADS	(4)

typedef struct stress_ctxt {
	stress_args_t *args;
	const char *path;
	bool writeable;
} stress_ctxt_t;

typedef struct {
	const char *filename;
	void (*stress_func)(stress_args_t *args, const int fd);
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

/*
 *  stress_dirent_proc_prune()
 *	remove . and .. and pid files from directory list
 */
static int stress_dirent_proc_prune(struct dirent **dlist, const int n)
{
	int i, j, digit_count = 0;

	for (i = 0, j = 0; i < n; i++) {
		if (dlist[i]) {
			register bool ignore = false;

			if (stress_is_dot_filename(dlist[i]->d_name))
				ignore = true;
			else if (isdigit((unsigned char)dlist[i]->d_name[0])) {
				/* only allow a small numeric files.. */
				ignore = (digit_count > 5);
				digit_count++;
			}

			if (ignore) {
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
 *  stress_proc_scandir()
 *	scan dir with pruning of dot and numeric proc files
 */
static int stress_proc_scandir(
	const char *dirp,
	struct dirent ***namelist,
	int (*filter)(const struct dirent *),
	int (*compar)(const struct dirent **, const struct dirent **))
{
	int n;

	n = scandir(dirp, namelist, filter, compar);
	if (n <= 0)
		return n;
	n = stress_dirent_proc_prune(*namelist, n);
	return n;
}

/*
 *  mixup_hash()
 *	for numerical pids reverse bits to mash order, otherwise
 *	use fast string hash function
 */
static uint32_t mixup_hash(const char *str)
{
	if (isdigit((int)str[0])) {
		const uint32_t val = atol(str);

		return stress_reverse32(val ^ mixup);
	}
	return stress_hash_pjw(str) ^ mixup;

}

static int mixup_sort(const struct dirent **d1, const struct dirent **d2)
{
	register const uint32_t s1 = mixup_hash((*d1)->d_name);
	register const uint32_t s2 = mixup_hash((*d2)->d_name);

	if (s1 == s2)
		return 0;
	return (s1 < s2) ? -1 : 1;
}

/*
 *  stress_proc_self_mem()
 *	check if /proc/self/mem can be mmap'd and read and values
 *	match that of mmap'd page and page read via the fd
 */
static void stress_proc_self_mem(stress_args_t *args, const int fd)
{
	uint8_t *page, *buf;
	const uint8_t rnd = stress_mwc8();
	const size_t page_size = stress_get_page_size();
	off_t offset;

	buf = (uint8_t *)malloc(page_size);
	if (!buf)
		return;

	page = (uint8_t *)mmap(NULL, page_size, PROT_READ | PROT_WRITE,
				MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (page == MAP_FAILED) {
		free(buf);
		return;
	}
	stress_set_vma_anon_name(page, page_size, "proc-self-mem");
	offset = (off_t)(uintptr_t)page;

	(void)shim_memset(page, rnd, page_size);
	if (lseek(fd, offset, SEEK_SET) == offset) {
		ssize_t ret;
		const size_t mem_offset = stress_mwc32modn((uint32_t)page_size);

		ret = read(fd, buf, page_size);
		if ((ret == (ssize_t)page_size) &&
		    (buf[mem_offset] != page[mem_offset])) {
			pr_inf("%s /proc/self/mem read/mmap failure at offset %p, mmap value 0x%2x vs read value 0x%2x\n",
				args->name, page + mem_offset, page[mem_offset], buf[mem_offset]);
		}
	}

	(void)munmap((void *)page, page_size);
	free(buf);
}

#if defined(HAVE_ASM_MTRR_H) &&		\
    defined(HAVE_MTRR_GENTRY) &&	\
    defined(MTRRIOC_GET_ENTRY)
/*
 *  stress_proc_mtrr()
 *	exercise /proc/mtrr ioctl MTRRIOC_GET_ENTRY
 */
static void stress_proc_mtrr(stress_args_t *args, const int fd)
{
	struct mtrr_gentry gentry;
#if defined(HAVE_MTRR_SENTRY)
	struct mtrr_sentry sentry;
#endif
#if defined(HAVE_MTRR_SENTRY)
	const uint32_t base = stress_mwc32() & ~(4095U);
	bool base_unique = true;
#endif

	(void)args;

	(void)shim_memset(&gentry, 0, sizeof(gentry));
	while (ioctl(fd, MTRRIOC_GET_ENTRY, &gentry) == 0) {
#if defined(HAVE_MTRR_SENTRY)
		if (base == gentry.base)
			base_unique = false;
#endif
		gentry.regnum++;

#if defined(HAVE_MTRR_SENTRY) &&	\
    defined(MTRRIOC_SET_ENTRY)
		(void)memset(&sentry, 0, sizeof(sentry));
		sentry.base = gentry.base;
		sentry.size = 0;
		sentry.type = gentry.type;
		VOID_RET(int, ioctl(fd, MTRRIOC_SET_ENTRY, &sentry));

		(void)memset(&sentry, 0, sizeof(sentry));
		sentry.base = gentry.base;
		sentry.size = gentry.size;
		sentry.type = ~0;
		VOID_RET(int, ioctl(fd, MTRRIOC_SET_ENTRY, &sentry));

		(void)memset(&sentry, 0, sizeof(sentry));
		sentry.base = gentry.base;
		sentry.size = ~4095;
		sentry.type = gentry.type;
		VOID_RET(int, ioctl(fd, MTRRIOC_SET_ENTRY, &sentry));
#endif
	}

#if defined(HAVE_MTRR_SENTRY)
	if (!base_unique)
		return;

#if defined(MTRRIOC_DEL_ENTRY)
	/* delete non-existent mtrr, will fail */
	sentry.base = base;
	sentry.size = 4096;
	sentry.type = 1;
	VOID_RET(int, ioctl(fd, MTRRIOC_DEL_ENTRY, &sentry));
#endif

#if defined(MTRRIOC_KILL_ENTRY)
	/* kill non-existent mtrr, will fail */
	sentry.base = base;
	sentry.size = 4096;
	sentry.type = 1;
	VOID_RET(int, ioctl(fd, MTRRIOC_KILL_ENTRY, &sentry));
#endif
#endif
}
#endif

/*
 *  stress_proc_pci()
 *	exercise PCI PCIIOC_CONTROLLER
 */
#if defined(HAVE_LINUX_PCI_H) &&	\
    defined(PCIIOC_CONTROLLER) &&	\
    !defined(STRESS_ARCH_SH4)
static void stress_proc_pci(stress_args_t *args, const int fd)
{
	(void)args;

	VOID_RET(int, ioctl(fd, PCIIOC_CONTROLLER));
#if defined(PCIIOC_BASE)
	/* EINVAL ioctl */
	VOID_RET(int, ioctl(fd, PCIIOC_BASE | 0xff));
#endif
}

#endif

static const stress_proc_info_t stress_proc_info[] = {
	{ "/proc/self/mem",		stress_proc_self_mem },
#if defined(HAVE_ASM_MTRR_H) &&		\
    defined(HAVE_MTRR_GENTRY) &&	\
    defined(MTRRIOC_GET_ENTRY)
	{ "/proc/mtrr",			stress_proc_mtrr },
#endif
#if defined(HAVE_LINUX_PCI_H) &&	\
    defined(PCIIOC_CONTROLLER) &&	\
    !defined(STRESS_ARCH_SH4)
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

	while ((loops == -1) || (loops > 0)) {
		double t_start;
		uint8_t *ptr;
		struct stat statbuf;
		size_t len;
		ssize_t i;
		int procfs_flag = PROCFS_FLAG_READ_WRITE;

		ret = shim_pthread_spin_lock(&lock);
		if (ret)
			return;
		(void)shim_strscpy(path, proc_path, sizeof(path));
		(void)shim_pthread_spin_unlock(&lock);

redo:
		if (UNLIKELY(!*path || !stress_continue_flag()))
			break;
		if (!strncmp(path, "/proc/self", 10))
			procfs_flag &= ~PROCFS_FLAG_WRITE;
		if (!strncmp(path, "/proc/", 6) && isdigit((unsigned char)path[6]))
			procfs_flag &= ~PROCFS_FLAG_WRITE;
#if defined(__CYGWIN__)
		/*
		 *  Concurrent access on /proc/$PID/maps and /proc/$PID/ctty
		 *  on Cygwin causes issues (Jul 2025), so skip these
		 */
		if ((!strncmp(path, "/proc/", 6) && isdigit((unsigned char)path[6]))) {
			if (strstr(path, "maps") || strstr(path, "ctty"))
				return;
		}
#endif

		t_start = stress_time_now();
		if ((fd = open(path, O_RDONLY | O_NONBLOCK)) < 0)
			return;

		if ((stress_time_now() - t_start) > threshold)
			goto timeout_close;
		/*
		 *  Check if there any special features to exercise
		 */
		for (i = 0; i < (ssize_t)SIZEOF_ARRAY(stress_proc_info); i++) {
			if (!strcmp(path, stress_proc_info[i].filename)) {
				stress_proc_info[i].stress_func(ctxt->args, fd);
				break;
			}
		}

		/*
		 *  fstat the file, skip char and block devices
		 */
#if defined(S_IFMT)
		ret = shim_fstat(fd, &statbuf);
		if (ret == 0) {
			char linkpath[PATH_MAX + 1];

			switch (statbuf.st_mode & S_IFMT) {
#if defined(S_IFLNK)
			case S_IFLNK:
				/* should never get here */
				ret = shim_readlink(path, linkpath, sizeof(linkpath) - 1);
				(void)close(fd);
				if (ret < 0)
					return;
				linkpath[ret] = '\0';
				(void)shim_strscpy(path, linkpath, sizeof(path));
				goto redo;
#endif
#if defined(S_IFIFO)
			case S_IFIFO:
				/* Avoid reading/writing pipes */
				procfs_flag &= ~PROCFS_FLAG_READ_WRITE;
				break;
#endif
#if defined(S_IFCHR)
			case S_IFCHR:
				/*
				 *  Reading from char devices such as tty devices steals
				 *  user input so avoid using these
				 */
				procfs_flag &= ~PROCFS_FLAG_READ_WRITE;
				break;
#endif
#if defined(S_IFBLK)
			case S_IFBLK:
				/* Avoid reading/writing block devices */
				procfs_flag &= ~PROCFS_FLAG_READ_WRITE;
				break;
#endif
#if defined(S_IFSOCK)
			case S_IFSOCK:
				/* Avoid reading/writing sockets */
				procfs_flag &= ~PROCFS_FLAG_READ_WRITE;
				break;
#endif
			default:
				break;
			}
		}
#endif

#if defined(__linux__)
		/*
		 *  Linux name space symlinks can be exercised
		 *  with some special name space ioctls:
		 */
		if ((ret == 0) && (statbuf.st_mode & S_IFLNK)) {
			if (!strncmp(path, "/proc/self", 10) && (strstr(path, "/ns/"))) {
				int ns_fd;
				uid_t uid;

				ns_fd = ioctl(fd, NS_GET_USERNS);
				if (ns_fd >= 0)
					(void)close(ns_fd);
				ns_fd = ioctl(fd, NS_GET_PARENT);
				if (ns_fd >= 0)
					(void)close(ns_fd);
				VOID_RET(int, ioctl(fd, NS_GET_NSTYPE));
				/* The following returns -EINVAL */
				VOID_RET(int, ioctl(fd, NS_GET_OWNER_UID, &uid));
			}
		}
#endif

		/*
		 *  Random sized reads at non-power of two integers
		 *  on /proc/bus/pci/00 on SH4 5.16 kernels trips
		 *  SIGBUS/SIGSEGV faults. Currently skip this test.
		 */
#if defined(STRESS_ARCH_SH4)
		if (!strncmp(path, "/proc/bus/pci/00", 16))
			goto next;
#endif
		/*
		 *  Multiple randomly sized reads
		 */
		if (procfs_flag & PROCFS_FLAG_READ) {
			for (i = 0; i < 4096 * PROC_BUF_SZ; i++) {
				const ssize_t sz = 1 + stress_mwc32modn((uint32_t)sizeof(buffer));

				if (UNLIKELY(!stress_continue_flag()))
					break;
				ret = read(fd, buffer, (size_t)sz);
				if (ret < 0)
					break;
				if (ret < sz)
					break;
				i += sz;

				if ((stress_time_now() - t_start) > threshold)
					goto timeout_close;
			}
			(void)close(fd);

			/* Multiple 1 char sized reads */
			if (procfs_flag & PROCFS_FLAG_READ) {
				if ((fd = open(path, O_RDONLY | O_NONBLOCK)) < 0)
					return;
				for (i = 0; ; i++) {
					if (UNLIKELY(!stress_continue_flag()))
						break;
					if (((i & 0x0f) == 0) && ((stress_time_now() - t_start) > threshold))
						goto timeout_close;
					ret = read(fd, buffer, 1);
					if (ret < 1)
						break;
				}
				(void)close(fd);
			}

			if ((fd = open(path, O_RDONLY | O_NONBLOCK)) < 0)
				return;
			if ((stress_time_now() - t_start) > threshold)
				goto timeout_close;
			/*
			 *  Zero sized reads
			 */
			ret = read(fd, buffer, 0);
			if (ret < 0)
				goto err;
			/*
			 *  Broken offset reads, see Linux commit
			 *  3bfa7e141b0bbb818b25e0daafb65aee92e49ac4
			 *  "fs/seq_file.c: seq_read(): add info message
			 *  about buggy .next functions"
			 */
			pos = lseek(fd, 0, SEEK_SET);
			if (pos < 0)
				goto mmap_test;
			(void)shim_memset(buffer, 0, sizeof(buffer));
			ret = read(fd, buffer, sizeof(buffer));
			if (ret < 0)
				goto mmap_test;
			if (ret < (ssize_t)(sizeof(buffer) >> 1)) {
				char *bptr;

				for (bptr = buffer; *bptr && (*bptr != '\n'); bptr++)
					;
				if (*bptr == '\n') {
					const off_t offset = 2 + (bptr - buffer);

					pos = lseek(fd, offset, SEEK_SET);
					if (pos == offset) {
						/* Causes incorrect 2nd read */
						VOID_RET(ssize_t, read(fd, buffer, sizeof(buffer)));
					}
				}
			}

			/*
			 *  exercise 13 x 5 byte reads backwards through procfs file to
			 *  ensure we perform some weird misaligned non-word sized reads
			 */
			off_t dec;

			pos = lseek(fd, 0, SEEK_END);
			if (pos < 0)
				goto mmap_test;
			dec = pos / 13;
			if (dec < 1)
				dec = 1;
			while (pos > 0) {
				off_t seek_pos;

				seek_pos = lseek(fd, pos, SEEK_SET);
				if (seek_pos < 0)
					break;
				VOID_RET(ssize_t, read(fd, buffer, 5));

				if (dec > pos)
					dec = pos;
				pos -= dec;
			}
		}

mmap_test:
		if (procfs_flag & PROCFS_FLAG_READ) {
			/*
			 *  mmap it
			 */
			ptr = (uint8_t *)stress_mmap_populate(NULL, page_size, PROT_READ,
				MAP_SHARED | MAP_ANONYMOUS, fd, 0);
			if (ptr != MAP_FAILED) {
				stress_uint8_put(*ptr);
				(void)munmap((void *)ptr, page_size);
			}

			if ((stress_time_now() - t_start) > threshold)
				goto timeout_close;
		}

#if defined(FIONREAD)
		{
			int nbytes;

			/*
			 *  ioctl(), bytes ready to read
			 */
			VOID_RET(int, ioctl(fd, FIONREAD, &nbytes));
		}
		if ((stress_time_now() - t_start) > threshold)
			goto timeout_close;
#endif

#if defined(HAVE_POLL_H) &&	\
    defined(HAVE_POLL)
		{
			struct pollfd fds[1];

			fds[0].fd = fd;
			fds[0].events = POLLIN;
			fds[0].revents = 0;

			VOID_RET(int, poll(fds, 1, 0));
		}
#endif

#if defined(HAVE_POLL_H) &&	\
    defined(HAVE_PPOLL) &&	\
    defined(SIGPIPE)
		{
			struct pollfd fds[1];
			struct timespec ts;
			sigset_t sigmask;

			ts.tv_sec = 0;
                        ts.tv_nsec = 2000;

                        (void)sigemptyset(&sigmask);
                        (void)sigaddset(&sigmask, SIGPIPE);

			fds[0].fd = fd;
			fds[0].events = POLLIN;
			fds[0].revents = 0;

			VOID_RET(int, shim_ppoll(fds, 1, &ts, &sigmask));
		}
#endif

		if (procfs_flag & PROCFS_FLAG_READ) {
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

			if ((stress_time_now() - t_start) > threshold)
				goto timeout_close;

			VOID_RET(ssize_t, read(fd, buffer, 1));
		}
err:
		if ((stress_time_now() - t_start) > threshold)
			goto timeout_close;

		(void)close(fd);

		if ((procfs_flag & PROCFS_FLAG_WRITE) && ctxt->writeable) {
			/*
			 *  Zero sized writes
			 */
			if ((fd = open(path, O_WRONLY | O_NONBLOCK)) < 0)
				return;
			VOID_RET(ssize_t, write(fd, buffer, 0));
			(void)close(fd);
		}

		/*
		 *  Create /proc/ filename with - corruption to force
		 *  ENOENT procfs open failures
		 */
		len = strlen(path);

		/* /proc + ... */
		if (len > 5) {
			char *pptr = path + 5 + stress_mwc16modn(len - 5);

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
			if (procfs_flag & PROCFS_FLAG_TIMEOUT)
				break;
			loops--;
		}
		continue;

timeout_close:
		(void)close(fd);
		procfs_flag |= PROCFS_FLAG_TIMEOUT;
		goto next;
	}
}

/*
 *  stress_proc_rw_thread
 *	keep exercising a procfs entry until
 *	controlling thread triggers an exit
 */
static void *stress_proc_rw_thread(void *ctxt_ptr)
{
	const stress_ctxt_t *ctxt = (stress_ctxt_t *)ctxt_ptr;

	/*
	 *  Block all signals, let controlling thread
	 *  handle these
	 */
	(void)sigprocmask(SIG_BLOCK, &set, NULL);
	(void)pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);

	while (stress_continue_flag()) {
		stress_proc_rw(ctxt, -1);
		if (!*proc_path)
			break;
	}

	return &g_nowt;
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
	stress_args_t *args = ctxt->args;
	int32_t loops = args->instance < 8 ?
			(int32_t)(args->instance + 1) : 8;
	int i, n, ret;
	char tmp[PATH_MAX];

	if (UNLIKELY(!stress_continue_flag()))
		return;

	/* Don't want to go too deep */
	if (depth > 20)
		return;

#if defined(__CYGWIN__)
	/*
	 * Cygwin maps the Windows registry to /proc/registry{,32,64}
	 * (>1M entries, >50K entries on depth 2) and the path names of the
	 * NTDLL layer to /proc/sys, ignore both for now
	 */
	if (!strncmp(path, "/proc/registry", 14) || !strcmp(path, "/proc/sys"))
		return;
#endif

	mixup = stress_mwc32();
	dlist = NULL;
	n = stress_proc_scandir(path, &dlist, NULL, mixup_sort);
	if (n <= 0) {
		stress_dirent_list_free(dlist, n);
		return;
	}

	/* Non-directories files first */
	for (i = 0; LIKELY((i < n) && stress_continue_flag()); i++) {
		struct dirent *d = dlist[i];
		unsigned char type;

		if (stress_is_dot_filename(d->d_name)) {
			free(d);
			dlist[i] = NULL;
			continue;
		}

		type = shim_dirent_type(path, d);
		if ((type == SHIM_DT_REG) || (type == SHIM_DT_LNK)) {
			ret = shim_pthread_spin_lock(&lock);
			if (!ret) {
				(void)stress_mk_filename(tmp, sizeof(tmp), path, d->d_name);
				(void)shim_strscpy(proc_path, tmp, sizeof(proc_path));
				(void)shim_pthread_spin_unlock(&lock);

				stress_proc_rw(ctxt, loops);
				stress_bogo_inc(args);
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
	for (i = 0; LIKELY((i < n) && stress_continue_flag()); i++) {
		struct dirent *d = dlist[i];

		if (d && (shim_dirent_type(path, d) == SHIM_DT_DIR)) {
			(void)stress_mk_filename(tmp, sizeof(tmp), path, d->d_name);

			free(d);
			dlist[i] = NULL;

			stress_proc_dir(ctxt, tmp, recurse, depth + 1);
			stress_bogo_inc(args);
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
	size_t j;

	(void)shim_strscpy(path, "/proc/self", sizeof(path));

	mixup = stress_mwc32();
	n = stress_proc_scandir("/proc", &dlist, NULL, mixup_sort);
	if (!n) {
		stress_dirent_list_free(dlist, n);
		return path;
	}

	/*
	 *  try 32 random probes before giving up
	 */
	for (i = 0, j = 0; i < 32; i++) {
		const char *name;

		j += (size_t)stress_mwc32();
		j %= (size_t)n;

		name = dlist[j]->d_name;

		if (isdigit((unsigned char)name[0])) {
			(void)stress_mk_filename(path, sizeof(path), "/proc", name);
			break;
		}
	}

	stress_dirent_list_free(dlist, n);
	return path;
}

/*
 *  stress_procfs_no_entries()
 *	report when no /proc entries are found
 */
static int stress_procfs_no_entries(stress_args_t *args)
{
	if (stress_instance_zero(args))
		pr_inf_skip("%s: no /proc entries found, skipping stressor\n", args->name);
	return EXIT_NO_RESOURCE;
}

/*
 *  stress_procfs
 *	stress reading all of /proc
 */
static int stress_procfs(stress_args_t *args)
{
	int i, n;
	pthread_t pthreads[MAX_PROCFS_THREADS];
	int rc, ret[MAX_PROCFS_THREADS];
	stress_ctxt_t ctxt;
	struct dirent **dlist = NULL;

	n = stress_proc_scandir("/proc", &dlist, NULL, mixup_sort);
	if (n <= 0)
		return stress_procfs_no_entries(args);

	(void)sigfillset(&set);

	(void)shim_strscpy(proc_path, "/proc/self", sizeof(proc_path));

	ctxt.args = args;
	ctxt.writeable = !stress_check_capability(SHIM_CAP_IS_ROOT);

	rc = shim_pthread_spin_init(&lock, PTHREAD_PROCESS_PRIVATE);
	if (rc) {
		pr_err("%s: pthread_spin_init failed, errno=%d (%s)\n",
			args->name, rc, strerror(rc));
		return EXIT_NO_RESOURCE;
	}

	(void)shim_memset(ret, 0, sizeof(ret));

	for (i = 0; i < MAX_PROCFS_THREADS; i++) {
		ret[i] = pthread_create(&pthreads[i], NULL,
				stress_proc_rw_thread, &ctxt);
	}

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		size_t j = stress_mwc32() % n;

		for (i = 0; i < n; i++) {
			char procfspath[PATH_MAX];
			const struct dirent *d = dlist[j];
			unsigned char type;

			if (UNLIKELY(!stress_continue(args)))
				break;

			stress_mk_filename(procfspath, sizeof(procfspath), "/proc", d->d_name);
			type = shim_dirent_type("/proc", d);
			if ((type == SHIM_DT_REG) || (type == SHIM_DT_LNK)) {
				if (!shim_pthread_spin_lock(&lock)) {
					(void)shim_strscpy(proc_path, procfspath, sizeof(proc_path));
					(void)shim_pthread_spin_unlock(&lock);

					stress_proc_rw(&ctxt, 8);
					stress_bogo_inc(args);
				}
			} else if (type == SHIM_DT_DIR) {
				stress_proc_dir(&ctxt, procfspath, true, 0);
			}

			j = (j + args->instance + 1) % n;
			stress_bogo_inc(args);
		}

		if (UNLIKELY(!stress_continue(args)))
			break;

		stress_proc_dir(&ctxt, stress_random_pid(), true, 0);

		stress_bogo_inc(args);
	} while (stress_continue(args));

	rc = shim_pthread_spin_lock(&lock);
	if (rc) {
		pr_dbg("%s: spin lock failed for %s\n", args->name, proc_path);
	} else {
		(void)shim_strscpy(proc_path, "", sizeof(proc_path));
		VOID_RET(int, shim_pthread_spin_unlock(&lock));
	}

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	for (i = 0; i < MAX_PROCFS_THREADS; i++) {
		if (ret[i] == 0) {
			(void)pthread_cancel(pthreads[i]);
			(void)pthread_join(pthreads[i], NULL);
		}
	}
	(void)shim_pthread_spin_destroy(&lock);

	stress_dirent_list_free(dlist, n);

	return EXIT_SUCCESS;
}

const stressor_info_t stress_procfs_info = {
	.stressor = stress_procfs,
	.classifier = CLASS_FILESYSTEM | CLASS_OS,
	.help = help
};
#else
const stressor_info_t stress_procfs_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_FILESYSTEM | CLASS_OS,
	.help = help,
	.unimplemented_reason = "built without librt or only supported on Linux"
};
#endif
