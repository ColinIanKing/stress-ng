// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
 *
 */
#include "stress-ng.h"
#include "core-builtin.h"
#include "core-out-of-memory.h"

#if defined(HAVE_MALLOC_H)
#include <malloc.h>
#endif

#define MIN_BIGHEAP_BYTES	(64 * KB)
#define MAX_BIGHEAP_BYTES	(MAX_MEM_LIMIT)
#define DEFAULT_BIGHEAP_BYTES	(MAX_MEM_LIMIT)

#define MIN_BIGHEAP_GROWTH	(4 * KB)
#define MAX_BIGHEAP_GROWTH	(64 * MB)
#define DEFAULT_BIGHEAP_GROWTH	(64 * KB)

#define STRESS_BIGHEAP_INIT		(0)
#define STRESS_BIGHEAP_LOWMEM_CHECK	(1)
#define STRESS_BIGHEAP_MALLOC_TRIM	(2)
#define STRESS_BIGHEAP_REALLOC		(3)
#define STRESS_BIGHEAP_MALLOC		(4)
#define STRESS_BIGHEAP_OUT_OF_MEMORY	(5)
#define STRESS_BIGHEAP_WRITE_HEAP_END	(6)
#define STRESS_BIGHEAP_WRITE_HEAP_FULL	(7)
#define STRESS_BIGHEAP_READ_VERIFY_END	(8)
#define STRESS_BIGHEAP_READ_VERIFY_FULL	(9)
#define STRESS_BIGHEAP_FINISHED		(10)

static const stress_help_t help[] = {
	{ "B N","bigheap N",		"start N workers that grow the heap using realloc()" },
	{ NULL,	"bigheap-bytes N",	"grow heap up to N bytes in total" },
	{ NULL,	"bigheap-growth N",	"grow heap by N bytes per iteration" },
	{ NULL,	"bigheap-mlock",	"attempt to mlock newly mapped pages" },
	{ NULL,	"bigheap-ops N",	"stop after N bogo bigheap operations" },
	{ NULL,	NULL,			NULL }
};

static sigjmp_buf jmp_env;
static volatile int phase;
static volatile void *fault_addr;
static volatile int signo;
static volatile int sigcode;

/*
 *  stress_bigheap_phase()
 *	map phase to human readable description
 */
static const char *stress_bigheap_phase(void)
{
	static const char *phases[] = {
		"initialization",
		"low memory check",
		"malloc trim",
		"realloc",
		"malloc",
		"alloc out of memory",
		"write to end",
		"write full",
		"read verify end",
		"read verify full",
		"finished"
	};

	if ((phase < 0) || (phase >= (int)SIZEOF_ARRAY(phases)))
		return "unknown";
	return phases[phase];
}

/*
 *  stress_set_bigheap_mlock
 *	enable mlocking on allocated pages
 */
static int stress_set_bigheap_mlock(const char *opt)
{
	return stress_set_setting_true("bigheap-mlock", opt);
}

/*
 *  stress_set_bigheap_bytes()
 *  	Set maximum allocation amount in bytes
 */
static int stress_set_bigheap_bytes(const char *opt)
{
	size_t bigheap_bytes;

	bigheap_bytes = (size_t)stress_get_uint64_byte_memory(opt, 1);
	stress_check_range_bytes("bigheap-bytes", (uint64_t)bigheap_bytes,
		MIN_BIGHEAP_BYTES, MAX_BIGHEAP_BYTES);
	return stress_set_setting("bigheap-bytes", TYPE_ID_SIZE_T, &bigheap_bytes);
}

/*
 *  stress_set_bigheap_growth()
 *  	Set bigheap growth from given opt arg string
 */
static int stress_set_bigheap_growth(const char *opt)
{
	uint64_t bigheap_growth;

	bigheap_growth = stress_get_uint64_byte(opt);
	stress_check_range_bytes("bigheap-growth", bigheap_growth,
		MIN_BIGHEAP_GROWTH, MAX_BIGHEAP_GROWTH);
	return stress_set_setting("bigheap-growth", TYPE_ID_UINT64, &bigheap_growth);
}

/*
 *  stress_bigheap_segvhandler()
 *	SEGV handler
 */
#if defined(SA_SIGINFO)
static void NORETURN MLOCKED_TEXT stress_bigheap_segvhandler(
	int num,
	siginfo_t *info,
	void *ucontext)
{
	(void)num;
	(void)ucontext;

	fault_addr = info->si_addr;
	signo = info->si_signo;
	sigcode = info->si_code;

	siglongjmp(jmp_env, 1);		/* Ugly, bounce back */
}
#else
static void NORETURN MLOCKED_TEXT stress_bigheap_segvhandler(int signum)
{
	(void)signum;

	siglongjmp(jmp_env, 1);		/* Ugly, bounce back */
}
#endif

static int stress_bigheap_child(const stress_args_t *args, void *context)
{
	uint64_t bigheap_growth = DEFAULT_BIGHEAP_GROWTH;
	size_t bigheap_bytes = DEFAULT_BIGHEAP_BYTES;
	NOCLOBBER void *ptr = NULL, *last_ptr = NULL;
	NOCLOBBER uint8_t *last_ptr_end = NULL;
	NOCLOBBER size_t size = 0;
	NOCLOBBER double duration = 0.0, count = 0.0;
	NOCLOBBER bool segv_reported = false;
	const size_t page_size = args->page_size;
	const size_t stride = page_size;
	double rate;
	const bool verify = !!(g_opt_flags & OPT_FLAGS_VERIFY);
	const bool oom_avoid = !!(g_opt_flags & OPT_FLAGS_OOM_AVOID);
	bool bigheap_mlock = false;
	struct sigaction action;
	int ret;

	fault_addr = NULL;
	signo = -1;
	sigcode = -1;
	phase = 0;

	(void)context;

	(void)stress_get_setting("bigheap-mlock", &bigheap_mlock);
	(void)stress_get_setting("bigheap-bytes", &bigheap_bytes);
	if (!stress_get_setting("bigheap-growth", &bigheap_growth)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			bigheap_growth = MAX_BIGHEAP_GROWTH;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			bigheap_growth = MIN_BIGHEAP_GROWTH;
	}
	if (bigheap_growth < page_size)
		bigheap_growth = page_size;

	/* Round growth size to nearest page size */
	bigheap_growth &= ~(page_size - 1);

	(void)shim_memset(&action, 0, sizeof action);
	(void)sigemptyset(&action.sa_mask);
#if defined(SA_SIGINFO)
	action.sa_sigaction = stress_bigheap_segvhandler;
	action.sa_flags = SA_SIGINFO;
#else
	action.sa_handler = stress_bigheap_segvhandler;
#endif
	VOID_RET(int, sigaction(SIGSEGV, &action, NULL));

	ret = sigsetjmp(jmp_env, 1);
	/*
	 * We return here if we segfault, so
	 * first check if we need to terminate
	 */
	if (ret) {
		if (!segv_reported) {
			const char *signame = stress_get_signal_name(signo);

			segv_reported = true;
			pr_inf("%s: caught signal %d (%s), si_code = %d, fault address %p, phase %d '%s', alloc = %p .. %p\n",
				args->name, signo, signame ? signame : "unknown",
				sigcode, fault_addr, phase, stress_bigheap_phase(),
				ptr, (uint8_t *)ptr + size);
		}
		/* just abort */
		return EXIT_FAILURE;
	}

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

#if defined(MCL_FUTURE)
	if (bigheap_mlock)
		(void)shim_mlockall(MCL_FUTURE);
#else
	UNEXPECTED
#endif

	do {
		void *old_ptr = ptr;
		double t;

		/*
		 * With many instances running it is wise to
		 * double check before the next realloc as
		 * sometimes process start up is delayed for
		 * some time and we should bail out before
		 * exerting any more memory pressure
		 */
		if (!stress_continue(args))
			goto finish;

		phase = STRESS_BIGHEAP_LOWMEM_CHECK;
		/* Low memory avoidance, re-start */
		if ((size > bigheap_bytes) ||
		    (oom_avoid && stress_low_memory((size_t)bigheap_growth))) {
			free(old_ptr);
#if defined(HAVE_MALLOC_TRIM)
			phase = STRESS_BIGHEAP_MALLOC_TRIM;
			(void)malloc_trim(0);
#endif
			old_ptr = NULL;
			size = 0;
		}
		size += (size_t)bigheap_growth;

		t = stress_time_now();
		if (old_ptr) {
			phase = STRESS_BIGHEAP_REALLOC;
			ptr = realloc(old_ptr, size);
		} else {
			phase = STRESS_BIGHEAP_MALLOC;
			ptr = malloc(size);
		}
		if (ptr == NULL) {
			phase = STRESS_BIGHEAP_OUT_OF_MEMORY;
			pr_dbg("%s: out of memory at %" PRIu64
				" MB (instance %d)\n",
				args->name, (uint64_t)(4096ULL * size) >> 20,
				args->instance);
			if (old_ptr)
				free(old_ptr);
			size = 0;
		} else {
			uintptr_t *uintptr, *uintptr_end = (uintptr_t *)((uint8_t*)ptr + size);

			duration += stress_time_now() - t;
			count += 1.0;

			if (!stress_continue(args))
				goto finish;

			if (last_ptr == ptr) {
				phase = STRESS_BIGHEAP_WRITE_HEAP_END;
				uintptr = (uintptr_t *)last_ptr_end;
			} else {
				phase = STRESS_BIGHEAP_WRITE_HEAP_FULL;
				uintptr = (uintptr_t *)ptr;
			}
			while (uintptr < uintptr_end) {
				if (!stress_continue(args))
					goto finish;
				*uintptr = (uintptr_t)uintptr;
				uintptr += stride / sizeof(uintptr_t);
			}

			if (verify) {
				if (last_ptr == ptr) {
					phase = STRESS_BIGHEAP_WRITE_HEAP_END;
					uintptr = (uintptr_t *)last_ptr_end;
				} else {
					phase = STRESS_BIGHEAP_WRITE_HEAP_FULL;
					uintptr = (uintptr_t *)ptr;
				}
				while (uintptr < uintptr_end) {
					if (!stress_continue(args))
						goto finish;
					if (*uintptr != (uintptr_t)uintptr)
						pr_fail("%s: data at location %p was 0x%" PRIxPTR
							" instead of 0x%" PRIxPTR "\n",
							args->name, (void *)uintptr, *uintptr,
							(uintptr_t)uintptr);
					uintptr += stride / sizeof(uintptr_t);
				}
			}
			last_ptr = ptr;
			last_ptr_end = (void *)uintptr_end;
		}
		stress_bogo_inc(args);
	} while (stress_continue(args));

finish:
	phase = STRESS_BIGHEAP_FINISHED;
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	rate = (duration > 0.0) ? count / duration : 0.0;
	stress_metrics_set(args, 0, "realloc calls per sec", rate);

	free(ptr);

	return EXIT_SUCCESS;
}

/*
 *  stress_bigheap()
 *	stress heap allocation
 */
static int stress_bigheap(const stress_args_t *args)
{
	return stress_oomable_child(args, NULL, stress_bigheap_child, STRESS_OOMABLE_NORMAL);
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_bigheap_bytes,	stress_set_bigheap_bytes },
	{ OPT_bigheap_growth,	stress_set_bigheap_growth },
	{ OPT_bigheap_mlock,	stress_set_bigheap_mlock },
	{ 0,			NULL },
};

stressor_info_t stress_bigheap_info = {
	.stressor = stress_bigheap,
	.class = CLASS_OS | CLASS_VM,
	.opt_set_funcs = opt_set_funcs,
	.verify = VERIFY_OPTIONAL,
	.help = help
};
