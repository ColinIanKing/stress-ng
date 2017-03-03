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

static size_t opt_mremap_bytes = DEFAULT_MREMAP_BYTES;
static bool set_mremap_bytes = false;

void stress_set_mremap_bytes(const char *optarg)
{
	set_mremap_bytes = true;
	opt_mremap_bytes = (size_t)
		get_uint64_byte_memory(optarg,
			stressor_instances(STRESS_MREMAP));
	check_range_bytes("mmap-bytes", opt_mremap_bytes,
		MIN_MREMAP_BYTES, MAX_MEM_LIMIT);
}

#if defined(__linux__) && NEED_GLIBC(2,4,0)

#if defined(MREMAP_FIXED)
/*
 *  rand_mremap_addr()
 *	try and find a random unmapped region of memory
 */
static inline void *rand_mremap_addr(const size_t sz, int flags)
{
	void *addr;

	flags &= ~(MREMAP_FIXED | MAP_SHARED | MAP_POPULATE);
	flags |= (MAP_PRIVATE | MAP_ANONYMOUS);

	addr = mmap(NULL, sz, PROT_READ | PROT_WRITE, flags, -1, 0);
	if (addr == MAP_FAILED)
		return NULL;

	munmap(addr, sz);

	/*
	 * At this point, we know that we can remap to this addr
	 * in this process if we don't do any memory mappings between
	 * the munmap above and the remapping
	 */

	return addr;
}
#endif



/*
 *  try_remap()
 *	try and remap old size to new size
 */
static int try_remap(
	const args_t *args,
	uint8_t **buf,
	const size_t old_sz,
	const size_t new_sz)
{
	uint8_t *newbuf;
	int retry, flags = 0;
#if defined(MREMAP_MAYMOVE)
	const int maymove = MREMAP_MAYMOVE;
#else
	const int maymove = 0;
#endif

#if defined(MREMAP_FIXED) && defined(MREMAP_MAYMOVE)
	flags = maymove | (mwc32() & MREMAP_FIXED);
#else
	flags = maymove;
#endif

	for (retry = 0; retry < 100; retry++) {
#if defined(MREMAP_FIXED)
		void *addr = rand_mremap_addr(new_sz, flags);
#endif
		if (!g_keep_stressing_flag)
			return 0;
#if defined(MREMAP_FIXED)
		if (addr) {
			newbuf = mremap(*buf, old_sz, new_sz, flags, addr);
		} else {
			newbuf = mremap(*buf, old_sz, new_sz, flags & ~MREMAP_FIXED);
		}
#else
		newbuf = mremap(*buf, old_sz, new_sz, flags);
#endif
		if (newbuf != MAP_FAILED) {
			*buf = newbuf;
			return 0;
		}

		switch (errno) {
		case ENOMEM:
		case EAGAIN:
			continue;
		case EINVAL:
#if defined(MREMAP_FIXED)
			/*
			 * Earlier kernels may not support this or we
			 * chose a bad random address, so just fall
			 * back to non fixed remapping
			 */
			if (flags & MREMAP_FIXED) {
				flags &= ~MREMAP_FIXED;
				continue;
			}
#endif
			break;
		case EFAULT:
		default:
			break;
		}
	}
	pr_fail_err("mremap");
	return -1;
}

static int stress_mremap_child(
	const args_t *args,
	const size_t sz,
	size_t new_sz,
	const size_t page_size,
	int *flags)
{
	do {
		uint8_t *buf = NULL;
		size_t old_sz;

		if (!g_keep_stressing_flag)
			break;

		buf = mmap(NULL, new_sz, PROT_READ | PROT_WRITE, *flags, -1, 0);
		if (buf == MAP_FAILED) {
			/* Force MAP_POPULATE off, just in case */
#if defined(MAP_POPULATE)
			*flags &= ~MAP_POPULATE;
#endif
			continue;	/* Try again */
		}
		(void)madvise_random(buf, new_sz);
		(void)mincore_touch_pages(buf, opt_mremap_bytes);

		/* Ensure we can write to the mapped pages */
		if (g_opt_flags & OPT_FLAGS_VERIFY) {
			mmap_set(buf, new_sz, page_size);
			if (mmap_check(buf, sz, page_size) < 0) {
				pr_fail("%s: mmap'd region of %zu "
					"bytes does not contain expected data\n",
					args->name, sz);
				munmap(buf, new_sz);
				return EXIT_FAILURE;
			}
		}

		old_sz = new_sz;
		new_sz >>= 1;
		while (new_sz > page_size) {
			if (try_remap(args, &buf, old_sz, new_sz) < 0) {
				munmap(buf, old_sz);
				return EXIT_FAILURE;
			}
			(void)madvise_random(buf, new_sz);
			if (g_opt_flags & OPT_FLAGS_VERIFY) {
				if (mmap_check(buf, new_sz, page_size) < 0) {
					pr_fail("%s: mremap'd region "
						"of %zu bytes does "
						"not contain expected data\n",
						args->name, sz);
					munmap(buf, new_sz);
					return EXIT_FAILURE;
				}
			}
			old_sz = new_sz;
			new_sz >>= 1;
		}

		new_sz <<= 1;
		while (new_sz < opt_mremap_bytes) {
			if (try_remap(args, &buf, old_sz, new_sz) < 0) {
				munmap(buf, old_sz);
				return EXIT_FAILURE;
			}
			(void)madvise_random(buf, new_sz);
			old_sz = new_sz;
			new_sz <<= 1;
		}
		(void)munmap(buf, old_sz);

		inc_counter(args);
	} while (keep_stressing());

	return EXIT_SUCCESS;
}

/*
 *  stress_mremap()
 *	stress mmap
 */
int stress_mremap(const args_t *args)
{
	const size_t page_size = args->page_size;
	size_t sz, new_sz;
	int rc = EXIT_SUCCESS, flags = MAP_PRIVATE | MAP_ANONYMOUS;
	pid_t pid;
	uint32_t ooms = 0, segvs = 0, buserrs = 0;

#if defined(MAP_POPULATE)
	flags |= MAP_POPULATE;
#endif
	if (!set_mremap_bytes) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			opt_mremap_bytes = MAX_MREMAP_BYTES;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			opt_mremap_bytes = MIN_MREMAP_BYTES;
	}
	new_sz = sz = opt_mremap_bytes & ~(page_size - 1);

	/* Make sure this is killable by OOM killer */
	set_oom_adjustment(args->name, true);

again:
	if (!g_keep_stressing_flag)
		return EXIT_SUCCESS;
	pid = fork();
	if (pid < 0) {
		if (errno == EAGAIN)
			goto again;
		pr_err("%s: fork failed: errno=%d: (%s)\n",
			args->name, errno, strerror(errno));
	} else if (pid > 0) {
		int status, ret;

		(void)setpgid(pid, g_pgrp);
		/* Parent, wait for child */
		ret = waitpid(pid, &status, 0);
		if (ret < 0) {
			if (errno != EINTR)
				pr_dbg("%s: waitpid(): errno=%d (%s)\n",
					args->name, errno, strerror(errno));
			(void)kill(pid, SIGTERM);
			(void)kill(pid, SIGKILL);
			(void)waitpid(pid, &status, 0);
		} else if (WIFSIGNALED(status)) {
			/* If we got killed by sigbus, re-start */
			if (WTERMSIG(status) == SIGBUS) {
				/* Happens frequently, so be silent */
				buserrs++;
				goto again;
			}

			pr_dbg("%s: child died: %s (instance %d)\n",
				args->name, stress_strsignal(WTERMSIG(status)),
				args->instance);
			/* If we got killed by OOM killer, re-start */
			if (WTERMSIG(status) == SIGKILL) {
				log_system_mem_info();
				pr_dbg("%s: assuming killed by OOM "
					"killer, restarting again "
					"(instance %d)\n",
					args->name, args->instance);
				ooms++;
				goto again;
			}
			/* If we got killed by sigsegv, re-start */
			if (WTERMSIG(status) == SIGSEGV) {
				pr_dbg("%s: killed by SIGSEGV, "
					"restarting again "
					"(instance %d)\n",
					args->name, args->instance);
				segvs++;
				goto again;
			}
		} else {
			rc = WEXITSTATUS(status);
		}
	} else if (pid == 0) {
		(void)setpgid(0, g_pgrp);
		stress_parent_died_alarm();

		/* Make sure this is killable by OOM killer */
		set_oom_adjustment(args->name, true);

		rc = stress_mremap_child(args, sz,
			new_sz, page_size, &flags);
		exit(rc);
	}

	if (ooms + segvs + buserrs > 0)
		pr_dbg("%s: OOM restarts: %" PRIu32
			", SEGV restarts: %" PRIu32
			", SIGBUS signals: %" PRIu32 "\n",
			args->name, ooms, segvs, buserrs);

	return rc;
}
#else
int stress_mremap(const args_t *args)
{
	return stress_not_implemented(args);
}
#endif
