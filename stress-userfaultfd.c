/*
 * Copyright (C) 2013-2017 Canonical, Ltd.
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

#if defined(__linux__) && defined(__NR_userfaultfd)

#include <poll.h>
#include <linux/userfaultfd.h>

#define STACK_SIZE	(64 * 1024)

/* Context for clone */
typedef struct {
	const args_t *args;
	uint8_t *data;
	size_t page_size;
	size_t sz;
	pid_t parent;
} context_t;

#endif

static size_t opt_userfaultfd_bytes = DEFAULT_MMAP_BYTES;
static bool set_userfaultfd_bytes = false;

void stress_set_userfaultfd_bytes(const char *optarg)
{
	set_userfaultfd_bytes = true;
	opt_userfaultfd_bytes = (size_t)
		get_uint64_byte_memory(optarg,
			stressor_instances(STRESS_USERFAULTFD));
	check_range_bytes("userfaultfd-bytes", opt_userfaultfd_bytes,
		MIN_MMAP_BYTES, MAX_MEM_LIMIT);
}

#if defined(__linux__) && defined(__NR_userfaultfd)

/*
 *  stress_child_alarm_handler()
 *	SIGALRM handler to terminate child immediately
 */
static void MLOCKED stress_child_alarm_handler(int dummy)
{
	(void)dummy;

	_exit(0);
}

/*
 *  stress_userfaultfd_child()
 *	generate page faults for parent to handle
 */
static int stress_userfaultfd_child(void *arg)
{
	context_t *c = (context_t *)arg;
	const args_t *args = c->args;

	(void)setpgid(0, g_pgrp);
	stress_parent_died_alarm();
	if (stress_sighandler(args->name, SIGALRM, stress_child_alarm_handler, NULL) < 0)
		return EXIT_NO_RESOURCE;

	do {
		uint8_t *ptr, *end = c->data + c->sz;

		/* hint we don't need these pages */
		if (shim_madvise(c->data, c->sz, MADV_DONTNEED) < 0) {
			pr_fail_err("userfaultfd madvise failed");
			(void)kill(c->parent, SIGALRM);
			return -1;
		}
		/* and trigger some page faults */
		for (ptr = c->data; ptr < end; ptr += c->page_size)
			*ptr = 0xff;
	} while (keep_stressing());

	return 0;
}

/*
 *  handle_page_fault()
 *	handle a write page fault caused by child
 */
static inline int handle_page_fault(
	const args_t *args,
	int fd,
	uint8_t *addr,
	void *zero_page,
	uint8_t *data_start,
	uint8_t *data_end,
	size_t page_size)
{
	if ((addr < data_start) || (addr >= data_end)) {
		pr_fail_err("userfaultfd page fault address out of range");
		return -1;
	}

	if (mwc32() & 1) {
		struct uffdio_copy copy;

		copy.copy = 0;
		copy.mode = 0;
		copy.dst = (unsigned long)addr;
		copy.src = (unsigned long)zero_page;
		copy.len = page_size;

		if (ioctl(fd, UFFDIO_COPY, &copy) < 0) {
			pr_fail_err("userfaultfd page fault copy ioctl failed");
			return -1;
		}
	} else {
		struct uffdio_zeropage zeropage;

		zeropage.range.start = (unsigned long)addr;
		zeropage.range.len = page_size;
		zeropage.mode = 0;
		if (ioctl(fd, UFFDIO_ZEROPAGE, &zeropage) < 0) {
			pr_fail_err("userfaultfd page fault zeropage ioctl failed");
			return -1;
		}
	}
	return 0;
}

/*
 *  stress_userfaultfd_oomable()
 *	stress userfaultfd system call, this
 *	is an OOM-able child process that the
 *	parent can restart
 */
static int stress_userfaultfd_oomable(const args_t *args)
{
	const size_t page_size = args->page_size;
	size_t sz;
	uint8_t *data;
	void *zero_page = NULL;
	int fd = -1, status, rc = EXIT_SUCCESS;
	const unsigned int uffdio_copy = 1 << _UFFDIO_COPY;
	const unsigned int uffdio_zeropage = 1 << _UFFDIO_ZEROPAGE;
	pid_t pid;
	struct uffdio_api api;
	struct uffdio_register reg;
	context_t c;
	bool do_poll = true;

	/* Child clone stack */
	static uint8_t stack[STACK_SIZE];
	const ssize_t stack_offset =
		stress_get_stack_direction() * (STACK_SIZE - 64);
	uint8_t *stack_top = stack + stack_offset;

	if (!set_userfaultfd_bytes) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			opt_userfaultfd_bytes = MAX_MMAP_BYTES;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			opt_userfaultfd_bytes = MIN_MMAP_BYTES;
	}
	sz = opt_userfaultfd_bytes & ~(page_size - 1);

	if (posix_memalign(&zero_page, page_size, page_size)) {
		pr_err("%s: zero page allocation failed\n", args->name);
		return EXIT_NO_RESOURCE;
	}

	data = mmap(NULL, sz, PROT_READ | PROT_WRITE,
		MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (data == MAP_FAILED) {
		rc = EXIT_NO_RESOURCE;
		pr_err("%s: mmap failed\n", args->name);
		goto free_zeropage;
	}

	/* Get userfault fd */
	if ((fd = shim_userfaultfd(0)) < 0) {
		rc = exit_status(errno);
		pr_err("%s: userfaultfd failed, errno = %d (%s)\n",
			args->name, errno, strerror(errno));
		goto unmap_data;
	}

	if (stress_set_nonblock(fd) < 0)
		do_poll = false;

	/* API sanity check */
	memset(&api, 0, sizeof(api));
	api.api = UFFD_API;
	api.features = 0;
	if (ioctl(fd, UFFDIO_API, &api) < 0) {
		pr_err("%s: ioctl UFFDIO_API failed, errno = %d (%s)\n",
			args->name, errno, strerror(errno));
		rc = EXIT_FAILURE;
		goto unmap_data;
	}
	if (api.api != UFFD_API) {
		pr_err("%s: ioctl UFFDIO_API API check failed\n",
			args->name);
		rc = EXIT_FAILURE;
		goto unmap_data;
	}

	/* Register fault handling mode */
	memset(&reg, 0, sizeof(reg));
	reg.range.start = (unsigned long)data;
	reg.range.len = sz;
	reg.mode = UFFDIO_REGISTER_MODE_MISSING;
	if (ioctl(fd, UFFDIO_REGISTER, &reg) < 0) {
		pr_err("%s: ioctl UFFDIO_REGISTER failed, errno = %d (%s)\n",
			args->name, errno, strerror(errno));
		rc = EXIT_FAILURE;
		goto unmap_data;
	}

	/* OK, so do we have copy supported? */
	if ((reg.ioctls & uffdio_copy) != uffdio_copy) {
		pr_err("%s: ioctl UFFDIO_REGISTER did not support _UFFDIO_COPY\n",
			args->name);
		rc = EXIT_FAILURE;
		goto unmap_data;
	}
	/* OK, so do we have zeropage supported? */
	if ((reg.ioctls & uffdio_zeropage) != uffdio_zeropage) {
		pr_err("%s: ioctl UFFDIO_REGISTER did not support _UFFDIO_ZEROPAGE\n",
			args->name);
		rc = EXIT_FAILURE;
		goto unmap_data;
	}

	/* Set up context for child */
	c.args = args;
	c.data = data;
	c.sz = sz;
	c.page_size = page_size;
	c.parent = getpid();

	/*
	 *  We need to clone the child and share the same VM address space
	 *  as parent so we can perform the page fault handling
	 */
	pid = clone(stress_userfaultfd_child, align_stack(stack_top),
		SIGCHLD | CLONE_FILES | CLONE_FS | CLONE_SIGHAND | CLONE_VM, &c);
	if (pid < 0) {
		pr_err("%s: fork failed, errno = %d (%s)\n",
			args->name, errno, strerror(errno));
		goto unreg;
	}

	/* Parent */
	do {
		struct uffd_msg msg;
		ssize_t ret;

		/* check we should break out before we block on the read */
		if (!g_keep_stressing_flag)
			break;

		/*
		 * polled wait exercises userfaultfd_poll
		 * in the kernel, but only works if fd is NONBLOCKing
		 */
		if (do_poll) {
			struct pollfd fds[1];

			memset(fds, 0, sizeof fds);
			fds[0].fd = fd;
			fds[0].events = POLLIN;
			/* wait for 1 second max */

			ret = poll(fds, 1, 1000);
			if (ret == 0)
				continue;	/* timed out, redo the poll */
			if (ret < 0) {
				if (errno == EINTR)
					continue;
				if (errno != ENOMEM) {
					pr_fail_err("poll userfaultfd");
					if (!g_keep_stressing_flag)
						break;
				}
				/*
				 *  poll ran out of free space for internal
				 *  fd tables, so give up and block on the
				 *  read anyway
				 */
				goto do_read;
			}
			/* No data, re-poll */
			if (!(fds[0].revents & POLLIN))
				continue;
		}

do_read:
		if ((ret = read(fd, &msg, sizeof(msg))) < 0) {
			if (errno == EINTR)
				continue;
			pr_fail_err("read userfaultfd");
			if (!g_keep_stressing_flag)
				break;
			continue;
		}
		/* We only expect a page fault event */
		if (msg.event != UFFD_EVENT_PAGEFAULT) {
			pr_fail_err("userfaultfd msg not pagefault event");
			continue;
		}
		/* We only expect a write fault */
		if (!(msg.arg.pagefault.flags & UFFD_PAGEFAULT_FLAG_WRITE)) {
			pr_fail_err("userfaultfd msg not write page fault event");
			continue;
		}
		/* Go handle the page fault */
		if (handle_page_fault(args, fd, (uint8_t *)(ptrdiff_t)msg.arg.pagefault.address,
				zero_page, data, data + sz, page_size) < 0)
			break;
		inc_counter(args);
	} while (keep_stressing());

	/* Run it over, zap child */
	(void)kill(pid, SIGKILL);
	if (waitpid(pid, &status, 0) < 0) {
		pr_dbg("%s: waitpid failed, errno = %d (%s)\n",
			args->name, errno, strerror(errno));
	}
unreg:
	if (ioctl(fd, UFFDIO_UNREGISTER, &reg) < 0) {
		pr_err("%s: ioctl UFFDIO_UNREGISTER failed, errno = %d (%s)\n",
			args->name, errno, strerror(errno));
		rc = EXIT_FAILURE;
		goto unmap_data;
	}
unmap_data:
	(void)munmap(data, sz);
free_zeropage:
	free(zero_page);
	return rc;
}

/*
 *  stress_userfaultfd()
 *	stress userfaultfd
 */
int stress_userfaultfd(const args_t *args)
{
	pid_t pid;
	int rc = EXIT_FAILURE;

	pid = fork();
	if (pid < 0) {
		if (errno == EAGAIN)
			return EXIT_NO_RESOURCE;
		pr_err("%s: fork failed: errno=%d: (%s)\n",
			args->name, errno, strerror(errno));
	} else if (pid > 0) {
		/* Parent */
		int status, ret;

		(void)setpgid(pid, g_pgrp);
		ret = waitpid(pid, &status, 0);
		if (ret < 0) {
			if (errno != EINTR)
				pr_dbg("%s: waitpid(): errno=%d (%s)\n",
					args->name, errno, strerror(errno));
			(void)kill(pid, SIGTERM);
			(void)kill(pid, SIGKILL);
			(void)waitpid(pid, &status, 0);
		} else if (WIFSIGNALED(status)) {
			pr_dbg("%s: child died: %s (instance %d)\n",
				args->name, stress_strsignal(WTERMSIG(status)),
				args->instance);
			/* If we got killed by OOM killer, report this */
			if (WTERMSIG(status) == SIGKILL) {
				log_system_mem_info();
				pr_dbg("%s: assuming killed by OOM "
					"killer, aborting "
					"(instance %d)\n",
					args->name, args->instance);
				return EXIT_NO_RESOURCE;
			}
			return EXIT_FAILURE;
		}
		rc = WEXITSTATUS(status);
	} else if (pid == 0) {
		/* Child */
		(void)setpgid(0, g_pgrp);
		stress_parent_died_alarm();

		_exit(stress_userfaultfd_oomable(args));
	}
	return rc;
}
#else
int stress_userfaultfd(const args_t *args)
{
	return stress_not_implemented(args);
}
#endif
