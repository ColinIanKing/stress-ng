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
#include "core-killpid.h"

#include <sched.h>

#if defined(HAVE_SYS_UIO_H)
#include <sys/uio.h>
#else
UNEXPECTED
#endif

#define MIN_VM_RW_BYTES		(4 * KB)
#define MAX_VM_RW_BYTES		(MAX_MEM_LIMIT)
#define DEFAULT_VM_RW_BYTES	(16 * MB)

static const stress_help_t help[] = {
	{ NULL,	"vm-rw N",	 "start N vm read/write process_vm* copy workers" },
	{ NULL,	"vm-rw-bytes N", "transfer N bytes of memory per bogo operation" },
	{ NULL,	"vm-rw-ops N",	 "stop after N vm process_vm* copy bogo operations" },
	{ NULL,	NULL,		 NULL }
};

#if defined(HAVE_PROCESS_VM_READV) &&	\
    defined(HAVE_PROCESS_VM_WRITEV) &&	\
    defined(HAVE_CLONE) &&		\
    defined(CLONE_VM)

#define STACK_SIZE	(64 * 1024)
#define CHUNK_SIZE	(1 * GB)

typedef struct {
	stress_args_t *args;
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

static const stress_opt_t opts[] = {
	{ OPT_vm_rw_bytes, "vm-rw-bytes", TYPE_ID_SIZE_T_BYTES_VM, MIN_VM_RW_BYTES, MAX_VM_RW_BYTES, NULL },
	END_OPT,
};

#if defined(HAVE_PROCESS_VM_READV) &&	\
    defined(HAVE_PROCESS_VM_WRITEV) &&	\
    defined(HAVE_CLONE) &&		\
    defined(CLONE_VM)

static int OPTIMIZE3 stress_vm_child(void *arg)
{
	const stress_context_t *ctxt = (stress_context_t *)arg;
	stress_args_t *args = ctxt->args;
	const bool verify = !!(g_opt_flags & OPT_FLAGS_VERIFY);

	uint8_t *buf;
	int rc = EXIT_SUCCESS;
	stress_addr_msg_t msg_rd ALIGN64, msg_wr ALIGN64;

	stress_parent_died_alarm();

	/* Close unwanted ends */
	(void)close(ctxt->pipe_wr[0]);
	(void)close(ctxt->pipe_rd[1]);

	buf = mmap(NULL, ctxt->sz, PROT_READ | PROT_WRITE,
		MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (buf == MAP_FAILED) {
		rc = stress_exit_status(errno);
		pr_fail("%s: failed to mmap %zu bytes%s, errno=%d (%s)\n",
			args->name, ctxt->sz,
			stress_get_memfree_str(), errno, strerror(errno));
		goto cleanup;
	}
	stress_set_vma_anon_name(buf, ctxt->sz, "context");

	while (stress_continue_flag()) {
		register uint8_t *ptr;
		register const uint8_t *end = buf + ctxt->sz;
		ssize_t rwret;

		(void)shim_memset(&msg_wr, 0, sizeof(msg_wr));
		msg_wr.addr = buf;
		msg_wr.val = 0;

		/* Send address of buffer to parent */
redo_wr1:
		rwret = write(ctxt->pipe_wr[1], &msg_wr, sizeof(msg_wr));
		if (UNLIKELY(rwret < 0)) {
			if ((errno == EAGAIN) || (errno == EINTR))
				goto redo_wr1;
			if (errno != EBADF) {
				pr_fail("%s: write failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				rc = EXIT_FAILURE;
			}
			break;
		}
redo_rd1:
		/* Wait for parent to populate data */
		rwret = read(ctxt->pipe_rd[0], &msg_rd, sizeof(msg_rd));
		if (UNLIKELY(rwret < 0)) {
			if ((errno == EAGAIN) || (errno == EINTR))
				goto redo_rd1;
			pr_fail("%s: read failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			rc = EXIT_FAILURE;
			break;
		}
		if (UNLIKELY(rwret != sizeof(msg_rd))) {
			if (rwret == 0)
				break;
			pr_fail("%s: read failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			rc = EXIT_FAILURE;
			break;
		}

		if (UNLIKELY(verify)) {
			/* Check memory altered by parent is sane */
			for (ptr = buf; ptr < end; ptr += args->page_size) {
				if (UNLIKELY(*ptr != msg_rd.val)) {
					pr_fail("%s: memory at %p (offset %tx): %d vs %d\n",
						args->name, (void *)ptr, ptr - buf, *ptr, msg_rd.val);
					rc = EXIT_FAILURE;
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
	if (UNLIKELY(write(ctxt->pipe_wr[1], &msg_wr, sizeof(msg_wr)) <= 0)) {
		if (errno != EBADF)
			pr_dbg("%s: failed to write termination message "
				"over pipe, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
	}

	(void)close(ctxt->pipe_wr[1]);
	(void)close(ctxt->pipe_rd[0]);
	(void)munmap((void *)buf, ctxt->sz);
	return rc;
}


static int OPTIMIZE3 stress_vm_parent(stress_context_t *ctxt)
{
	/* Parent */
	uint8_t val = 0x10;
	uint8_t *localbuf;
	stress_addr_msg_t msg_rd, msg_wr;
	stress_args_t *args = ctxt->args;
	size_t sz;
	const bool verify = !!(g_opt_flags & OPT_FLAGS_VERIFY);

	localbuf = mmap(NULL, ctxt->sz, PROT_READ | PROT_WRITE,
			MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (localbuf == MAP_FAILED) {
		pr_fail("%s: failed to mmap %zu bytes%s, errno=%d (%s)\n",
			args->name, ctxt->sz,
			stress_get_memfree_str(), errno, strerror(errno));
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
		struct iovec local[1] ALIGN64, remote[1] ALIGN64;
		uint8_t *ptr1, *ptr2;
		const uint8_t *end = localbuf + ctxt->sz;
		ssize_t rwret;
		size_t i, len;

		/* Wait for address of child's buffer */
redo_rd2:
		if (UNLIKELY(!stress_continue_flag()))
			break;
		rwret = read(ctxt->pipe_wr[0], &msg_rd, sizeof(msg_rd));
		if (UNLIKELY(rwret < 0)) {
			if ((errno == EAGAIN) || (errno == EINTR))
				goto redo_rd2;
			pr_fail("%s: read failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			break;
		}
		if (UNLIKELY(rwret != sizeof(msg_rd))) {
			if (rwret == 0)
				break;
			pr_fail("%s: read failed, expected %zd bytes, got %zd\n",
				args->name, sizeof(msg_rd), rwret);
			break;
		}
		/* Child telling us it's terminating? */
		if (UNLIKELY(!msg_rd.addr))
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

			if (UNLIKELY(process_vm_readv(ctxt->pid, local, 1, remote, 1, 0) < 0)) {
				pr_fail("%s: process_vm_readv failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				goto fail;
			}
		}

		if (UNLIKELY(verify)) {
			/* Check data is sane */
			for (ptr1 = localbuf; ptr1 < end; ptr1 += args->page_size) {
				if (UNLIKELY(*ptr1)) {
					pr_fail("%s: memory at %p (offset %tx): %d vs %d\n",
						args->name, (void *)ptr1, ptr1 - localbuf, *ptr1, msg_rd.val);
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
		(void)process_vm_readv(ctxt->pid, local, 1, remote, 1, ~0U);

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
			if (UNLIKELY(process_vm_writev(ctxt->pid, local, 1, remote, 1, 0) < 0)) {
				pr_fail("%s: process_vm_writev failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				goto fail;
			}
		}
		msg_wr.val = val;
		val++;
redo_wr2:
		if (UNLIKELY(!stress_continue_flag()))
			break;
		/* Inform child that memory has been changed */
		rwret = write(ctxt->pipe_rd[1], &msg_wr, sizeof(msg_wr));
		if (UNLIKELY(rwret < 0)) {
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
		(void)process_vm_writev(ctxt->pid, local, 1, remote, 1, ~0U);

		/* Exercise invalid pid */
		local[0].iov_base = localbuf;
		local[0].iov_len = len;
		remote[0].iov_base = msg_wr.addr;
		remote[0].iov_len = len;
		(void)process_vm_writev(~0, local, 1, remote, 1, 0);

		stress_bogo_inc(args);
	} while (stress_continue(args));
fail:
	/* Tell child we're done */
	msg_wr.addr = NULL;
	msg_wr.val = 0;
	if (write(ctxt->pipe_wr[0], &msg_wr, sizeof(msg_wr)) < 0) {
		if (errno != EBADF)
			pr_dbg("%s: failed to write "
				"termination message "
				"over pipe, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
	}
	(void)close(ctxt->pipe_wr[0]);
	(void)close(ctxt->pipe_rd[1]);
	if (ctxt->pid > 1)
		stress_kill_and_wait(args, ctxt->pid, SIGALRM, false);
	(void)munmap((void *)localbuf, ctxt->sz);

	return EXIT_SUCCESS;
}

/*
 *  stress_vm_rw
 *	stress vm_read_v/vm_write_v
 */
static int stress_vm_rw(stress_args_t *args)
{
	stress_context_t ctxt;
	uint8_t stack[64*1024];
	uint8_t *stack_top = (uint8_t *)stress_get_stack_top((void *)stack, STACK_SIZE);
	size_t vm_rw_bytes, vm_rw_bytes_total = DEFAULT_VM_RW_BYTES;
	int rc;

	if (!stress_get_setting("vm-rw-bytes", &vm_rw_bytes_total)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			vm_rw_bytes_total = MAX_32;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			vm_rw_bytes_total = MIN_VM_RW_BYTES;
	}
	vm_rw_bytes = vm_rw_bytes_total / args->instances;
	if (vm_rw_bytes < MIN_VM_RW_BYTES)
		vm_rw_bytes = MIN_VM_RW_BYTES;
	if (vm_rw_bytes < args->page_size)
		vm_rw_bytes = args->page_size;
	if (stress_instance_zero(args))
		stress_usage_bytes(args, vm_rw_bytes, vm_rw_bytes * args->instances);
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

	(void)shim_memset(stack, 0, sizeof(stack));

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);
again:
	ctxt.pid = clone(stress_vm_child, stress_align_stack(stack_top),
		SIGCHLD | CLONE_VM, &ctxt);
	if (ctxt.pid < 0) {
		if (stress_continue_flag() && (errno == EAGAIN))
			goto again;
		pr_fail("%s: clone failed, errno=%d (%s)\n",
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

const stressor_info_t stress_vm_rw_info = {
	.stressor = stress_vm_rw,
	.classifier = CLASS_VM | CLASS_MEMORY | CLASS_OS,
	.opts = opts,
	.verify = VERIFY_OPTIONAL,
	.help = help
};
#else
const stressor_info_t stress_vm_rw_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_VM | CLASS_MEMORY | CLASS_OS,
	.opts = opts,
	.verify = VERIFY_OPTIONAL,
	.help = help,
	.unimplemented_reason = "built without process_vm_readv(), process_vm_writev(), clone() or CLONE_VM defined"
};
#endif
