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

static const help_t help[] = {
	{ NULL,	"procfs N",	"start N workers reading portions of /proc" },
	{ NULL,	"procfs-ops N",	"stop procfs workers after N bogo read operations" },
	{ NULL,	NULL,		NULL }
};

#if defined(HAVE_LIB_PTHREAD) && defined(__linux__)

#define PROC_BUF_SZ		(4096)
#define MAX_READ_THREADS	(4)

typedef struct ctxt {
	const args_t *args;
	const char *path;
	char *badbuf;
	bool writeable;
} ctxt_t;

static sigset_t set;
static shim_pthread_spinlock_t lock;
static char *proc_path;
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

/*
 *  stress_proc_rw()
 *	read a proc file
 */
static inline void stress_proc_rw(
	const ctxt_t *ctxt,
	int32_t loops)
{
	int fd;
	ssize_t ret, i = 0;
	char buffer[PROC_BUF_SZ];
	char path[PATH_MAX];
	const double threshold = 0.2;
	const size_t page_size = ctxt->args->page_size;
	off_t pos;

	while (loops == -1 || loops > 0) {
		double t_start;
		bool timeout = false;
		uint8_t *ptr;

		ret = shim_pthread_spin_lock(&lock);
		if (ret)
			return;
		(void)shim_strlcpy(path, proc_path, sizeof(path));
		(void)shim_pthread_spin_unlock(&lock);

		if (!*path || !g_keep_stressing_flag)
			break;

		t_start = time_now();

		if ((fd = open(path, O_RDONLY | O_NONBLOCK)) < 0)
			return;

		if (time_now() - t_start > threshold) {
			timeout = true;
			(void)close(fd);
			goto next;
		}

		/*
		 *  Multiple randomly sized reads
		 */
		while (i < (4096 * PROC_BUF_SZ)) {
			ssize_t sz = 1 + (mwc32() % sizeof(buffer));
			if (!g_keep_stressing_flag)
				break;
			ret = read(fd, buffer, sz);
			if (ret < 0)
				break;
			if (ret < sz)
				break;
			i += sz;

			if (time_now() - t_start > threshold) {
				timeout = true;
				(void)close(fd);
				goto next;
			}
		}
		(void)close(fd);

		if ((fd = open(path, O_RDONLY | O_NONBLOCK)) < 0)
			return;

		if (time_now() - t_start > threshold) {
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
		/*
		 *  Bad read buffer, should fail!
		 */
		if (ctxt->badbuf) {
			ret = read(fd, ctxt->badbuf, PROC_BUF_SZ);
			if (ret == 0)
				goto err;
		}

		if (time_now() - t_start > threshold) {
			timeout = true;
			(void)close(fd);
			goto next;
		}

		/*
		 *  mmap it
		 */
		ptr = mmap(NULL, page_size, PROT_READ,
			MAP_SHARED | MAP_ANONYMOUS, fd, 0);
		if (ptr != MAP_FAILED) {
			uint8_put(*ptr);
			(void)munmap(ptr, page_size);
		}

		if (time_now() - t_start > threshold) {
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
		if (time_now() - t_start > threshold) {
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

		if (time_now() - t_start > threshold) {
			timeout = true;
			(void)close(fd);
			goto next;
		}

		ret = read(fd, buffer, 1);
		(void)ret;
err:
		(void)close(fd);
		if (time_now() - t_start > threshold) {
			timeout = true;
			goto next;
		}

		if (ctxt->writeable) {
			/*
			 *  Zero sized writes
			 */
			if ((fd = open(path, O_WRONLY | O_NONBLOCK)) < 0)
				return;
			ret = write(fd, buffer, 0);
			(void)ret;
			(void)close(fd);

			/*
			 *  Write using badbuf, expect it to fail
			 */
			if (ctxt->badbuf) {
				if ((fd = open(path, O_WRONLY | O_NONBLOCK)) < 0)
					return;
				ret = write(fd, ctxt->badbuf, PROC_BUF_SZ);
				(void)ret;
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
	uint8_t stack[SIGSTKSZ + STACK_ALIGNMENT];
	ctxt_t *ctxt = (ctxt_t *)ctxt_ptr;

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
		stress_proc_rw(ctxt, -1);

	return &nowt;
}

/*
 *  stress_proc_dir()
 *	read directory
 */
static void stress_proc_dir(
	const ctxt_t *ctxt,
	const char *path,
	const bool recurse,
	const int depth)
{
	struct dirent **dlist;
	const args_t *args = ctxt->args;
	int32_t loops = args->instance < 8 ? args->instance + 1 : 8;
	int i, n;

	if (!g_keep_stressing_flag)
		return;

	/* Don't want to go too deep */
	if (depth > 20)
		return;

	mixup = mwc32();
	dlist = NULL;
	n = scandir(path, &dlist, NULL, mixup_sort);
	if (n <= 0)
		goto done;

	for (i = 0; i < n; i++) {
		int ret;
		char filename[PATH_MAX];
		char tmp[PATH_MAX];
		struct dirent *d = dlist[i];

		if (!g_keep_stressing_flag)
			break;
		if (stress_is_dot_filename(d->d_name))
			continue;

		(void)snprintf(tmp, sizeof(tmp), "%s/%s", path, d->d_name);
		switch (d->d_type) {
		case DT_DIR:
			if (!recurse)
				continue;

			inc_counter(args);
			stress_proc_dir(ctxt, tmp, recurse, depth + 1);
			break;
		case DT_REG:
			ret = shim_pthread_spin_lock(&lock);
			if (!ret) {
				(void)shim_strlcpy(filename, tmp, sizeof(filename));
				proc_path = filename;
				(void)shim_pthread_spin_unlock(&lock);
				stress_proc_rw(ctxt, loops);
				inc_counter(args);
			}
			break;
		default:
			break;
		}
	}
done:
	if (dlist) {
		for (i = 0; i < n; i++)
			free(dlist[i]);
		free(dlist);
	}
}

/*
 *  stress_procfs
 *	stress reading all of /proc
 */
static int stress_procfs(const args_t *args)
{
	size_t i;
	pthread_t pthreads[MAX_READ_THREADS];
	int rc, ret[MAX_READ_THREADS];
	ctxt_t ctxt;

	(void)sigfillset(&set);

	proc_path = "/proc/self";

	ctxt.args = args;
	ctxt.writeable = (geteuid() != 0);

	rc = shim_pthread_spin_init(&lock, PTHREAD_PROCESS_PRIVATE);
	if (rc) {
		pr_inf("%s: pthread_spin_init failed, errno=%d (%s)\n",
			args->name, rc, strerror(rc));
		return EXIT_NO_RESOURCE;
	}

	(void)memset(ret, 0, sizeof(ret));

	ctxt.badbuf = mmap(NULL, PROC_BUF_SZ, PROT_READ,
			MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (ctxt.badbuf == MAP_FAILED) {
		pr_inf("%s: mmap failed: errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}

	for (i = 0; i < MAX_READ_THREADS; i++) {
		ret[i] = pthread_create(&pthreads[i], NULL,
				stress_proc_rw_thread, &ctxt);
	}

	i = args->instance;
	do {
		i %= 12;
		switch (i) {
		case 0:
			stress_proc_dir(&ctxt, "/proc", false, 0);
			break;
		case 1:
			stress_proc_dir(&ctxt, "/proc/self", true, 0);
			break;
		case 2:
			stress_proc_dir(&ctxt, "/proc/thread_self", true, 0);
			break;
		case 3:
			stress_proc_dir(&ctxt, "/proc/sys", true, 0);
			break;
		case 4:
			stress_proc_dir(&ctxt, "/proc/sysvipc", true, 0);
			break;
		case 5:
			stress_proc_dir(&ctxt, "/proc/fs", true, 0);
			break;
		case 6:
			stress_proc_dir(&ctxt, "/proc/bus", true, 0);
			break;
		case 7:
			stress_proc_dir(&ctxt, "/proc/irq", true, 0);
			break;
		case 8:
			stress_proc_dir(&ctxt, "/proc/scsi", true, 0);
			break;
		case 9:
			stress_proc_dir(&ctxt, "/proc/tty", true, 0);
			break;
		case 10:
			stress_proc_dir(&ctxt, "/proc/driver", true, 0);
			break;
		case 11:
			stress_proc_dir(&ctxt, "/proc/tty", true, 0);
			break;
		default:
			break;
		}
		i++;
		inc_counter(args);
		if (!keep_stressing())
			break;
	} while (keep_stressing());

	rc = shim_pthread_spin_lock(&lock);
	if (rc) {
		pr_dbg("%s: failed to lock spin lock for sysfs_path\n", args->name);
	} else {
		proc_path = "";
		rc = shim_pthread_spin_unlock(&lock);
		(void)rc;
	}

	for (i = 0; i < MAX_READ_THREADS; i++) {
		if (ret[i] == 0)
			(void)pthread_join(pthreads[i], NULL);
	}
	(void)munmap(ctxt.badbuf, PROC_BUF_SZ);
	(void)shim_pthread_spin_destroy(&lock);

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
