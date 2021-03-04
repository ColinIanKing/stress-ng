/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
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
	{ NULL,	"vm-rw N",	 "start N vm read/write process_vm* copy workers" },
	{ NULL,	"vm-rw-bytes N", "transfer N bytes of memory per bogo operation" },
	{ NULL,	"vm-rw-ops N",	 "stop after N vm process_vm* copy bogo operations" },
	{ NULL,	NULL,		 NULL }
};

#if defined(HAVE_PROCESS_VM_READV) &&	\
    defined(HAVE_PROCESS_VM_READV) &&	\
    defined(HAVE_CLONE) &&		\
    defined(CLONE_VM)

#define STACK_SIZE	(64 * 1024)
#define CHUNK_SIZE	(1 * GB)

typedef struct {
	const stress_args_t *args;
	size_t sz;
	size_t iov_count;
	pid_t pid;
	int pipe_wr[2];
	int pipe_rd[2];
} stress_context_t;

typedef struct {
	void *addr;	/* Buffer to read/write to */
	uint8_t val;	/* Value to check */
} stress_addr_msg_t;

#endif

static int stress_set_vm_rw_bytes(const char *opt)
{
	size_t vm_rw_bytes;

	vm_rw_bytes = (size_t)stress_get_uint64_byte_memory(opt, 1);
	stress_check_range_bytes("vm-rw-bytes", vm_rw_bytes,
		MIN_VM_RW_BYTES, MAX_MEM_LIMIT);
	return stress_set_setting("vm-rw-bytes", TYPE_ID_SIZE_T, &vm_rw_bytes);
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_vm_rw_bytes,	stress_set_vm_rw_bytes },
	{ 0,			NULL }
};

#if defined(HAVE_PROCESS_VM_READV) &&	\
    defined(HAVE_PROCESS_VM_READV) &&	\
    defined(HAVE_CLONE) &&		\
    defined(CLONE_VM)

static int stress_vm_child(void *arg)
{
	const stress_context_t *ctxt = (stress_context_t *)arg;
	const stress_args_t *args = ctxt->args;

	uint8_t *buf;
	int ret = EXIT_SUCCESS;
	stress_addr_msg_t msg_rd, msg_wr;

	(void)setpgid(0, g_pgrp);
	stress_parent_died_alarm();

	/* Close unwanted ends */
	(void)close(ctxt->pipe_wr[0]);
	(void)close(ctxt->pipe_rd[1]);

	buf = mmap(NULL, ctxt->sz, PROT_READ | PROT_WRITE,
		MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (buf == MAP_FAILED) {
		ret = exit_status(errno);
		pr_fail("%s: mmap failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		goto cleanup;
	}

	while (keep_stressing_flag()) {
		uint8_t *ptr, *end = buf + ctxt->sz;
		int rwret;

		(void)memset(&msg_wr, 0, sizeof(msg_wr));
		msg_wr.addr = buf;
		msg_wr.val = 0;

		/* Send address of buffer to parent */
redo_wr1:
		rwret = write(ctxt->pipe_wr[1], &msg_wr, sizeof(msg_wr));
		if (rwret < 0) {
			if ((errno == EAGAIN) || (errno == EINTR))
				goto redo_wr1;
			if (errno != EBADF)
				pr_fail("%s: write failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
			break;
		}
redo_rd1:
		/* Wait for parent to populate data */
		rwret = read(ctxt->pipe_rd[0], &msg_rd, sizeof(msg_rd));
		if (rwret < 0) {
			if ((errno == EAGAIN) || (errno == EINTR))
				goto redo_rd1;
			pr_fail("%s: read failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			break;
		}
		if (rwret == 0)
			break;
		if (rwret != sizeof(msg_rd)) {
			pr_fail("%s: read failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			break;
		}

		if (g_opt_flags & OPT_FLAGS_VERIFY) {
			/* Check memory altered by parent is sane */
			for (ptr = buf; ptr < end; ptr += args->page_size) {
				if (*ptr != msg_rd.val) {
					pr_fail("%s: memory at %p (offset %tx): %d vs %d\n",
						args->name, ptr, ptr - buf, *ptr, msg_rd.val);
					goto cleanup;
				}
				*ptr = 0;
			}
		}
	}
cleanup:
	/* Tell parent we're done */
	msg_wr.addr = 0;
	msg_wr.val = 0;
	if (write(ctxt->pipe_wr[1], &msg_wr, sizeof(msg_wr)) <= 0) {
		if (errno != EBADF)
			pr_dbg("%s: failed to write termination message "
				"over pipe: errno=%d (%s)\n",
				args->name, errno, strerror(errno));
	}

	(void)close(ctxt->pipe_wr[1]);
	(void)close(ctxt->pipe_rd[0]);
	(void)munmap(buf, ctxt->sz);
	return ret;
}


static int stress_vm_parent(stress_context_t *ctxt)
{
	/* Parent */
	int status;
	uint8_t val = 0x10;
	uint8_t *localbuf;
	stress_addr_msg_t msg_rd, msg_wr;
	const stress_args_t *args = ctxt->args;
	size_t sz;

	(void)setpgid(ctxt->pid, g_pgrp);

	localbuf = mmap(NULL, ctxt->sz, PROT_READ | PROT_WRITE,
			MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (localbuf == MAP_FAILED) {
		pr_fail("%s: mmap failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		(void)close(ctxt->pipe_wr[0]);
		(void)close(ctxt->pipe_wr[1]);
		(void)close(ctxt->pipe_rd[0]);
		(void)close(ctxt->pipe_rd[1]);
		return EXIT_FAILURE;
	}

	/* Close unwanted ends */
	(void)close(ctxt->pipe_wr[1]);
	(void)close(ctxt->pipe_rd[0]);

	do {
		struct iovec local[1], remote[1];
		uint8_t *ptr1, *ptr2, *end = localbuf + ctxt->sz;
		int ret;
		size_t i, len;

		/* Wait for address of child's buffer */
redo_rd2:
		if (!keep_stressing_flag())
			break;
		ret = read(ctxt->pipe_wr[0], &msg_rd, sizeof(msg_rd));
		if (ret < 0) {
			if ((errno == EAGAIN) || (errno == EINTR))
				goto redo_rd2;
			pr_fail("%s: read failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			break;
		}
		if (ret == 0)
			break;
		if (ret != sizeof(msg_rd)) {
			pr_fail("%s: read failed, expected %zd bytes, got %d\n",
				args->name, sizeof(msg_rd), ret);
			break;
		}
		/* Child telling us it's terminating? */
		if (!msg_rd.addr)
			break;

		/* Perform read from child's memory */
		ptr1 = localbuf;
		ptr2 = msg_rd.addr;
		sz = ctxt->sz;
		for (i = 0; i < ctxt->iov_count; i++) {
			len = sz >= CHUNK_SIZE ? CHUNK_SIZE : sz;

			local[0].iov_base = ptr1;
			local[0].iov_len = len;
			ptr1 += len;
			remote[0].iov_base = ptr2;
			remote[0].iov_len = len;
			ptr2 += len;
			sz -= len;

			if (process_vm_readv(ctxt->pid, local, 1, remote, 1, 0) < 0) {
				pr_fail("%s: process_vm_readv failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				goto fail;
			}
		}

		if (g_opt_flags & OPT_FLAGS_VERIFY) {
			/* Check data is sane */
			for (ptr1 = localbuf; ptr1 < end; ptr1 += args->page_size) {
				if (*ptr1) {
					pr_fail("%s: memory at %p (offset %tx): %d vs %d\n",
						args->name, ptr1, ptr1 - localbuf, *ptr1, msg_rd.val);
					goto fail;
				}
				*ptr1 = 0;
			}
			/* Set memory */
			for (ptr1 = localbuf; ptr1 < end; ptr1 += args->page_size)
				*ptr1 = val;
		}

		/* Exercise invalid flags */
		len = ctxt->sz >= CHUNK_SIZE ? CHUNK_SIZE : ctxt->sz;
		local[0].iov_base = localbuf;
		local[0].iov_len = len;
		remote[0].iov_base = msg_rd.addr;
		remote[0].iov_len = len;
		(void)process_vm_readv(ctxt->pid, local, 1, remote, 1, ~0);

		/* Exercise invalid pid */
		local[0].iov_base = localbuf;
		local[0].iov_len = len;
		remote[0].iov_base = msg_rd.addr;
		remote[0].iov_len = len;
		(void)process_vm_readv(~0, local, 1, remote, 1, 0);

		/* Write to child's memory */
		msg_wr = msg_rd;
		ptr1 = localbuf;
		ptr2 = msg_rd.addr;
		sz = ctxt->sz;
		for (i = 0; i < ctxt->iov_count; i++) {
			len = sz >= CHUNK_SIZE ? CHUNK_SIZE : sz;

			local[0].iov_base = ptr1;
			local[0].iov_len = len;
			ptr1 += len;
			remote[0].iov_base = ptr2;
			remote[0].iov_len = len;
			ptr2 += len;
			sz -= len;
			if (process_vm_writev(ctxt->pid, local, 1, remote, 1, 0) < 0) {
				pr_fail("%s: process_vm_writev failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				goto fail;
			}
		}
		msg_wr.val = val;
		val++;
redo_wr2:
		if (!keep_stressing_flag())
			break;
		/* Inform child that memory has been changed */
		ret = write(ctxt->pipe_rd[1], &msg_wr, sizeof(msg_wr));
		if (ret < 0) {
			if ((errno == EAGAIN) || (errno == EINTR))
				goto redo_wr2;
			if (errno != EBADF)
				pr_fail("%s: write failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
			break;
		}

		/* Exercise invalid flags */
		len = ctxt->sz >= CHUNK_SIZE ? CHUNK_SIZE : ctxt->sz;
		local[0].iov_base = localbuf;
		local[0].iov_len = len;
		remote[0].iov_base = msg_wr.addr;
		remote[0].iov_len = len;
		(void)process_vm_writev(ctxt->pid, local, 1, remote, 1, ~0);

		/* Exercise invalid pid */
		local[0].iov_base = localbuf;
		local[0].iov_len = len;
		remote[0].iov_base = msg_wr.addr;
		remote[0].iov_len = len;
		(void)process_vm_writev(~0, local, 1, remote, 1, 0);

		inc_counter(args);
	} while (keep_stressing(args));
fail:
	/* Tell child we're done */
	msg_wr.addr = NULL;
	msg_wr.val = 0;
	if (write(ctxt->pipe_wr[0], &msg_wr, sizeof(msg_wr)) < 0) {
		if (errno != EBADF)
			pr_dbg("%s: failed to write "
				"termination message "
				"over pipe: errno=%d (%s)\n",
				args->name, errno, strerror(errno));
	}
	(void)close(ctxt->pipe_wr[0]);
	(void)close(ctxt->pipe_rd[1]);
	(void)kill(ctxt->pid, SIGKILL);
	(void)shim_waitpid(ctxt->pid, &status, 0);
	(void)munmap(localbuf, ctxt->sz);

	return EXIT_SUCCESS;
}

/*
 *  stress_vm_rw
 *	stress vm_read_v/vm_write_v
 */
static int stress_vm_rw(const stress_args_t *args)
{
	stress_context_t ctxt;
	uint8_t stack[64*1024];
	const ssize_t stack_offset =
		stress_get_stack_direction() * (STACK_SIZE - 64);
	uint8_t *stack_top = stack + stack_offset;
	size_t vm_rw_bytes = DEFAULT_VM_RW_BYTES;
	int rc;

	if (!stress_get_setting("vm-rw-bytes", &vm_rw_bytes)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			vm_rw_bytes = MAX_32;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			vm_rw_bytes = MIN_VM_RW_BYTES;
	}
	vm_rw_bytes /= args->num_instances;
	if (vm_rw_bytes < MIN_VM_RW_BYTES)
		vm_rw_bytes = MIN_VM_RW_BYTES;
	if (vm_rw_bytes < args->page_size)
		vm_rw_bytes = args->page_size;
	ctxt.args = args;
	ctxt.sz = vm_rw_bytes & ~(args->page_size - 1);
	ctxt.iov_count = (ctxt.sz + CHUNK_SIZE - 1) / CHUNK_SIZE;

	if (pipe(ctxt.pipe_wr) < 0) {
		pr_fail("%s: pipe failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}
	if (pipe(ctxt.pipe_rd) < 0) {
		(void)close(ctxt.pipe_wr[0]);
		(void)close(ctxt.pipe_wr[1]);
		pr_fail("%s: pipe failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}

	stress_set_proc_state(args->name, STRESS_STATE_RUN);
again:
	ctxt.pid = clone(stress_vm_child, stress_align_stack(stack_top),
		SIGCHLD | CLONE_VM, &ctxt);
	if (ctxt.pid < 0) {
		if (keep_stressing_flag() && (errno == EAGAIN))
			goto again;
		pr_fail("%s: clone  failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		(void)close(ctxt.pipe_wr[0]);
		(void)close(ctxt.pipe_wr[1]);
		(void)close(ctxt.pipe_rd[0]);
		(void)close(ctxt.pipe_rd[1]);
		return EXIT_NO_RESOURCE;
	}
	rc = stress_vm_parent(&ctxt);

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return rc;
}

stressor_info_t stress_vm_rw_info = {
	.stressor = stress_vm_rw,
	.class = CLASS_VM | CLASS_MEMORY | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
#else
stressor_info_t stress_vm_rw_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_VM | CLASS_MEMORY | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
#endif
