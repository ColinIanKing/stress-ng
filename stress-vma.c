/*
 * Copyright (C) 2023      Colin Ian King.
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
 * Based on sample test code and ideas by Vegard Nossum <vegard.nossum@oracle.com>
 *
 */
#include "stress-ng.h"
#include "core-killpid.h"
#include "core-out-of-memory.h"

#define STRESS_VMA_PROCS	(2)
#define STRESS_VMA_PAGES	(16)

static const stress_help_t help[] = {
	{ NULL,	"vma N",	"start N workers that exercise kernel VMA structures" },
	{ NULL,	"vma-ops N",	"stop N workers after N mmap VMA operations" },
	{ NULL,	NULL,		NULL }
};

#if defined(HAVE_LIB_PTHREAD)

typedef struct {
	const stress_args_t *args;
	void *data;
	pid_t pid;
} stress_vma_context_t;

typedef void * (*stress_vma_func_t)(void *ptr);

typedef struct {
	stress_vma_func_t	vma_func;
	size_t 			count;
} stress_thread_info_t;

#define STRESS_VMA_MMAP		(0)
#define STRESS_VMA_MUNMAP	(1)
#define STRESS_VMA_MLOCK	(2)
#define STRESS_VMA_MUNLOCK	(3)
#define STRESS_VMA_MADVISE	(4)
#define STRESS_VMA_MINCORE	(5)
#define STRESS_VMA_MPROTECT	(6)
#define STRESS_VMA_MSYNC	(7)
#define STRESS_VMA_ACCESS	(8)
#define STRESS_VMA_PROC_MAPS	(9)
#define STRESS_VMA_SIGSEGV	(10)
#define STRESS_VMA_SIGBUS	(11)
#define STRESS_VMA_MAX		(12)

typedef struct {
	volatile uint64_t metrics[STRESS_VMA_MAX];	/* racy metrics */
} stress_vma_metrics_t;

static const char *stress_vma_metrics_name[] = {
	"mmaps",	/* STRESS_VMA_MMAP */
	"munmaps",	/* STRESS_VMA_MUNMAP */
	"mlocks",	/* STRESS_VMA_MLOCK */
	"munlocks",	/* STRESS_VMA_MUNLOCK */
	"madvices",	/* STRESS_VMA_MADVISE */
	"mincore",	/* STRESS_VMA_MINCORE */
	"mprotect",	/* STRESS_VMA_MPROTECT */
	"msync",	/* STRESS_VMA_MSYNC */
	"accesses",	/* STRESS_VMA_ACCESS */
	"proc-maps",	/* STRESS_VMA_PROC_MAPS */
	"SIGSEGVs",	/* STRESS_VMA_SIGSEGV */
	"SIGBUSes",	/* STRESS_VMA_SIGBUS */
};

static stress_vma_metrics_t *stress_vma_metrics;
static void *stress_vma_page;

static bool stress_vma_continue(const stress_args_t *args)
{
	if (UNLIKELY(!g_stress_continue_flag))
		return false;
	if (LIKELY(args->max_ops == 0))
		return true;
        return stress_vma_metrics->metrics[STRESS_VMA_MMAP] < args->max_ops;
}

/*
 *  stress_vma_get_addr()
 *	try to find an unmapp'd address
 */
static void *stress_mmapaddr_get_addr(const stress_args_t *args)
{
	const uintptr_t mask = ~(((uintptr_t)args->page_size) - 1);
	void *addr = NULL;
	uintptr_t ui_addr;

	while (stress_vma_continue(args)) {
		int fd[2], err;
		ssize_t ret;

		if (sizeof(uintptr_t) > 4) {
			uint64_t page_63 = (stress_mwc64() << 12) & 0x7fffffffffffffffULL;

			if (stress_mwc1()) {
				ui_addr = stress_mwc64modn((1ULL << 38) - 1) | page_63;
			} else {
				ui_addr = (1ULL << 36) | page_63;
			}
			/* occassionally use 32 bit addr in 64 bit addr space */
			if (stress_mwc8modn(5) == 0)
				ui_addr &= 0x7fffffffUL;
		} else {
			uint32_t page_31 = (stress_mwc32() << 12) & 0x7fffffffUL;

			if (stress_mwc1())
				ui_addr = stress_mwc32modn((1UL << 28) - 1) | page_31;
			else
				ui_addr = (1ULL << 20) | page_31;
		}
		addr = (void *)(ui_addr & mask);

		if (pipe(fd) < 0)
			return NULL;
		/* Can we read the page at addr into a pipe? */
		ret = write(fd[1], addr, args->page_size);
		err = errno;

		(void)close(fd[0]);
		(void)close(fd[1]);

		/* Not mapped or readable */
		if ((ret < 0) && (err == EFAULT))
			break;
	}
	return addr;
}

static void *stress_vma_mmap(void *ptr)
{
	stress_vma_context_t *ctxt = (stress_vma_context_t *)ptr;
	const stress_args_t *args = (const stress_args_t *)ctxt->args;
	const uintptr_t data = (uintptr_t)ctxt->data;
	const size_t page_size = args->page_size;

	while (stress_vma_continue(args)) {
		static const int prots[] = {
			PROT_NONE,
			PROT_READ,
			PROT_WRITE,
			PROT_READ | PROT_WRITE,
		};

		const size_t offset = page_size * stress_mwc8modn(STRESS_VMA_PAGES);
		const size_t size = page_size * stress_mwc8modn(STRESS_VMA_PAGES);
		const int prot = prots[stress_mwc8modn(SIZEOF_ARRAY(prots))];
		int flags = MAP_FIXED | MAP_ANONYMOUS;
		void *mapped;

		flags |= (stress_mwc1() ? MAP_SHARED : MAP_PRIVATE);
#if defined(MAP_GROWSDOWN)
		flags |= (stress_mwc1() ? MAP_GROWSDOWN : 0);
#endif

		/* Map and grow */
		errno = 0;
		mapped = mmap((void *)(data + offset), size, prot, flags, -1, 0);
		if (mapped != MAP_FAILED)
			stress_vma_metrics->metrics[STRESS_VMA_MMAP]++;
	}
	(void)kill(ctxt->pid, SIGALRM);
	return NULL;
}

static void *stress_vma_munmap(void *ptr)
{
	stress_vma_context_t *ctxt = (stress_vma_context_t *)ptr;
	const stress_args_t *args = (const stress_args_t *)ctxt->args;
	const uintptr_t data = (uintptr_t)ctxt->data;
	const size_t page_size = args->page_size;

	while (stress_vma_continue(args)) {
		const size_t offset = page_size * stress_mwc8modn(STRESS_VMA_PAGES);
		const size_t size = page_size * stress_mwc8modn(STRESS_VMA_PAGES);

		if (munmap((void *)(data + offset), size) == 0)
			stress_vma_metrics->metrics[STRESS_VMA_MUNMAP]++;
	}
	(void)kill(ctxt->pid, SIGALRM);
	return NULL;
}

static void *stress_vma_mlock(void *ptr)
{
	stress_vma_context_t *ctxt = (stress_vma_context_t *)ptr;
	const stress_args_t *args = (const stress_args_t *)ctxt->args;
	const uintptr_t data = (uintptr_t)ctxt->data;
	const size_t page_size = args->page_size;

	while (stress_vma_continue(args)) {
		const size_t offset = page_size * stress_mwc8modn(STRESS_VMA_PAGES);
		const size_t len = page_size * stress_mwc8modn(STRESS_VMA_PAGES);
#if defined(MLOCK_ONFAULT)
		const int flags = stress_mwc1() ? MLOCK_ONFAULT : 0;
#else
		const int flags = 0;
#endif

		if (shim_mlock2((void *)(data + offset), len, flags) == 0) {
			stress_vma_metrics->metrics[STRESS_VMA_MLOCK]++;
		} else {
			if (shim_mlock((void *)(data + offset), len) == 0)
				stress_vma_metrics->metrics[STRESS_VMA_MLOCK]++;
		}
	}
	(void)kill(ctxt->pid, SIGALRM);
	return NULL;
}

static void *stress_vma_munlock(void *ptr)
{
	stress_vma_context_t *ctxt = (stress_vma_context_t *)ptr;
	const stress_args_t *args = (const stress_args_t *)ctxt->args;
	const uintptr_t data = (uintptr_t)ctxt->data;
	const size_t page_size = args->page_size;

	while (stress_vma_continue(args)) {
		const size_t offset = page_size * stress_mwc8modn(STRESS_VMA_PAGES);
		const size_t len = page_size * stress_mwc8modn(STRESS_VMA_PAGES);

		if (munlock((void *)(data + offset), len) == 0)
			stress_vma_metrics->metrics[STRESS_VMA_MUNLOCK]++;
	}
	(void)kill(ctxt->pid, SIGALRM);
	return NULL;
}

static void *stress_vma_madvise(void *ptr)
{
	stress_vma_context_t *ctxt = (stress_vma_context_t *)ptr;
	const stress_args_t *args = (const stress_args_t *)ctxt->args;
	const uintptr_t data = (uintptr_t)ctxt->data;
	const size_t page_size = args->page_size;

	static const int advice[] = {
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
#if defined(MADV_MERGEABLE)
		MADV_MERGEABLE,
#endif
#if defined(MADV_UNMERGEABLE)
		MADV_UNMERGEABLE,
#endif
#if defined(MADV_DONTDUMP)
		MADV_DONTDUMP,
#endif
#if defined(MADV_DODUMP)
		MADV_DODUMP,
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
	};

	while (stress_vma_continue(args)) {
		const size_t i = stress_mwc8modn(SIZEOF_ARRAY(advice));
		const size_t offset = page_size * stress_mwc8modn(STRESS_VMA_PAGES);
		const size_t len = page_size * stress_mwc8modn(STRESS_VMA_PAGES);

		if (madvise((void *)(data + offset), len, advice[i]) == 0)
			stress_vma_metrics->metrics[STRESS_VMA_MADVISE]++;
	}
	(void)kill(ctxt->pid, SIGALRM);
	return NULL;
}

#if defined(HAVE_MINCORE)
static void *stress_vma_mincore(void *ptr)
{
	stress_vma_context_t *ctxt = (stress_vma_context_t *)ptr;
	const stress_args_t *args = (const stress_args_t *)ctxt->args;
	const uintptr_t data = (uintptr_t)ctxt->data;
	const size_t page_size = args->page_size;

	while (stress_vma_continue(args)) {
		const size_t offset = page_size * stress_mwc8modn(STRESS_VMA_PAGES);
		const size_t pages = stress_mwc8modn(STRESS_VMA_PAGES);
		const size_t len = page_size * pages;
		unsigned char vec[STRESS_VMA_PAGES];

		if (shim_mincore((void *)(data + offset), len, vec) == 0)
			stress_vma_metrics->metrics[STRESS_VMA_MINCORE]++;
	}
	(void)kill(ctxt->pid, SIGALRM);
	return NULL;
}
#endif

static void *stress_vma_mprotect(void *ptr)
{
	stress_vma_context_t *ctxt = (stress_vma_context_t *)ptr;
	const stress_args_t *args = (const stress_args_t *)ctxt->args;
	const uintptr_t data = (uintptr_t)ctxt->data;
	const size_t page_size = args->page_size;

	static const int prot[] = {
#if defined(PROT_NONE)
		PROT_NONE,
#endif
#if defined(PROT_READ)
		PROT_READ,
#endif
#if defined(PROT_WRITE)
		PROT_WRITE,
#endif
#if defined(PROT_READ) &&	\
    defined(PROT_WRITE)
		PROT_READ | PROT_WRITE,
#endif
	};

	while (stress_vma_continue(args)) {
		const size_t i = stress_mwc8modn(SIZEOF_ARRAY(prot));
		const size_t offset = page_size * stress_mwc8modn(STRESS_VMA_PAGES);
		const size_t len = page_size * stress_mwc8modn(STRESS_VMA_PAGES);

		if (mprotect((void *)(data + offset), len, prot[i]) == 0)
			stress_vma_metrics->metrics[STRESS_VMA_MPROTECT]++;
	}
	(void)kill(ctxt->pid, SIGALRM);
	return NULL;
}

static void *stress_vma_msync(void *ptr)
{
	stress_vma_context_t *ctxt = (stress_vma_context_t *)ptr;
	const stress_args_t *args = (const stress_args_t *)ctxt->args;
	const uintptr_t data = (uintptr_t)ctxt->data;
	const size_t page_size = args->page_size;

	static const int flags[] = {
#if defined(MS_ASYNC)
	MS_ASYNC,
#endif
#if defined(MS_SYNC)
	MS_SYNC,
#endif
#if defined(MS_INVALIDATE)
       MS_INVALIDATE,
#endif
	};

	while (stress_vma_continue(args)) {
		const size_t i = stress_mwc8modn(SIZEOF_ARRAY(flags));
		const size_t offset = page_size * stress_mwc8modn(STRESS_VMA_PAGES);
		const size_t len = page_size * stress_mwc8modn(STRESS_VMA_PAGES);

		if (msync((void *)(data + offset), len, flags[i]) == 0)
			stress_vma_metrics->metrics[STRESS_VMA_MSYNC]++;
	}
	(void)kill(ctxt->pid, SIGALRM);
	return NULL;
}

#if defined(__linux__)
static void *stress_vma_maps(void *ptr)
{
	stress_vma_context_t *ctxt = (stress_vma_context_t *)ptr;
	const stress_args_t *args = (const stress_args_t *)ctxt->args;
	int fd;

	fd = open("/proc/self/maps", O_RDONLY);
	if (fd != -1) {
		while (stress_vma_continue(args)) {
			char buf[4096];

			if (lseek(fd, 0, SEEK_SET) < 0) {
				pr_inf("lseek fail\n");
				break;
			}
			while (read(fd, buf, sizeof(buf)) > 1)
				;
		}
		stress_vma_metrics->metrics[STRESS_VMA_PROC_MAPS]++;
		(void)close(fd);
	}
	return NULL;
}
#endif

static void *stress_vma_access(void *ptr)
{
	stress_vma_context_t *ctxt = (stress_vma_context_t *)ptr;
	const stress_args_t *args = (const stress_args_t *)ctxt->args;
	const uintptr_t data = (uintptr_t)ctxt->data;
	const size_t page_size = args->page_size;

	while (stress_vma_continue(args)) {
		const size_t offset = page_size * stress_mwc8modn(STRESS_VMA_PAGES);
		uint8_t *ptr8 = (uint8_t *)(data + offset);

		stress_vma_metrics->metrics[STRESS_VMA_ACCESS]++;
		++(*ptr8);
	}
	(void)kill(ctxt->pid, SIGALRM);
	return NULL;
}

static const stress_thread_info_t vma_funcs[] = {
	{ stress_vma_mmap,	2 },
	{ stress_vma_munmap,	1 },
	{ stress_vma_mlock,	1 },
	{ stress_vma_munlock,	1 },
	{ stress_vma_madvise,	1 },
#if defined(HAVE_MINCORE)
	{ stress_vma_mincore,	1 },
#endif
	{ stress_vma_mprotect,	1 },
	{ stress_vma_msync,	1 },
#if defined(__linux__)
	{ stress_vma_maps,	1 },
#endif
	{ stress_vma_access,	20 }
};

static void stress_vm_handle_sig(int signo)
{
	if (stress_vma_metrics) {
		if (signo == SIGSEGV)
			stress_vma_metrics->metrics[STRESS_VMA_SIGSEGV]++;
		else if (signo == SIGBUS)
			stress_vma_metrics->metrics[STRESS_VMA_SIGBUS]++;
	}
}

static void stress_vma_loop(
	const stress_args_t *args,
	stress_vma_context_t *ctxt)
{
	size_t i, n;

	VOID_RET(int, stress_sighandler(args->name, SIGSEGV, stress_vm_handle_sig, NULL));
	VOID_RET(int, stress_sighandler(args->name, SIGBUS, stress_vm_handle_sig, NULL));

	ctxt->args = args;

	for (i = 0, n = 0; i < SIZEOF_ARRAY(vma_funcs); i++)
		n += vma_funcs[i].count;

	do {
		pid_t pid;

		stress_mwc_reseed();
		ctxt->data = stress_mmapaddr_get_addr(args);

		pid = fork();
		if (pid < 0) {
			shim_usleep_interruptible(100000);
			continue;
		} else if (pid == 0) {
			pthread_t pthreads[n];
			size_t j;

			stress_parent_died_alarm();
			(void)sched_settings_apply(true);

			for (i = 0, j = 0; stress_vma_continue(args) && (i < SIZEOF_ARRAY(vma_funcs)); i++) {
				size_t k;

				for (k = 0; stress_vma_continue(args) && (k < vma_funcs[i].count); k++, j++) {
					(void)pthread_create(&pthreads[j], NULL,
							vma_funcs[i].vma_func, (void *)ctxt);
				}
			}
			pause();
			_exit(0);
		}

		(void)sleep(15);
		stress_force_killed_bogo(args);
		stress_kill_pid(pid);
	} while (stress_vma_continue(args));
}

static int stress_vma_child(const stress_args_t *args, void *void_ctxt)
{
	size_t i;
	pid_t pids[STRESS_VMA_PROCS];
	stress_vma_context_t *ctxt = (stress_vma_context_t *)void_ctxt;

	ctxt->pid = getpid();

	for (i = 0; (stress_continue(args)) && (i < SIZEOF_ARRAY(pids)); i++) {
		pids[i] = fork();
		if (pids[i] < 0)
			continue;
		else if (pids[i] == 0) {
			stress_parent_died_alarm();
			(void)sched_settings_apply(true);

			stress_vma_loop(args, ctxt);
			_exit(0);
		}
	}

	do {
		sleep(1);
		stress_bogo_set(args, stress_vma_metrics->metrics[STRESS_VMA_MMAP]);
	} while (stress_continue(args));

	return stress_kill_and_wait_many(args, pids, i, SIGKILL, false);
}

/*
 *  stress_vma()
 *	stress vma operations
 */
static int stress_vma(const stress_args_t *args)
{
	int ret;
	size_t i;
	double t1, duration;
	stress_vma_context_t ctxt;

	stress_vma_page = mmap(NULL, sizeof(args->page_size), PROT_READ | PROT_WRITE,
				MAP_ANONYMOUS | MAP_SHARED, -1, 0);
	if (stress_vma_page == MAP_FAILED) {
		pr_inf_skip("%s: cannot mmap 1 page (%zd bytes) , errno=%d (%s), skipping stressor\n",
			args->name, args->page_size, errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}

	stress_vma_metrics = (stress_vma_metrics_t *)
		mmap(NULL, sizeof(*stress_vma_metrics), PROT_READ | PROT_WRITE,
			MAP_ANONYMOUS | MAP_SHARED, -1, 0);
	if (stress_vma_metrics == MAP_FAILED) {
		pr_inf_skip("%s: cannot mmap vma shared statistics data, errno=%d (%s), skipping stressor\n",
			args->name, errno, strerror(errno));
		(void)munmap(stress_vma_page, args->page_size);
		return EXIT_NO_RESOURCE;
	}

	stress_set_proc_state(args->name, STRESS_STATE_RUN);
	t1 = stress_time_now();
	ret = stress_oomable_child(args, &ctxt, stress_vma_child, STRESS_OOMABLE_NORMAL);
	duration = stress_time_now() - t1;
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	for (i = 0; i < SIZEOF_ARRAY(stress_vma_metrics->metrics); i++) {
		char msg[64];
		const double rate = duration > 0.0 ? (double)stress_vma_metrics->metrics[i] / duration : 0.0;

		(void)snprintf(msg, sizeof(msg), "%s per second", stress_vma_metrics_name[i]);
		stress_metrics_set(args, i, msg, rate);
	}

	(void)munmap((void *)stress_vma_metrics, sizeof(*stress_vma_metrics));
	(void)munmap(stress_vma_page, args->page_size);

	return ret;
}

stressor_info_t stress_vma_info = {
	.stressor = stress_vma,
	.class = CLASS_VM,
	.help = help
};
#else
stressor_info_t stress_vma_info = {
	.stressor = stress_unimplemented,
	.class = CLASS_VM,
	.help = help,
	.unimplemented_reason = "built without pthread support"
};
#endif
