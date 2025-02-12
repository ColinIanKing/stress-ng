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
#include "core-madvise.h"
#include "core-mincore.h"
#include "core-out-of-memory.h"
#include "core-pthread.h"

static const stress_help_t help[] = {
	{ NULL,	"madvise N",	 	"start N workers exercising madvise on memory" },
	{ NULL,	"madvise-ops N",	"stop after N bogo madvise operations" },
	{ NULL,	"madvise-hwpoison",	"enable hardware page poisoning (disabled by default)" },
	{ NULL,	NULL,			NULL }
};

static const stress_opt_t opts[] = {
	{ OPT_madvise_hwpoison,	"madvise-hwpoison", TYPE_ID_BOOL, 0, 1, NULL },
	END_OPT,
};

#if defined(HAVE_MADVISE)

#define NUM_MEM_RETRIES_MAX	(256)
#define NUM_HWPOISON_MAX	(2)
#define NUM_PTHREADS		(8)
#if defined(MADV_SOFT_OFFLINE)
#define NUM_SOFT_OFFLINE_MAX	(2)
#endif

typedef struct madvise_ctxt {
	stress_args_t *args;
	void *buf;
	char *smaps;
	size_t sz;
	bool  is_thread;
	bool  hwpoison;
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
/* Linux 6.0 */
#if defined(MADV_COLLAPSE)
	MADV_COLLAPSE,
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
/* Linux 6.12 */
#if defined(MADV_GUARD_INSTALL) &&	\
    defined(MADV_NORMAL)
	MADV_GUARD_INSTALL,
#endif
#if defined(MADV_GUARD_REMOVE)
	MADV_GUARD_REMOVE,
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
	ssize_t ret;
	char buffer[4096];
	int fd;

	if (ignore)
		return;

	fd = open(smaps, O_RDONLY);
	if (fd < 0) {
		ignore = true;
		return;
	}
	do {
		ret = read(fd, buffer, sizeof(buffer));
	} while (ret == (ssize_t)sizeof(buffer));
	(void)close(fd);
}
#endif

/*
 *  stress_random_advise()
 *	get a random advise option
 */
static int stress_random_advise(
	stress_args_t *args,
	void *addr,
	const size_t size,
	const bool hwpoison)
{
	const int idx = stress_mwc32modn((size_t)SIZEOF_ARRAY(madvise_options));
	const int advise = madvise_options[idx];
#if defined(MADV_HWPOISON) || defined(MADV_SOFT_OFFLINE)
	static int hwpoison_count = 0;
#if defined(MADV_NORMAL)
	const int madv_normal = MADV_NORMAL;
#else
	const int madv_normal = 0;
#endif
#endif

#if defined(MADV_HWPOISON)
	if (advise == MADV_HWPOISON) {
		if (hwpoison) {
			const size_t page_size = args->page_size;
			const size_t vec_size = (size + page_size - 1) / page_size;
			unsigned char *vec;
			const uint8_t *ptr = (uint8_t *)addr;

			/*
			 * Try for another madvise option if
			 * we've poisoned too many pages.
			 * We really need to use this sparingly
			 * else we run out of free memory
			 */
			if ((args->instance > 0) ||
			    (hwpoison_count >= NUM_HWPOISON_MAX)) {
				return madv_normal;
			}

			vec = (unsigned char *)calloc(vec_size, sizeof(*vec));
			if (LIKELY(vec != NULL)) {
				size_t i;
				int ret;

				/*
				 * Don't poison mapping if it's not physically backed
				 */
				ret = shim_mincore(addr, size, vec);
				if (ret < 0) {
					free(vec);
					return madv_normal;
				}
				for (i = 0; i < vec_size; i++) {
					if (vec[i] == 0) {
						free(vec);
						return madv_normal;
					}
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
				if (i == size) {
					free(vec);
					return madv_normal;
				}
				hwpoison_count++;
				free(vec);
			}
		} else {
			/* hwpoison disabled */
			return madv_normal;
		}
	}
#else
	UNEXPECTED
	(void)hwpoison;
	(void)args;
	(void)addr;
	(void)size;
#endif

#if defined(MADV_SOFT_OFFLINE)
	if (advise == MADV_SOFT_OFFLINE) {
		static int soft_offline_count;

		/* ..and minimize number of soft offline pages */
		if ((soft_offline_count >= NUM_SOFT_OFFLINE_MAX) ||
		    (hwpoison_count >= NUM_HWPOISON_MAX))
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
	stress_args_t *args = ctxt->args;
	void *buf = ctxt->buf;
	const size_t sz = ctxt->sz;
	const size_t page_size = args->page_size;

	if (ctxt->is_thread) {
		sigset_t set;

		(void)sigemptyset(&set);
		(void)sigaddset(&set, SIGBUS);
		(void)pthread_sigmask(SIG_SETMASK, &set, NULL);

		stress_random_small_sleep();
	}

	for (n = 0; n < sz; n += page_size) {
		void *ptr = (void *)(((uint8_t *)buf) + n);
		const int advise = stress_random_advise(args, ptr, page_size, ctxt->hwpoison);

		(void)shim_madvise(ptr, page_size, advise);
#if defined(MADV_FREE)
		if (advise == MADV_FREE)
			stress_read_proc_smaps(ctxt->smaps);
#endif
#if defined(MADV_GUARD_INSTALL) && defined(MADV_NORMAL)
		/* avoid segfaults by setting back to normal */
		if (advise == MADV_GUARD_INSTALL)
			(void)shim_madvise(ptr, page_size, MADV_NORMAL);
#endif
		(void)shim_msync(ptr, page_size, MS_ASYNC);
	}
	for (n = 0; n < sz; n += page_size) {
		const size_t m = (size_t)(stress_mwc64modn((uint64_t)sz) & ~(page_size - 1));
		void *ptr = (void *)(((uint8_t *)buf) + m);
		const int advise = stress_random_advise(args, ptr, page_size, ctxt->hwpoison);

		(void)shim_madvise(ptr, page_size, advise);
#if defined(MADV_GUARD_INSTALL) && defined(MADV_NORMAL)
		/* avoid segfaults by setting back to normal */
		if (advise == MADV_GUARD_INSTALL)
			(void)shim_madvise(ptr, page_size, MADV_NORMAL);
#endif
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
			(void)stress_munmap_retry_enomem(unmapped, page_size);
			(void)shim_madvise(unmapped, page_size, MADV_NORMAL);
		}
	}
#endif

	return &g_nowt;
}

static void stress_process_madvise(const pid_t pid, void *buf, const size_t sz)
{
	int pidfd;
	struct iovec vec;

	(void)pid;

	vec.iov_base = buf;
	vec.iov_len = sz;

	pidfd = shim_pidfd_open(pid, 0);
	if (pidfd >= 0) {
#if defined(MADV_PAGEOUT)
		VOID_RET(ssize_t, shim_process_madvise(pidfd, &vec, 1, MADV_PAGEOUT, 0));
#endif
#if defined(MADV_COLD)
		VOID_RET(ssize_t, shim_process_madvise(pidfd, &vec, 1, MADV_COLD, 0));
#endif

		/* exercise invalid behaviour */
		VOID_RET(ssize_t, shim_process_madvise(pidfd, &vec, 1, ~0, 0));

#if defined(MADV_PAGEOUT)
		/* exercise invalid flags */
		VOID_RET(ssize_t, shim_process_madvise(pidfd, &vec, 1, MADV_PAGEOUT, ~0U));
#endif

		(void)close(pidfd);
	}

#if defined(MADV_PAGEOUT)
	/* exercise invalid pidfd */
	VOID_RET(ssize_t, shim_process_madvise(-1, &vec, 1, MADV_PAGEOUT, 0));
#endif
}

/*
 *  stress_madvise()
 *	stress madvise
 */
static int stress_madvise(stress_args_t *args)
{
	const size_t page_size = args->page_size;
	const size_t sz = (4 *  MB) & ~(page_size - 1);
	const pid_t pid = getpid();
	int fd = -1;
	NOCLOBBER size_t advice = 0;
	NOCLOBBER int ret;
	NOCLOBBER int num_mem_retries;
	char filename[PATH_MAX];
	char smaps[PATH_MAX];
	char *page;
	size_t n;
	madvise_ctxt_t ctxt;
#if defined(MADV_FREE)
	NOCLOBBER uint64_t madv_frees_raced;
	NOCLOBBER uint64_t madv_frees;
	NOCLOBBER uint8_t madv_tries;
#endif

	(void)shim_memset(&ctxt, 0, sizeof(ctxt));
	(void)stress_get_setting("madvise-hwpoison", &ctxt.hwpoison);

	num_mem_retries = 0;
#if defined(MADV_FREE)
	madv_frees_raced = 0;
	madv_frees = 0;
	madv_tries = 0;
#endif

	page = (char *)stress_mmap_populate(NULL, page_size, PROT_READ | PROT_WRITE,
			MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (page == MAP_FAILED) {
		pr_inf_skip("%s: cannot allocate %zd byte page, skipping stressor\n",
			args->name, page_size);
		return EXIT_NO_RESOURCE;
	}
	stress_set_vma_anon_name(page, page_size, "data-page");

	(void)snprintf(smaps, sizeof(smaps), "/proc/%" PRIdMAX "/smaps", (intmax_t)pid);

	ret = sigsetjmp(jmp_env, 1);
	if (ret) {
		pr_fail("%s: sigsetjmp failed\n", args->name);
		(void)munmap((void *)page, page_size);
		return EXIT_NO_RESOURCE;
	}

	if (stress_sighandler(args->name, SIGBUS, stress_sigbus_handler, NULL) < 0) {
		(void)munmap((void *)page, page_size);
		return EXIT_FAILURE;
	}

	/* Make sure this is killable by OOM killer */
	stress_set_oom_adjustment(args, true);

	(void)shim_memset(page, 0xa5, page_size);

	ret = stress_temp_dir_mk_args(args);
	if (ret < 0) {
		(void)munmap((void *)page, page_size);
		return stress_exit_status(-ret);
	}

	(void)stress_temp_filename_args(args,
		filename, sizeof(filename), stress_mwc32());

	if ((fd = open(filename, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR)) < 0) {
		ret = stress_exit_status(errno);
		pr_fail("%s: open %s failed, errno=%d (%s)\n",
			args->name, filename, errno, strerror(errno));
		(void)shim_unlink(filename);
		(void)stress_temp_dir_rm_args(args);
		(void)munmap((void *)page, page_size);
		return ret;
	}

	(void)shim_unlink(filename);

	stress_file_rw_hint_short(fd);

	for (n = 0; n < sz; n += page_size) {
		VOID_RET(ssize_t, write(fd, page, page_size));
	}

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		NOCLOBBER uint8_t *buf;
		NOCLOBBER bool file_mapped;

		if (UNLIKELY(num_mem_retries >= NUM_MEM_RETRIES_MAX)) {
			pr_err("%s: gave up trying to mmap, no available memory\n",
				args->name);
			break;
		}

		if (UNLIKELY(!stress_continue_flag()))
			break;

		file_mapped = stress_mwc1();
		if (file_mapped) {
			buf = (uint8_t *)stress_mmap_populate(NULL, sz, PROT_READ | PROT_WRITE,
								MAP_PRIVATE, fd, 0);
		} else {
			buf = (uint8_t *)stress_mmap_populate(NULL, sz, PROT_READ | PROT_WRITE,
								MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
		}
		if (buf == MAP_FAILED) {
			/* Force MAP_POPULATE off, just in case */
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

		(void)shim_memset(buf, 0xff, sz);
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
				register uint8_t v = (uint8_t)(val + n);

				buf[n] = v;
			}
			if (madvise((void *)buf, sz, MADV_FREE) != 0)
				goto madv_free_out;
			if (lseek(fd, 0, SEEK_SET) != 0)
				goto madv_free_out;
			if (read(fd, buf, sz) != (ssize_t)sz)
				goto madv_free_out;

			for (n = 0; n < sz; n += page_size) {
				register uint8_t v = (uint8_t)(val + n);

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

		/*
		 * Some systems allow zero sized page zero madvise
		 * to see if that madvice is implemented, so try this
		 */
		(void)madvise(0, 0, madvise_options[advice]);
		advice++;
		advice = (advice >= SIZEOF_ARRAY(madvise_options)) ? 0: advice;

		stress_bogo_inc(args);
	} while (stress_continue(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	(void)close(fd);
	(void)stress_temp_dir_rm_args(args);
	(void)munmap((void *)page, page_size);

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

const stressor_info_t stress_madvise_info = {
	.stressor = stress_madvise,
	.class = CLASS_VM | CLASS_OS,
	.opts = opts,
	.help = help
};
#else
const stressor_info_t stress_madvise_info = {
	.stressor = stress_unimplemented,
	.class = CLASS_VM | CLASS_OS,
	.opts = opts,
	.help = help
};
#endif
