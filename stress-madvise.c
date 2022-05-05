/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C)      2022 Colin Ian King.
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

static const stress_help_t help[] = {
	{ NULL,	"madvise N",	 "start N workers exercising madvise on memory" },
	{ NULL,	"madvise-ops N", "stop after N bogo madvise operations" },
	{ NULL,	NULL,		 NULL }
};

#if defined(HAVE_MADVISE)

#define NUM_MEM_RETRIES_MAX	(256)
#define NUM_POISON_MAX		(2)
#define NUM_PTHREADS		(8)
#if defined(MADV_SOFT_OFFLINE)
#define NUM_SOFT_OFFLINE_MAX	(2)
#endif

typedef struct madvise_ctxt {
	const stress_args_t *args;
	void *buf;
	size_t sz;
	bool  is_thread;
	char *smaps;
} madvise_ctxt_t;

static sigjmp_buf jmp_env;
static uint64_t sigbus_count;

static const int madvise_options[] = {
#if defined(MADV_NORMAL)
	MADV_NORMAL,
#endif
#if defined(MADV_RANDOM)
	MADV_RANDOM,
#endif
#if defined(MADV_SEQUENTIAL)
	MADV_SEQUENTIAL,
#endif
#if defined(MADV_WILLNEED)
	MADV_WILLNEED,
#endif
#if defined(MADV_DONTNEED)
	MADV_DONTNEED,
#endif
#if defined(MADV_REMOVE)
	MADV_REMOVE,
#endif
#if defined(MADV_DONTFORK)
	MADV_DONTFORK,
#endif
#if defined(MADV_DOFORK)
	MADV_DOFORK,
#endif
#if defined(MADV_MERGEABLE)
	MADV_MERGEABLE,
#endif
#if defined(MADV_UNMERGEABLE)
	MADV_UNMERGEABLE,
#endif
#if defined(MADV_SOFT_OFFLINE)
	MADV_SOFT_OFFLINE,
#endif
#if defined(MADV_HUGEPAGE)
	MADV_HUGEPAGE,
#endif
#if defined(MADV_NOHUGEPAGE)
	MADV_NOHUGEPAGE,
#endif
#if defined(MADV_DONTDUMP)
	MADV_DONTDUMP,
#endif
#if defined(MADV_DODUMP)
	MADV_DODUMP,
#endif
#if defined(MADV_FREE)
	MADV_FREE,
#endif
#if defined(MADV_HWPOISON)
	MADV_HWPOISON,
#endif
#if defined(MADV_WIPEONFORK)
	MADV_WIPEONFORK,
#endif
#if defined(MADV_KEEPONFORK)
	MADV_KEEPONFORK,
#endif
#if defined(MADV_INHERIT_ZERO)
	MADV_INHERIT_ZERO,
#endif
#if defined(MADV_COLD)
	MADV_COLD,
#endif
#if defined(MADV_PAGEOUT)
	MADV_PAGEOUT,
#endif
#if defined(MADV_POPULATE_READ)
	MADV_POPULATE_READ,
#endif
#if defined(MADV_POPULATE_WRITE)
	MADV_POPULATE_WRITE,
#endif
#if defined(MADV_DONTNEED_LOCKED)
	MADV_DONTNEED_LOCKED,
#endif
/* FreeBSD */
#if defined(MADV_AUTOSYNC)
	MADV_AUTOSYNC,
#endif
/* FreeBSD and DragonFlyBSD */
#if defined(MADV_CORE)
	MADV_CORE,
#endif
/* FreeBSD */
#if defined(MADV_PROTECT)
	MADV_PROTECT,
#endif
/* Linux 5.14 */
#if defined(MADV_POPULATE_READ)
	MADV_POPULATE_READ,
#endif
/* Linux 5.14 */
#if defined(MADV_POPULATE_WRITE)
	MADV_POPULATE_WRITE,
#endif
/* OpenBSD */
#if defined(MADV_SPACEAVAIL)
	MADV_SPACEAVAIL,
#endif
/* OS X */
#if defined(MADV_ZERO_WIRED_PAGES)
	MADV_ZERO_WIRED_PAGES,
#endif
/* Solaris */
#if defined(MADV_ACCESS_DEFAULT)
	MADV_ACCESS_DEFAULT,
#endif
/* Solaris */
#if defined(MADV_ACCESS_LWP)
	MADV_ACCESS_LWP,
#endif
/* Solaris */
#if defined(MADV_ACCESS_MANY)
	MADV_ACCESS_MANY,
#endif
/* DragonFlyBSD */
#if defined(MADV_INVAL)
	MADV_INVAL,
#endif
/* DragonFlyBSD */
#if defined(MADV_NOCORE)
	MADV_NOCORE,
#endif
};

/*
 *  stress_sigbus_handler()
 *     SIGBUS handler
 */
static void NORETURN MLOCKED_TEXT stress_sigbus_handler(int signum)
{
	(void)signum;

	sigbus_count++;

	siglongjmp(jmp_env, 1);
}

#if defined(MADV_FREE)
/*
 *  stress_read_proc_smaps()
 *	read smaps file for extra kernel exercising
 */
static void stress_read_proc_smaps(const char *smaps)
{
	static bool ignore = false;
	const size_t sz = 4096;
	ssize_t ret;
	char buffer[sz];
	int fd;

	if (ignore)
		return;

	fd = open(smaps, O_RDONLY);
	if (fd < 0) {
		ignore = true;
		return;
	}
	do {
		ret = read(fd, buffer, sz);
	} while (ret == (ssize_t)sz);
	(void)close(fd);
}
#endif

/*
 *  stress_random_advise()
 *	get a random advise option
 */
static int stress_random_advise(
	const stress_args_t *args,
	void *addr,
	const size_t size)
{
	const int idx = stress_mwc32() % SIZEOF_ARRAY(madvise_options);	/* cppcheck-suppress moduloofone */
	const int advise = madvise_options[idx];
#if defined(MADV_HWPOISON) || defined(MADV_SOFT_OFFLINE)
	static int poison_count;
#if defined(MADV_NORMAL)
	const int madv_normal = MADV_NORMAL;
#else
	const int madv_normal = 0;
#endif
#endif

#if defined(MADV_HWPOISON)
	if (advise == MADV_HWPOISON) {
		const size_t page_size = args->page_size;
		const size_t vec_size = (size + page_size - 1) / page_size;
		size_t i;
		unsigned char vec[vec_size];
		int ret;
		uint8_t *ptr = (uint8_t *)addr;

		/*
		 * Try for another madvise option if
		 * we've poisoned too many pages.
		 * We really need to use this sparingly
		 * else we run out of free memory
		 */
		if ((args->instance > 0) ||
		    (poison_count >= NUM_POISON_MAX))
			return madv_normal;

		/*
		 * Don't poison page if it's not physically backed
		 */
		(void)memset(vec, 0, vec_size);
		ret = shim_mincore(addr, size, vec);
		if (ret < 0)
			return madv_normal;
		for (i = 0; i < vec_size; i++) {
			if (vec[i] == 0)
				return madv_normal;
		}
		/*
		 * Don't poison page if it's all zero as it may
		 * be mapped to the common zero page and poisoning
		 * this shared page can cause issues.
		 */
		for (i = 0; i < size; i++) {
			if (ptr[i])
				break;
		}
		/* ..all zero? then don't madvise it */
		if (i == size)
			return madv_normal;
		poison_count++;
	}
#else
	UNEXPECTED
	(void)args;
	(void)addr;
	(void)size;
#endif

#if defined(MADV_SOFT_OFFLINE)
	if (advise == MADV_SOFT_OFFLINE) {
		static int soft_offline_count;

		/* ..and minimize number of soft offline pages */
		if ((soft_offline_count >= NUM_SOFT_OFFLINE_MAX) ||
		    (poison_count >= NUM_POISON_MAX))
			return madv_normal;
		soft_offline_count++;
	}
#endif
	return advise;
}

/*
 *  stress_madvise_pages()
 *	exercise madvise settings
 */
static void *stress_madvise_pages(void *arg)
{
	size_t n;
	const madvise_ctxt_t *ctxt = (const madvise_ctxt_t *)arg;
	const stress_args_t *args = ctxt->args;
	void *buf = ctxt->buf;
	const size_t sz = ctxt->sz;
	const size_t page_size = args->page_size;
	static void *nowt = NULL;

	if (ctxt->is_thread) {
		sigset_t set;

		sigemptyset(&set);
		sigaddset(&set, SIGBUS);

		(void)pthread_sigmask(SIG_SETMASK, &set, NULL);
	}

	for (n = 0; n < sz; n += page_size) {
		void *ptr = (void *)(((uint8_t *)buf) + n);
		const int advise = stress_random_advise(args, ptr, page_size);

		(void)shim_madvise(ptr, page_size, advise);
#if defined(MADV_FREE)
		if (advise == MADV_FREE)
			stress_read_proc_smaps(ctxt->smaps);
#endif
		(void)shim_msync(ptr, page_size, MS_ASYNC);
	}
	for (n = 0; n < sz; n += page_size) {
		size_t m = (stress_mwc64() % sz) & ~(page_size - 1);
		void *ptr = (void *)(((uint8_t *)buf) + m);
		const int advise = stress_random_advise(args, ptr, page_size);

		(void)shim_madvise(ptr, page_size, advise);
		(void)shim_msync(ptr, page_size, MS_ASYNC);
	}

	/*
	 *  Exercise a highly likely bad advice option
	 */
	(void)shim_madvise(buf, page_size, ~0);

#if defined(MADV_NORMAL)
	/*
	 *  Exercise with non-page aligned address
	 */
	(void)shim_madvise(((uint8_t *)buf) + 1, page_size, MADV_NORMAL);
#endif
#if defined(_POSIX_MEMLOCK_RANGE) &&	\
    defined(HAVE_MLOCK) &&		\
    (defined(MADV_REMOVE) || defined(MADV_DONTNEED))
	{
		int ret;

		/*
		 *  Exercise MADV_REMOVE on locked page, should
		 *  generate EINVAL
		 */
		ret = shim_mlock(buf, page_size);
		if (ret == 0) {
#if defined(MADV_REMOVE)
			(void)shim_madvise(buf, page_size, MADV_REMOVE);
#endif
#if defined(MADV_DONTNEED)
			(void)shim_madvise(buf, page_size, MADV_DONTNEED);
#endif
			shim_munlock(buf, page_size);
		}
	}
#endif

#if defined(MADV_NORMAL)
	{
		void *unmapped;

		/*
		 *  Exercise an unmapped page
		 */
		unmapped = mmap(NULL, page_size, PROT_READ | PROT_WRITE,
				MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
		if (unmapped != MAP_FAILED) {
			(void)munmap(unmapped, page_size);
			(void)shim_madvise(unmapped, page_size, MADV_NORMAL);
		}
	}
#endif

	return &nowt;
}

static void stress_process_madvise(const pid_t pid, void *buf, const size_t sz)
{
	int pidfd, ret;
	struct iovec vec;

	(void)pid;

	vec.iov_base = buf;
	vec.iov_len = sz;

	pidfd = shim_pidfd_open(pid, 0);
	if (pidfd >= 0) {
#if defined(MADV_PAGEOUT)
		ret = shim_process_madvise(pidfd, &vec, 1, MADV_PAGEOUT, 0);
		(void)ret;
#endif
#if defined(MADV_COLD)
		ret = shim_process_madvise(pidfd, &vec, 1, MADV_COLD, 0);
		(void)ret;
#endif

		/* exercise invalid behaviour */
		ret = shim_process_madvise(pidfd, &vec, 1, ~0, 0);
		(void)ret;

#if defined(MADV_PAGEOUT)
		/* exercise invalid flags */
		ret = shim_process_madvise(pidfd, &vec, 1, MADV_PAGEOUT, ~0);
		(void)ret;
#endif

		(void)close(pidfd);
	}

#if defined(MADV_PAGEOUT)
	/* exercise invalid pidfd */
	ret = shim_process_madvise(-1, &vec, 1, MADV_PAGEOUT, 0);
#endif
	(void)ret;
}

/*
 *  stress_madvise()
 *	stress madvise
 */
static int stress_madvise(const stress_args_t *args)
{
	const size_t page_size = args->page_size;
	const size_t sz = (4 *  MB) & ~(page_size - 1);
	const pid_t pid = getpid();
	int fd = -1;
	NOCLOBBER int ret;
	NOCLOBBER int flags;
	NOCLOBBER int num_mem_retries;
	char filename[PATH_MAX];
	char page[page_size];
	char smaps[PATH_MAX];
	size_t n;
	madvise_ctxt_t ctxt;
#if defined(MADV_FREE)
	NOCLOBBER uint64_t madv_frees_raced;
	NOCLOBBER uint64_t madv_frees;
	NOCLOBBER uint8_t madv_tries;
#endif

	flags = MAP_PRIVATE;
	num_mem_retries = 0;
#if defined(MADV_FREE)
	madv_frees_raced = 0;
	madv_frees = 0;
	madv_tries = 0;
#endif

	(void)snprintf(smaps, sizeof(smaps), "/proc/%" PRIdMAX "/smaps", (intmax_t)pid);

	ret = sigsetjmp(jmp_env, 1);
	if (ret) {
		pr_fail("%s: sigsetjmp failed\n", args->name);
		return EXIT_FAILURE;
	}

	if (stress_sighandler(args->name, SIGBUS, stress_sigbus_handler, NULL) < 0)
		return EXIT_FAILURE;

#if defined(MAP_POPULATE)
	flags |= MAP_POPULATE;
#endif

	/* Make sure this is killable by OOM killer */
	stress_set_oom_adjustment(args->name, true);

	(void)memset(page, 0xa5, page_size);

	ret = stress_temp_dir_mk_args(args);
	if (ret < 0)
		return exit_status(-ret);

	(void)stress_temp_filename_args(args,
		filename, sizeof(filename), stress_mwc32());

	if ((fd = open(filename, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR)) < 0) {
		ret = exit_status(errno);
		pr_fail("%s: open %s failed, errno=%d (%s)\n",
			args->name, filename, errno, strerror(errno));
		(void)shim_unlink(filename);
		(void)stress_temp_dir_rm_args(args);
		return ret;
	}

	(void)shim_unlink(filename);
	for (n = 0; n < sz; n += page_size) {
		ssize_t wret;

		wret = write(fd, page, sizeof(page));
		(void)wret;
	}

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		NOCLOBBER uint8_t *buf;
		bool file_mapped;

		if (num_mem_retries >= NUM_MEM_RETRIES_MAX) {
			pr_err("%s: gave up trying to mmap, no available memory\n",
				args->name);
			break;
		}

		if (!keep_stressing_flag())
			break;

		file_mapped = stress_mwc1();
		if (file_mapped) {
			buf = (uint8_t *)mmap(NULL, sz, PROT_READ | PROT_WRITE, flags, fd, 0);
		} else {
			buf = (uint8_t *)mmap(NULL, sz, PROT_READ | PROT_WRITE,
				flags | MAP_ANONYMOUS, 0, 0);
		}
		if (buf == MAP_FAILED) {
			/* Force MAP_POPULATE off, just in case */
#if defined(MAP_POPULATE)
			flags &= ~MAP_POPULATE;
#endif
			num_mem_retries++;
			if (num_mem_retries > 1)
				(void)shim_usleep(100000);
			continue;	/* Try again */
		}
		ret = sigsetjmp(jmp_env, 1);
		if (ret) {
			(void)munmap((void *)buf, sz);
			/* Try again */
			continue;
		}

		(void)memset(buf, 0xff, sz);
		(void)stress_madvise_random(buf, sz);
		(void)stress_mincore_touch_pages(buf, sz);
		stress_process_madvise(pid, buf, sz);

		ctxt.args = args;
		ctxt.buf = buf;
		ctxt.sz = sz;
		ctxt.smaps = smaps;

#if defined(HAVE_LIB_PTHREAD)
		{
			pthread_t pthreads[NUM_PTHREADS];
			int rets[NUM_PTHREADS];
			size_t i;

			ctxt.is_thread = true;

			for (i = 0; i < NUM_PTHREADS; i++) {
				rets[i] = pthread_create(&pthreads[i], NULL,
						stress_madvise_pages, (void *)&ctxt);
			}
			for (i = 0; i < NUM_PTHREADS; i++) {
				if (rets[i] == 0)
					(void)pthread_join(pthreads[i], NULL);
			}
		}
#else
		{
			ctxt.is_thread = false;
			stress_madvise_pages(&ctxt);
		}
#endif

#if defined(MADV_NORMAL)
		/* Exercise no-op madvise on 0 size */
		(void)madvise((void *)buf, 0, MADV_NORMAL);

		/* Invalid size, ENOMEM */
		(void)madvise((void *)buf, 0xffff0000, MADV_NORMAL);

		/* Invalid advice option, EINVAL */
		(void)madvise((void *)buf, sz, ~0);

#endif

#if defined(MADV_FREE)
		if (file_mapped) {
			register uint8_t val;

			madv_tries++;
			if (madv_tries < 16)
				goto madv_free_out;

			madv_tries = 0;
			val = stress_mwc8();

			for (n = 0; n < sz; n += page_size) {
				register uint8_t v = val + n;

				buf[n] = v;
			}
			if (madvise((void *)buf, sz, MADV_FREE) != 0)
				goto madv_free_out;
			if (lseek(fd, 0, SEEK_SET) != 0)
				goto madv_free_out;
			if (read(fd, buf, sz) != (ssize_t)sz)
				goto madv_free_out;

			for (n = 0; n < sz; n += page_size) {
				register uint8_t v = val + n;

				if (buf[n] != v)
					madv_frees_raced++;
			}
			madv_frees += sz / page_size;
		}
madv_free_out:
#endif
		(void)munmap((void *)buf, sz);


#if defined(MADV_NORMAL)
		{
			void *bad_addr = (void *)(~(uintptr_t)0 & ~(page_size -1));

			/* Invalid madvise on unmapped pages */
			(void)madvise((void *)buf, sz, MADV_NORMAL);

			/* Invalid madvise on wrapped address */
			(void)madvise(bad_addr, page_size * 2, MADV_NORMAL);
		}
#endif


		inc_counter(args);
	} while (keep_stressing(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	(void)close(fd);
	(void)stress_temp_dir_rm_args(args);

#if defined(MADV_FREE)
	if (madv_frees_raced)
		pr_inf("%s: MADV_FREE: %" PRIu64" of %" PRIu64 " were racy\n",
			args->name, madv_frees_raced, madv_frees);
#endif

	if (sigbus_count)
		pr_inf("%s: caught %" PRIu64 " SIGBUS signal%s\n",
			args->name, sigbus_count, sigbus_count == 1 ? "" : "s");
	return EXIT_SUCCESS;
}

stressor_info_t stress_madvise_info = {
	.stressor = stress_madvise,
	.class = CLASS_VM | CLASS_OS,
	.help = help
};
#else
stressor_info_t stress_madvise_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_VM | CLASS_OS,
	.help = help
};
#endif
