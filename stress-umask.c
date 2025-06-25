/*
 * Copyright (C) 2025      Colin Ian King.
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

#include <sys/stat.h>

static const stress_help_t help[] = {
	{ NULL,	"umask N",	"start N workers exercising umask, file create/stat/close/unlink" },
	{ NULL,	"umask-ops N",	"stop after N umask and file operations" },
	{ NULL,	NULL,		NULL }
};

/*
 *  stress_umask
 *	stress file ioctls
 */
static int stress_umask(stress_args_t *args)
{
	int ret, rc = EXIT_FAILURE;
	mode_t mask, prev_mask;

	ret = stress_temp_dir_mk_args(args);
	if (ret < 0)
		return stress_exit_status(-ret);

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		int i;
		mode_t ret_mask;

		prev_mask = 0;
		(void)umask(prev_mask);

		for (mask = 0000; mask <= 0777; mask++) {
			char filename[PATH_MAX];
			struct stat statbuf;
			int fd;

			ret_mask = umask(mask);
			if (ret_mask >= 01000) {
				pr_inf("%s: invalid umask return 0%4.4o value\n",
					args->name, (unsigned int)ret_mask);
				goto fail;
			}
			if (ret_mask != prev_mask) {
				pr_inf("%s: invalid umask return 0%4.4o value, expecting 0%4.4o\n",
					args->name, (unsigned int)ret_mask, (unsigned int)prev_mask);
				goto fail;
			}
			prev_mask = mask;

			(void)stress_temp_filename_args(args, filename, sizeof(filename), stress_mwc32());
			fd = open(filename, O_CREAT | O_RDWR, 0777);
			if (fd < 0) {
				pr_fail("%s: cannot create file %s\n", args->name, filename);
				goto fail;
			}
			if (shim_fstat(fd, &statbuf) < 0) {
				pr_fail("%s: cannot stat file %s\n", args->name, filename);
				(void)close(fd);
				(void)shim_unlink(filename);
				goto fail;
			}
			if ((statbuf.st_mode & 0777) != (~mask & 0777)) {
				pr_fail("%s: file mode %3.3o differs from expected mode %3.3o\n",
						args->name, (unsigned int)statbuf.st_mode & 0777,
						(unsigned int)(~mask & 0777));
				(void)close(fd);
				(void)shim_unlink(filename);
				goto fail;
			}
			(void)close(fd);
			if (shim_unlink(filename) < 0) {
				pr_fail("%s: cannot unlink file %s\n", args->name, filename);
				goto fail;
			}
		}

		for (i = 0; i < 16; i++) {
			mask = stress_mwc16modn(0777);

			(void)umask(mask);
			ret_mask = umask(0);

			if (ret_mask != mask) {
				pr_inf("%s: invalid umask return 0%4.4o value, expecting 0%4.4o\n",
					args->name, (unsigned int)ret_mask, (unsigned int)mask);
				goto fail;
			}
		}
		stress_bogo_inc(args);
	} while (stress_continue(args));
	rc = EXIT_SUCCESS;
fail:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	(void)stress_temp_dir_rm_args(args);

	return rc;
}

const stressor_info_t stress_umask_info = {
	.stressor = stress_umask,
	.classifier = CLASS_FILESYSTEM | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help
};
