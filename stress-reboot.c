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
#include "core-capabilities.h"

#include <sched.h>

static const stress_help_t help[] = {
	{ NULL,	"reboot N",	"start N workers that exercise bad reboot calls" },
	{ NULL,	"reboot-ops N",	"stop after N bogo reboot operations" },
	{ NULL,	NULL,		NULL }
};

#if defined(__linux__) && 	\
    defined(__NR_reboot)

#define CLONE_STACK_SIZE	(16*1024)

#define SHIM_LINUX_BOOT_MAGIC1			(0xfee1dead)
#define SHIM_LINUX_REBOOT_MAGIC2		(0x28121969)
#define SHIM_LINUX_REBOOT_MAGIC2A		(0x05121996)
#define SHIM_LINUX_REBOOT_MAGIC2B		(0x16041998)
#define SHIM_LINUX_REBOOT_MAGIC2C		(0x20112000)

#define SHIM_LINUX_REBOOT_CMD_POWER_OFF		(0x4321fedc)
#define SHIM_LINUX_REBOOT_CMD_RESTART		(0x01234567)
#define SHIM_LINUX_REBOOT_CMD_SW_SUSPEND	(0xd000fce2)

static const uint32_t boot_magic[] = {
	SHIM_LINUX_REBOOT_MAGIC2,
	SHIM_LINUX_REBOOT_MAGIC2A,
	SHIM_LINUX_REBOOT_MAGIC2B,
	SHIM_LINUX_REBOOT_MAGIC2C,
	0x00000000,			/* Intentionally invalid */
	0xffffffff,			/* Intentionally invalid */
};

#if defined(HAVE_CLONE)
/*
 *  reboot_func()
 *	reboot a process in a PID namespace
 */
static inline void NORETURN no_return_reboot_clone_func(void *arg)
{
	size_t i, j = stress_mwc8modn(SIZEOF_ARRAY(boot_magic));

	(void)arg;

	/* Random starting reboot command */
	for (i = 0; i < SIZEOF_ARRAY(boot_magic); i++) {
		errno = 0;
		VOID_RET(int, shim_reboot((int)SHIM_LINUX_BOOT_MAGIC1,
			(int)boot_magic[j],
			(int)SHIM_LINUX_REBOOT_CMD_POWER_OFF, NULL));
		j++;
		if (j >= SIZEOF_ARRAY(boot_magic))
			j = 0;
	}

	_exit(errno);
}

static int reboot_clone_func(void *arg)
{
	no_return_reboot_clone_func(arg);

	/* Should never get here */
	return -1;
}
#endif

/*
 *  stress_reboot()
 *	stress reboot system call
 */
static int stress_reboot(stress_args_t *args)
{
	const bool reboot_capable = stress_check_capability(SHIM_CAP_SYS_BOOT);
	int rc = EXIT_SUCCESS;
#if defined(HAVE_CLONE)
	char *stack;

	stack = (char *)malloc(CLONE_STACK_SIZE);
	if (!stack) {
		pr_inf_skip("%s: cannot allocate %zu byte stack%s, skipping stressor\n",
			args->name, (size_t)CLONE_STACK_SIZE,
			stress_get_memfree_str());
		return EXIT_NO_RESOURCE;
	}
#endif

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		int ret;
#if defined(HAVE_CLONE)
		char *stack_top = (char *)stress_get_stack_top((void *)stack, CLONE_STACK_SIZE);
		pid_t pid;

		pid = clone(reboot_clone_func, stress_align_stack(stack_top),
			CLONE_NEWPID | CLONE_NEWNS, NULL);
		if (pid >= 0) {
			int status;

			(void)stress_mwc8();

			(void)shim_waitpid(pid, &status, (int)__WCLONE);
			ret = WEXITSTATUS(status);
			if (WIFEXITED(status) && (ret != 0)) {
				pr_fail("%s: reboot in PID namespace failed, errno=%d (%s)\n",
					args->name, ret, strerror(ret));
				rc = EXIT_FAILURE;
			}
		}
#endif
		ret = shim_reboot(0, 0, (int)SHIM_LINUX_REBOOT_CMD_RESTART, 0);
		if (ret < 0) {
			if (reboot_capable) {
				if (errno == EPERM) {
					pr_inf_skip("%s: no permission, skipping stressor\n", args->name);
					rc = EXIT_NO_RESOURCE;
					break;
				}
				if (errno != EINVAL) {
					pr_fail("%s: reboot with incorrect magic didn't return EINVAL, errno=%d (%s)\n",
						args->name, errno, strerror(errno));
					rc = EXIT_FAILURE;
				}
			} else {
				if ((errno != EPERM) && (errno != EINVAL)) {
					pr_fail("%s: reboot when not reboot capable didn't return EPERM, errno=%d (%s)\n",
						args->name, errno, strerror(errno));
					rc = EXIT_FAILURE;
				}
			}
		}

		ret = shim_reboot(0, 0, (int)SHIM_LINUX_REBOOT_CMD_SW_SUSPEND, 0);
		if (ret < 0) {
			if (reboot_capable) {
				if (errno == EPERM) {
					pr_inf_skip("%s: no permission, skipping stressor\n", args->name);
					rc = EXIT_NO_RESOURCE;
					break;
				}
				if (errno != EINVAL) {
					pr_fail("%s: reboot with incorrect magic didn't return EINVAL, errno=%d (%s)\n",
						args->name, errno, strerror(errno));
					rc = EXIT_FAILURE;
				}
			} else {
				if ((errno != EPERM) && (errno != EINVAL)) {
					pr_fail("%s: reboot when not reboot capable didn't return EPERM, errno=%d (%s)\n",
						args->name, errno, strerror(errno));
					rc = EXIT_FAILURE;
				}
			}
		}

		if (!reboot_capable) {
			size_t i;

			for (i = 0; i < SIZEOF_ARRAY(boot_magic); i++) {
				errno = 0;
				VOID_RET(int, shim_reboot((int)SHIM_LINUX_BOOT_MAGIC1,
					(int)boot_magic[i],
					(int)SHIM_LINUX_REBOOT_CMD_POWER_OFF, NULL));
				if (errno == EINVAL)
					continue;
				if (errno != EPERM) {
					pr_fail("%s: reboot when not reboot capable didn't return EPERM, errno=%d (%s)\n",
						args->name, errno, strerror(errno));
					rc = EXIT_FAILURE;
				}
			}
		}
		stress_bogo_inc(args);
	} while ((rc == EXIT_SUCCESS) && stress_continue(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

#if defined(HAVE_CLONE)
	free(stack);
#endif

	return rc;
}

const stressor_info_t stress_reboot_info = {
	.stressor = stress_reboot,
	.classifier = CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help
};

#else

const stressor_info_t stress_reboot_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "only supported on Linux with reboot() support"
};
#endif
