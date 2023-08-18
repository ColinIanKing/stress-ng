// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
 *
 */
#include "stress-ng.h"
#include "core-builtin.h"

#if defined(HAVE_UCONTEXT_H)
#include <ucontext.h>
#endif

#define STRESS_CONTEXTS		(3)

static stress_help_t help[] = {
	{ NULL,	"context N",	 "start N workers exercising user context" },
	{ NULL,	"context-ops N", "stop context workers after N bogo operations" },
	{ NULL,	NULL,		 NULL }
};

#if defined(HAVE_SWAPCONTEXT) &&	\
    defined(HAVE_UCONTEXT_H)

#define CONTEXT_STACK_SIZE	(16384)

typedef struct {
	uint32_t check0;	/* memory clobbering check canary */
	ucontext_t uctx;	/* swapcontext context */
	uint32_t check1;	/* memory clobbering check canary */
} chk_ucontext_t;

typedef struct {
	uint32_t check0;	/* copy of original check1 canary */
	uint32_t check1;	/* copy of original check1 canary */
} chk_canary_t;

typedef struct {
	chk_ucontext_t	cu ALIGN64;	/* check ucontext */
	uint8_t		stack[CONTEXT_STACK_SIZE + STACK_ALIGNMENT]; /* stack */
	chk_canary_t	canary;	/* copy of canary */
} context_data_t;

static ucontext_t uctx_main;
static context_data_t *context;
static uint64_t context_counter ALIGN64;
static uint64_t stress_max_ops;

static void OPTIMIZE3 stress_thread1(void)
{
	register ucontext_t *uctx0 = &context[0].cu.uctx;
	register ucontext_t *uctx1 = &context[1].cu.uctx;
	register const uint64_t max_ops = stress_max_ops;

	if (max_ops) {
		do {
			context_counter++;
			(void)swapcontext(uctx0, uctx1);
		} while (stress_continue_flag() && (context_counter < max_ops));
	} else {
		do {
			context_counter++;
			(void)swapcontext(uctx0, uctx1);
		} while (stress_continue_flag());
	}

	(void)swapcontext(uctx0, &uctx_main);
}

static void OPTIMIZE3 stress_thread2(void)
{
	register ucontext_t *uctx1 = &context[1].cu.uctx;
	register ucontext_t *uctx2 = &context[2].cu.uctx;
	register const uint64_t max_ops = stress_max_ops;

	if (max_ops) {
		do {
			context_counter++;
			(void)swapcontext(uctx1, uctx2);
		} while (stress_continue_flag() && (!max_ops || (context_counter < max_ops)));
	} else {
		do {
			context_counter++;
			(void)swapcontext(uctx1, uctx2);
		} while (stress_continue_flag());
	}
	(void)swapcontext(uctx1, &uctx_main);
}

static void OPTIMIZE3 stress_thread3(void)
{
	register ucontext_t *uctx2 = &context[2].cu.uctx;
	register ucontext_t *uctx0 = &context[0].cu.uctx;
	register const uint64_t max_ops = stress_max_ops;

	if (max_ops) {
		do {
			context_counter++;
			(void)swapcontext(uctx2, uctx0);
		} while (stress_continue_flag() && (!max_ops || (context_counter < max_ops)));
	} else {
		do {
			context_counter++;
			(void)swapcontext(uctx2, uctx0);
		} while (stress_continue_flag());
	}
	(void)swapcontext(uctx2, &uctx_main);
}

static void (*stress_threads[STRESS_CONTEXTS])(void) = {
	stress_thread1,
	stress_thread2,
	stress_thread3,
};

static int stress_context_init(
	const stress_args_t *args,
	void (*func)(void),
	ucontext_t *uctx_link,
	context_data_t *context_data)
{
	(void)shim_memset(context_data, 0, sizeof(*context_data));

	if (getcontext(&context_data->cu.uctx) < 0) {
		pr_fail("%s: getcontext failed: %d (%s)\n",
			args->name, errno, strerror(errno));
		return -1;
	}

	context_data->canary.check0 = stress_mwc32();
	context_data->canary.check1 = stress_mwc32();

	context_data->cu.check0 = context_data->canary.check0;
	context_data->cu.check1 = context_data->canary.check1;
	context_data->cu.uctx.uc_stack.ss_sp =
		(void *)stress_align_address(context_data->stack, STACK_ALIGNMENT);
	context_data->cu.uctx.uc_stack.ss_size = CONTEXT_STACK_SIZE;
	context_data->cu.uctx.uc_link = uctx_link;
	makecontext(&context_data->cu.uctx, func, 0);

	return 0;
}

/*
 *  stress_context()
 *	stress that exercises CPU context save/restore
 */
static int stress_context(const stress_args_t *args)
{
	size_t i;
	const size_t context_size = 3 * sizeof(*context);
	double duration, rate, t;
	int rc = EXIT_FAILURE;

	context = (context_data_t *)mmap(NULL, context_size,
					PROT_READ | PROT_WRITE,
					MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (context == MAP_FAILED) {
		pr_inf("%s: failed to allocate %d x %zd byte context buffers, skipping stressor\n",
			args->name, STRESS_CONTEXTS, sizeof(context_data_t));
		return EXIT_NO_RESOURCE;
	}
	(void)shim_memset(&uctx_main, 0, sizeof(uctx_main));

	context_counter = 0;
	stress_max_ops = args->max_ops * 1000;

	/* Create 3 micro threads */
	for (i = 0; i < STRESS_CONTEXTS; i++) {
		if (stress_context_init(args, stress_threads[i], &uctx_main, &context[i]) < 0)
			goto fail;
	}

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	/* And start.. */
	t = stress_time_now();
	if (swapcontext(&uctx_main, &context[0].cu.uctx) < 0) {
		pr_fail("%s: swapcontext failed: %d (%s)\n",
			args->name, errno, strerror(errno));
		goto fail;
	}
	duration = stress_time_now() - t;

	stress_bogo_set(args, context_counter / 1000);

	for (i = 0; i < STRESS_CONTEXTS; i++) {
		if (context[i].canary.check0 != context[i].cu.check0) {
			pr_fail("%s: swapcontext clobbered data before context region\n",
				args->name);
		}
		if (context[i].canary.check1 != context[i].cu.check1) {
			pr_fail("%s: swapcontext clobbered data after context region\n",
				args->name);
		}
	}
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	rate = (duration > 0.0) ? (double)context_counter / duration : 0.0;
	stress_metrics_set(args, 0, "swapcontext calls per sec", rate);

	rc = EXIT_SUCCESS;

fail:
	(void)munmap((void *)context, context_size);
	return rc;
}

stressor_info_t stress_context_info = {
	.stressor = stress_context,
	.class = CLASS_MEMORY | CLASS_CPU,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
stressor_info_t stress_context_info = {
	.stressor = stress_unimplemented,
	.class = CLASS_MEMORY | CLASS_CPU,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without ucontext.h"
};
#endif
