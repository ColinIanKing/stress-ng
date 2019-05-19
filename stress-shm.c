/*
 * Copyright (C) 2013-2019 Canonical, Ltd.
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

#define SHM_NAME_LEN	128

typedef struct {
	ssize_t	index;
	char	shm_name[SHM_NAME_LEN];
} shm_msg_t;

static const help_t help[] = {
	{ NULL,	"shm N",	"start N workers that exercise POSIX shared memory" },
	{ NULL,	"shm-ops N",	"stop after N POSIX shared memory bogo operations" },
	{ NULL,	"shm-bytes N",	"allocate/free N bytes of POSIX shared memory" },
	{ NULL,	"shm-segs N",	"allocate N POSIX shared memory segments per iteration" },
	{ NULL,	NULL,		NULL }
};

static int stress_set_shm_posix_bytes(const char *opt)
{
	size_t shm_posix_bytes;

	shm_posix_bytes = (size_t)get_uint64_byte_memory(opt, 1);
	check_range_bytes("shm-bytes", shm_posix_bytes,
		MIN_SHM_POSIX_BYTES, MAX_MEM_LIMIT);
	return set_setting("shm-bytes", TYPE_ID_SIZE_T, &shm_posix_bytes);
}

static int stress_set_shm_posix_objects(const char *opt)
{
	size_t shm_posix_objects;

	shm_posix_objects = (size_t)get_uint64(opt);
	check_range("shm-objs", shm_posix_objects,
		MIN_SHM_POSIX_OBJECTS, MAX_48);
	return set_setting("shm-objs", TYPE_ID_SIZE_T, &shm_posix_objects);
}

static const opt_set_func_t opt_set_funcs[] = {
	{ OPT_shm_bytes,	stress_set_shm_posix_bytes },
	{ OPT_shm_objects,	stress_set_shm_posix_objects },
	{ 0,			NULL }
};

#if defined(HAVE_LIB_RT)

/*
 *  stress_shm_posix_check()
 *	simple check if shared memory is sane
 */
static int stress_shm_posix_check(
	uint8_t *buf,
	const size_t sz,
	const size_t page_size)
{
	uint8_t *ptr, *end = buf + sz;
	uint8_t val;

	(void)memset(buf, 0xa5, sz);

	for (val = 0, ptr = buf; ptr < end; ptr += page_size, val++) {
		*ptr = val;
	}

	for (val = 0, ptr = buf; ptr < end; ptr += page_size, val++) {
		if (*ptr != val)
			return -1;

	}
	return 0;
}

/*
 *  stress_shm_posix_child()
 * 	stress out the shm allocations. This can be killed by
 *	the out of memory killer, so we need to keep the parent
 *	informed of the allocated shared memory ids so these can
 *	be reaped cleanly if this process gets prematurely killed.
 */
static int stress_shm_posix_child(
	const args_t *args,
	const int fd,
	size_t sz,
	size_t shm_posix_objects)
{
	void **addrs;
	char *shm_names;
	shm_msg_t msg;
	int i;
	int rc = EXIT_SUCCESS;
	bool ok = true;
	pid_t pid = getpid();
	uint64_t id = 0;
	const size_t page_size = args->page_size;

	addrs = calloc(shm_posix_objects, sizeof(*addrs));
	if (!addrs) {
		pr_fail_err("calloc on addrs");
		return EXIT_NO_RESOURCE;
	}
	shm_names = calloc(shm_posix_objects, SHM_NAME_LEN);
	if (!shm_names) {
		free(addrs);
		pr_fail_err("calloc on shm_names");
		return EXIT_NO_RESOURCE;
	}

	/* Make sure this is killable by OOM killer */
	set_oom_adjustment(args->name, true);

	do {
		for (i = 0; ok && (i < (ssize_t)shm_posix_objects); i++) {
			int shm_fd, ret;
			void *addr;
			char *shm_name = &shm_names[i * SHM_NAME_LEN];
			struct stat statbuf;

			shm_name[0] = '\0';

			if (!g_keep_stressing_flag)
				goto reap;

			(void)snprintf(shm_name, SHM_NAME_LEN,
				"/stress-ng-%d-%" PRIx64 "-%" PRIx32,
					(int)pid, id, mwc32());

			shm_fd = shm_open(shm_name, O_CREAT | O_RDWR | O_TRUNC,
				S_IRUSR | S_IWUSR);
			if (shm_fd < 0) {
				ok = false;
				pr_fail_err("shm_open");
				rc = EXIT_FAILURE;
				goto reap;
			}

			/* Inform parent of the new shm name */
			msg.index = i;
			shm_name[SHM_NAME_LEN - 1] = '\0';
			(void)shim_strlcpy(msg.shm_name, shm_name, SHM_NAME_LEN);
			if (write(fd, &msg, sizeof(msg)) < 0) {
				pr_err("%s: write failed: errno=%d: (%s)\n",
					args->name, errno, strerror(errno));
				rc = EXIT_FAILURE;
				(void)close(shm_fd);
				goto reap;
			}

			addr = mmap(NULL, sz, PROT_READ | PROT_WRITE,
				MAP_PRIVATE | MAP_ANONYMOUS, shm_fd, 0);
			if (addr == MAP_FAILED) {
				ok = false;
				pr_fail_err("mmap");
				rc = EXIT_FAILURE;
				(void)close(shm_fd);
				goto reap;
			}
			addrs[i] = addr;

			if (!g_keep_stressing_flag) {
				(void)close(shm_fd);
				goto reap;
			}
			(void)mincore_touch_pages(addr, sz);

			if (!g_keep_stressing_flag) {
				(void)close(shm_fd);
				goto reap;
			}
			(void)madvise_random(addr, sz);
			(void)shim_msync(addr, sz, mwc1() ? MS_ASYNC : MS_SYNC);
			(void)shim_fsync(shm_fd);

			/* Expand and shrink the mapping */
			(void)shim_fallocate(shm_fd, 0, 0, sz + page_size);
			(void)shim_fallocate(shm_fd, 0, 0, sz);

			/* Now truncated it back */
			ret = ftruncate(shm_fd, sz);
			if (ret < 0)
				pr_fail("%s: ftruncate of shared memory failed\n", args->name);
			(void)shim_fsync(shm_fd);

			/* fstat shared memory */
			ret = fstat(shm_fd, &statbuf);
			if (ret < 0) {
				pr_fail("%s: fstat failed on shared memory\n", args->name);
			} else {
				if (statbuf.st_size != (off_t)sz) {
					pr_fail("%s: fstat reports different size of shared memory, "
						"got %jd bytes, expected %zd bytes\n", args->name,
						(intmax_t)statbuf.st_size, sz);
				}
			}

			/* Make it read only */
			ret = fchmod(shm_fd, S_IRUSR);
			if (ret < 0) {
				pr_fail("%s: failed to fchmod to S_IRUSR on shared memory\n", args->name);
			}

			(void)close(shm_fd);

			if (!keep_stressing())
				goto reap;
			if (stress_shm_posix_check(addr, sz, page_size) < 0) {
				ok = false;
				pr_fail("%s: memory check failed\n", args->name);
				rc = EXIT_FAILURE;
				goto reap;
			}
			id++;
			inc_counter(args);
		}
reap:
		for (i = 0; ok && (i < (ssize_t)shm_posix_objects); i++) {
			char *shm_name = &shm_names[i * SHM_NAME_LEN];

			if (addrs[i])
				(void)munmap(addrs[i], sz);
			if (*shm_name) {
				if (shm_unlink(shm_name) < 0) {
					pr_fail_err("shm_unlink");
				}
			}

			/* Inform parent shm ID is now free */
			msg.index = i;
			msg.shm_name[SHM_NAME_LEN - 1] = '\0';
			(void)shim_strlcpy(msg.shm_name, shm_name, SHM_NAME_LEN - 1);
			if (write(fd, &msg, sizeof(msg)) < 0) {
				pr_dbg("%s: write failed: errno=%d: (%s)\n",
					args->name, errno, strerror(errno));
				ok = false;
			}
			addrs[i] = NULL;
			*shm_name = '\0';
		}
	} while (ok && keep_stressing());

	/* Inform parent of end of run */
	msg.index = -1;
	(void)shim_strlcpy(msg.shm_name, "", SHM_NAME_LEN);
	if (write(fd, &msg, sizeof(msg)) < 0) {
		pr_err("%s: write failed: errno=%d: (%s)\n",
			args->name, errno, strerror(errno));
		rc = EXIT_FAILURE;
	}
	free(shm_names);
	free(addrs);

	return rc;
}

/*
 *  stress_shm()
 *	stress SYSTEM V shared memory
 */
static int stress_shm(const args_t *args)
{
	const size_t page_size = args->page_size;
	size_t orig_sz, sz;
	int pipefds[2];
	int rc = EXIT_SUCCESS;
	ssize_t i;
	pid_t pid;
	bool retry = true;
	uint32_t restarts = 0;
	size_t shm_posix_bytes = DEFAULT_SHM_POSIX_BYTES;
	size_t shm_posix_objects = DEFAULT_SHM_POSIX_OBJECTS;

	if (!get_setting("shm-bytes", &shm_posix_bytes)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			shm_posix_bytes = MAX_SHM_POSIX_BYTES;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			shm_posix_bytes = MIN_SHM_POSIX_BYTES;
	}
	shm_posix_bytes /= args->num_instances;
	if (shm_posix_bytes < MIN_SHM_POSIX_BYTES)
		shm_posix_bytes = MIN_SHM_POSIX_BYTES;
	if (shm_posix_bytes < page_size)
		shm_posix_bytes = page_size;

	if (!get_setting("shm-objs", &shm_posix_objects)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			shm_posix_objects = MAX_SHM_POSIX_OBJECTS;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			shm_posix_objects = MIN_SHM_POSIX_OBJECTS;
	}
	orig_sz = sz = shm_posix_bytes & ~(page_size - 1);

	while (g_keep_stressing_flag && retry) {
		if (pipe(pipefds) < 0) {
			pr_fail_dbg("pipe");
			return EXIT_FAILURE;
		}
fork_again:
		pid = fork();
		if (pid < 0) {
			/* Can't fork, retry? */
			if (errno == EAGAIN)
				goto fork_again;
			pr_err("%s: fork failed: errno=%d: (%s)\n",
				args->name, errno, strerror(errno));
			(void)close(pipefds[0]);
			(void)close(pipefds[1]);

			/* Nope, give up! */
			return EXIT_FAILURE;
		} else if (pid > 0) {
			/* Parent */
			int status;
			char *shm_names;

			shm_names = calloc(shm_posix_objects, SHM_NAME_LEN);
			if (!shm_names) {
				pr_fail_err("calloc on shm_names");
				rc = EXIT_NO_RESOURCE;
				goto err;
			}
			(void)setpgid(pid, g_pgrp);
			(void)close(pipefds[1]);

			while (g_keep_stressing_flag) {
				ssize_t n;
				shm_msg_t 	msg;
				char *shm_name;

				/*
				 *  Blocking read on child shm ID info
				 *  pipe.  We break out if pipe breaks
				 *  on child death, or child tells us
				 *  about its demise.
				 */
				(void)memset(&msg, 0, sizeof(msg));
				n = read(pipefds[0], &msg, sizeof(msg));
				if (n <= 0) {
					if ((errno == EAGAIN) || (errno == EINTR))
						continue;
					if (errno) {
						pr_fail_dbg("read");
						break;
					}
					pr_fail_dbg("zero byte read");
					break;
				}
				if ((msg.index < 0) ||
				    (msg.index >= (ssize_t)shm_posix_objects)) {
					retry = false;
					break;
				}

				shm_name = &shm_names[msg.index * SHM_NAME_LEN];
				msg.shm_name[SHM_NAME_LEN - 1] = '\0';
				(void)shim_strlcpy(shm_name, msg.shm_name, SHM_NAME_LEN);
			}
			(void)kill(pid, SIGKILL);
			(void)shim_waitpid(pid, &status, 0);
			if (WIFSIGNALED(status)) {
				if ((WTERMSIG(status) == SIGKILL) ||
				    (WTERMSIG(status) == SIGKILL)) {
					log_system_mem_info();
					pr_dbg("%s: assuming killed by OOM killer, "
						"restarting again (instance %d)\n",
						args->name, args->instance);
					restarts++;
				}
			}
			(void)close(pipefds[1]);

			/*
			 *  The child may have been killed by the OOM killer or
			 *  some other way, so it may have left the shared
			 *  memory segment around.  At this point the child
			 *  has died, so we should be able to remove the
			 *  shared memory segment.
			 */
			for (i = 0; i < (ssize_t)shm_posix_objects; i++) {
				char *shm_name = &shm_names[i * SHM_NAME_LEN];
				if (*shm_name)
					(void)shm_unlink(shm_name);
			}
			free(shm_names);
		} else if (pid == 0) {
			/* Child, stress memory */
			(void)setpgid(0, g_pgrp);
			stress_parent_died_alarm();

			(void)close(pipefds[0]);
			rc = stress_shm_posix_child(args, pipefds[1], sz, shm_posix_objects);
			(void)close(pipefds[1]);
			_exit(rc);
		}
	}
	if (orig_sz != sz)
		pr_dbg("%s: reduced shared memory size from "
			"%zu to %zu bytes\n", args->name, orig_sz, sz);
	if (restarts) {
		pr_dbg("%s: OOM restarts: %" PRIu32 "\n",
			args->name, restarts);
	}
err:
	return rc;
}

stressor_info_t stress_shm_info = {
	.stressor = stress_shm,
	.class = CLASS_VM | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
#else
stressor_info_t stress_shm_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_VM | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
#endif
