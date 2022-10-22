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

#if defined(HAVE_TERMIO_H)
#include <termio.h>
#endif

#define MIN_PTYS		(8)
#define MAX_PTYS		(65536)
#define DEFAULT_PTYS		(65536)

static const stress_help_t help[] = {
	{ NULL,	"pty N",	"start N workers that exercise pseudoterminals" },
	{ NULL,	"pty-max N",	"attempt to open a maximum of N ptys" },
	{ NULL,	"pty-ops N",	"stop pty workers after N pty bogo operations" },
	{ NULL,	NULL,          NULL }
};

#if defined(HAVE_TERMIOS_H) &&	\
    defined(HAVE_TERMIO_H) && \
    defined(HAVE_PTSNAME)

typedef struct {
	char *followername;
	int leader;
	int follower;
} stress_pty_info_t;

#endif

/*
 *  stress_set_pty_max()
 *	set ptr maximum
 */
static int stress_set_pty_max(const char *opt)
{
	uint64_t pty_max;

	pty_max = stress_get_uint64(opt);
	stress_check_range("pty-max", pty_max,
		MIN_PTYS, MAX_PTYS);
	return stress_set_setting("pty-max", TYPE_ID_UINT64, &pty_max);
}

static const stress_opt_set_func_t opt_set_funcs[] = {
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
static int stress_pty(const stress_args_t *args)
{
	uint64_t pty_max = DEFAULT_PTYS;
	stress_pty_info_t *ptys;
	const pid_t pid = getpid();

	(void)stress_get_setting("pty-max", &pty_max);

	ptys = calloc(pty_max, sizeof(*ptys));
	if (!ptys) {
		pr_inf("%s: allocation of pty array failed: %d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		size_t i, n;

		for (n = 0; n < pty_max; n++) {
			ptys[n].follower = -1;
			ptys[n].leader = open("/dev/ptmx", O_RDWR);
			if (ptys[n].leader < 0) {
				if ((errno != ENOMEM) &&
				    (errno != ENOSPC) &&
				    (errno != EIO) &&
				    (errno != EMFILE)) {
					pr_fail("%s: open /dev/ptmx failed, errno=%d (%s)\n",
						args->name, errno, strerror(errno));
					goto clean;
				}
			} else {
				ptys[n].followername = ptsname(ptys[n].leader);
				if (!ptys[n].followername) {
					pr_fail("%s: ptsname failed, errno=%d (%s)\n",
						args->name, errno, strerror(errno));
					goto clean;
				}
				if (grantpt(ptys[n].leader) < 0) {
					pr_fail("%s: grantpt failed, errno=%d (%s)\n",
						args->name, errno, strerror(errno));
					goto clean;
				}
				if (unlockpt(ptys[n].leader) < 0) {
					pr_fail("%s: unlockpt failed, errno=%d (%s)\n",
						args->name, errno, strerror(errno));
					goto clean;
				}
				ptys[n].follower = open(ptys[n].followername, O_RDWR);
				if (ptys[n].follower < 0) {
					if (errno != EMFILE) {
						pr_fail("%s: open %s failed, errno=%d (%s)\n",
							args->name, ptys[n].followername, errno, strerror(errno));
						goto clean;
					}
				}
			}
			if (!keep_stressing_flag())
				goto clean;
		}
		/*
		 *  ... and exercise ioctls ...
		 */
		for (i = 0; i < n; i++) {
			if ((ptys[i].leader < 0) || (ptys[i].follower < 0))
				continue;

			(void)stress_read_fdinfo(pid, ptys[i].leader);
			(void)stress_read_fdinfo(pid, ptys[i].follower);

#if defined(HAVE_TCGETATTR)
			{
				struct termios ios;

				if (tcgetattr(ptys[i].leader, &ios) < 0) {
					pr_fail("%s: tcgetattr on leader pty failed, errno=%d (%s)\n",
						args->name, errno, strerror(errno));
				}
			}
#endif
#if defined(HAVE_TCDRAIN)
			{
				if (tcdrain(ptys[i].follower) < 0) {
					pr_fail("%s: tcdrain on follower pty failed, errno=%d (%s)\n",
						args->name, errno, strerror(errno));
				}
			}
#endif
#if defined(HAVE_TCFLUSH)
			{
#if defined(TCIFLUSH)
				if (tcflush(ptys[i].follower, TCIFLUSH) < 0) {
					pr_fail("%s: tcflush TCIFLUSH on follower pty failed, errno=%d (%s)\n",
						args->name, errno, strerror(errno));
				}
#endif
#if defined(TCOFLUSH)
				if (tcflush(ptys[i].follower, TCOFLUSH) < 0) {
					pr_fail("%s: tcflush TCOFLUSH on follower pty failed, errno=%d (%s)\n",
						args->name, errno, strerror(errno));
				}
#endif
#if defined(TCIOFLUSH)
				if (tcflush(ptys[i].follower, TCIOFLUSH) < 0) {
					pr_fail("%s: tcflush TCOOFLUSH on follower pty failed, errno=%d (%s)\n",
						args->name, errno, strerror(errno));
				}
#endif
			}
#endif
#if defined(HAVE_TCFLOW)
#if defined(TCOOFF) && \
    defined(TCOON)
			{
				if (tcflow(ptys[i].follower, TCOOFF) < 0) {
					pr_fail("%s: tcflow TCOOFF on follower pty failed, errno=%d (%s)\n",
						args->name, errno, strerror(errno));
				}
				if (tcflow(ptys[i].follower, TCOON) < 0) {
					pr_fail("%s: tcflow TCOON on follower pty failed, errno=%d (%s)\n",
						args->name, errno, strerror(errno));
				}
			}
#endif
#if defined(TCIOFF) && \
    defined(TCION)
			{
				if (tcflow(ptys[i].follower, TCIOFF) < 0) {
					pr_fail("%s: tcflow TCIOFF on follower pty failed, errno=%d (%s)\n",
						args->name, errno, strerror(errno));
				}
				if (tcflow(ptys[i].follower, TCION) < 0) {
					pr_fail("%s: tcflow TCION on follower pty failed, errno=%d (%s)\n",
						args->name, errno, strerror(errno));
				}
			}
#endif
#endif
#if defined(TCGETS)
			{
				struct termios ios;

				if ((ioctl(ptys[i].follower, TCGETS, &ios) < 0) &&
				    (errno != EINTR))
					pr_fail("%s: ioctl TCGETS on follower pty failed, errno=%d (%s)\n",
						args->name, errno, strerror(errno));
			}
#endif
#if defined(TCSETS)
			{
				struct termios ios;

				if ((ioctl(ptys[i].follower, TCSETS, &ios) < 0) &&
				    (errno != EINTR))
					pr_fail("%s: ioctl TCSETS on follower pty failed, errno=%d (%s)\n",
						args->name, errno, strerror(errno));
			}
#endif
#if defined(TCSETSW)
			{
				struct termios ios;

				if ((ioctl(ptys[i].follower, TCSETSW, &ios) < 0) &&
				    (errno != EINTR))
					pr_fail("%s: ioctl TCSETSW on follower pty failed, errno=%d (%s)\n",
						args->name, errno, strerror(errno));
			}
#endif
#if defined(TCSETSF)
			{
				struct termios ios;

				if ((ioctl(ptys[i].follower, TCSETSF, &ios) < 0) &&
				    (errno != EINTR))
					pr_fail("%s: ioctl TCSETSF on follower pty failed, errno=%d (%s)\n",
						args->name, errno, strerror(errno));
			}
#endif
#if defined(TCGETA)
			{
				struct termio io;

				if ((ioctl(ptys[i].follower, TCGETA, &io) < 0) &&
				    (errno != EINTR))
					pr_fail("%s: ioctl TCGETA on follower pty failed, errno=%d (%s)\n",
						args->name, errno, strerror(errno));
			}
#endif
#if defined(TCSETA)
			{
				struct termio io;

				if ((ioctl(ptys[i].follower, TCSETA, &io) < 0) &&
				    (errno != EINTR))
					pr_fail("%s: ioctl TCSETA on follower pty failed, errno=%d (%s)\n",
						args->name, errno, strerror(errno));
			}
#endif
#if defined(TCSETAW)
			{
				struct termio io;

				if ((ioctl(ptys[i].follower, TCSETAW, &io) < 0) &&
				    (errno != EINTR))
					pr_fail("%s: ioctl TCSETAW on follower pty failed, errno=%d (%s)\n",
						args->name, errno, strerror(errno));
			}
#endif
#if defined(TCSETAF)
			{
				struct termio io;

				if ((ioctl(ptys[i].follower, TCSETAF, &io) < 0) &&
				    (errno != EINTR))
					pr_fail("%s: ioctl TCSETAF on follower pty failed, errno=%d (%s)\n",
						args->name, errno, strerror(errno));
			}
#endif
#if defined(TIOCGLCKTRMIOS)
			{
				struct termios ios;

				if ((ioctl(ptys[i].follower, TIOCGLCKTRMIOS, &ios) < 0) &&
				    (errno != EINTR))
					pr_fail("%s: ioctl TIOCGLCKTRMIOS on follower pty failed, errno=%d (%s)\n",
						args->name, errno, strerror(errno));
			}
#endif
#if defined(TIOCGWINSZ)
			{
				struct winsize ws;

				if ((ioctl(ptys[i].follower, TIOCGWINSZ, &ws) < 0) &&
				    (errno != EINTR))
					pr_fail("%s: ioctl TIOCGWINSZ on follower pty failed, errno=%d (%s)\n",
						args->name, errno, strerror(errno));
			}
#endif
#if defined(TIOCSWINSZ)
			{
				struct winsize ws;

				if ((ioctl(ptys[i].follower, TIOCSWINSZ, &ws) < 0) &&
				    (errno != EINTR))
					pr_fail("%s: ioctl TIOCSWINSZ on follower pty failed, errno=%d (%s)\n",
						args->name, errno, strerror(errno));
			}
#endif
#if defined(FIONREAD)
			{
				int arg;

				if ((ioctl(ptys[i].follower, FIONREAD, &arg) < 0) &&
				    (errno != EINTR))
					pr_fail("%s: ioctl FIONREAD on follower pty failed, errno=%d (%s)\n",
						args->name, errno, strerror(errno));
			}
#endif
#if defined(TIOCINQ)
			{
				int arg;

				if ((ioctl(ptys[i].follower, TIOCINQ, &arg) < 0) &&
				    (errno != EINTR))
					pr_fail("%s: ioctl TIOCINQ on follower pty failed, errno=%d (%s)\n",
						args->name, errno, strerror(errno));
			}
#endif
#if defined(TIOCOUTQ)
			{
				int arg;

				if ((ioctl(ptys[i].follower, TIOCOUTQ, &arg) < 0) &&
				    (errno != EINTR))
					pr_fail("%s: ioctl TIOCOUTQ on follower pty failed, errno=%d (%s)\n",
						args->name, errno, strerror(errno));
			}
#endif
#if defined(TIOCGPTLCK)
			{
				int ret, locked = 0;

				ret = ioctl(ptys[i].leader, TIOCGPTLCK, &locked);
#if defined(TIOCSPTLCK)
				if (ret == 0)
					ret = ioctl(ptys[i].leader, TIOCSPTLCK, &locked);
#endif

				(void)ret;
			}
#endif
#if defined(TIOCGPTN)
			{
				unsigned int ptynum = 0;

				(void)ioctl(ptys[i].leader, TIOCGPTN, &ptynum);	/* BSD */
			}
#endif
#if defined(TIOCGPKT)
			{
				int val, ret;

				ret = ioctl(ptys[i].leader, TIOCGPKT, &val);
#if defined(TIOCPKT)
				if (ret == 0)
					ret = ioctl(ptys[i].leader, TIOCPKT, &val);
#endif
				(void)ret;
			}
#endif
			if (!keep_stressing_flag())
				goto clean;
		}
clean:
		/*
		 *  and close
		 */
		for (i = 0; i < n; i++) {
			if (ptys[i].follower != -1)
				(void)close(ptys[i].follower);
			if (ptys[i].leader != -1)
				(void)close(ptys[i].leader);
		}
		inc_counter(args);
	} while (keep_stressing(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	free(ptys);

	return EXIT_SUCCESS;
}

stressor_info_t stress_pty_info = {
	.stressor = stress_pty,
	.class = CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.verify = VERIFY_ALWAYS,
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
