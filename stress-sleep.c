/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
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
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 */
#include "stress-ng.h"
#include "core-asm-x86.h"
#include "core-cpu.h"

#if defined(HAVE_SYS_SELECT_H)
#include <sys/select.h>
#else
UNEXPECTED
#endif

#define MIN_SLEEP		(1)
#define MAX_SLEEP		(30000)
#define DEFAULT_SLEEP		(256)

#if defined(HAVE_LIB_PTHREAD)

typedef struct {
	const stress_args_t *args;
	uint64_t counter;
	uint64_t sleep_max;
	pthread_t pthread;
} stress_ctxt_t;

static volatile bool thread_terminate;
static sigset_t set;
#endif

static const stress_help_t help[] = {
	{ NULL,	"sleep N",	"start N workers performing various duration sleeps" },
	{ NULL,	"sleep-max P",	"create P threads at a time by each worker" },
	{ NULL,	"sleep-ops N",	"stop after N bogo sleep operations" },
	{ NULL,	NULL,		NULL }
};

static int stress_set_sleep_max(const char *opt)
{
	uint64_t sleep_max;

	sleep_max = stress_get_uint64(opt);
	stress_check_range("sleep-max", sleep_max,
		MIN_SLEEP, MAX_SLEEP);
	return stress_set_setting("sleep-max", TYPE_ID_UINT64, &sleep_max);
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_sleep_max,	stress_set_sleep_max },
	{ 0,			NULL }
};

#if defined(HAVE_LIB_PTHREAD)

static void MLOCKED_TEXT stress_sigalrm_handler(int signum)
{
	(void)signum;

	thread_terminate = true;
}

/*
 *  stress_pthread_func()
 *	pthread that performs different ranges of sleeps
 */
static void *stress_pthread_func(void *c)
{
	static void *nowt = NULL;
	stress_ctxt_t *ctxt = (stress_ctxt_t *)c;
	const stress_args_t *args = ctxt->args;
	const uint64_t max_ops =
		args->max_ops ? (args->max_ops / ctxt->sleep_max) + 1 : 0;
#if defined(HAVE_ASM_X86_TPAUSE)
	const bool x86_has_waitpkg = stress_cpu_x86_has_waitpkg();
#endif

	while (keep_stressing(args) &&
	       !thread_terminate &&
	       (!max_ops || (ctxt->counter < max_ops))) {
		struct timespec tv;
#if defined(HAVE_SYS_SELECT_H) &&	\
    defined(HAVE_SELECT)
		struct timeval timeout;
#endif

		tv.tv_sec = 0;
		tv.tv_nsec = 1;
		if (!keep_stressing_flag() && (nanosleep(&tv, NULL) < 0))
			break;
		tv.tv_sec = 0;
		tv.tv_nsec = 10;
		if (!keep_stressing_flag() && (nanosleep(&tv, NULL) < 0))
			break;
		tv.tv_sec = 0;
		tv.tv_nsec = 100;
		if (!keep_stressing_flag() && (nanosleep(&tv, NULL) < 0))
			break;
		if (!keep_stressing_flag() && (shim_usleep(1) < 0))
			break;
		if (!keep_stressing_flag() && (shim_usleep(10) < 0))
			break;
		if (!keep_stressing_flag() && (shim_usleep(100) < 0))
			break;
		if (!keep_stressing_flag() && (shim_usleep(1000) < 0))
			break;
		if (!keep_stressing_flag() && (shim_usleep(10000) < 0))
			break;
#if defined(HAVE_PSELECT)
		tv.tv_sec = 0;
		tv.tv_nsec = 1;
		if (!keep_stressing_flag() && (pselect(0, NULL, NULL, NULL, &tv, NULL) < 0))
			goto skip_pselect;
		tv.tv_sec = 0;
		tv.tv_nsec = 10;
		if (!keep_stressing_flag() && (pselect(0, NULL, NULL, NULL, &tv, NULL) < 0))
			goto skip_pselect;
		tv.tv_sec = 0;
		tv.tv_nsec = 100;
		if (!keep_stressing_flag() && (pselect(0, NULL, NULL, NULL, &tv, NULL) < 0))
			goto skip_pselect;
		tv.tv_sec = 0;
		tv.tv_nsec = 1000;
		if (!keep_stressing_flag() && (pselect(0, NULL, NULL, NULL, &tv, NULL) < 0))
			goto skip_pselect;
skip_pselect:
#endif

#if defined(HAVE_SYS_SELECT_H) &&	\
    defined(HAVE_SELECT)
		timeout.tv_sec = 0;
		timeout.tv_usec = 10;
		if (!keep_stressing_flag() && (select(0, NULL, NULL, NULL, &timeout) < 0))
			break;
		timeout.tv_sec = 0;
		timeout.tv_usec = 100;
		if (!keep_stressing_flag() && (select(0, NULL, NULL, NULL, &timeout) < 0))
			break;
		timeout.tv_sec = 0;
		timeout.tv_usec = 1000;
		if (!keep_stressing_flag() && (select(0, NULL, NULL, NULL, &timeout) < 0))
			break;
		timeout.tv_sec = 0;
		timeout.tv_usec = 10000;
		if (!keep_stressing_flag() && (select(0, NULL, NULL, NULL, &timeout) < 0))
			break;
#endif
#if defined(HAVE_ASM_X86_TPAUSE) &&	\
    !defined(__PCC__)
		if (x86_has_waitpkg) {
			int i;

			for (i = 1; keep_stressing_flag() && (i < 1024); i <<= 1)
				stress_asm_x86_tpause(0, i);
		}
#endif

		ctxt->counter++;
	}
	return &nowt;
}

/*
 *  stress_sleep()
 *	stress by many sleeping threads
 */
static int stress_sleep(const stress_args_t *args)
{
	uint64_t i, n, limited = 0;
	uint64_t sleep_max = DEFAULT_SLEEP;
	static stress_ctxt_t ctxts[MAX_SLEEP];
	int ret = EXIT_SUCCESS;

	if (!stress_get_setting("sleep-max", &sleep_max)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			sleep_max = MAX_SLEEP;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			sleep_max = MIN_SLEEP;
	}

	if (stress_sighandler(args->name, SIGALRM, stress_sigalrm_handler, NULL) < 0)
		return EXIT_FAILURE;

	(void)memset(ctxts, 0, sizeof(ctxts));
	(void)sigfillset(&set);

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	for (n = 0; n < sleep_max; n++) {
		ctxts[n].args = args;
		ctxts[n].sleep_max = sleep_max;
		ret = pthread_create(&ctxts[n].pthread, NULL,
			stress_pthread_func, &ctxts[n]);
		if (ret) {
			/* Out of resources, don't try any more */
			if (ret == EAGAIN) {
				limited++;
				break;
			}
			/* Something really unexpected */
			pr_fail("%s: pthread create failed, errno=%d (%s)\n",
				args->name, ret, strerror(ret));
			ret = EXIT_NO_RESOURCE;
			goto tidy;
		}
		/* Timed out? abort! */
		if (!keep_stressing_flag())
			goto tidy;
	}

	do {
		set_counter(args, 0);
		(void)shim_usleep_interruptible(10000);
		for (i = 0; i < n; i++)
			add_counter(args, ctxts[i].counter);
	}  while (!thread_terminate && keep_stressing(args));

	ret = EXIT_SUCCESS;
tidy:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	thread_terminate = true;
	for (i = 0; i < n; i++) {
		VOID_RET(int, pthread_join(ctxts[i].pthread, NULL));
	}

	if (limited) {
		pr_inf("%s: %.2f%% of iterations could not reach "
			"requested %" PRIu64 " threads (instance %"
			PRIu32 ")\n",
			args->name,
			100.0 * (double)limited / (double)sleep_max,
			sleep_max, args->instance);
	}

	return ret;
}

stressor_info_t stress_sleep_info = {
	.stressor = stress_sleep,
	.class = CLASS_INTERRUPT | CLASS_SCHEDULER | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
#else
stressor_info_t stress_sleep_info = {
	.stressor = stress_unimplemented,
	.class = CLASS_INTERRUPT | CLASS_SCHEDULER | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.help = help,
	.unimplemented_reason = "built without pthread support"
};
#endif
