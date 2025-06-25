/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
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

#if defined(HAVE_RENAMEAT) &&	\
    defined(O_DIRECTORY)
#define EXERCISE_RENAMEAT	(1)
#endif

#if defined(HAVE_RENAMEAT2) &&	\
    defined(O_DIRECTORY) &&	\
    defined(RENAME_NOREPLACE)
#define EXERCISE_RENAMEAT2	(1)
#endif

static const stress_help_t help[] = {
	{ "R",	"rename N",	"start N workers exercising file renames" },
	{ NULL,	"rename-ops N",	"stop after N rename bogo operations" },
	{ NULL,	NULL,		NULL }
};

#if defined(EXERCISE_RENAMEAT)
/*
 *  exercise_renameat()
 *	exercise renameat with various illegal argument combinations
 */
static int exercise_renameat(
	stress_args_t *args,
	const char *old_name,
	const int old_fd,
	const char *new_name,
	const int new_fd,
	const int bad_fd)
{
	int ret;

	/* Exercise on bad_fd */
	ret = renameat(bad_fd, old_name, new_fd, new_name);
	if (ret >= 0) {
		pr_fail("%s: renameat unexpectedly succeeded on a bad file "
			"descriptor, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return -1;
	}

#if defined(HAVE_OPENAT)
	{
		/* Exercise on file fd */

		int file_fd;

		file_fd = openat(old_fd, old_name, O_RDONLY);
		if (file_fd < 0)
			return -1;

		ret = renameat(file_fd, old_name, new_fd, new_name);
		if (ret >= 0) {
			pr_fail("%s: renameat unexpectedly succeeded on a file "
				"descriptor rather than a directory descriptor, "
				"errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			(void)close(file_fd);
			return -1;
		}
		(void)close(file_fd);
	}
#endif

	return 0;
}
#endif

#if defined(EXERCISE_RENAMEAT2)
/*
 *  exercise_renameat2()
 *	exercise renameat2 with various illegal argument combinations
 */
static int exercise_renameat2(
	stress_args_t *args,
	const char *old_name,
	const int oldfd,
	const char *new_name,
	const int newfd,
	const int bad_fd)
{
	int ret, file_fd;

	/* Exercise with invalid flags */
	ret = renameat2(oldfd, old_name, newfd, new_name, (unsigned int)~0);
	if (ret >= 0) {
		pr_fail("%s: reanameat2 with illegal flags "
			"unexpectedly succeeded\n",
			args->name);
		return -1;
	}

#if defined(RENAME_EXCHANGE)
	/* Exercise with invalid combination of flags */
	ret = renameat2(oldfd, old_name, newfd, new_name,
		RENAME_EXCHANGE | RENAME_NOREPLACE);
	if (ret >= 0) {
		pr_fail("%s: renameat2 with invalid flags "
			"RENAME_EXCHANGE | RENAME_NOREPLACE "
			"unexpectedly succeeded\n",
			args->name);
		return -1;
	}

#if defined(RENAME_WHITEOUT)
	ret = renameat2(oldfd, old_name, newfd, new_name,
		RENAME_EXCHANGE | RENAME_WHITEOUT);
	if (ret >= 0)
		return -1;
#endif

	/* Exercise RENAME_EXCHANGE on non-existed directory */
	ret = renameat2(oldfd, old_name, newfd, new_name, RENAME_EXCHANGE);
	if (ret >= 0) {
		pr_fail("%s: renameat2 unexpectedly succeeded on "
			"non-existent directory with RENAME_EXCHANGE "
			"flag, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return -1;
	}

	/*
	 * Exercise RENAME_EXCHANGE on same file creating absolutely
	 * no difference other than increase in kernel test coverage
	 */
	VOID_RET(int, renameat2(oldfd, old_name, oldfd, old_name, RENAME_EXCHANGE));

#endif

	/* Exercise RENAME_NOREPLACE on same file */
	ret = renameat2(oldfd, old_name, oldfd, old_name, RENAME_NOREPLACE);
	if (ret >= 0) {
		pr_fail("%s: renameat2 unexpectedly succeeded on existent "
			"directory/file with RENAME_NOREPLACE flag, "
			"errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return -1;
	}

	/* Exercise on bad_fd */
	ret = renameat2(bad_fd, old_name, newfd, new_name, RENAME_NOREPLACE);
	if (ret >= 0) {
		pr_fail("%s: renameat2 unexpectedly succeeded on bad file "
			"descriptor, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return -1;
	}

	/* Exercise on file fd */
	file_fd = open(old_name, O_RDONLY);
	if (file_fd < 0)
		return -1;
	ret = renameat2(file_fd, old_name, newfd, new_name, RENAME_NOREPLACE);
	if (ret >= 0) {
		pr_fail("%s: renameat2 unexpectedly succeeded on file "
			"descriptor rather than" "directory descriptor, "
			"errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		(void)close(file_fd);
		return -1;
	}
	(void)close(file_fd);

	return 0;
}
#endif

#if defined(EXERCISE_RENAMEAT) ||	\
    defined(EXERCISE_RENAMEAT2)
/*
 *  stress_basename()
 *	non-destructive basename()
 */
static char *stress_basename(char *filename)
{
	char *ptr = filename;
	char *last_slash = filename;

	while (*ptr) {
		if (*ptr == '/')
			last_slash = ptr + 1;
		ptr++;
	}
	return last_slash;
}
#endif

/*
 *  stress_rename()
 *	stress system by renames
 */
static int stress_rename(stress_args_t *args)
{
	char name1[PATH_MAX], name2[PATH_MAX];
	char *oldname = name1, *newname = name2, *tmpname;
	FILE *fp;
	uint64_t i = 0;
	const uint32_t inst1 = args->instance * 2;
	const uint32_t inst2 = inst1 + 1;
#if defined(EXERCISE_RENAMEAT) ||	\
    defined(EXERCISE_RENAMEAT2)
	const int bad_fd = stress_get_bad_fd();
	int tmp_fd;
#endif

	(void)shim_memset(name1, 0, sizeof(name1));
	(void)shim_memset(name2, 0, sizeof(name2));

	if (stress_temp_dir_mk(args->name, args->pid, inst1) < 0)
		return EXIT_FAILURE;
	if (stress_temp_dir_mk(args->name, args->pid, inst2) < 0) {
		(void)stress_temp_dir_rm(args->name, args->pid, inst1);
		return EXIT_FAILURE;
	}

#if defined(EXERCISE_RENAMEAT) ||	\
    defined(EXERCISE_RENAMEAT2)
	{
		char tmp[PATH_MAX];

		stress_temp_dir_args(args, tmp, sizeof(tmp));
		tmp_fd = open(tmp, O_RDONLY | O_DIRECTORY);
	}
#endif

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);
restart:
	(void)stress_temp_filename(oldname, PATH_MAX,
		args->name, args->pid, inst1, i++);

	if ((fp = fopen(oldname, "w+")) == NULL) {
		int rc = stress_exit_status(errno);

		pr_err("%s: fopen failed, errno=%d: (%s)%s\n",
			args->name, errno, strerror(errno),
			stress_get_fs_type(oldname));
		(void)stress_temp_dir_rm(args->name, args->pid, inst1);
		(void)stress_temp_dir_rm(args->name, args->pid, inst2);
#if defined(EXERCISE_RENAMEAT) ||	\
    defined(EXERCISE_RENAMEAT2)
		if (tmp_fd >= 0)
			(void)close(tmp_fd);
#endif
		return rc;
	}
	(void)fflush(fp);
	(void)fclose(fp);

	while (stress_continue(args)) {
		(void)stress_temp_filename(newname, PATH_MAX,
			args->name, args->pid, inst2, i++);
		if (rename(oldname, newname) < 0) {
			(void)shim_unlink(oldname);
			(void)shim_unlink(newname);
			goto restart;
		}

		tmpname = oldname;
		oldname = newname;
		newname = tmpname;
		stress_bogo_inc(args);
		if (UNLIKELY(!stress_continue(args)))
			break;

		(void)stress_temp_filename(newname, PATH_MAX,
			args->name, args->pid, inst1, i++);
		if (rename(oldname, newname) < 0) {
			(void)shim_unlink(oldname);
			(void)shim_unlink(newname);
			goto restart;
		}

		tmpname = oldname;
		oldname = newname;
		newname = tmpname;
		stress_bogo_inc(args);
		if (UNLIKELY(!stress_continue(args)))
			break;

#if defined(EXERCISE_RENAMEAT)
		if (tmp_fd >= 0) {
			char *old, *new;

			(void)stress_temp_filename(newname, PATH_MAX,
				args->name, args->pid, inst1, i++);

			/* Skip over tmp_path prefix */
			old = stress_basename(oldname);
			new = stress_basename(newname);

			if (exercise_renameat(args, old, tmp_fd, new, tmp_fd, bad_fd) < 0) {
				(void)shim_unlink(oldname);
				(void)shim_unlink(newname);
				goto restart;
			}

			if (renameat(tmp_fd, old, tmp_fd, new) < 0) {
				(void)shim_unlink(oldname);
				(void)shim_unlink(newname);
				goto restart;
			}
			(void)shim_fsync(tmp_fd);
			tmpname = oldname;
			oldname = newname;
			newname = tmpname;

			stress_bogo_inc(args);
			if (UNLIKELY(!stress_continue(args)))
				break;
		}
#endif

#if defined(EXERCISE_RENAMEAT2)
		if (tmp_fd >= 0) {
			char *old, *new;

			(void)stress_temp_filename(newname, PATH_MAX,
				args->name, args->pid, inst1, i++);

			/* Skip over tmp_path prefix */
			old = stress_basename(oldname);
			new = stress_basename(newname);

			if (exercise_renameat2(args, old, tmp_fd, new, tmp_fd, bad_fd) < 0) {
				(void)shim_unlink(oldname);
				(void)shim_unlink(newname);
				goto restart;
			}

			if (renameat2(tmp_fd, old, tmp_fd, new, RENAME_NOREPLACE) < 0) {
				(void)shim_unlink(oldname);
				(void)shim_unlink(newname);
				goto restart;
			}
			tmpname = oldname;
			oldname = newname;
			newname = tmpname;

			stress_bogo_inc(args);
			if (UNLIKELY(!stress_continue(args)))
				break;
		}
#endif
	}

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

#if defined(EXERCISE_RENAMEAT) ||	\
    defined(EXERCISE_RENAMEAT2)
	if (tmp_fd >= 0)
		(void)close(tmp_fd);
#endif
	if (*oldname)
		(void)shim_unlink(oldname);
	if (*newname)
		(void)shim_unlink(newname);
	(void)stress_temp_dir_rm(args->name, args->pid, inst1);
	(void)stress_temp_dir_rm(args->name, args->pid, inst2);

	return EXIT_SUCCESS;
}

const stressor_info_t stress_rename_info = {
	.stressor = stress_rename,
	.classifier = CLASS_FILESYSTEM | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help
};
