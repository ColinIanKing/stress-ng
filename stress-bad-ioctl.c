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
#include "core-killpid.h"
#include "core-lock.h"
#include "core-out-of-memory.h"
#include "core-pthread.h"
#include "core-try-open.h"

#include <ctype.h>
#include <sys/ioctl.h>

static const stress_help_t help[] = {
	{ NULL,	"bad-ioctl N",		"start N stressors that perform illegal ioctls on devices" },
	{ NULL,	"bad-ioctl-ops  N",	"stop after N bad ioctl bogo operations" },
	{ NULL,	"bad-ioctl-method M",	"method of selecting ioctl command [ random | inc | random-inc | stride ]" },
	{ NULL,	NULL,			NULL }
};

/* Index order to stress_bad_ioctl_methods */
#define STRESS_BAD_IOCTL_CMD_INC	(0)
#define STRESS_BAD_IOCTL_CMD_RANDOM	(1)
#define STRESS_BAD_IOCTL_CMD_RANDOM_INC	(2)
#define STRESS_BAD_IOCTL_CMD_STRIDE	(3)

static const char * const stress_bad_ioctl_methods[] = {
	"inc",
	"random",
	"random-inc",
	"stride",
};

static const char *stress_bad_ioctl_method(const size_t i)
{
	return (i < SIZEOF_ARRAY(stress_bad_ioctl_methods)) ? stress_bad_ioctl_methods[i] : NULL;
}

static const stress_opt_t opts[] = {
	{ OPT_bad_ioctl_method, "bad-ioctl-method", TYPE_ID_SIZE_T_METHOD, 0, 0, stress_bad_ioctl_method },
	END_OPT,
};

#if defined(HAVE_LIB_PTHREAD) &&	\
    defined(__linux__) &&		\
    defined(_IOR)

#define MAX_DEV_THREADS		(4)

typedef struct {
	pthread_t	pthread;
	stress_pthread_args_t pa;
	int		ret;
	int		thread_index;
} stress_bad_ioctl_thread_t;

typedef struct dev_ioctl_info {
	char	*dev_path;
	struct dev_ioctl_info *left;
	struct dev_ioctl_info *right;
	bool	ignore;
	volatile uint16_t	ioctl_state;
	bool	exercised[MAX_DEV_THREADS];
} dev_ioctl_info_t;

static sigset_t set;
static void *lock;
static uint32_t mixup;
static dev_ioctl_info_t *dev_ioctl_info_head;
static volatile dev_ioctl_info_t *dev_ioctl_node;

typedef struct stress_bad_ioctl_func {
	const char *devpath;
	const size_t devpath_len;
	void (*func)(const char *name, const int fd, const char *devpath);
} stress_bad_ioctl_func_t;

static sigjmp_buf jmp_env;

/*
 *  stress_bad_ioctl_dev_new()
 *	add a new ioctl device path to tree
 */
static dev_ioctl_info_t *stress_bad_ioctl_dev_new(
	dev_ioctl_info_t **head,
	const char *dev_path)
{
	dev_ioctl_info_t *node;

	while (*head) {
		const int cmp = strcmp((*head)->dev_path, dev_path);

		if (cmp == 0)
			return *head;
		head = (cmp > 0) ? &(*head)->left : &(*head)->right;
	}
	node = (dev_ioctl_info_t *)calloc(1, sizeof(*node));
	if (!node)
		return NULL;
	node->dev_path = shim_strdup(dev_path);
	if (!node->dev_path) {
		free(node);
		return NULL;
	}
	node->ignore = false;
	node->ioctl_state = stress_mwc16();

	*head = node;
	return node;
}

/*
 *  stress_bad_ioctl_dev_free()
 *	free tree
 */
static void stress_bad_ioctl_dev_free(dev_ioctl_info_t *node)
{
	if (node) {
		stress_bad_ioctl_dev_free(node->left);
		stress_bad_ioctl_dev_free(node->right);
		free(node->dev_path);
		free(node);
	}
}

/*
 *  stress_bad_ioctl_dev_dir()
 *	read dev directory, add device information to tree
 */
static void stress_bad_ioctl_dev_dir(
	stress_args_t *args,
	const char *path,
	const int depth)
{
	struct dirent **dlist;
	const mode_t flags = S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;
	int i, n;

	if (UNLIKELY(!stress_continue_flag()))
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
		char tmp[PATH_MAX];
		const struct dirent *d = dlist[i];
		size_t len;

		if (UNLIKELY(!stress_continue(args)))
			break;
		if (stress_is_dot_filename(d->d_name))
			continue;

		len = strlen(d->d_name);

		/*
		 *  Exercise no more than 1 of the same device
		 *  driver, e.g. ttyS0..ttyS1
		 */
		if (len > 1) {
			int dev_n;
			const char *ptr = d->d_name + len - 1;

			while ((ptr > d->d_name) && isdigit((unsigned char)*ptr))
				ptr--;
			ptr++;
			if (sscanf(ptr, "%d", &dev_n) != 1)
				continue;
			if (dev_n > 1)
				continue;
		}

		switch (shim_dirent_type(path, d)) {
		case SHIM_DT_DIR:
			(void)stress_mk_filename(tmp, sizeof(tmp), path, d->d_name);
			ret = shim_stat(tmp, &buf);
			if (ret < 0)
				continue;
			if ((buf.st_mode & flags) == 0)
				continue;
			stress_bad_ioctl_dev_dir(args, tmp, depth + 1);
			break;
		case SHIM_DT_BLK:
		case SHIM_DT_CHR:
			(void)stress_mk_filename(tmp, sizeof(tmp), path, d->d_name);
			if (strstr(tmp, "watchdog"))
				continue;
			stress_bad_ioctl_dev_new(&dev_ioctl_info_head, tmp);
			break;
		default:
			break;
		}
	}
done:
	stress_dirent_list_free(dlist, n);
}

static void NORETURN MLOCKED_TEXT stress_segv_handler(int signum)
{
	(void)signum;

	siglongjmp(jmp_env, 1);
	stress_no_return();
}

/*
 *  stress_bad_ioctl_rw()
 *	exercise a dev entry
 */
static inline void stress_bad_ioctl_rw(
	stress_args_t *args,
	const bool is_pthread,
	const int thread_index)
{
	const double threshold = 0.25;
	const size_t page_size = args->page_size;
	uint64_t *buf, *buf_page1;
	uint8_t *buf8;
	uint16_t *buf16;
	uint32_t *buf32, *ptr, *buf_end;
	uint64_t *buf64;

	typedef struct {
		uint8_t page[4096];
	} stress_4k_page_t;

	/*
	 *  Map in 2 pages, the last page will be unmapped to
	 *  create a single mapped page with an unmapped page
	 *  following it.  Note that buf points to 64 bit values
	 *  so the compiler can deduce that buf8..buf32 casts
	 *  will align correctly for the smaller types.
	 */
	buf = (uint64_t *)mmap(NULL, page_size << 1, PROT_READ | PROT_WRITE,
		MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (buf == MAP_FAILED)
		return;
	stress_set_vma_anon_name(buf, page_size << 1, "ioctl-rw");

	/*
	 *  Unmap last page so we know that the page following
	 *  buf is definitely not read/writeable
	 */
	buf_page1 = buf + page_size / sizeof(*buf_page1);
	(void)munmap((void *)buf_page1, page_size);

	buf8 =  ((uint8_t *)buf_page1) - 1;
	buf16 = ((uint16_t *)buf_page1) - 1;
	buf32 = ((uint32_t *)buf_page1) - 1;
	buf64 = ((uint64_t *)buf_page1) - 1;
	buf_end	= (uint32_t *)buf_page1;

	for (ptr = (uint32_t *)buf; ptr < buf_end; ptr++) {
		*ptr = stress_mwc32();
	}

	do {
		int fd, ret;
		double t_start;
		uint8_t type, nr;
		uint64_t rnd = stress_mwc32();
		volatile dev_ioctl_info_t *node;

		ret = stress_lock_acquire(lock);
		if (ret)
			return;
		node = dev_ioctl_node;
		(void)stress_lock_release(lock);

		if (!node || !stress_continue_flag())
			break;
		type = (node->ioctl_state >> 8) & 0xff;
		nr = (node->ioctl_state) & 0xff;

		t_start = stress_time_now();

		for (ptr = (uint32_t *)buf; ptr < buf_end; ptr++) {
			*ptr ^= rnd;
		}

		if ((fd = open(node->dev_path, O_RDONLY | O_NONBLOCK)) < 0)
			break;

		ret = sigsetjmp(jmp_env, 1);
		if (ret != 0) {
			(void)close(fd);
			continue;
		}

		(void)shim_memset(buf, 0, page_size);

		VOID_RET(int, ioctl(fd, _IOR(type, nr, uint64_t), buf64));
		if (stress_time_now() - t_start > threshold) {
			(void)close(fd);
			break;
		}

		VOID_RET(int, ioctl(fd, _IOR(type, nr, uint32_t), buf32));
		if (stress_time_now() - t_start > threshold) {
			(void)close(fd);
			break;
		}

		VOID_RET(int, ioctl(fd, _IOR(type, nr, uint16_t), buf16));
		if (stress_time_now() - t_start > threshold) {
			(void)close(fd);
			break;
		}

		VOID_RET(int, ioctl(fd, _IOR(type, nr, uint8_t), buf8));
		if (stress_time_now() - t_start > threshold) {
			(void)close(fd);
			break;
		}

		VOID_RET(int, ioctl(fd, _IOR(type, nr, stress_4k_page_t), buf));
		if (stress_time_now() - t_start > threshold) {
			(void)close(fd);
			break;
		}

		VOID_RET(int, ioctl(fd, _IOR(type, nr, uint64_t), NULL));
		if (stress_time_now() - t_start > threshold) {
			(void)close(fd);
			break;
		}

		VOID_RET(int, ioctl(fd, _IOR(type, nr, uint32_t), NULL));
		if (stress_time_now() - t_start > threshold) {
			(void)close(fd);
			break;
		}

		VOID_RET(int, ioctl(fd, _IOR(type, nr, uint16_t), NULL));
		if (stress_time_now() - t_start > threshold) {
			(void)close(fd);
			break;
		}

		VOID_RET(int, ioctl(fd, _IOR(type, nr, uint8_t), NULL));
		if (stress_time_now() - t_start > threshold) {
			(void)close(fd);
			break;
		}

		VOID_RET(int, ioctl(fd, _IOR(type, nr, uint64_t), args->mapped->page_none));
		if (stress_time_now() - t_start > threshold) {
			(void)close(fd);
			break;
		}

		VOID_RET(int, ioctl(fd, _IOR(type, nr, uint32_t), args->mapped->page_none));
		if (stress_time_now() - t_start > threshold) {
			(void)close(fd);
			break;
		}

		VOID_RET(int, ioctl(fd, _IOR(type, nr, uint16_t), args->mapped->page_none));
		if (stress_time_now() - t_start > threshold) {
			(void)close(fd);
			break;
		}

		VOID_RET(int, ioctl(fd, _IOR(type, nr, uint8_t), args->mapped->page_none));
		if (stress_time_now() - t_start > threshold) {
			(void)close(fd);
			break;
		}

		VOID_RET(int, ioctl(fd, _IOR(type, nr, uint32_t), args->mapped->page_ro));
		if (stress_time_now() - t_start > threshold) {
			(void)close(fd);
			break;
		}

		VOID_RET(int, ioctl(fd, _IOR(type, nr, uint16_t), args->mapped->page_ro));
		if (stress_time_now() - t_start > threshold) {
			(void)close(fd);
			break;
		}

		VOID_RET(int, ioctl(fd, _IOR(type, nr, uint8_t), args->mapped->page_ro));
		if (stress_time_now() - t_start > threshold) {
			(void)close(fd);
			break;
		}

#if defined(_IOW)
		VOID_RET(int, ioctl(fd, _IOW(type, nr, uint64_t), args->mapped->page_none));
		if (stress_time_now() - t_start > threshold) {
			(void)close(fd);
			break;
		}

		VOID_RET(int, ioctl(fd, _IOW(type, nr, uint32_t), args->mapped->page_none));
		if (stress_time_now() - t_start > threshold) {
			(void)close(fd);
			break;
		}

		VOID_RET(int, ioctl(fd, _IOW(type, nr, uint16_t), args->mapped->page_none));
		if (stress_time_now() - t_start > threshold) {
			(void)close(fd);
			break;
		}

		VOID_RET(int, ioctl(fd, _IOW(type, nr, uint8_t), args->mapped->page_none));
		if (stress_time_now() - t_start > threshold) {
			(void)close(fd);
			break;
		}
#endif

		(void)close(fd);
		if ((thread_index >= 0) && (thread_index < MAX_DEV_THREADS)) {
			ret = stress_lock_acquire(lock);
			if (ret)
				return;
			node->exercised[thread_index] = true;
			(void)stress_lock_release(lock);
		}
	} while (is_pthread);

	(void)munmap((void *)buf, page_size);
}

/*
 *  stress_bad_ioctl_thread
 *	keep exercising a /dev entry until
 *	controlling thread triggers an exit
 */
static void *stress_bad_ioctl_thread(void *arg)
{
	const stress_pthread_args_t *pa = (stress_pthread_args_t *)arg;
	stress_args_t *args = pa->args;
	const stress_bad_ioctl_thread_t *thread = (const stress_bad_ioctl_thread_t *)pa->data;

	/*
	 *  Block all signals, let controlling thread
	 *  handle these
	 */
	(void)sigprocmask(SIG_BLOCK, &set, NULL);

	stress_random_small_sleep();

	while (stress_continue_flag())
		stress_bad_ioctl_rw(args, true, thread->thread_index);

	return &g_nowt;
}

/*
 *  stress_bad_ioctl_dir()
 *	read directory
 */
static void stress_bad_ioctl_dir(
	stress_args_t *args,
	dev_ioctl_info_t *node,
	uint32_t offset,
	const int bad_ioctl_method)
{
	int ret;

	if (UNLIKELY(!stress_continue_flag()))
		return;
	if (!node)
		return;

	stress_bad_ioctl_dir(args, node->left, offset, bad_ioctl_method);
	ret = stress_try_open(args, node->dev_path, O_RDONLY | O_NONBLOCK, 15000000);
	if (ret == STRESS_TRY_OPEN_FAIL) {
		node->ignore = true;
	} else if (!node->ignore) {
		if (offset > 1) {
			offset--;
		} else {
			ret = stress_lock_acquire(lock);
			if (!ret) {
				size_t i;
				bool all_exercised = true;

				for (i = 0; i < SIZEOF_ARRAY(node->exercised); i++) {
					if (!node->exercised[i]) {
						all_exercised = false;
						break;
					}
				}
				if (all_exercised) {
					uint8_t type, nr;

					switch (bad_ioctl_method) {
					case STRESS_BAD_IOCTL_CMD_RANDOM:
						node->ioctl_state = stress_mwc16();
						break;
					case STRESS_BAD_IOCTL_CMD_INC:
						node->ioctl_state++;
						break;
					default:
					case STRESS_BAD_IOCTL_CMD_RANDOM_INC:
						node->ioctl_state += stress_mwc8();
						break;
					case STRESS_BAD_IOCTL_CMD_STRIDE:
						type = ((node->ioctl_state >> 8) & 0xff) - 3;
						nr = ((node->ioctl_state) & 0xff) + 1;
						node->ioctl_state = (((uint16_t)type) << 8) | nr;
						break;
					}
				}
				dev_ioctl_node = node;
				(void)shim_memset(node->exercised, 0, sizeof(node->exercised));
				(void)stress_lock_release(lock);
				stress_bad_ioctl_rw(args, false, -1);
			}
		}
		stress_bogo_inc(args);
	}
	stress_bad_ioctl_dir(args, node->right, offset, bad_ioctl_method);
}

/*
 *  stress_bad_ioctl
 *	stress read-only ioctls on all of /dev
 */
static int stress_bad_ioctl(stress_args_t *args)
{
	stress_bad_ioctl_thread_t threads[MAX_DEV_THREADS];
	int rc = EXIT_SUCCESS;
	int bad_ioctl_method = STRESS_BAD_IOCTL_CMD_RANDOM_INC;

	lock = NULL;
	dev_ioctl_info_head = NULL;
	dev_ioctl_node = NULL;

	(void)stress_get_setting("bad-ioctl-method", &bad_ioctl_method);

	stress_bad_ioctl_dev_dir(args, "/dev", 0);
	dev_ioctl_node = dev_ioctl_info_head;

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		pid_t pid;

again:
		pid = fork();
		if (pid < 0) {
			if (stress_redo_fork(args, errno))
				goto again;
		} else if (pid > 0) {
			int status;
			pid_t wret;

			/* Parent, wait for child */
			wret = shim_waitpid(pid, &status, 0);
			if (wret < 0) {
				if (errno != EINTR)
					pr_dbg("%s: waitpid() on PID %" PRIdMAX " failed, errno=%d (%s)\n",
						args->name, (intmax_t)pid, errno, strerror(errno));
				stress_force_killed_bogo(args);
				(void)stress_kill_pid_wait(pid, NULL);
			} else {
				if (WIFEXITED(status) &&
				    (WEXITSTATUS(status) != 0)) {
					rc = EXIT_FAILURE;
					break;
				}
			}
		} else if (pid == 0) {
			size_t i;
			int r, ssjret;
			uint32_t offset;

			stress_set_proc_state(args->name, STRESS_STATE_RUN);
			ssjret = sigsetjmp(jmp_env, 1);
			if (ssjret != 0) {
				pr_fail("%s: caught an unexpected segmentation fault\n", args->name);
				_exit(EXIT_FAILURE);
			}

			if (stress_sighandler(args->name, SIGSEGV, stress_segv_handler, NULL) < 0)
				_exit(EXIT_NO_RESOURCE);

			stress_parent_died_alarm();
			(void)sched_settings_apply(true);
			lock = stress_lock_create("dev-path");
			if (!lock) {
				pr_inf("%s: lock create failed\n", args->name);
				_exit(EXIT_NO_RESOURCE);
			}

			/* Make sure this is killable by OOM killer */
			stress_set_oom_adjustment(args, true);
			mixup = stress_mwc32();

			for (i = 0; i < MAX_DEV_THREADS; i++) {
				threads[i].thread_index = i;
				threads[i].pa.args = args;
				threads[i].pa.data = &threads[i];
				threads[i].ret = pthread_create(&threads[i].pthread, NULL,
							stress_bad_ioctl_thread,
							(void *)&threads[i].pa);
			}

			offset = args->instance;
			do {
				stress_bad_ioctl_dir(args, dev_ioctl_info_head, offset, bad_ioctl_method);
				offset = 0;
			} while (stress_continue(args));

			r = stress_lock_acquire(lock);
			if (r) {
				pr_dbg("%s: failed to acquire lock for dev_path\n", args->name);
			} else {
				dev_ioctl_node = NULL;
				(void)stress_lock_release(lock);
			}

			for (i = 0; i < MAX_DEV_THREADS; i++) {
				if (threads[i].ret == 0)
					(void)pthread_join(threads[i].pthread, NULL);
			}
			(void)stress_lock_destroy(lock);
			_exit(EXIT_SUCCESS);
		}
	} while (stress_continue(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	stress_bad_ioctl_dev_free(dev_ioctl_info_head);

	return rc;
}
const stressor_info_t stress_bad_ioctl_info = {
	.stressor = stress_bad_ioctl,
	.classifier = CLASS_DEV | CLASS_OS | CLASS_PATHOLOGICAL,
	.opts = opts,
	.help = help
};
#else
const stressor_info_t stress_bad_ioctl_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_DEV | CLASS_OS | CLASS_PATHOLOGICAL,
	.opts = opts,
	.help = help,
	.unimplemented_reason = "built without pthread and/or ioctl() _IOR macro or is not Linux"
};
#endif
