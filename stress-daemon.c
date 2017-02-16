/*
 * Copyright (C) 2013-2017 Canonical, Ltd.
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

static MLOCKED void handle_daemon_sigalrm(int dummy)
{
	(void)dummy;

	g_keep_stressing_flag = false;
}

/*
 *  daemons()
 *	fork off a child and let the parent die
 */
static void daemons(const args_t *args, const int fd)
{
	int fds[3];

	if (stress_sighandler(args->name, SIGALRM, handle_daemon_sigalrm, NULL) < 0)
		goto err;
	if (setsid() < 0)
		goto err;
	(void)close(0);
	(void)close(1);
	(void)close(2);

	if ((fds[0] = open("/dev/null", O_RDWR)) < 0)
		goto err;
	if ((fds[1] = dup(0)) < 0)
		goto err0;
	if ((fds[2] = dup(0)) < 0)
		goto err1;

	while (g_keep_stressing_flag) {
		pid_t pid;

		pid = fork();
		if (pid < 0) {
			goto tidy;
		} else if (pid == 0) {
			/* Child */
			char buf[1] = { 0xff };
			ssize_t sz;
			if (chdir("/") < 0)
				goto err2;
			(void)umask(0);
			sz = write(fd, buf, sizeof(buf));
			if (sz != sizeof(buf))
				goto err2;

		} else {
			/* Parent, will be reaped by init */
			break;
		}
	}

tidy:
	(void)close(fds[2]);
	(void)close(fds[1]);
	(void)close(fds[0]);
	return;

err2:	(void)close(fds[2]);
err1:	(void)close(fds[1]);
err0: 	(void)close(fds[0]);
err:	(void)close(fd);
}

/*
 *  stress_daemon()
 *	stress by multiple daemonizing forks
 */
int stress_daemon(const args_t *args)
{
	int fds[2];
	pid_t pid;

	if (stress_sighandler(args->name, SIGALRM, handle_daemon_sigalrm, NULL) < 0)
		return EXIT_FAILURE;

	if (pipe(fds) < 0) {
		pr_fail_dbg("pipe");
		return EXIT_FAILURE;
	}
	pid = fork();
	if (pid < 0) {
		pr_fail_dbg("fork");
		(void)close(fds[0]);
		(void)close(fds[1]);
		return EXIT_FAILURE;
	} else if (pid == 0) {
		/* Children */
		(void)close(fds[0]);
		daemons(args, fds[1]);
		(void)close(fds[1]);
	} else {
		/* Parent */
		(void)close(fds[1]);
		do {
			ssize_t n;
			char buf[1];

			n = read(fds[0], buf, sizeof(buf));
			if (n < 0) {
				(void)close(fds[0]);
				if (errno != EINTR) {
					pr_dbg("read failed: "
						"errno=%d (%s)\n",
						errno, strerror(errno));
				}
				break;
			}
			inc_counter(args);
		} while (keep_stressing());
	}

	return EXIT_SUCCESS;
}
