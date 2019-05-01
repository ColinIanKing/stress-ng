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
	{ NULL,	"pty N",	"start N workers that exercise pseudoterminals" },
	{ NULL,	"pty-ops N",	"stop pty workers after N pty bogo operations" },
	{ NULL,	"pty-max N",	"attempt to open a maximum of N ptys" },
	{ NULL,	NULL,          NULL }
};

#if defined(HAVE_TERMIOS_H) &&	\
    defined(HAVE_TERMIO_H) && \
    defined(HAVE_PTSNAME)

typedef struct {
	char *slavename;
	int master;
	int slave;
} pty_info_t;

#endif

/*
 *  stress_set_pty_max()
 *	set ptr maximum
 */
static int stress_set_pty_max(const char *opt)
{
	uint64_t pty_max;

	pty_max = get_uint64(opt);
	check_range("pty-max", pty_max,
		MIN_PTYS, MAX_PTYS);
	return set_setting("pty-max", TYPE_ID_UINT64, &pty_max);
}

static const opt_set_func_t opt_set_funcs[] = {
	{ OPT_pty_max,	stress_set_pty_max },
	{ 0,		NULL }
};

#if defined(HAVE_TERMIOS_H) &&	\
    defined(HAVE_TERMIO_H) &&	\
    defined(HAVE_PTSNAME)

/*
 *  stress_pty
 *	stress pty handling
 */
static int stress_pty(const args_t *args)
{
	uint64_t pty_max = DEFAULT_PTYS;

	(void)get_setting("pty-max", &pty_max);

	do {
		size_t i, n;
		pty_info_t ptys[pty_max];

		(void)memset(ptys, 0, sizeof ptys);

		for (n = 0; n < pty_max; n++) {
			ptys[n].slave = -1;
			ptys[n].master = open("/dev/ptmx", O_RDWR);
			if (ptys[n].master < 0) {
				if ((errno != ENOMEM) &&
				    (errno != ENOSPC) &&
				    (errno != EIO) &&
				    (errno != EMFILE)) {
					pr_fail_err("open /dev/ptmx");
					goto clean;
				}
			} else {
				ptys[n].slavename = ptsname(ptys[n].master);
				if (!ptys[n].slavename) {
					pr_fail_err("ptsname");
					goto clean;
				}
				if (grantpt(ptys[n].master) < 0) {
					pr_fail_err("grantpt");
					goto clean;
				}
				if (unlockpt(ptys[n].master) < 0) {
					pr_fail_err("unlockpt");
					goto clean;
				}
				ptys[n].slave = open(ptys[n].slavename, O_RDWR);
				if (ptys[n].slave < 0) {
					if (errno != EMFILE) {
						pr_fail_err("open slave pty");
						goto clean;
					}
				}
			}
			if (!g_keep_stressing_flag)
				goto clean;
		}
		/*
		 *  ... and exercise ioctls ...
		 */
		for (i = 0; i < n; i++) {
			if ((ptys[i].master < 0) || (ptys[i].slave < 0))
				continue;
#if defined(TCGETS)
			{
				struct termios ios;

				if (ioctl(ptys[i].slave, TCGETS, &ios) < 0)
					pr_fail_err("ioctl TCGETS on slave pty");	
			}
#endif
#if defined(TCSETS)
			{
				struct termios ios;

				if (ioctl(ptys[i].slave, TCSETS, &ios) < 0)
					pr_fail_err("ioctl TCSETS on slave pty");
			}
#endif
#if defined(TCSETSW)
			{
				struct termios ios;

				if (ioctl(ptys[i].slave, TCSETSW, &ios) < 0)
					pr_fail_err("ioctl TCSETSW on slave pty");
			}
#endif
#if defined(TCSETSF)
			{
				struct termios ios;

				if (ioctl(ptys[i].slave, TCSETSF, &ios) < 0)
					pr_fail_err("ioctl TCSETSF on slave pty");
			}
#endif
#if defined(TCGETA)
			{
				struct termio io;

				if (ioctl(ptys[i].slave, TCGETA, &io) < 0)
					pr_fail_err("ioctl TCGETA on slave pty");
			}
#endif
#if defined(TCSETA)
			{
				struct termio io;

				if (ioctl(ptys[i].slave, TCSETA, &io) < 0)
					pr_fail_err("ioctl TCSETA on slave pty");
			}
#endif
#if defined(TCSETAW)
			{
				struct termio io;

				if (ioctl(ptys[i].slave, TCSETAW, &io) < 0)
					pr_fail_err("ioctl TCSETAW on slave pty");
			}
#endif
#if defined(TCSETAF)
			{
				struct termio io;

				if (ioctl(ptys[i].slave, TCSETAF, &io) < 0)
					pr_fail_err("ioctl TCSETAF on slave pty");
			}
#endif
#if defined(TIOCGLCKTRMIOS)
			{
				struct termios ios;

				if (ioctl(ptys[i].slave, TIOCGLCKTRMIOS, &ios) < 0)
					pr_fail_err("ioctl TIOCGLCKTRMIOS on slave pty");
			}
#endif
#if defined(TIOCGLCKTRMIOS)
			{
				struct termios ios;

				if (ioctl(ptys[i].slave, TIOCGLCKTRMIOS, &ios) < 0)
					pr_fail_err("ioctl TIOCGLCKTRMIOS on slave pty");
			}
#endif
#if defined(TIOCGWINSZ)	
			{
				struct winsize ws;

				if (ioctl(ptys[i].slave, TIOCGWINSZ, &ws) < 0)
					pr_fail_err("ioctl TIOCGWINSZ on slave pty");
			}
#endif
#if defined(TIOCSWINSZ)
			{
				struct winsize ws;

				if (ioctl(ptys[i].slave, TIOCSWINSZ, &ws) < 0)
					pr_fail_err("ioctl TIOCSWINSZ on slave pty");
			}
#endif
#if defined(FIONREAD)
			{
				int arg;

				if (ioctl(ptys[i].slave, FIONREAD, &arg) < 0)
					pr_fail_err("ioctl FIONREAD on slave pty");
			}
#endif
#if defined(TIOCINQ)
			{
				int arg;

				if (ioctl(ptys[i].slave, TIOCINQ, &arg) < 0)
					pr_fail_err("ioctl TIOCINQ on slave pty");
			}
#endif
#if defined(TIOCOUTQ)
			{
				int arg;

				if (ioctl(ptys[i].slave, TIOCOUTQ, &arg) < 0)
					pr_fail_err("ioctl TIOCOUTQ on slave pty");
			}
#endif

			if (!g_keep_stressing_flag)
				goto clean;
		}
clean:
		/*
		 *  and close
		 */
		for (i = 0; i < n; i++) {
			if (ptys[i].slave != -1)
				(void)close(ptys[i].slave);
			if (ptys[i].master != -1)
				(void)close(ptys[i].master);
		}
		inc_counter(args);
	} while (keep_stressing());

	return EXIT_SUCCESS;
}

stressor_info_t stress_pty_info = {
	.stressor = stress_pty,
	.class = CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
#else
stressor_info_t stress_pty_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
#endif
