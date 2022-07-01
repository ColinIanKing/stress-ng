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

#if defined(HAVE_LINUX_AUDIT_H)
#include <linux/audit.h>
#endif

#if defined(HAVE_LINUX_FILTER_H)
#include <linux/filter.h>
#endif

#if defined(HAVE_LINUX_SECCOMP_H)
#include <linux/seccomp.h>
#endif

#if defined(HAVE_SYS_PRCTL_H)
#include <sys/prctl.h>
#endif

static const stress_help_t help[] = {
	{ NULL,	"opcode N",	   "start N workers exercising random opcodes" },
	{ NULL,	"opcode-ops N",	   "stop after N opcode bogo operations" },
	{ NULL,	"opcode-method M", "set opcode stress method (M = random, inc, mixed, text)" },
	{ NULL, NULL,		   NULL }
};

#if defined(HAVE_LINUX_SECCOMP_H) &&	\
    defined(HAVE_LINUX_AUDIT_H) &&	\
    defined(HAVE_LINUX_FILTER_H) &&	\
    defined(HAVE_MPROTECT) &&		\
    defined(HAVE_SYS_PRCTL_H)

#define SYSCALL_NR	(offsetof(struct seccomp_data, nr))

#define ALLOW_SYSCALL(syscall)					\
	BPF_JUMP(BPF_JMP+BPF_JEQ+BPF_K, __NR_##syscall, 0, 1), 	\
	BPF_STMT(BPF_RET+BPF_K, SECCOMP_RET_ALLOW)

#define PAGES		(16)
#define TRACK_SIGCOUNT	(0)

typedef void(*stress_opcode_func)(uint8_t *ops_begin, const uint8_t *ops_end, uint32_t *op);

typedef struct {
	const char *name;
	const stress_opcode_func func;
} stress_opcode_method_info_t;

static const int sigs[] = {
#if defined(SIGILL)
	SIGILL,
#endif
#if defined(SIGTRAP)
	SIGTRAP,
#endif
#if defined(SIGFPE)
	SIGFPE,
#endif
#if defined(SIGBUS)
	SIGBUS,
#endif
#if defined(SIGSEGV)
	SIGSEGV,
#endif
#if defined(SIGIOT)
	SIGIOT,
#endif
#if defined(SIGEMT)
	SIGEMT,
#endif
#if defined(SIGALRM)
	SIGALRM,
#endif
#if defined(SIGINT)
	SIGINT,
#endif
#if defined(SIGHUP)
	SIGHUP,
#endif
#if defined(SIGSYS)
	SIGSYS
#endif
};

#if defined(HAVE_LINUX_SECCOMP_H) &&	\
    defined(SECCOMP_SET_MODE_FILTER)
static struct sock_filter filter[] = {
	BPF_STMT(BPF_LD+BPF_W+BPF_ABS, SYSCALL_NR),
#if defined(__NR_exit_group)
	ALLOW_SYSCALL(exit_group),
#endif
	BPF_STMT(BPF_RET+BPF_K, SECCOMP_RET_TRAP)
};

static struct sock_fprog prog = {
	.len = (unsigned short)SIZEOF_ARRAY(filter),
	.filter = filter
};

#endif

#if TRACK_SIGCOUNT
#if defined(NSIG)
#define MAX_SIGS	(NSIG)
#elif defined(_NSIG)
#define MAX_SIGS	(_NSIG)
#else
#define MAX_SIGS	(256)
#endif

static uint64_t *sig_count;
#endif

static void MLOCKED_TEXT NORETURN stress_badhandler(int signum)
{
#if TRACK_SIGCOUNT
	if (signum < MAX_SIGS)
		sig_count[signum]++;
#else
	(void)signum;
#endif
	_exit(1);
}

static inline uint32_t reverse32(register uint64_t x)
{
	x = (((x & 0xaaaaaaaa) >> 1) | ((x & 0x55555555) << 1));
	x = (((x & 0xcccccccc) >> 2) | ((x & 0x33333333) << 2));
	x = (((x & 0xf0f0f0f0) >> 4) | ((x & 0x0f0f0f0f) << 4));
	x = (((x & 0xff00ff00) >> 8) | ((x & 0x00ff00ff) << 8));
	return (uint32_t)(x >> 16) | (uint32_t)(x << 16);
}

static void stress_opcode_random(
	uint8_t *ops_begin,
	const uint8_t *ops_end,
	uint32_t *op)
{
	register uint8_t *ops = (uint8_t *)ops_begin;
	(void)op;

	while (ops < ops_end)
		*(ops++) = stress_mwc8();
}

static void stress_opcode_inc(
	uint8_t *ops_begin,
	const uint8_t *ops_end,
	uint32_t *op)
{
	register uint32_t tmp = *op;
	register uint32_t *ops = (uint32_t *)ops_begin;

	while (ops < (const uint32_t *)ops_end)
		*(ops++) = tmp++;

	*op = tmp;
}

static void stress_opcode_mixed(
	uint8_t *ops_begin,
	const uint8_t *ops_end,
	uint32_t *op)
{
	register uint32_t tmp = *op;
	register uint32_t *ops = (uint32_t *)ops_begin;

	while (ops < (const uint32_t *)ops_end) {
		register uint32_t rnd = stress_mwc32();

		*(ops++) = tmp;
		*(ops++) = tmp ^ 0xffffffff;	/* Inverted */
		*(ops++) = ((tmp >> 1) ^ tmp);	/* Gray */
		*(ops++) = reverse32(tmp);

		*(ops++) = rnd;
		*(ops++) = rnd ^ 0xffffffff;
		*(ops++) = ((rnd >> 1) ^ rnd);
		*(ops++) = reverse32(rnd);
	}
	*op = tmp;
}

static void stress_opcode_text(
	uint8_t *ops_begin,
	const uint8_t *ops_end,
	uint32_t *op)
{
	char *text_start, *text_end;
	const size_t ops_len = (uintptr_t)ops_end - (uintptr_t)ops_begin;
	const size_t text_len = stress_text_addr(&text_start, &text_end) - 8;
	uint8_t *ops;
	size_t offset;

	if (text_len < ops_len) {
		stress_opcode_random(ops_begin, ops_end, op);
		return;
	}

	offset = stress_mwc64() % (text_len - ops_len);
	offset &= ~(0x7ULL);

	(void)memcpy(ops_begin, text_start + offset, ops_len);
	for (ops = ops_begin; ops < ops_end; ops++) {
		uint8_t rnd = stress_mwc8();

		/* 1 in 8 chance of random bit corruption */
		if (rnd < 32) {
			uint8_t bit = (uint8_t)(1 << (rnd & 7));
			*ops ^= bit;
		}
	}
}

static const stress_opcode_method_info_t stress_opcode_methods[] = {
	{ "random",	stress_opcode_random },
	{ "text",	stress_opcode_text },
	{ "inc",	stress_opcode_inc },
	{ "mixed",	stress_opcode_mixed },
	{ NULL,		NULL }
};

/*
 *  stress_set_opcode_method()
 *      set default opcode stress method
 */
static int stress_set_opcode_method(const char *name)
{
	stress_opcode_method_info_t const *info;

	for (info = stress_opcode_methods; info->func; info++) {
		if (!strcmp(info->name, name)) {
			stress_set_setting("opcode-method", TYPE_ID_UINTPTR_T, &info);
			return 0;
		}
	}

	(void)fprintf(stderr, "opcode-method must be one of:");
	for (info = stress_opcode_methods; info->func; info++) {
		(void)fprintf(stderr, " %s", info->name);
	}
	(void)fprintf(stderr, "\n");

	return -1;
}

/*
 *  stress_opcode
 *	stress with random opcodes
 */
static int stress_opcode(const stress_args_t *args)
{
	const size_t page_size = args->page_size;
	int rc;
	uint32_t op = 0;
	size_t i;
	const stress_opcode_method_info_t *opcode_method = &stress_opcode_methods[0];
#if TRACK_SIGCOUNT
	const size_t sig_count_size = MAX_SIGS * sizeof(*sig_count);
#endif

#if TRACK_SIGCOUNT
	sig_count = (uint64_t *)mmap(NULL, sig_count_size, PROT_READ | PROT_WRITE,
		MAP_ANONYMOUS | MAP_SHARED, -1, 0);
	if (sig_count == MAP_FAILED) {
		pr_fail("%s: mmap failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}
#endif

	(void)stress_get_setting("opcode-method", &opcode_method);

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		pid_t pid;

		/*
		 *  Force a new random value so that child always
		 *  gets a different random value on each fork
		 */
		(void)stress_mwc32();
		op += 1024;
again:
		pid = fork();
		if (pid < 0) {
			if (stress_redo_fork(errno))
				goto again;
			if (!keep_stressing(args))
				goto finish;
			pr_fail("%s: fork failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			rc = EXIT_NO_RESOURCE;
			goto err;
		}
		if (pid == 0) {
			struct itimerval it;
			uint8_t *opcodes, *ops_begin, *ops_end;

			(void)sched_settings_apply(true);

			/* We don't want bad ops clobbering this region */
			stress_shared_unmap();

			/* We don't want core dumps either */
			stress_process_dumpable(false);

			/* Drop all capabilities */
			if (stress_drop_capabilities(args->name) < 0) {
				_exit(EXIT_NO_RESOURCE);
			}
			for (i = 0; i < SIZEOF_ARRAY(sigs); i++) {
				if (stress_sighandler(args->name, sigs[i], stress_badhandler, NULL) < 0)
					_exit(EXIT_FAILURE);
			}

			opcodes = mmap(NULL, page_size * PAGES, PROT_READ | PROT_WRITE,
				MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
			if (opcodes == MAP_FAILED) {
				pr_fail("%s: mmap failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				_exit(EXIT_NO_RESOURCE);
			}
			/* Force pages resident */
			(void)memset(opcodes, 0x00, page_size * PAGES);

			ops_begin = opcodes + page_size;
			ops_end = opcodes + (page_size * (PAGES - 1));

			(void)mprotect(opcodes, page_size, PROT_NONE);
			(void)mprotect(ops_end, page_size, PROT_NONE);
			(void)mprotect(ops_begin, page_size, PROT_WRITE);

			opcode_method->func(ops_begin, ops_end, &op);

			(void)mprotect(ops_begin, page_size, PROT_READ | PROT_EXEC);
			shim_flush_icache((char *)ops_begin, (char *)ops_end);
			(void)setpgid(0, g_pgrp);
			stress_parent_died_alarm();

			/*
			 * Force abort if the opcodes magically
			 * do an infinite loop
			 */
			it.it_interval.tv_sec = 0;
			it.it_interval.tv_usec = 50000;
			it.it_value.tv_sec = 0;
			it.it_value.tv_usec = 50000;
			if (setitimer(ITIMER_REAL, &it, NULL) < 0) {
				pr_fail("%s: setitimer failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				_exit(EXIT_NO_RESOURCE);
			}

			/* Disable stack smashing messages */
			stress_set_stack_smash_check_flag(false);

			/*
			 * Flush and close stdio fds, we
			 * really don't care if the child dies
			 * in a bad way and libc or whatever
			 * reports of stack smashing or heap
			 * corruption since the child will
			 * die soon anyhow
			 */
			(void)fflush(NULL);
			(void)close(fileno(stdin));
			(void)close(fileno(stdout));
			(void)close(fileno(stderr));

			for (i = 0; i < 1024; i++) {
#if defined(HAVE_LINUX_SECCOMP_H) &&	\
    defined(SECCOMP_SET_MODE_FILTER)
				/*
				 * Limit syscall using seccomp
				 */
				(void)shim_seccomp(SECCOMP_SET_MODE_FILTER, 0, &prog);
#endif
				((void (*)(void))(ops_begin + i))();
			}

			/*
			 * Originally we unmapped these, but this is
			 * another system call required that may go
			 * wrong because libc or the stack has been
			 * trashed, so just skip it.
			 *
			(void)munmap(opcodes, page_size * PAGES);
			 */
			_exit(0);
		}
		if (pid > 0) {
			int ret, status;

			ret = shim_waitpid(pid, &status, 0);
			if (ret < 0) {
				if (errno != EINTR)
					pr_dbg("%s: waitpid(): errno=%d (%s)\n",
						args->name, errno, strerror(errno));
				(void)kill(pid, SIGTERM);
				(void)kill(pid, SIGKILL);
				(void)shim_waitpid(pid, &status, 0);
			}
			inc_counter(args);
		}
	} while (keep_stressing(args));

finish:
	rc = EXIT_SUCCESS;

#if TRACK_SIGCOUNT
	for (i = 0; i < MAX_SIGS; i++) {
		if (sig_count[i]) {
			pr_dbg("%s: %-25.25s: %" PRIu64 "\n",
				args->name, strsignal(i), sig_count[i]);
		}
	}
#endif
err:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

#if TRACK_SIGCOUNT
	(void)munmap(sig_count, sig_count_size);
#endif
	return rc;
}

static void stress_opcode_set_default(void)
{
	stress_set_opcode_method("random");
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_opcode_method,	stress_set_opcode_method },
	{ 0,			NULL }
};

stressor_info_t stress_opcode_info = {
	.stressor = stress_opcode,
	.set_default = stress_opcode_set_default,
	.class = CLASS_CPU | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
#else

static int stress_set_opcode_method(const char *name)
{
	(void)name;

	(void)fprintf(stderr, "opcode-method not implemented");

	return -1;
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_opcode_method,	stress_set_opcode_method },
	{ 0,			NULL }
};

stressor_info_t stress_opcode_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_CPU | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
#endif
