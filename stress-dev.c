/*
 * Copyright (C) 2013-2018 Canonical, Ltd.
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

#if defined(HAVE_LIB_PTHREAD) && !defined(__sun__)

#include <poll.h>
#include <termios.h>

#if defined(HAVE_LINUX_MEDIA_H)
#include <linux/media.h>
#endif
#if defined(HAVE_LINUX_VT_H)
#include <linux/vt.h>
#endif
#if defined(HAVE_LINUX_DM_IOCTL_H)
#include <linux/dm-ioctl.h>
#endif
#if defined(HAVE_LINUX_VIDEODEV2_H)
#include <linux/videodev2.h>
#endif
#if defined(HAVE_SCSI_SCSI_H)
#include <scsi/scsi.h>
#endif
#if defined(HAVE_SCSI_SG_H)
#include <scsi/sg.h>
#endif
#if defined(HAVE_LINUX_RANDOM_H)
#include <linux/random.h>
#endif

#define MAX_DEV_THREADS		(4)

static sigset_t set;
static shim_pthread_spinlock_t lock;
static char *dev_path;
static uint32_t mixup;

typedef struct {
	const char *devpath;
	const size_t devpath_len;
	void (*func)(const char *name, const int fd, const char *devpath);
} dev_func_t;

static uint32_t path_sum(const char *path)
{
	const char *ptr = path;
	register uint32_t h = mixup;

	while (*ptr) {
		register uint32_t g;

		h = (h << 4) + (*(ptr++));
		if (0 != (g = h & 0xf0000000)) {
			h ^= (g >> 24);
			h ^= g;
		}
	}
	return h;
}

#if defined(__NetBSD__)
static int mixup_sort(const void *p1, const void *p2)
{
	uint32_t s1, s2;
	const struct dirent *d1 = p1;
	const struct dirent *d2 = p2;

	s1 = path_sum(d1->d_name);
	s2 = path_sum(d2->d_name);

	if (s1 == s2)
		return 0;
	return (s1 < s2) ? -1 : 1;
}
#else
static int mixup_sort(const struct dirent **d1, const struct dirent **d2)
{
	uint32_t s1, s2;

	s1 = path_sum((*d1)->d_name);
	s2 = path_sum((*d2)->d_name);

	if (s1 == s2)
		return 0;
	return (s1 < s2) ? -1 : 1;
}
#endif

#if defined(__linux__) && defined(HAVE_LINUX_MEDIA_H) && \
    defined(MEDIA_IOC_DEVICE_INFO)
static void stress_dev_media_linux(const char *name, const int fd, const char *devpath)
{
	(void)name;
	(void)fd;
	(void)devpath;

#if defined(MEDIA_IOC_DEVICE_INFO)
	{
		struct media_device_info mdi;
		int ret;

		ret = ioctl(fd, MEDIA_IOC_DEVICE_INFO, &mdi);
		if (ret < 0)
			return;

		if (!mdi.driver[0])
			pr_inf("%s: ioctl MEDIA_IOC_DEVICE_INFO %s: null driver name\n",
				name, devpath);
		if (!mdi.model[0])
			pr_inf("%s: ioctl MEDIA_IOC_DEVICE_INFO %s: null model name\n",
				name, devpath);
		if (!mdi.bus_info[0])
			pr_inf("%s: ioctl MEDIA_IOC_DEVICE_INFO %s: null bus_info field\n",
				name, devpath);
	}
#endif
}
#endif

#if defined(HAVE_LINUX_VT_H)
static void stress_dev_vcs_linux(const char *name, const int fd, const char *devpath)
{
	(void)name;
	(void)fd;
	(void)devpath;

#if defined(VT_GETMODE)
	{
		struct vt_mode mode;
		int ret;

		ret = ioctl(fd, VT_GETMODE, &mode);
		(void)ret;
	}
#endif
#if defined(VT_GETSTATE)
	{
		struct vt_stat vt_stat;
		int ret;

		ret = ioctl(fd, VT_GETSTATE, &vt_stat);
		(void)ret;
	}
#endif
}
#endif

#if defined(HAVE_LINUX_DM_IOCTL_H)
static void stress_dev_dm_linux(const char *name, const int fd, const char *devpath)
{
	(void)name;
	(void)fd;
	(void)devpath;

#if defined(DM_VERSION)
	{
		struct dm_ioctl dm;
		int ret;

		ret = ioctl(fd, DM_VERSION, &dm);
		(void)ret;
	}
#endif
#if defined(DM_STATUS)
	{
		struct dm_ioctl dm;
		int ret;

		ret = ioctl(fd, DM_STATUS, &dm);
		(void)ret;
	}
#endif
}
#endif

#if defined(HAVE_LINUX_VIDEODEV2_H)
static void stress_dev_video_linux(const char *name, const int fd, const char *devpath)
{
	(void)name;
	(void)fd;
	(void)devpath;

#if defined(VIDIOC_QUERYCAP)
	{
		struct v4l2_capability c;
		int ret;

		ret = ioctl(fd, VIDIOC_QUERYCAP, &c);
		(void)ret;
	}
#endif
}
#endif

#if defined(TCGETS)
static void stress_dev_tty(const char *name, const int fd, const char *devpath)
{
	int ret;
	struct termios t;

	(void)name;
	(void)devpath;

	ret = tcgetattr(fd, &t);
	(void)ret;
#if defined(TCGETS)
	{
		ret = ioctl(fd, TCGETS, &t);
		(void)ret;
	}
#endif
#if defined(TIOCGPTLCK)
	{
		int lck;

		ret = ioctl(fd, TIOCGPTLCK, &lck);
		(void)ret;
	}
#endif
#if defined(TIOCGPKT)
	{
		int pktmode;

		ret = ioctl(fd, TIOCGPKT, &pktmode);
		(void)ret;
	}
#endif
#if defined(TIOCGPTN)
	{
		int ptnum;

		ret = ioctl(fd, TIOCGPTN, &ptnum);
		(void)ret;
	}
#endif
#if defined(TIOCGWINSZ)
	{
		struct winsize ws;

		ret = ioctl(fd, TIOCGWINSZ, &ws);
		(void)ret;
	}
#endif
#if defined(FIONREAD)
	{
		int n;

		ret = ioctl(fd, FIONREAD, &n);
		(void)ret;
	}
#endif
#if defined(TIOCINQ)
	{
		int n;

		ret = ioctl(fd, TIOCINQ, &n);
		(void)ret;
	}
#endif
#if defined(TIOCOUTQ)
	{
		int n;

		ret = ioctl(fd, TIOCOUTQ, &n);
		(void)ret;
	}
#endif
#if defined(TIOCGPGRP)
	{
		pid_t pgrp;

		ret = ioctl(fd, TIOCGPGRP, &pgrp);
		(void)ret;
	}
#endif
#if defined(TIOCGSID)
	{
		pid_t gsid;

		ret = ioctl(fd, TIOCGSID, &gsid);
		(void)ret;
	}
#endif
#if defined(TIOCGEXCL)
	{
		int excl;

		ret = ioctl(fd, TIOCGEXCL, &excl);
		(void)ret;
	}
#endif
#if defined(TIOCGETD)
	{
		int ldis;

		ret = ioctl(fd, TIOCGETD, &ldis);
		(void)ret;
	}
#endif
	/* Modem */
#if defined(TIOCGSOFTCAR)
	{
		int flag;

		ret = ioctl(fd, TIOCGSOFTCAR, &flag);
		(void)ret;
	}
#endif
}
#endif

/*
 *  stress_dev_blk()
 *	block device specific ioctls
 */
static void stress_dev_blk(const char *name, const int fd, const char *devpath)
{
	(void)name;
	(void)fd;
	(void)devpath;

#if defined(BLKFLSBUF)
	{
		int ret;
		ret = ioctl(fd, BLKFLSBUF, 0);
		(void)ret;
	}
#endif
#if defined(BLKRAGET)
	/* readahead */
	{
		unsigned long ra;
		int ret;

		ret = ioctl(fd, BLKRAGET, &ra);
		(void)ret;
	}
#endif
#if defined(BLKROGET)
	/* readonly state */
	{
		int ret, ro;

		ret = ioctl(fd, BLKROGET, &ro);
		(void)ret;
	}
#endif
#if defined(BLKBSZGET)
	/* get block device soft block size */
	{
		int ret, sz;

		ret = ioctl(fd, BLKBSZGET, &sz);
		(void)ret;
	}
#endif
#if defined(BLKPBSZGET)
	/* get block device physical block size */
	{
		unsigned int sz;
		int ret;

		ret = ioctl(fd, BLKPBSZGET, &sz);
		(void)ret;
	}
#endif
#if defined(BLKIOMIN)
	{
		unsigned int sz;
		int ret;

		ret = ioctl(fd, BLKIOMIN, &sz);
		(void)ret;
	}
#endif
#if defined(BLKIOOPT)
	{
		unsigned int sz;
		int ret;

		ret = ioctl(fd, BLKIOOPT, &sz);
		(void)ret;
	}
#endif
#if defined(BLKALIGNOFF)
	{
		unsigned int sz;
		int ret;

		ret = ioctl(fd, BLKALIGNOFF, &sz);
		(void)ret;
	}
#endif
#if defined(BLKROTATIONAL)
	{
		unsigned short rotational;
		int ret;

		ret = ioctl(fd, BLKROTATIONAL, &rotational);
		(void)ret;
	}
#endif
#if defined(BLKSECTGET)
	{
		unsigned short max_sectors;
		int ret;

		ret = ioctl(fd, BLKSECTGET, &max_sectors);
		(void)ret;
	}
#endif
#if defined(BLKGETSIZE)
	{
		unsigned long sz;
		int ret;

		ret = ioctl(fd, BLKGETSIZE, &sz);
		(void)ret;
	}
#endif
#if defined(BLKGETSIZE64)
	{
		uint64_t sz;
		int ret;

		ret = ioctl(fd, BLKGETSIZE64, &sz);
		(void)ret;
	}
#endif
#if defined(FIBMAP)
	{
		int ret, block = 0;

		ret = ioctl(fd, FIBMAP, &block);
		(void)ret;
	}
#endif
}

/*
 *  stress_dev_scsi_blk()
 *	SCSI block device specific ioctls
 */
static void stress_dev_scsi_blk(const char *name, const int fd, const char *devpath)
{
	int ret;

	(void)name;
	(void)fd;
	(void)devpath;
	(void)ret;

#if defined(SG_GET_VERSION_NUM)
	{
		int ver;

		ret = ioctl(fd, SG_GET_VERSION_NUM, &ver);
		(void)ret;
	}
#endif
#if defined(SCSI_IOCTL_GET_IDLUN)
	{
		int lun;

		ret = ioctl(fd, SCSI_IOCTL_GET_IDLUN, &lun);
		(void)ret;
	}
#endif
#if defined(SCSI_IOCTL_GET_BUS_NUMBER)
	{
		int bus;

		ret = ioctl(fd, SCSI_IOCTL_GET_BUS_NUMBER, &bus);
		(void)ret;
	}
#endif
#if defined(SCSI_IOCTL_GET_TIMEOUT)
	{
		ret = ioctl(fd, SCSI_IOCTL_GET_TIMEOUT, 0);
		(void)ret;
	}
#endif
#if defined(SCSI_IOCTL_GET_RESERVED_SIZE)
	{
		int sz;

		ret = ioctl(fd, SCSI_IOCTL_GET_RESERVED_SIZE, &sz);
		(void)ret;
	}
#endif
}

#if defined(__linux__)
static void stress_dev_random_linux(const char *name, const int fd, const char *devpath)
{
	(void)name;
	(void)fd;
	(void)devpath;

#if defined(RNDGETENTCNT)
	{
		long entropy;
		int ret;

		ret = ioctl(fd, RNDGETENTCNT, &entropy);
		(void)ret;
	}
#endif
}
#endif

#if defined(__linux__)

static void stress_dev_mem_mmap_linux(const int fd, const bool read_page)
{
	void *ptr;
	const size_t page_size = stress_get_pagesize();

	ptr = mmap(NULL, page_size, PROT_READ, MAP_PRIVATE, fd, 0);
	if (ptr != MAP_FAILED) {
		munmap(ptr, page_size);
	}
	if (read_page) {
		char buffer[page_size];
		ssize_t ret;

		ret = read(fd, buffer, page_size);
		(void)ret;
	}

	ptr = mmap(NULL, page_size, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
	if (ptr != MAP_FAILED) {
		munmap(ptr, page_size);
	}

}

static void stress_dev_mem_linux(const char *name, const int fd, const char *devpath)
{
	(void)name;
	(void)devpath;

	stress_dev_mem_mmap_linux(fd, false);
}
#endif

#if defined(__linux__)
static void stress_dev_kmem_linux(const char *name, const int fd, const char *devpath)
{
	(void)name;
	(void)devpath;

	stress_dev_mem_mmap_linux(fd, false);
}
#endif

#if defined(__linux__)
static void stress_dev_kmsg_linux(const char *name, const int fd, const char *devpath)
{
	(void)name;
	(void)devpath;

	stress_dev_mem_mmap_linux(fd, true);
}
#endif

#if defined(__linux__) && defined(STRESS_X86)
static void stress_dev_port_linux(const char *name, const int fd, const char *devpath)
{
	off_t off;
	uint8_t *ptr;
	const size_t page_size = stress_get_pagesize();

	(void)name;
	(void)devpath;

	/* seek and read port 0x80 */
	off = lseek(fd, (off_t)0x80, SEEK_SET);
	if (off == 0) {
		char data[1];
		ssize_t ret;

		ret = read(fd, data, sizeof(data));
		(void)ret;
	}

	/* Should fail */
	ptr = mmap(NULL, page_size, PROT_READ, MAP_PRIVATE, fd, 0);
	if (ptr != MAP_FAILED)
		munmap(ptr, page_size);
}
#endif

static void stress_dev_null_nop(const char *name, const int fd, const char *devpath)
{
	(void)name;
	(void)fd;
	(void)devpath;
}

#define DEV_FUNC(dev, func) \
	{ dev, sizeof(dev) - 1, func }

static const dev_func_t dev_funcs[] = {
#if defined(__linux__) && defined(HAVE_LINUX_MEDIA_H) && \
    defined(MEDIA_IOC_DEVICE_INFO)
	DEV_FUNC("/dev/media",	stress_dev_media_linux),
#endif
#if defined(HAVE_LINUX_VT_H)
	DEV_FUNC("/dev/vcs",	stress_dev_vcs_linux),
#endif
#if defined(HAVE_LINUX_DM_IOCTL_H)
	DEV_FUNC("/dev/dm",	stress_dev_dm_linux),
#endif
#if defined(HAVE_LINUX_VIDEODEV2_H)
	DEV_FUNC("/dev/video",	stress_dev_video_linux),
#endif
#if defined(__linux__)
	DEV_FUNC("/dev/random",	stress_dev_random_linux),
	DEV_FUNC("/dev/mem",	stress_dev_mem_linux),
	DEV_FUNC("/dev/kmem",	stress_dev_kmem_linux),
	DEV_FUNC("/dev/kmsg",	stress_dev_kmsg_linux),
#endif
#if defined(__linux__) && defined(STRESS_X86)
	DEV_FUNC("/dev/port",	stress_dev_port_linux),
#endif	
	DEV_FUNC("/dev/null",	stress_dev_null_nop),
};

/*
 *  stress_dev_rw()
 *	exercise a dev entry
 */
static inline void stress_dev_rw(
	const args_t *args,
	int32_t loops)
{
	int fd, ret;
	off_t off;
	struct stat buf;
	struct pollfd fds[1];
	fd_set rfds;
	void *ptr;
	size_t i;
	char path[PATH_MAX];
	const double threshold = 0.25;

	while (loops == -1 || loops > 0) {
		double t_start;
		bool timeout = false;
#if defined(TCGETS)
		struct termios tios;
#endif

		ret = shim_pthread_spin_lock(&lock);
		if (ret)
			return;
		shim_strlcpy(path, dev_path, sizeof(path));
		(void)shim_pthread_spin_unlock(&lock);

		if (!dev_path || !g_keep_stressing_flag)
			break;

		t_start = time_now();

		if ((fd = open(path, O_RDONLY | O_NONBLOCK)) < 0)
			goto rdwr;

		if (time_now() - t_start > threshold) {
			timeout = true;
			(void)close(fd);
			goto next;
		}

		if (fstat(fd, &buf) < 0) {
			pr_fail_err("stat");
		} else {
			if ((S_ISBLK(buf.st_mode) | (S_ISCHR(buf.st_mode))) == 0) {
				(void)close(fd);
				goto next;
			}
		}

		if (buf.st_mode & S_IFBLK) {
			stress_dev_blk(args->name, fd, path);
			stress_dev_scsi_blk(args->name, fd, path);
		}
#if defined(TCGETS)
		if (ioctl(fd, TCGETS, &tios) == 0)
			stress_dev_tty(args->name, fd, path);
#endif

		off = lseek(fd, 0, SEEK_SET);
		(void)off;
		off = lseek(fd, 0, SEEK_CUR);
		(void)off;
		off = lseek(fd, 0, SEEK_END);
		(void)off;

		if (time_now() - t_start > threshold) {
			timeout = true;
			(void)close(fd);
			goto next;
		}

		FD_ZERO(&rfds);
		fds[0].fd = fd;
		fds[0].events = POLLIN;
		ret = poll(fds, 1, 0);
		(void)ret;

		if (time_now() - t_start > threshold) {
			timeout = true;
			(void)close(fd);
			goto next;
		}

#if !defined(__NetBSD__)
		{
			struct timeval tv;
			fd_set wfds;

			FD_ZERO(&rfds);
			FD_SET(fd, &rfds);
			FD_ZERO(&wfds);
			FD_SET(fd, &wfds);
			tv.tv_sec = 0;
			tv.tv_usec = 10000;
			ret = select(fd + 1, &rfds, &wfds, NULL, &tv);
			(void)ret;

			if (time_now() - t_start > threshold) {
				timeout = true;
				(void)close(fd);
				goto next;
			}
		}
#endif

#if defined(F_GETFD)
		ret = fcntl(fd, F_GETFD, NULL);
		(void)ret;

		if (time_now() - t_start > threshold) {
			timeout = true;
			(void)close(fd);
			goto next;
		}
#endif
#if defined(F_GETFL)
		ret = fcntl(fd, F_GETFL, NULL);
		(void)ret;

		if (time_now() - t_start > threshold) {
			timeout = true;
			(void)close(fd);
			goto next;
		}
#endif
#if defined(F_GETSIG)
		ret = fcntl(fd, F_GETSIG, NULL);
		(void)ret;

		if (time_now() - t_start > threshold) {
			timeout = true;
			(void)close(fd);
			goto next;
		}
#endif
		ptr = mmap(NULL, args->page_size, PROT_READ, MAP_PRIVATE, fd, 0);
		if (ptr != MAP_FAILED)
			munmap(ptr, args->page_size);
		ptr = mmap(NULL, args->page_size, PROT_READ, MAP_SHARED, fd, 0);
		if (ptr != MAP_FAILED)
			munmap(ptr, args->page_size);
		(void)close(fd);

		if (time_now() - t_start > threshold) {
			timeout = true;
			goto next;
		}

		if ((fd = open(path, O_RDONLY | O_NONBLOCK)) < 0)
			goto rdwr;
		ptr = mmap(NULL, args->page_size, PROT_WRITE, MAP_PRIVATE, fd, 0);
		if (ptr != MAP_FAILED)
			munmap(ptr, args->page_size);
		ptr = mmap(NULL, args->page_size, PROT_WRITE, MAP_SHARED, fd, 0);
		if (ptr != MAP_FAILED)
			munmap(ptr, args->page_size);

		ret = fsync(fd);
		(void)ret;

		for (i = 0; i < SIZEOF_ARRAY(dev_funcs); i++) {
			if (!strncmp(path, dev_funcs[i].devpath, dev_funcs[i].devpath_len))
				dev_funcs[i].func(args->name, fd, path);
		}
		(void)close(fd);
		if (time_now() - t_start > threshold) {
			timeout = true;
			goto next;
		}
rdwr:
		/*
		 *   O_RDONLY | O_WRONLY allows one to
		 *   use the fd for ioctl() only operations
		 */
		fd = open(path, O_RDONLY | O_WRONLY | O_NONBLOCK);
		if (fd >= 0)
			(void)close(fd);

next:
		if (loops > 0) {
			if (timeout)
				break;
			loops--;
		}
	}
}

/*
 *  stress_dev_thread
 *	keep exercising a /dev entry until
 *	controlling thread triggers an exit
 */
static void *stress_dev_thread(void *arg)
{
	static void *nowt = NULL;
	uint8_t stack[SIGSTKSZ + STACK_ALIGNMENT];
	const pthread_args_t *pa = (pthread_args_t *)arg;
	const args_t *args = pa->args;

	/*
	 *  Block all signals, let controlling thread
	 *  handle these
	 */
	(void)sigprocmask(SIG_BLOCK, &set, NULL);

	/*
	 *  According to POSIX.1 a thread should have
	 *  a distinct alternative signal stack.
	 *  However, we block signals in this thread
	 *  so this is probably just totally unncessary.
	 */
	(void)memset(stack, 0, sizeof(stack));
	if (stress_sigaltstack(stack, SIGSTKSZ) < 0)
		return &nowt;

	while (g_keep_stressing_flag)
		stress_dev_rw(args, -1);

	return &nowt;
}

/*
 *  stress_dev_dir()
 *	read directory
 */
static void stress_dev_dir(
	const args_t *args,
	const char *path,
	const bool recurse,
	const int depth,
	const uid_t euid)
{
	struct dirent **dlist;
	const mode_t flags = S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;
	int32_t loops = args->instance < 8 ? args->instance + 1 : 8;
	int n;

	if (!g_keep_stressing_flag)
		return;

	/* Don't want to go too deep */
	if (depth > 20)
		return;

	dlist = NULL;
	n = scandir(path, &dlist, NULL, mixup_sort);
	if (n <= 0)
		goto done;

	while (n--) {
		int ret;
		struct stat buf;
		char filename[PATH_MAX];
		char tmp[PATH_MAX];
		struct dirent *d = dlist[n];
		size_t len;

		if (!keep_stressing())
			break;
		if (is_dot_filename(d->d_name))
			continue;
		/*
		 * Xen clients hang on hpet when running as root
		 * see: LP#1741409, so avoid opening /dev/hpet
		 */
		if (!euid && !strcmp(d->d_name, "hpet"))
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

			ret = stat(tmp, &buf);
			if (ret < 0)
				continue;
			if ((buf.st_mode & flags) == 0)
				continue;

			inc_counter(args);
			stress_dev_dir(args, tmp, recurse, depth + 1, euid);
			break;
		case DT_BLK:
		case DT_CHR:
			if (strstr(tmp, "watchdog"))
				continue;
			ret = shim_pthread_spin_lock(&lock);
			if (!ret) {
				shim_strlcpy(filename, tmp, sizeof(filename));
				dev_path = filename;
				(void)shim_pthread_spin_unlock(&lock);
				stress_dev_rw(args, loops);
				inc_counter(args);
			}
			break;
		default:
			break;
		}
	}
done:
	if (dlist)
		free(dlist);
}

/*
 *  stress_dev
 *	stress reading all of /dev
 */
static int stress_dev(const args_t *args)
{
	pthread_t pthreads[MAX_DEV_THREADS];
	int ret[MAX_DEV_THREADS], rc = EXIT_SUCCESS;
	uid_t euid = geteuid();
	pthread_args_t pa;

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
			if (errno == EAGAIN)
				goto again;
		} else if (pid > 0) {
			int status, wret;

			(void)setpgid(pid, g_pgrp);
			/* Parent, wait for child */
			wret = waitpid(pid, &status, 0);
			if (wret < 0) {
				if (errno != EINTR)
					pr_dbg("%s: waitpid(): errno=%d (%s)\n",
						args->name, errno, strerror(errno));
				(void)kill(pid, SIGTERM);
				(void)kill(pid, SIGKILL);
				(void)waitpid(pid, &status, 0);
			} else {
				if (WIFEXITED(status) &&
				    WEXITSTATUS(status) != 0) {
					rc = EXIT_FAILURE;
					break;
				}
			}
		} else if (pid == 0) {
			size_t i;

			(void)setpgid(0, g_pgrp);
			stress_parent_died_alarm();
			rc = shim_pthread_spin_init(&lock, SHIM_PTHREAD_PROCESS_SHARED);
			if (rc) {
				pr_inf("%s: pthread_spin_init failed, errno=%d (%s)\n",
					args->name, rc, strerror(rc));
				return EXIT_NO_RESOURCE;
			}

			/* Make sure this is killable by OOM killer */
			set_oom_adjustment(args->name, true);
			mixup = mwc32();

			for (i = 0; i < MAX_DEV_THREADS; i++) {
				ret[i] = pthread_create(&pthreads[i], NULL,
						stress_dev_thread, (void *)&pa);
			}

			do {
				stress_dev_dir(args, "/dev", true, 0, euid);
			} while (keep_stressing());

			for (i = 0; i < MAX_DEV_THREADS; i++) {
				if (ret[i] == 0)
					pthread_join(pthreads[i], NULL);
			}
			_exit(!g_keep_stressing_flag);
		}
	} while (keep_stressing());

	(void)shim_pthread_spin_destroy(&lock);

	return rc;
}
stressor_info_t stress_dev_info = {
	.stressor = stress_dev,
	.class = CLASS_DEV | CLASS_OS
};
#else
stressor_info_t stress_dev_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_DEV | CLASS_OS
};
#endif
