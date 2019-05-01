/*
 * Copyright (C) 2013-2019 Canonical, Ltd.
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

static const help_t help[] = {
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
	SIGHUP
#endif
};

static const char *dev_watchdog = "/dev/watchdog";
static int fd;

static void stress_watchdog_magic_close(void)
{
	/*
	 *  Some watchdog drivers support the magic close option
	 *  where writing "V" will forcefully disable the watchdog
	 */
	if (fd >= 0) {
		int ret;

		ret = write(fd, "V", 1);
		(void)ret;
	}
}

static void MLOCKED_TEXT stress_watchdog_handler(int signum)
{
	(void)signum;

	stress_watchdog_magic_close();

	/* trigger early termination */
	g_keep_stressing_flag = 0;

	/* jump back */
        siglongjmp(jmp_env, 1);
}

/*
 *  stress_watchdog()
 *	stress /dev/watchdog
 */
static int stress_watchdog(const args_t *args)
{
	int ret, rc = EXIT_SUCCESS;
	size_t i;

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
			pr_inf("%s: %s does not exist, skipping test\n",
				args->name, dev_watchdog);
			return EXIT_SUCCESS;
		} else {
			pr_inf("%s: cannot access %s, errno=%d (%s), skipping test\n",
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

	while (keep_stressing()) {
		fd = open(dev_watchdog, O_RDWR);

		/* Mutiple stressors can lock the device, so retry */
		if (fd < 0) {
			struct timespec tv;

			tv.tv_sec = 0;
			tv.tv_nsec = 10000;
			(void)nanosleep(&tv, NULL);
			continue;
		}

		stress_watchdog_magic_close();

#if defined(WDIOC_KEEPALIVE)
		ret = ioctl(fd, WDIOC_KEEPALIVE, 0);
		(void)ret;
#endif

#if defined(WDIOC_GETTIMEOUT)
		{
			int timeout;

			ret = ioctl(fd, WDIOC_GETTIMEOUT, &timeout);
			(void)ret;
		}
#endif

#if defined(WDIOC_GETPRETIMEOUT)
		{
			int timeout;

			ret = ioctl(fd, WDIOC_GETPRETIMEOUT, &timeout);
			(void)ret;
		}
#endif

#if defined(WDIOC_GETPRETIMEOUT)
		{
			int timeout;

			ret = ioctl(fd, WDIOC_GETTIMELEFT, &timeout);
			(void)ret;
		}
#endif

#if defined(WDIOC_GETSUPPORT)
		{
			struct watchdog_info ident;

			ret = ioctl(fd, WDIOC_GETSUPPORT, &ident);
			(void)ret;
		}
#endif

#if defined(WDIOC_GETSTATUS)
		{
			int flags;

			ret = ioctl(fd, WDIOC_GETSTATUS, &flags);
			(void)ret;
		}
#endif

#if defined(WDIOC_GETBOOTSTATUS)
		{
			int flags;

			ret = ioctl(fd, WDIOC_GETBOOTSTATUS, &flags);
			(void)ret;
		}
#endif

#if defined(WDIOC_GETTEMP)
		{
			int temperature;

			ret = ioctl(fd, WDIOC_GETBOOTSTATUS, &temperature);
			(void)ret;
		}
#endif
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
		inc_counter(args);
	}

	return rc;
}

stressor_info_t stress_watchdog_info = {
	.stressor = stress_watchdog,
	.class = CLASS_VM | CLASS_OS | CLASS_PATHOLOGICAL,
	.help = help
};
#else
stressor_info_t stress_watchdog_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_VM | CLASS_OS | CLASS_PATHOLOGICAL,
	.help = help
};
#endif
