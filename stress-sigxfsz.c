/*
 * Copyright (C) 2024-2025 Colin Ian King.
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

static const stress_help_t help[] = {
	{ NULL,	"sigxfsz N",		"start N workers that exercise SIGXFSZ signals" },
	{ NULL,	"sigxfsz-ops N",	"stop after N bogo SIGXFSZ signals" },
	{ NULL,	NULL,			NULL }
};

#if defined(SIGXFSZ) &&	\
    defined(RLIMIT_FSIZE)

static uint64_t async_sigs;
/*
 *  stress_sigxfsz_handler()
 *      SIGXFSZ handler
 */
static void MLOCKED_TEXT OPTIMIZE3 stress_sigxfsz_handler(int signum)
{
	(void)signum;

	async_sigs += (signum == SIGXFSZ);
}

/*
 *  stress_sigxfsz
 *	stress reading of /dev/zero using SIGXFSZ
 */
static int stress_sigxfsz(stress_args_t *args)
{
	int ret, rc = EXIT_SUCCESS, fd;
	double t_start, t_delta, rate;
	char buffer[4] ALIGN64;
	char filename[PATH_MAX];
	struct rlimit limit;
	uint32_t max_sz = ~(uint32_t)0;

	async_sigs = 0;

	if (stress_sighandler(args->name, SIGXFSZ, stress_sigxfsz_handler, NULL) < 0)
		return EXIT_FAILURE;

	if (getrlimit(RLIMIT_FSIZE, &limit) < 0) {
		pr_inf("%s: getrimit failed, errno=%d (%s), skipping stressor\n",
			args->name, errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}

	ret = stress_temp_dir_mk_args(args);
	if (ret < 0)
		return stress_exit_status((int)-ret);

	(void)shim_memset(buffer, 0xff, sizeof(buffer));

	(void)stress_temp_filename_args(args,
		filename, sizeof(filename), stress_mwc32());
	fd = open(filename, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
	if (fd < 0) {
		pr_inf("%s: cannot open file '%s', errno=%d (%s), skipping stressor\n",
			args->name, filename, errno, strerror(errno));
		rc = EXIT_NO_RESOURCE;
		goto tidy_dir;
	}
	(void)unlink(filename);

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	t_start = stress_time_now();
	do {
		ssize_t wret;

		limit.rlim_cur = stress_mwc32modn(max_sz);
		if (setrlimit(RLIMIT_FSIZE, &limit) < 0) {
			if (LIKELY(errno == EINVAL)) {
				max_sz >>= 1;
				if (max_sz > 512)
					continue;
			}
			pr_inf("%s: setrlimit failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			rc = EXIT_FAILURE;
			break;
		}
#if defined(HAVE_PWRITE)
		wret = pwrite(fd, buffer, sizeof(buffer), (off_t)limit.rlim_cur);
#else
		if (UNLIKELY(lseek(fd, (off_t)limit.rlim_cur, SEEK_SET) < 0)) {
			pr_inf("%s: seek to start of file failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			rc = EXIT_FAILURE;
			break;
		}
		wret = write(fd, buffer, sizeof(buffer));
#endif
		if ((wret < 0) && (errno == EFBIG))
			stress_bogo_inc(args);
	} while (stress_continue(args));
	t_delta = stress_time_now() - t_start;
	rate = (t_delta > 0.0) ? (double)async_sigs / t_delta : 0.0;
	stress_metrics_set(args, 0, "SIGXFSZ signals per sec",
		rate, STRESS_METRIC_HARMONIC_MEAN);

	/*  And ignore IO signals from now on */
	VOID_RET(int, stress_sighandler(args->name, SIGXFSZ, SIG_IGN, NULL));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	(void)close(fd);
tidy_dir:
	(void)stress_temp_dir_rm_args(args);

	return rc;
}

const stressor_info_t stress_sigxfsz_info = {
	.stressor = stress_sigxfsz,
	.classifier = CLASS_SIGNAL | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
const stressor_info_t stress_sigxfsz_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_SIGNAL | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without SIGXFSZ or RLIMIT_FSIZE"
};
#endif
