/*
 * Copyright (C) 2013-2016 Canonical, Ltd.
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
#define _GNU_SOURCE

#include "stress-ng.h"

#if defined(STRESS_BIND_MOUNT)

#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/mount.h>

#define CLONE_STACK_SIZE	(64*1024)

/* Context for clone */
typedef struct {
	uint64_t max_ops;
	uint64_t *counter;
	const char *name;
} context_t;

/*
 *  stress_bind_mount_child()
 *	aggressively perform bind mounts, this can force out of memory
 *	situations
 */
int stress_bind_mount_child(void *arg)
{
	context_t *context = (context_t *)arg;

	setpgid(0, pgrp);
	stress_parent_died_alarm();

	do {
		if (mount("/", "/", "", MS_BIND | MS_REC, 0) < 0) {
			pr_fail(stderr, "%s: mount failed: errno=%d (%s)\n",
				context->name, errno, strerror(errno));
			break;
		}
		/*
		 *  The following fails with -EBUSY, but try it anyhow
	`	 *  just to make the kernel work harder
		 */
		(void)umount("/");
	} while (opt_do_run &&
		 (!context->max_ops || *(context->counter) < context->max_ops));

	return 0;
}

/*
 *  stress_bind_mount()
 *      stress bind mounting
 */
int stress_bind_mount(
        uint64_t *const counter,
        const uint32_t instance,
        const uint64_t max_ops,
        const char *name)
{
	int pid = 0, status;
	context_t context;
	const ssize_t stack_offset =
		stress_get_stack_direction(&pid) *
		(CLONE_STACK_SIZE - 64);
	char stack[CLONE_STACK_SIZE];
	char *stack_top = stack + stack_offset;

	(void)instance;

	context.name = name;
	context.max_ops = max_ops;
	context.counter = counter;

	pid = clone(stress_bind_mount_child,
		align_stack(stack_top),
		CLONE_NEWUSER | CLONE_NEWNS | CLONE_NEWPID | CLONE_VM,
		&context, 0);
	if (pid < 0) {
		int rc = exit_status(errno);

		pr_fail(stderr, "%s: clone failed: errno=%d (%s)\n",
			name, errno, strerror(errno));
		return rc;
	}

	do {
		/* Twiddle thumbs */
		sleep(1);
	} while (opt_do_run && (!max_ops || *counter < max_ops));

	(void)kill(pid, SIGKILL);
	(void)waitpid(pid, &status, 0);

	return EXIT_SUCCESS;
}

#endif
