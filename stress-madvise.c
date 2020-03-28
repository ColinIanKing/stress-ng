/*
 * Copyright (C) 2013-2020 Canonical, Ltd.
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
	{ NULL,	"madvise N",	 "start N workers exercising madvise on memory" },
	{ NULL,	"madvise-ops N", "stop after N bogo madvise operations" },
	{ NULL,	NULL,		 NULL }
};

#if defined(HAVE_MADVISE)

#define NUM_MEM_RETRIES_MAX	(256)
#define NUM_POISON_MAX		(2)
#define NUM_SOFT_OFFLINE_MAX	(2)
#define NUM_PTHREADS		(8)

typedef struct madvise_ctxt {
	const stress_args_t *args;
	void *buf;
	size_t sz;
	bool  is_thread;
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
/* FreeBSD */
#if defined(MADV_AUTOSYNC)
	MADV_AUTOSYNC,
#endif
/* FreeBSD */
#if defined(MADV_CORE)
	MADV_CORE,
#endif
/* FreeBSD */
#if defined(MADV_PROTECT)
	MADV_PROTECT,
#endif
};

/*
 *  stress_sigbus_handler()
 *     SIGBUS handler
 */
static void MLOCKED_TEXT stress_sigbus_handler(int signum)
{
	(void)signum;

	sigbus_count++;

	siglongjmp(jmp_env, 1);
}

/*
 *  stress_random_advise()
 *	get a random advise option
 */
static int stress_random_advise(const stress_args_t *args)
{
	const int idx = stress_mwc32() % SIZEOF_ARRAY(madvise_options);
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
		/*
		 * Try for another madvise option if
		 * we've poisoned too many pages.
		 * We really need to use this sparingly
		 * else we run out of free memory
		 */
		if ((args->instance > 0) ||
		    (poison_count >= NUM_POISON_MAX))
			return madv_normal;
		poison_count++;
	}
#else
	(void)args;
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
		const int advise = stress_random_advise(args);
		void *ptr = (void *)(((uint8_t *)buf) + n);

		(void)shim_madvise(ptr, page_size, advise);
		(void)shim_msync(ptr, page_size, MS_ASYNC);
	}
	for (n = 0; n < sz; n += page_size) {
		size_t m = (stress_mwc64() % sz) & ~(page_size - 1);
		const int advise = stress_random_advise(args);
		void *ptr = (void *)(((uint8_t *)buf) + m);

		(void)shim_madvise(ptr, page_size, advise);
		(void)shim_msync(ptr, page_size, MS_ASYNC);
	}

	return &nowt;
}

/*
 *  stress_madvise()
 *	stress madvise
 */
static int stress_madvise(const stress_args_t *args)
{
	const size_t page_size = args->page_size;
	size_t sz = 4 *  MB;
	int fd = -1;
	int ret;
	NOCLOBBER int flags = MAP_PRIVATE;
	NOCLOBBER int num_mem_retries = 0;
	char filename[PATH_MAX];
	char page[page_size];
	size_t n;
	madvise_ctxt_t ctxt;

	ret = sigsetjmp(jmp_env, 1);
	if (ret) {
		pr_fail_err("sigsetjmp");
		return EXIT_FAILURE;
       }

	if (stress_sighandler(args->name, SIGBUS, stress_sigbus_handler, NULL) < 0)
		return EXIT_FAILURE;

#if defined(MAP_POPULATE)
	flags |= MAP_POPULATE;
#endif
	sz &= ~(page_size - 1);

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
		pr_fail_err("open");
		(void)unlink(filename);
		(void)stress_temp_dir_rm_args(args);
		return ret;
	}

	(void)unlink(filename);
	for (n = 0; n < sz; n += page_size) {
		ret = write(fd, page, sizeof(page));
		(void)ret;
	}

	do {
		NOCLOBBER void *buf;

		if (num_mem_retries >= NUM_MEM_RETRIES_MAX) {
			pr_err("%s: gave up trying to mmap, no available memory\n",
				args->name);
			break;
		}

		if (!keep_stressing_flag())
			break;

		if (stress_mwc1()) {
			buf = mmap(NULL, sz, PROT_READ | PROT_WRITE, flags, fd, 0);
		} else {
			buf = mmap(NULL, sz, PROT_READ | PROT_WRITE,
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

		ctxt.args = args;
		ctxt.buf = buf;
		ctxt.sz = sz;

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
					pthread_join(pthreads[i], NULL);
			}
		}
#else
		{
			ctxt.is_thread = false;
			stress_madvise_pages(&ctxt);
		}
#endif

		(void)munmap((void *)buf, sz);
		inc_counter(args);
	} while (keep_stressing());

	(void)close(fd);
	(void)stress_temp_dir_rm_args(args);

	if (sigbus_count)
		pr_inf("%s: caught %" PRIu64 " SIGBUS signals\n",
			args->name, sigbus_count);
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
