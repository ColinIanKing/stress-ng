/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
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

#if defined(HAVE_LINUX_LANDLOCK_H)
#include <linux/landlock.h>
#endif

#if defined(HAVE_SYS_PRCTL_H)
#include <sys/prctl.h>
#endif

static const stress_help_t help[] = {
	{ NULL,	"landlock N",	  "start N workers stressing landlock file operations" },
	{ NULL,	"landlock-ops N", "stop after N landlock bogo operations" },
	{ NULL,	NULL,		  NULL }
};
#define SHIM_LANDLOCK_CREATE_RULESET_VERSION	(1U << 0)

#define SHIM_LANDLOCK_ACCESS_FS_EXECUTE		(1ULL << 0)
#define SHIM_LANDLOCK_ACCESS_FS_WRITE_FILE	(1ULL << 1)
#define SHIM_LANDLOCK_ACCESS_FS_READ_FILE	(1ULL << 2)
#define SHIM_LANDLOCK_ACCESS_FS_READ_DIR	(1ULL << 3)
#define SHIM_LANDLOCK_ACCESS_FS_REMOVE_DIR	(1ULL << 4)
#define SHIM_LANDLOCK_ACCESS_FS_REMOVE_FILE	(1ULL << 5)
#define SHIM_LANDLOCK_ACCESS_FS_MAKE_CHAR	(1ULL << 6)
#define SHIM_LANDLOCK_ACCESS_FS_MAKE_DIR	(1ULL << 7)
#define SHIM_LANDLOCK_ACCESS_FS_MAKE_REG	(1ULL << 8)
#define SHIM_LANDLOCK_ACCESS_FS_MAKE_SOCK	(1ULL << 9)
#define SHIM_LANDLOCK_ACCESS_FS_MAKE_FIFO	(1ULL << 10)
#define SHIM_LANDLOCK_ACCESS_FS_MAKE_BLOCK	(1ULL << 11)
#define SHIM_LANDLOCK_ACCESS_FS_MAKE_SYM	(1ULL << 12)

#if defined(HAVE_LINUX_LANDLOCK_H) &&		\
    defined(HAVE_LANDLOCK_RULE_TYPE) &&		\
    defined(HAVE_LANDLOCK_RULESET_ATTR) &&	\
    defined(HAVE_SYSCALL) &&			\
    defined(__NR_landlock_create_ruleset) &&	\
    defined(__NR_landlock_restrict_self) &&	\
    defined(__NR_landlock_add_rule)

typedef int (*stress_landlock_func)(const stress_args_t *args, void *ctxt);

static int shim_landlock_create_ruleset(
	struct landlock_ruleset_attr *attr,
	size_t size,
	uint32_t flags)
{
#if defined(__NR_landlock_create_ruleset)
	return (int)syscall(__NR_landlock_create_ruleset, attr, size, flags);
#else
	(void)attr;
	(void)size;
	(void)flags;

	errno = ENOSYS;
	return -1;
#endif
}

static int shim_landlock_restrict_self(const int fd, const uint32_t flags)
{
#if defined(__NR_landlock_restrict_self)
	return (int)syscall(__NR_landlock_restrict_self, fd, flags);
#else
	(void)fd;
	(void)flags;

	errno = ENOSYS;
	return -1;
#endif
}

static int shim_landlock_add_rule(
	const int fd,
	const enum landlock_rule_type type,
	const void *const rule_attr,
	const uint32_t flags)
{
#if defined(__NR_landlock_add_rule)
	return (int)syscall(__NR_landlock_add_rule, fd, type, rule_attr, flags);
#else
	(void)fd;
	(void)type;
	(void)rule_attr;
	(void)flags;

	errno = ENOSYS;
	return -1;
#endif
}

static int stress_landlock_supported(const char *name)
{
	int ruleset_fd;
	struct landlock_ruleset_attr ruleset_attr;

	(void)shim_memset(&ruleset_attr, 0, sizeof(ruleset_attr));
	ruleset_attr.handled_access_fs = SHIM_LANDLOCK_ACCESS_FS_READ_FILE;

	ruleset_fd = shim_landlock_create_ruleset(&ruleset_attr, sizeof(ruleset_attr), 0);
	if (ruleset_fd < 0) {
		if (errno == ENOSYS) {
			pr_inf_skip("%s: stressor will be skipped, landlock_create_ruleset system call"
				" is not supported\n", name);
		} else {
			pr_inf_skip("%s: stressor will be skipped, perhaps "
				"lsm=landlock is not enabled\n", name);
		}
		return -1;
	}

	(void)close(ruleset_fd);
	return 0;
}

static int stress_landlock_flag(const stress_args_t *args, void *ctxt)
{
	uint32_t flag = *(uint32_t *)ctxt;
	const char *path = stress_get_temp_path();
	char filename[PATH_MAX];

	int ruleset_fd, fd, ret, rc = EXIT_SUCCESS;
	const pid_t pid = getpid();
	struct landlock_ruleset_attr ruleset_attr;
	struct landlock_path_beneath_attr path_beneath = {
		.allowed_access = LANDLOCK_ACCESS_FS_READ_FILE |
				  LANDLOCK_ACCESS_FS_READ_DIR,
	};
	struct landlock_path_beneath_attr bad_path_beneath = {
		.allowed_access = LANDLOCK_ACCESS_FS_READ_FILE |
				  LANDLOCK_ACCESS_FS_READ_DIR,
	};

	if (!path)
		return 0;

	(void)shim_memset(&ruleset_attr, 0, sizeof(ruleset_attr));
	/* Exercise illegal ruleset sizes, EINVAL */
	VOID_RET(int, shim_landlock_create_ruleset(&ruleset_attr, 0, 0));
	/* Exercise illegal ruleset sizes, E2BIG */
	VOID_RET(int, shim_landlock_create_ruleset(&ruleset_attr, 4096, 0));
	/* Exercise fetch of ruleset API version, ignore return */
	VOID_RET(int, shim_landlock_create_ruleset(NULL, 0, SHIM_LANDLOCK_CREATE_RULESET_VERSION));

	(void)shim_memset(&ruleset_attr, 0, sizeof(ruleset_attr));
	ruleset_attr.handled_access_fs = flag;

	ruleset_fd = shim_landlock_create_ruleset(&ruleset_attr, sizeof(ruleset_attr), 0);
	if (ruleset_fd < 0)
		return 0;

	/* Exercise illegal parent_fd */
	path_beneath.parent_fd = -1;
	VOID_RET(int, shim_landlock_add_rule(ruleset_fd, LANDLOCK_RULE_PATH_BENEATH,
		&path_beneath, 0));

	path_beneath.parent_fd = open(path, O_PATH | O_CLOEXEC);
	if (path_beneath.parent_fd < 0)
		goto close_ruleset;

	/* Exercise illegal fd */
	VOID_RET(int, shim_landlock_add_rule(-1, LANDLOCK_RULE_PATH_BENEATH,
		&path_beneath, 0));

	/* Exercise illegal flags */
	VOID_RET(int, shim_landlock_add_rule(ruleset_fd, LANDLOCK_RULE_PATH_BENEATH,
		&path_beneath, ~0U));
	/* Exercise illegal rule type */
	VOID_RET(int, shim_landlock_add_rule(ruleset_fd, (enum landlock_rule_type)~0,
		&path_beneath, 0));

	ret = shim_landlock_add_rule(ruleset_fd, LANDLOCK_RULE_PATH_BENEATH,
		&path_beneath, 0);
	if (ret < 0)
		goto close_parent;

	/* Exercise illegal parent_fd */
	bad_path_beneath.parent_fd = ruleset_fd;
	VOID_RET(int, shim_landlock_add_rule(ruleset_fd, LANDLOCK_RULE_PATH_BENEATH,
		&bad_path_beneath, 0));

	ret = prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
	if (ret < 0)
		goto close_parent;

	ret = shim_landlock_restrict_self(ruleset_fd, 0);
	if (ret < 0)
		goto close_parent;

	/*
	 *  Got a valid landlocked restricted child process,
	 *  so now sanity check it on some test files
	 */
	(void)snprintf(filename, sizeof(filename), "%s/landlock-%" PRIdMAX, path, (intmax_t)pid);

	fd = open(path, O_PATH | O_CLOEXEC);
	if (fd > -1)
		(void)close(fd);

	fd = open(filename, O_CREAT | O_RDWR | O_CLOEXEC, S_IRUSR | S_IWUSR);
	if (fd > -1) {
		pr_fail("%s: failed to landlock writable file %s\n",
			args->name, filename);
		(void)shim_unlink(filename);
		(void)close(fd);
		rc = EXIT_FAILURE;
		goto close_parent;
	}
	if ((fd < 0) && (errno != EACCES)) {
		pr_fail("%s: landlocked file create should have returned "
			"errno=%d (%s), got errno=%d (%s) instead\n",
			args->name,
			EACCES, strerror(EACCES),
			errno, strerror(errno));
		rc = EXIT_FAILURE;
		goto close_parent;
	}

close_parent:
	(void)close(path_beneath.parent_fd);
close_ruleset:
	(void)close(ruleset_fd);

	return rc;
}

static void stress_landlock_test(
	const stress_args_t *args,
	stress_landlock_func func,
	void *ctxt,
	int *failures)
{
	int status;
	pid_t pid;

again:
	pid = fork();
	if (pid < 0) {
		if (stress_redo_fork(args, errno))
			goto again;
		return;
	} else if (pid == 0) {
		_exit(func(args, ctxt));
	} else {
		if (shim_waitpid(pid, &status, 0) < 0) {
			if (errno != EINTR) {
				pr_err("%s: waitpid errno=%d (%s)\n",
					args->name, errno, strerror(errno));
			} else {
				/* Probably an SIGARLM, force reap */
				(void)shim_kill(pid, SIGKILL);
				(void)shim_waitpid(pid, &status, 0);
				return;
			}
		}
		if (WIFEXITED(status)) {
			int rc = WEXITSTATUS(status);

			if (rc != EXIT_SUCCESS)
				(*failures)++;
			return;
		}
	}
}

/*
 *  stress_landlock()
 *	stress landlock API
 */
static int stress_landlock(const stress_args_t *args)
{
	static const int landlock_access_flags[] = {
		SHIM_LANDLOCK_ACCESS_FS_EXECUTE,
		SHIM_LANDLOCK_ACCESS_FS_WRITE_FILE,
		SHIM_LANDLOCK_ACCESS_FS_READ_FILE,
		SHIM_LANDLOCK_ACCESS_FS_READ_DIR,
		SHIM_LANDLOCK_ACCESS_FS_REMOVE_DIR,
		SHIM_LANDLOCK_ACCESS_FS_REMOVE_FILE,
		SHIM_LANDLOCK_ACCESS_FS_MAKE_CHAR,
		SHIM_LANDLOCK_ACCESS_FS_MAKE_DIR,
		SHIM_LANDLOCK_ACCESS_FS_MAKE_REG,
		SHIM_LANDLOCK_ACCESS_FS_MAKE_SOCK,
		SHIM_LANDLOCK_ACCESS_FS_MAKE_FIFO,
		SHIM_LANDLOCK_ACCESS_FS_MAKE_BLOCK,
		SHIM_LANDLOCK_ACCESS_FS_MAKE_SYM,
		0,
	};
	int failures = 0;

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		size_t i;
		uint32_t flags = 0;

		/* Exercise with a mix of valid and invalid flags */
		for (i = 0; i < SIZEOF_ARRAY(landlock_access_flags); i++) {
			uint32_t flag = (uint32_t)landlock_access_flags[i];

			flags |= flag;
			stress_landlock_test(args, stress_landlock_flag, &flag, &failures);
			if (failures >= 5)
				goto err;
		}
		stress_landlock_test(args, stress_landlock_flag, &flags, &failures);
		if (failures >= 5)
			goto err;
		flags = ~flags;
		stress_landlock_test(args, stress_landlock_flag, &flags, &failures);
		if (failures >= 5)
			goto err;

		stress_bogo_inc(args);
	} while (stress_continue(args));

err:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return EXIT_SUCCESS;
}

stressor_info_t stress_landlock_info = {
	.stressor = stress_landlock,
	.class = CLASS_OS,
	.supported = stress_landlock_supported,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
stressor_info_t stress_landlock_info = {
	.stressor = stress_unimplemented,
	.class = CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "linux/landlock.h or __NR_landlock* syscall macros"
};
#endif
