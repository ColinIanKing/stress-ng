/*
 * Copyright (C) 2013-2016 Canonical, Ltd.
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
#define _GNU_SOURCE

#include "stress-ng.h"

#if defined(STRESS_PTY)

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <termio.h>
#include <termios.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>

#define MAX_PTYS	(65536)

typedef struct {
	char *slavename;
	int master;
	int slave;
} pty_info_t;

/*
 *  stress_pty
 *	stress pyt handling
 */
int stress_pty(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	int rc = EXIT_FAILURE;

	(void)instance;

	do {
		size_t i, n;
		pty_info_t ptys[MAX_PTYS];

		memset(ptys, 0, sizeof ptys);

		for (n = 0; n < MAX_PTYS; n++) {
			ptys[n].slave = -1;
			ptys[n].master = open("/dev/ptmx", O_RDWR);
			if (ptys[n].master < 0) {
				if (errno != ENOSPC)
					pr_fail_err(name, "open /dev/ptmx");
				goto clean;
			}
			ptys[n].slavename = ptsname(ptys[n].master);
			if (!ptys[n].slavename) {
				pr_fail_err(name, "ptsname");
				goto clean;
			}
			if (grantpt(ptys[n].master) < 0) {
				pr_fail_err(name, "grantpt");
				goto clean;
			}
			if (unlockpt(ptys[n].master) < 0) {
				pr_fail_err(name, "unlockpt");
				goto clean;
			}
			ptys[n].slave = open(ptys[n].slavename, O_RDWR);
			if (ptys[n].slave < 0) {
				pr_fail_err(name, "open slave pty");
				goto clean;
			}
			if (!opt_do_run)
				goto clean;
		}
		/*
		 *  ... and exercise ioctls ...
		 */
		for (i = 0; i < n; i++) {
			struct termios ios;
			struct termio io;
			struct winsize ws;
			int arg;

#if defined(TCGETS)
			if (ioctl(ptys[i].slave, TCGETS, &ios) < 0)
				pr_fail_err(name, "ioctl TCGETS on slave pty");
#endif
#if defined(TCSETS)
			if (ioctl(ptys[i].slave, TCSETS, &ios) < 0)
				pr_fail_err(name, "ioctl TCSETS on slave pty");
#endif
#if defined(TCSETSW)
			if (ioctl(ptys[i].slave, TCSETSW, &ios) < 0)
				pr_fail_err(name, "ioctl TCSETSW on slave pty");
#endif
#if defined(TCSETSF)
			if (ioctl(ptys[i].slave, TCSETSF, &ios) < 0)
				pr_fail_err(name, "ioctl TCSETSF on slave pty");
#endif
#if defined(TCGETA)
			if (ioctl(ptys[i].slave, TCGETA, &io) < 0)
				pr_fail_err(name, "ioctl TCGETA on slave pty");
#endif
#if defined(TCSETA)
			if (ioctl(ptys[i].slave, TCSETA, &io) < 0)
				pr_fail_err(name, "ioctl TCSETA on slave pty");
#endif
#if defined(TCSETAW)
			if (ioctl(ptys[i].slave, TCSETAW, &io) < 0)
				pr_fail_err(name, "ioctl TCSETAW on slave pty");
#endif
#if defined(TCSETAF)
			if (ioctl(ptys[i].slave, TCSETAF, &io) < 0)
				pr_fail_err(name, "ioctl TCSETAF on slave pty");
#endif
#if defined(TIOCGLCKTRMIOS)
			if (ioctl(ptys[i].slave, TIOCGLCKTRMIOS, &ios) < 0)
				pr_fail_err(name, "ioctl TIOCGLCKTRMIOS on slave pty");
#endif
#if defined(TIOCGLCKTRMIOS)
			if (ioctl(ptys[i].slave, TIOCGLCKTRMIOS, &ios) < 0)
				pr_fail_err(name, "ioctl TIOCGLCKTRMIOS on slave pty");
#endif
#if defined(TIOCGWINSZ)
			if (ioctl(ptys[i].slave, TIOCGWINSZ, &ws) < 0)
				pr_fail_err(name, "ioctl TIOCGWINSZ on slave pty");
#endif
#if defined(TIOCSWINSZ)
			if (ioctl(ptys[i].slave, TIOCSWINSZ, &ws) < 0)
				pr_fail_err(name, "ioctl TIOCSWINSZ on slave pty");
#endif
#if defined(FIONREAD)
			if (ioctl(ptys[i].slave, FIONREAD, &arg) < 0)
				pr_fail_err(name, "ioctl FIONREAD on slave pty");
#endif
#if defined(TIOCINQ)
			if (ioctl(ptys[i].slave, TIOCINQ, &arg) < 0)
				pr_fail_err(name, "ioctl TIOCINQ on slave pty");
#endif
#if defined(TIOCOUTQ)
			if (ioctl(ptys[i].slave, TIOCOUTQ, &arg) < 0)
				pr_fail_err(name, "ioctl TIOCOUTQ on slave pty");
#endif

			if (!opt_do_run)
				goto clean;
		}
clean:
		/*
		 *  and close
		 */
		for (i = 0; i < n; i++) {
			if (ptys[i].slave != -1)
				(void)close(ptys[i].slave);
			(void)close(ptys[i].master);
		}
		(*counter)++;
	} while (opt_do_run && (!max_ops || *counter < max_ops));

	rc = EXIT_SUCCESS;

	return rc;
}

#endif
