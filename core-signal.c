/*
 * Copyright (C) 2014-2021 Canonical, Ltd.
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
#include "core-mmap.h"

#include <stdarg.h>

#if defined(NSIG)
#define STRESS_NSIG	NSIG
#elif defined(_NSIG)
#define STRESS_NSIG	_NSIG
#endif

typedef struct {
	const int  signum;	/* signal number */
	const char *name;	/* human readable signal name */
} stress_sig_name_t;

#define SIG_NAME(x) { x, #x }

static const stress_sig_name_t sig_names[] = {
#if defined(SIGABRT)
	SIG_NAME(SIGABRT),
#endif
#if defined(SIGALRM)
	SIG_NAME(SIGALRM),
#endif
#if defined(SIGBUS)
	SIG_NAME(SIGBUS),
#endif
#if defined(SIGCHLD)
	SIG_NAME(SIGCHLD),
#endif
#if defined(SIGCLD)
	SIG_NAME(SIGCLD),
#endif
#if defined(SIGCONT)
	SIG_NAME(SIGCONT),
#endif
#if defined(SIGEMT)
	SIG_NAME(SIGEMT),
#endif
#if defined(SIGFPE)
	SIG_NAME(SIGFPE),
#endif
#if defined(SIGHUP)
	SIG_NAME(SIGHUP),
#endif
#if defined(SIGILL)
	SIG_NAME(SIGILL),
#endif
#if defined(SIGINFO)
	SIG_NAME(SIGINFO),
#endif
#if defined(SIGINT)
	SIG_NAME(SIGINT),
#endif
#if defined(SIGIO)
	SIG_NAME(SIGIO),
#endif
#if defined(SIGIOT)
	SIG_NAME(SIGIOT),
#endif
#if defined(SIGKILL)
	SIG_NAME(SIGKILL),
#endif
#if defined(SIGLOST)
	SIG_NAME(SIGLOST),
#endif
#if defined(SIGPIPE)
	SIG_NAME(SIGPIPE),
#endif
#if defined(SIGPOLL)
	SIG_NAME(SIGPOLL),
#endif
#if defined(SIGPROF)
	SIG_NAME(SIGPROF),
#endif
#if defined(SIGPWR)
	SIG_NAME(SIGPWR),
#endif
#if defined(SIGQUIT)
	SIG_NAME(SIGQUIT),
#endif
#if defined(SIGSEGV)
	SIG_NAME(SIGSEGV),
#endif
#if defined(SIGSTKFLT)
	SIG_NAME(SIGSTKFLT),
#endif
#if defined(SIGSTOP)
	SIG_NAME(SIGSTOP),
#endif
#if defined(SIGSYS)
	SIG_NAME(SIGSYS),
#endif
#if defined(SIGTERM)
	SIG_NAME(SIGTERM),
#endif
#if defined(SIGTRAP)
	SIG_NAME(SIGTRAP),
#endif
#if defined(SIGTSTP)
	SIG_NAME(SIGTSTP),
#endif
#if defined(SIGTTIN)
	SIG_NAME(SIGTTIN),
#endif
#if defined(SIGTTOU)
	SIG_NAME(SIGTTOU),
#endif
#if defined(SIGUNUSED)
	SIG_NAME(SIGUNUSED),
#endif
#if defined(SIGURG)
	SIG_NAME(SIGURG),
#endif
#if defined(SIGUSR1)
	SIG_NAME(SIGUSR1),
#endif
#if defined(SIGUSR2)
	SIG_NAME(SIGUSR2),
#endif
#if defined(SIGVTALRM)
	SIG_NAME(SIGVTALRM),
#endif
#if defined(SIGWINCH)
	SIG_NAME(SIGWINCH),
#endif
#if defined(SIGXCPU)
	SIG_NAME(SIGXCPU),
#endif
#if defined(SIGXFSZ)
	SIG_NAME(SIGXFSZ),
#endif
};

static void stress_dbg(const char *fmt, ...) FORMAT(printf, 1, 2);

/*
 *  stress_dbg()
 *	simple debug, messages must be less than 256 bytes
 */
static void stress_dbg(const char *fmt, ...)
{
	va_list ap;
	int n, sz;
	static char buf[256];
	n = snprintf(buf, sizeof(buf), "stress-ng: debug: [%" PRIdMAX"] ", (intmax_t)getpid());
	if (UNLIKELY(n < 0))
		return;
	sz = n;
	va_start(ap, fmt);
	n = vsnprintf(buf + sz, sizeof(buf) - sz, fmt, ap);
	va_end(ap);
	sz += n;

	VOID_RET(ssize_t, write(fileno(stdout), buf, (size_t)sz));
}

/*
 *  stress_dump_data()
 *	dump to stdout 16 bytes of data code if it is readable. SIGILL address
 *	data is indicated with < > around it.
 */
static void stress_dump_data(
	const uint8_t *addr,
	const uint8_t *fault_addr,
	const size_t len)
{
	if (stress_addr_readable(addr, len)) {
		size_t i;
		bool show_opcode = false;
		int n, sz = 0;
		char buf[128];

		n = snprintf(buf + sz, sizeof(buf) - sz, "stress-ng: info: 0x%16.16" PRIxPTR ":", (uintptr_t)addr);
		if (n < 0)
			return;
		sz += n;

		for (i = 0; i < len; i++) {
			if (&addr[i] == fault_addr) {
				n = snprintf(buf + sz, sizeof(buf) - sz, "<%-2.2x>", addr[i]);
				if (n < 0)
					return;
				sz += n;
				show_opcode = true;
			} else {
				n = snprintf(buf + sz, sizeof(buf) - sz, "%s%-2.2x", show_opcode ? "" : " ", addr[i]);
				if (n < 0)
					return;
				sz += n;
				show_opcode = false;
			}
		}
		stress_dbg("%s\n", buf);
	} else {
		stress_dbg("stress-ng: info: 0x%16.16" PRIxPTR " not readable\n", (uintptr_t)addr);
	}
}

/*
 *  stress_dump_readable_data()
 *	3 lines of memory hexdump, aligned to 16 bytes boundary
 */
static void stress_dump_readable_data(uint8_t *fault_addr)
{
	int i;
	uint8_t *addr = (uint8_t *)((uintptr_t)fault_addr & ~0xf);

	for (i = 0; i < 3; i++, addr += 16) {
		stress_dump_data(addr, fault_addr, 16);
	}
}

/*
 *  stress_dump_map_info()
 *	find fault address in /proc/self/maps, dump out map info
 */
static void stress_dump_map_info(uint8_t *fault_addr)
{
#if defined(__linux__)
	FILE *fp;
	char buf[1024];

	fp = fopen("/proc/self/maps", "r");
	if (UNLIKELY(!fp))
		return;
	while ((fgets(buf, sizeof(buf), fp)) != NULL) {
		uintptr_t begin, end;

		if (sscanf(buf, "%" SCNxPTR "-%" SCNxPTR, &begin, &end) == 2) {
			if (((uintptr_t)fault_addr >= begin) &&
			    ((uintptr_t)fault_addr <= end)) {
				char *ptr1, *ptr2;

				/* truncate to first \n found */
				ptr1 = strchr(buf, (int)'\n');
				if (ptr1)
					*ptr1 = '\0';

				/* squeeze out duplicated spaces */
				for (ptr1 = buf, ptr2 = buf; *ptr1; ptr1++) {
					if ((*ptr1 == ' ') && (*(ptr1 + 1) == ' '))
						continue;
					*ptr2 = *ptr1;
					ptr2++;

				}
				*ptr2 = '\0';
				stress_dbg("stress-ng: info: %s\n", buf);
				break;
			}
		}
	}
	(void)fclose(fp);
#else
	(void)fault_addr;
#endif
}

/*
 *  stress_get_signal_name()
 *	return string version of signal number, NULL if not found
 */
const char PURE *stress_get_signal_name(const int signum)
{
	size_t i;

#if defined(SIGRTMIN) &&	\
    defined(SIGRTMAX)
	if ((signum >= SIGRTMIN) && (signum <= SIGRTMAX)) {
		static char sigrtname[10];

		(void)snprintf(sigrtname, sizeof(sigrtname), "SIGRT%d",
			signum - SIGRTMIN);
		return sigrtname;
	}
#endif
	for (i = 0; i < SIZEOF_ARRAY(sig_names); i++) {
		if (signum == sig_names[i].signum)
			return sig_names[i].name;
	}
	return NULL;
}

/*
 *  stress_strsignal()
 *	signum to human readable string
 */
const char *stress_strsignal(const int signum)
{
	static char buffer[40];
	const char *str = stress_get_signal_name(signum);

	if (str)
		(void)snprintf(buffer, sizeof(buffer), "signal %d '%s'",
			signum, str);
	else
		(void)snprintf(buffer, sizeof(buffer), "signal %d", signum);
	return buffer;
}

/*
 * stress_mask_longjump_signals()
 *	mask all signals which may have handlers which use siglongjmp()
 */
void stress_mask_longjump_signals(sigset_t *set)
{
#if defined(SIGBUS)
	sigaddset(set, SIGBUS);
#endif
#if defined(SIGFPE)
	sigaddset(set, SIGFPE);
#endif
#if defined(SIGILL)
	sigaddset(set, SIGILL);
#endif
#if defined(SIGSEGV)
	sigaddset(set, SIGSEGV);
#endif
#if defined(SIGXFSZ)
	sigaddset(set, SIGXFSZ);
#endif
#if defined(SIGXCPU)
	sigaddset(set, SIGXCPU);
#endif
#if defined(SIGRTMIN)
	sigaddset(set, SIGRTMIN);
#endif
}

/*
 *  stress_sighandler()
 *	set signal handler in generic way
 */
int stress_sighandler(
	const char *name,
	const int signum,
	void (*handler)(int),
	struct sigaction *orig_action)
{
	struct sigaction new_action;
#if defined(HAVE_SIGALTSTACK)
	{
		static uint8_t *stack = NULL;

		if (stack == NULL) {
			/* Allocate stack, we currently leak this */
			stack = (uint8_t *)stress_mmap_populate(NULL, STRESS_SIGSTKSZ,
					PROT_READ | PROT_WRITE,
					MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
			if (stack == MAP_FAILED) {
				pr_inf("%s: sigaction %s: cannot allocated signal stack, "
					"errno=%d (%s)\n",
					name, stress_strsignal(signum),
					errno, strerror(errno));
				return -1;
			}
			stress_set_vma_anon_name(stack, STRESS_SIGSTKSZ, "sigstack");
			if (stress_sigaltstack(stack, STRESS_SIGSTKSZ) < 0)
				return -1;
		}
	}
#endif
	(void)shim_memset(&new_action, 0, sizeof new_action);
	new_action.sa_handler = handler;
	(void)sigemptyset(&new_action.sa_mask);
	/*
	 *  Signals intended to stop stress-ng should never be interrupted
	 *  by a signal with a handler which may not return to the caller.
	 */
	if ((signum == SIGALRM) || (signum == SIGINT) || (signum == SIGHUP)
		|| (signum == SIGTERM))
		stress_mask_longjump_signals(&new_action.sa_mask);
	new_action.sa_flags = SA_NOCLDSTOP;
#if defined(HAVE_SIGALTSTACK)
	new_action.sa_flags |= SA_ONSTACK;
#endif

	if (sigaction(signum, &new_action, orig_action) < 0) {
		pr_fail("%s: sigaction %s, errno=%d (%s)\n",
			name, stress_strsignal(signum), errno, strerror(errno));
		return -1;
	}
	return 0;
}

/*  stress_sigchld_helper_handler()
 *	parent is informed child has terminated and
 * 	it's time to stop
 */
static void MLOCKED_TEXT stress_sigchld_helper_handler(int signum)
{
	if (signum == SIGCHLD)
		stress_continue_set_flag(false);
}

/*
 *  stress_sigchld_set_handler()
 *	set sigchld handler
 */
int stress_sigchld_set_handler(stress_args_t *args)
{
	return stress_sighandler(args->name, SIGCHLD, stress_sigchld_helper_handler, NULL);
}

/*
 *  stress_sighandler_default
 *	restore signal handler to default handler
 */
int stress_sighandler_default(const int signum)
{
	struct sigaction new_action;

	(void)shim_memset(&new_action, 0, sizeof new_action);
	new_action.sa_handler = SIG_DFL;

	return sigaction(signum, &new_action, NULL);
}

/*
 *  stress_handle_stop_stressing()
 *	set flag to indicate to stressor to stop stressing
 */
void stress_handle_stop_stressing(const int signum)
{
	(void)signum;

	stress_continue_set_flag(false);
	/*
	 * Trigger another SIGARLM until stressor gets the message
	 * that it needs to terminate
	 */
	(void)alarm(1);
}

/*
 *  stress_sig_stop_stressing()
 *	install a handler that sets the global flag
 *	to indicate to a stressor to stop stressing
 */
int stress_sig_stop_stressing(const char *name, const int sig)
{
	return stress_sighandler(name, sig, stress_handle_stop_stressing, NULL);
}

/*
 *  stress_sigrestore()
 *	restore a handler
 */
int stress_sigrestore(
	const char *name,
	const int signum,
	struct sigaction *orig_action)
{
	if (UNLIKELY(sigaction(signum, orig_action, NULL) < 0)) {
		pr_fail("%s: sigaction %s restore, errno=%d (%s)\n",
			name, stress_strsignal(signum), errno, strerror(errno));
		return -1;
	}
	return 0;
}

/*
 *  stress_sigalrm_pending()
 *	return true if SIGALRM is pending
 */
bool stress_sigalrm_pending(void)
{
	sigset_t set;

	(void)sigemptyset(&set);
	(void)sigpending(&set);
	return sigismember(&set, SIGALRM);

}

/*
 *  stress_sig_handler_exit()
 *	signal handler that exits a process via _exit(0) for
 *	immediate dead stop termination.
 */
void NORETURN MLOCKED_TEXT stress_sig_handler_exit(int signum)
{
	(void)signum;

	_exit(0);
}

/*
 *  stress_sighandler_nop()
 *	no-operation signal handler
 */
void stress_sighandler_nop(int sig)
{
	(void)sig;
}

/*
 *  stress_catch_sig_si_code()
 *	convert signal and si_code into human readable form
 */
static const CONST char *stress_catch_sig_si_code(const int sig, const int sig_code)
{
	static const char unknown[] = "UNKNOWN";

	switch (sig) {
	case SIGILL:
		switch (sig_code) {
#if defined(ILL_ILLOPC)
		case ILL_ILLOPC:
			return "ILL_ILLOPC";
#endif
#if defined(ILL_ILLOPN)
		case ILL_ILLOPN:
			return "ILL_ILLOPN";
#endif
#if defined(ILL_ILLADR)
		case ILL_ILLADR:
			return "ILL_ILLADR";
#endif
#if defined(ILL_ILLTRP)
		case ILL_ILLTRP:
			return "ILL_ILLTRP";
#endif
#if defined(ILL_PRVOPC)
		case ILL_PRVOPC:
			return "ILL_PRVOPC";
#endif
#if defined(ILL_PRVREG)
		case ILL_PRVREG:
			return "ILL_PRVREG";
#endif
#if defined(ILL_COPROC)
		case ILL_COPROC:
			return "ILL_COPROC";
#endif
#if defined(ILL_BADSTK)
		case ILL_BADSTK:
			return "ILL_BADSTK";
#endif
		default:
			return unknown;
		}
		break;
	case SIGSEGV:
		switch (sig_code) {
#if defined(SEGV_MAPERR)
		case SEGV_MAPERR:
			return "SEGV_MAPERR";
#endif
#if defined(SEGV_ACCERR)
		case SEGV_ACCERR:
			return "SEGV_ACCERR";
#endif
#if defined(SEGV_BNDERR)
		case SEGV_BNDERR:
			return "SEGV_BNDERR";
#endif
#if defined(SEGV_PKUERR)
		case SEGV_PKUERR:
			return "SEGV_PKUERR";
#endif
		default:
			return unknown;
		}
		break;
	}
	return unknown;
}

/*
 *  stress_catch_sig_handler()
 *	handle signal, dump 16 bytes before and after the illegal opcode
 *	and terminate immediately to avoid any recursive signal handling
 */
static void stress_catch_sig_handler(
	int sig,
	siginfo_t *info,
	void *ucontext,
	const int sig_expected,
	const char *sig_expected_name)
{
	static bool handled = false;

	(void)sig;
	(void)ucontext;

	if (handled)
		_exit(EXIT_FAILURE);
	handled = true;
	if (sig == sig_expected) {
		if (info) {
			stress_dbg("caught %s, address 0x%16.16" PRIxPTR " (%s)\n",
				sig_expected_name, (uintptr_t)info->si_addr,
				stress_catch_sig_si_code(sig, info->si_code));
			stress_dump_readable_data((uint8_t *)info->si_addr);
			stress_dump_map_info((uint8_t *)info->si_addr);
		} else {
			stress_dbg("caught %s, unknown address\n", sig_expected_name);
		}
	} else {
		if (info) {
			stress_dbg("caught unexpected SIGNAL %d, address 0x%16.16" PRIxPTR "\n",
				sig, (uintptr_t)info->si_addr);
			stress_dump_readable_data((uint8_t *)info->si_addr);
			stress_dump_map_info((uint8_t *)info->si_addr);
		} else {
			stress_dbg("caught unexpected SIGNAL %d, unknown address\n", sig);
		}
	}
	/* Big fat abort */
	_exit(EXIT_FAILURE);
}

/*
 *  stress_catch_sigill_handler()
 *	handler for SIGILL
 */
static void stress_catch_sigill_handler(
	int sig,
	siginfo_t *info,
	void *ucontext)
{
	stress_catch_sig_handler(sig, info, ucontext, SIGILL, "SIGILL");
}

/*
 *  stress_catch_sigsegv_handler()
 *	handler for SIGSEGV
 */
static void stress_catch_sigsegv_handler(
	int sig,
	siginfo_t *info,
	void *ucontext)
{
	stress_catch_sig_handler(sig, info, ucontext, SIGSEGV, "SIGSEGV");
}

/*
 *  stress_catch_sig()
 *	add signal handler to catch and dump illegal instructions,
 *	this is mainly to be used by any code using target clones
 *	just in case the compiler emits code that the target cannot
 *	actually execute.
 */
static void stress_catch_sig(
	const int sig,
	void (*handler)(int sig, siginfo_t *info, void *ucontext)
)
{
	struct sigaction sa;

	(void)shim_memset(&sa, 0, sizeof(sa));

	sa.sa_sigaction = handler;
#if defined(SA_SIGINFO)
	sa.sa_flags = SA_SIGINFO;
#endif
	(void)sigaction(sig, &sa, NULL);
}

/*
 *  stress_catch_sigill()
 *	catch and dump SIGILL signals
 */
void stress_catch_sigill(void)
{
	stress_catch_sig(SIGILL, stress_catch_sigill_handler);
}

/*
 *  stress_catch_sigsegv()
 *	catch and dump SIGSEGV signals
 */
void stress_catch_sigsegv(void)
{
	stress_catch_sig(SIGSEGV, stress_catch_sigsegv_handler);
}
