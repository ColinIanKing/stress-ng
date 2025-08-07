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
#include "core-mincore.h"
#include "core-out-of-memory.h"

static const stress_help_t help[] = {
	{ NULL,	"rmap N",	"start N workers that stress reverse mappings" },
	{ NULL,	"rmap-ops N",	"stop after N rmap bogo operations" },
	{ NULL,	NULL,		NULL }
};

#if defined(HAVE_SIGLONGJMP)

static volatile bool do_jmp = true;
static sigjmp_buf jmp_env;

typedef struct {
	int fd;
	double start;
} stress_rlimit_context_t;

#define MAX_RLIMIT_CPU		(1)
#define MAX_RLIMIT_FSIZE	(1)
#define MAX_RLIMIT_AS		(32 * MB)
#define MAX_RLIMIT_DATA		(16 * MB)
#define MAX_RLIMIT_STACK	(1 * MB)
#define MAX_RLIMIT_NOFILE	(32)

typedef struct {
	const shim_rlimit_resource_t resource;	/* rlimit resource ID */
	const struct rlimit new_limit;	/* new rlimit setting */
	struct rlimit old_limit;	/* original old rlimit setting */
	int ret;			/* saved old rlimit setting return status */
} stress_limits_t;

typedef struct {
	const shim_rlimit_resource_t resource;
	const char *name;
} stress_resource_id_t;

#define RESOURCE_ID(x)	{ x, # x }

/*
 *  limits that are tested to see if we can hit these limits
 */
static stress_limits_t limits[] = {
#if defined(RLIMIT_CPU)
	{ RLIMIT_CPU,	{ MAX_RLIMIT_CPU, MAX_RLIMIT_CPU }, { 0, 0 }, false },
#endif
#if defined(RLIMIT_FSIZE)
	{ RLIMIT_FSIZE,	{ MAX_RLIMIT_FSIZE, MAX_RLIMIT_FSIZE }, { 0, 0 }, -1 },
#endif
#if defined(RLIMIT_AS)
	{ RLIMIT_AS,	{ MAX_RLIMIT_AS, MAX_RLIMIT_AS }, { 0, 0 }, -1 },
#endif
#if defined(RLIMIT_DATA)
	{ RLIMIT_DATA,	{ MAX_RLIMIT_DATA, MAX_RLIMIT_DATA }, { 0, 0 }, -1 },
#endif
#if defined(RLIMIT_STACK)
	{ RLIMIT_STACK,	{ MAX_RLIMIT_STACK, MAX_RLIMIT_STACK }, { 0, 0 }, -1 },
#endif
#if defined(RLIMIT_NOFILE)
	{ RLIMIT_NOFILE,{ MAX_RLIMIT_NOFILE, MAX_RLIMIT_NOFILE }, { 0, 0 }, -1 },
#endif
};

/*
 *  all known resource id types
 */
static const stress_resource_id_t resource_ids[] = {
#if defined(RLIMIT_AS)
	RESOURCE_ID(RLIMIT_AS),
#endif
#if defined(RLIMIT_CORE)
	RESOURCE_ID(RLIMIT_CORE),
#endif
#if defined(RLIMIT_CPU)
	RESOURCE_ID(RLIMIT_CPU),
#endif
#if defined(RLIMIT_DATA)
	RESOURCE_ID(RLIMIT_DATA),
#endif
#if defined(RLIMIT_FSIZE)
	RESOURCE_ID(RLIMIT_FSIZE),
#endif
#if defined(RLIMIT_LOCKS)
	RESOURCE_ID(RLIMIT_LOCKS),
#endif
#if defined(RLIMIT_MEMLOCK)
	RESOURCE_ID(RLIMIT_MEMLOCK),
#endif
#if defined(RLIMIT_MSGQUEUE)
	RESOURCE_ID(RLIMIT_MSGQUEUE),
#endif
#if defined(RLIMIT_NICE)
	RESOURCE_ID(RLIMIT_NICE),
#endif
#if defined(RLIMIT_NPROC)
	RESOURCE_ID(RLIMIT_NPROC),
#endif
#if defined(RLIMIT_RSS)
	RESOURCE_ID(RLIMIT_RSS),
#endif
#if defined(RLIMIT_RTTIME)
	RESOURCE_ID(RLIMIT_RTTIME),
#endif
#if defined(RLIMIT_SIGPENDING)
	RESOURCE_ID(RLIMIT_SIGPENDING),
#endif
#if defined(RLIMIT_STACK)
	RESOURCE_ID(RLIMIT_STACK),
#endif
};

/*
 *  stress_rlimit_handler()
 *	rlimit generic handler
 */
static void MLOCKED_TEXT stress_rlimit_handler(int signum)
{
	(void)signum;

	if (do_jmp) {
		siglongjmp(jmp_env, 1);
		stress_no_return();
	}
}


static int stress_rlimit_child(stress_args_t *args, void *ctxt)
{
	const stress_rlimit_context_t *context = (stress_rlimit_context_t *)ctxt;
	uint8_t *stack;

	stack = (uint8_t *)mmap(NULL, STRESS_MINSIGSTKSZ, PROT_READ | PROT_WRITE,
			MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (stack == MAP_FAILED) {
		pr_inf("%s: failed to mmap %zu byte signal stack%s, errno=%d (%s)\n",
			args->name, (size_t)STRESS_MINSIGSTKSZ,
			stress_get_memfree_str(), errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}

	if (stress_sigaltstack(stack, STRESS_MINSIGSTKSZ) < 0) {
		(void)munmap((void *)stack, STRESS_MINSIGSTKSZ);
		return EXIT_NO_RESOURCE;
	}
	stress_set_vma_anon_name(stack, STRESS_MINSIGSTKSZ, "stack");

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	/* Child rlimit stressor */
	do {
		int ret;
		size_t i;
		struct rlimit rlim;

		/*
		 *  Exercise all known good resource ids
		 */
		for (i = 0; i < SIZEOF_ARRAY(resource_ids); i++) {
			ret = getrlimit(resource_ids[i].resource, &rlim);
			if (UNLIKELY(ret < 0))
				continue;

			ret = setrlimit(resource_ids[i].resource, &rlim);
			if (UNLIKELY(ret < 0)) {
				pr_fail("%s: setrlimit %s failed, errno=%d (%s)\n",
					args->name, resource_ids[i].name,
					errno, strerror(errno));
				return EXIT_FAILURE;
			}
		}
		/*
		 *  Exercise illegal bad resource id
		 */
		VOID_RET(int, getrlimit((shim_rlimit_resource_t)~0, &rlim));


		/*
		 *  Now set limits and see if we can hit them
		 */
		for (i = 0; i < SIZEOF_ARRAY(limits); i++) {
			(void)setrlimit(limits[i].resource, &limits[i].new_limit);
		}

		ret = sigsetjmp(jmp_env, 1);

		/* Check for timer overrun */
		if (UNLIKELY((stress_time_now() - context->start) > (double)g_opt_timeout))
			break;
		/* Check for counter limit reached */
		if (UNLIKELY(!stress_continue(args)))
			break;

		if (LIKELY(ret == 0)) {
			uint8_t *ptr;
			void *oldbrk;
			int fds[MAX_RLIMIT_NOFILE];

			switch (stress_mwc8modn(5)) {
			default:
			case 0:
				/* Trigger an rlimit signal */
				if (ftruncate(context->fd, 2) < 0) {
					/* Ignore error */
				}
				break;
			case 1:
				/* Trigger RLIMIT_AS */
				ptr = (uint8_t *)mmap(NULL, MAX_RLIMIT_AS, PROT_READ | PROT_WRITE,
					MAP_ANONYMOUS | MAP_SHARED, -1, 0);
				if (ptr != MAP_FAILED)
					(void)stress_munmap_force((void *)ptr, MAX_RLIMIT_AS);
				break;
			case 2:
				/* Trigger RLIMIT_DATA */
				oldbrk = shim_sbrk(0);
				if (oldbrk != (void *)-1) {
					ptr = shim_sbrk(MAX_RLIMIT_DATA);
					if (ptr != (void *)-1) {
						VOID_RET(int, shim_brk(oldbrk));
					}
				}
				break;
			case 3:
				/* Trigger RLIMIT_STACK */
				{
					static uint8_t garbage[MAX_RLIMIT_STACK];

					(void)stress_mincore_touch_pages_interruptible(garbage, MAX_RLIMIT_STACK);
				}
				break;
			case 4:
				/* Hit NOFILE limit */
				for (i = 0; i < MAX_RLIMIT_NOFILE; i++) {
					fds[i] = open("/dev/null", O_RDONLY);
				}
				stress_close_fds(fds, MAX_RLIMIT_NOFILE);
				break;
			}
		} else if (ret == 1) {
			stress_bogo_inc(args);	/* SIGSEGV/SIGILL occurred */
		} else {
			break;		/* Something went wrong! */
		}
	} while (stress_continue(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	(void)close(context->fd);
	(void)munmap((void *)stack, STRESS_MINSIGSTKSZ);

	return EXIT_SUCCESS;
}

/*
 *  stress_rlimit
 *	stress by generating rlimit signals
 */
static int stress_rlimit(stress_args_t *args)
{
	struct sigaction old_action_xcpu, old_action_xfsz, old_action_segv;
	size_t i;
	char filename[PATH_MAX];
	stress_rlimit_context_t context;
	int ret;

	context.start = stress_time_now();

	if (stress_sighandler(args->name, SIGSEGV, stress_rlimit_handler, &old_action_segv) < 0)
		return EXIT_FAILURE;
	if (stress_sighandler(args->name, SIGXCPU, stress_rlimit_handler, &old_action_xcpu) < 0)
		return EXIT_FAILURE;
	if (stress_sighandler(args->name, SIGXFSZ, stress_rlimit_handler, &old_action_xfsz) < 0)
		return EXIT_FAILURE;

	(void)stress_temp_filename_args(args,
		filename, sizeof(filename), stress_mwc32());
	ret = stress_temp_dir_mk_args(args);
	if (ret < 0)
		return stress_exit_status(-ret);
	if ((context.fd = creat(filename, S_IRUSR | S_IWUSR)) < 0) {
		pr_fail("%s: creat %s failed, errno=%d (%s)\n",
			args->name, filename, errno, strerror(errno));
		(void)stress_temp_dir_rm_args(args);
		return EXIT_FAILURE;
	}
	(void)shim_unlink(filename);

	for (i = 0; i < SIZEOF_ARRAY(limits); i++) {
		limits[i].ret = getrlimit(limits[i].resource, &limits[i].old_limit);
	}

	ret = stress_oomable_child(args, &context, stress_rlimit_child, STRESS_OOMABLE_NORMAL);

	do_jmp = false;
	(void)stress_sigrestore(args->name, SIGXCPU, &old_action_xcpu);
	(void)stress_sigrestore(args->name, SIGXFSZ, &old_action_xfsz);
	(void)stress_sigrestore(args->name, SIGSEGV, &old_action_segv);
	(void)close(context.fd);
	(void)stress_temp_dir_rm_args(args);

	return ret;
}

const stressor_info_t stress_rlimit_info = {
	.stressor = stress_rlimit,
	.classifier = CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help
};

#else

const stressor_info_t stress_rlimit_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without siglongjmp support"
};

#endif
