// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
 *
 */
#include "stress-ng.h"

#if defined(HAVE_LINUX_WATCHDOG_H)
#include <linux/watchdog.h>
#endif

static const stress_help_t help[] = {
	{ NULL,	"watchdog N",	  "start N workers that exercise /dev/watchdog" },
	{ NULL,	"watchdog-ops N", "stop after N bogo watchdog operations" },
	{ NULL,	NULL,		  NULL }
};

#if defined(HAVE_LINUX_WATCHDOG_H)

static sigjmp_buf jmp_env;

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
};

static const char dev_watchdog[] = "/dev/watchdog";
static int fd;

static void stress_watchdog_magic_close(void)
{
	/*
	 *  Some watchdog drivers support the magic close option
	 *  where writing "V" will forcefully disable the watchdog
	 */
	if (fd >= 0) {
		VOID_RET(ssize_t, write(fd, "V", 1));
	}
}

static void /*NORETURN*/ MLOCKED_TEXT stress_watchdog_handler(int signum)
{
	(void)signum;

	stress_watchdog_magic_close();

	/* trigger early termination */
	stress_continue_set_flag(false);
}

/*
 *  stress_watchdog()
 *	stress /dev/watchdog
 */
static int stress_watchdog(const stress_args_t *args)
{
	int ret;
	size_t i;
	NOCLOBBER int rc = EXIT_SUCCESS;

	fd = -1;
	for (i = 0; i < SIZEOF_ARRAY(sigs); i++) {
		if (stress_sighandler(args->name, sigs[i], stress_watchdog_handler, NULL) < 0)
			return EXIT_FAILURE;
	}

	/*
	 *  Sanity check for existence and r/w permissions
	 *  on /dev/shm, it may not be configure for the
	 *  kernel, so don't make it a failure of it does
	 *  not exist or we can't access it.
	 */
	if (access(dev_watchdog, R_OK | W_OK) < 0) {
		if (errno == ENOENT) {
			if (args->instance == 0)
				pr_inf_skip("%s: %s does not exist, skipping test\n",
					args->name, dev_watchdog);
			return EXIT_SUCCESS;
		} else {
			if (args->instance == 0)
				pr_inf_skip("%s: cannot access %s, errno=%d (%s), skipping test\n",
					args->name, dev_watchdog, errno, strerror(errno));
			return EXIT_SUCCESS;
		}
	}

	fd = 0;
	ret = sigsetjmp(jmp_env, 1);
	if (ret) {
		/* We got interrupted, so abort cleanly */
		if (fd >= 0) {
			stress_watchdog_magic_close();
			(void)close(fd);
		}
		return EXIT_SUCCESS;
	}

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	while (stress_continue(args)) {
		fd = open(dev_watchdog, O_RDWR);

		/* Multiple stressors can lock the device, so retry */
		if (fd < 0) {
			struct timespec tv;

			tv.tv_sec = 0;
			tv.tv_nsec = 10000;
			(void)nanosleep(&tv, NULL);
			continue;
		}

		stress_watchdog_magic_close();

#if defined(WDIOC_KEEPALIVE)
		if (!stress_continue_flag())
			goto watchdog_close;
		VOID_RET(int, ioctl(fd, WDIOC_KEEPALIVE, 0));
#else
		UNEXPECTED
#endif

#if defined(WDIOC_GETTIMEOUT)
		{
			int timeout = 0;

			if (!stress_continue_flag())
				goto watchdog_close;
			if ((ioctl(fd, WDIOC_GETTIMEOUT, &timeout) == 0) &&
			    (timeout < 0)) {
				pr_fail("%s: ioctl WDIOC_GETTIMEOUT returned unexpected timeout value %d\n",
					args->name, timeout);
				rc = EXIT_FAILURE;
			}
		}
#else
		UNEXPECTED
#endif

#if defined(WDIOC_GETPRETIMEOUT)
		{
			int timeout = 0;

			if (!stress_continue_flag())
				goto watchdog_close;
			if ((ioctl(fd, WDIOC_GETPRETIMEOUT, &timeout) == 0) &&
			    (timeout < 0)) {
				pr_fail("%s: ioctl WDIOC_GETPRETIMEOUT returned unexpected timeout value %d\n",
					args->name, timeout);
				rc = EXIT_FAILURE;
			}
		}
#else
		UNEXPECTED
#endif

#if defined(WDIOC_GETTIMELEFT)
		{
			int timeout = 0;

			if (!stress_continue_flag())
				goto watchdog_close;
			if ((ioctl(fd, WDIOC_GETTIMELEFT, &timeout) == 0) &&
			    (timeout < 0)) {
				pr_fail("%s: ioctl WDIOC_GETTIMELEFT returned unexpected timeout value %d\n",
					args->name, timeout);
				rc = EXIT_FAILURE;
			}
		}
#else
		UNEXPECTED
#endif

#if defined(WDIOC_GETSUPPORT)
		{
			struct watchdog_info ident;

			if (!stress_continue_flag())
				goto watchdog_close;
			VOID_RET(int, ioctl(fd, WDIOC_GETSUPPORT, &ident));
		}
#else
		UNEXPECTED
#endif

#if defined(WDIOC_GETSTATUS)
		{
			int flags;

			if (!stress_continue_flag())
				goto watchdog_close;
			VOID_RET(int, ioctl(fd, WDIOC_GETSTATUS, &flags));
		}
#else
		UNEXPECTED
#endif

#if defined(WDIOC_GETBOOTSTATUS)
		{
			int flags;

			if (!stress_continue_flag())
				goto watchdog_close;
			VOID_RET(int, ioctl(fd, WDIOC_GETBOOTSTATUS, &flags));
		}
#else
		UNEXPECTED
#endif

#if defined(WDIOC_GETTEMP)
		{
			int temperature = 0;

			if (!stress_continue_flag())
				goto watchdog_close;
			if ((ioctl(fd, WDIOC_GETTEMP, &temperature) == 0) &&
			    (temperature < 0)) {
				pr_fail("%s: ioctl WDIOC_GETTEMP returned unexpected temperature value %d\n",
					args->name, temperature);
				rc = EXIT_FAILURE;
			}
		}
#else
		UNEXPECTED
#endif

watchdog_close:
		stress_watchdog_magic_close();
		ret = close(fd);
		fd = -1;
		if (ret < 0) {
			pr_fail("%s: cannot close %s, errno=%d (%s)\n",
				args->name, dev_watchdog, errno, strerror(errno));
			rc = EXIT_FAILURE;
			break;
		}
		(void)shim_sched_yield();
		stress_bogo_inc(args);
	}

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return rc;
}

stressor_info_t stress_watchdog_info = {
	.stressor = stress_watchdog,
	.class = CLASS_OS | CLASS_PATHOLOGICAL,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
stressor_info_t stress_watchdog_info = {
	.stressor = stress_unimplemented,
	.class = CLASS_OS | CLASS_PATHOLOGICAL,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without linux/watchdog.h"
};
#endif
