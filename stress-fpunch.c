/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
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

static const stress_help_t help[] = {
	{ NULL,	"fpunch N",	"start N workers punching holes in a 16MB file" },
	{ NULL,	"fpunch-ops N",	"stop after N punch bogo operations" },
	{ NULL,	NULL,		NULL }
};

#if defined(HAVE_FALLOCATE)

#define DEFAULT_FPUNCH_LENGTH		(16 * MB)
#define BUF_SIZE			(4096)
#define STRESS_PUNCH_PIDS		(4)

typedef struct {
	int mode;			/* fallocate mode */
	bool write_before;		/* write data before fallocate op */
	bool write_after;		/* write data after fallocate op */
} stress_fallocate_modes_t;

static const stress_fallocate_modes_t modes[] = {
	{ 0,						false, true },
#if defined(FALLOC_FL_KEEP_SIZE)
	{ FALLOC_FL_KEEP_SIZE,				true, false },
#endif
#if defined(FALLOC_FL_KEEP_SIZE) &&     \
    defined(FALLOC_FL_PUNCH_HOLE)
	{ FALLOC_FL_KEEP_SIZE | FALLOC_FL_PUNCH_HOLE,	false, true },
#endif
#if defined(FALLOC_FL_ZERO_RANGE)
	{ FALLOC_FL_ZERO_RANGE,				true, true },
#endif
#if defined(FALLOC_FL_COLLAPSE_RANGE)
        { FALLOC_FL_COLLAPSE_RANGE,			true, true },
#endif
#if defined(FALLOC_FL_INSERT_RANGE)
	{ FALLOC_FL_INSERT_RANGE,			false, true },
#endif
};

static void NORETURN MLOCKED_TEXT stress_fpunch_child_handler(int signum)
{
	(void)signum;

	_exit(EXIT_SUCCESS);
}

/*
 *  stress_punch_pwrite()
 *	fill a file hole with data, try to use pwrite if possible
 *	as this does not need an lseek overhead call
 */
static ssize_t stress_punch_pwrite(
	const stress_args_t *args,
	const int fd,
	const char *buf,
	const size_t size,
	const off_t offset)
{
#if defined(HAVE_PWRITEV)
	if (!keep_stressing(args))
		return 0;
	return pwrite(fd, buf, size, offset);
#else
	if (!keep_stressing(args))
		return 0;
	if (lseek(fd, offset, SEEK_SET) < (off_t)-1)
		return 0;

	if (!keep_stressing(args))
		return 0;
	return write(fd, buf, size);
#endif
}

/*
 *  stress_punch_action()
 *	perform a fallocate of a given mode. Where necessary
 *	pre-write data (if a hole is to be punched) or post-write
 *	data.
 */
static void stress_punch_action(
	const stress_args_t *args,
	const int fd,
	const stress_fallocate_modes_t *mode,
	const off_t offset,
	const char buf_before[BUF_SIZE],
	const char buf_after[BUF_SIZE],
	const size_t size)
{
	static size_t prev_size = ~(size_t)0;
	static off_t prev_offset = ~(off_t)0;

	if (!keep_stressing(args))
		return;

	/* Don't duplicate writes to previous location */
	if ((mode->write_before) &&
	    (prev_size == size) && (prev_offset == offset))
		(void)stress_punch_pwrite(args, fd, buf_before, size, offset);
	if (!keep_stressing(args))
		return;
	(void)shim_fallocate(fd, mode->mode, offset, (off_t)size);
	if (!keep_stressing(args))
		return;

	if (mode->write_after)
		(void)stress_punch_pwrite(args, fd, buf_after, size, offset);
	if (!keep_stressing(args))
		return;

	prev_size = size;
	prev_offset = offset;
}

/*
 *  stress_punch_file()
 *	exercise fallocate punching operations
 */
static void stress_punch_file(
	const stress_args_t *args,
	const int fd,
	const off_t punch_length,
	const char buf_before[BUF_SIZE],
	const char buf_after[BUF_SIZE])
{
	off_t offset = 0;

	do {
		size_t i;

		/*
		 *  Various actions at various offsets, some will
		 *  fail as these are not naturally aligned or sized
		 *  to the requirements of the underlying filesystem
		 *  and we just ignore failures. Aim is to thrash
		 *  the fallocate hole punching and filling.
		 */
		for (i = 0; i < SIZEOF_ARRAY(modes); i++)
			stress_punch_action(args, fd, &modes[i], offset + 511, buf_before, buf_after, 512);

		for (i = 0; i < SIZEOF_ARRAY(modes); i++)
			stress_punch_action(args, fd, &modes[i], offset + 1, buf_before, buf_after, 512);

		for (i = 0; i < SIZEOF_ARRAY(modes); i++)
			stress_punch_action(args, fd, &modes[i], offset, buf_before, buf_after, 512);

		/*
		 * FALLOC_FL_COLLAPSE_RANGE may need 4K sized for ext4 to work
		 */
		for (i = 0; i < SIZEOF_ARRAY(modes); i++)
			stress_punch_action(args, fd, &modes[i], offset, buf_before, buf_after, 4096);

#if defined(FALLOC_FL_PUNCH_HOLE)
		/* Create some holes to make more extents */

		(void)shim_fallocate(fd, FALLOC_FL_PUNCH_HOLE, offset, 16);
		if (!keep_stressing(args))
			break;
		(void)shim_fallocate(fd, FALLOC_FL_PUNCH_HOLE, offset + 128, 16);
		if (!keep_stressing(args))
			break;
		(void)shim_fallocate(fd, FALLOC_FL_PUNCH_HOLE, (off_t)stress_mwc32modn((uint32_t)punch_length), 16);
		if (!keep_stressing(args))
			break;
#endif
		offset += 256;
		if (offset + 4096 > punch_length)
			offset = 0;

		inc_counter(args);
	} while (keep_stressing(args));
}

/*
 *  stress_fpunch
 *	stress punching holes in files
 */
static int stress_fpunch(const stress_args_t *args)
{
	int fd = -1, ret, rc = EXIT_SUCCESS;
	char filename[PATH_MAX];
	off_t offset, punch_length = DEFAULT_FPUNCH_LENGTH;
	pid_t pids[STRESS_PUNCH_PIDS];
	size_t i, extents, n;
	char buf_before[BUF_SIZE], buf_after[BUF_SIZE];
	const size_t stride = sizeof(buf_before) << 1;
	const size_t max_punches = (size_t)(punch_length / (off_t)stride);

	ret = stress_temp_dir_mk_args(args);
	if (ret < 0)
		return stress_exit_status(-ret);

	(void)stress_temp_filename_args(args,
		filename, sizeof(filename), stress_mwc32());
	if ((fd = open(filename, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR)) < 0) {
		ret = stress_exit_status(errno);
		pr_fail("%s: open %s failed, errno=%d (%s)\n",
			args->name, filename, errno, strerror(errno));
		(void)stress_temp_dir_rm_args(args);
		return ret;
	}
	(void)shim_unlink(filename);

	(void)memset(buf_before, 0xff, sizeof(buf_before));
	(void)memset(buf_after, 0xa5, sizeof(buf_after));

	/*
	 *  Create file with lots of holes and extents by populating
	 *  it with 50% data and 50% holes by writing it backwards
	 *  and skipping over stride sized hunks.
	 */
	offset = punch_length;
	n = 0;
	for (i = 0; keep_stressing(args) && (i < max_punches); i++) {
		ssize_t r;

		offset -= stride;
		r = stress_punch_pwrite(args, fd, buf_before, sizeof(buf_before), offset);
		n += (r > 0) ? (size_t)r : 0;
	}

	if (!keep_stressing(args))
		goto tidy;

	/* Zero sized file is a bit concerning, so abort */
	if (n == 0) {
		pr_inf_skip("%s: cannot allocate file of %jd bytes, skipping stressor\n",
			args->name, (intmax_t)punch_length);
		rc = EXIT_NO_RESOURCE;
		goto tidy;
	}

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	(void)memset(pids, 0, sizeof(pids));
	for (i = 0; i < STRESS_PUNCH_PIDS; i++) {
		pids[i] = fork();
		if (pids[i] == 0) {
			VOID_RET(int, stress_sighandler(args->name, SIGALRM, stress_fpunch_child_handler, NULL));
			stress_punch_file(args, fd, punch_length, buf_before, buf_after);
			_exit(EXIT_SUCCESS);
		}
	}

	/* Wait for test run duration to complete */
	(void)sleep((unsigned int)g_opt_timeout);

	for (i = 0; i < STRESS_PUNCH_PIDS; i++) {
		if (pids[i] > 1)
			(void)kill(pids[i], SIGKILL);
	}
	for (i = 0; i < STRESS_PUNCH_PIDS; i++) {
		if (pids[i] > 1) {
			int status;

			(void)waitpid(pids[i], &status, 0);
		}
	}

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	extents = stress_get_extents(fd);
	stress_metrics_set(args, 0, "extents per file", (double)extents);

tidy:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	if (fd != -1)
		(void)close(fd);
	(void)stress_temp_dir_rm_args(args);

	return rc;
}

stressor_info_t stress_fpunch_info = {
	.stressor = stress_fpunch,
	.class = CLASS_FILESYSTEM | CLASS_OS,
	.help = help
};
#else
stressor_info_t stress_fpunch_info = {
	.stressor = stress_unimplemented,
	.class = CLASS_FILESYSTEM | CLASS_OS,
	.help = help,
	.unimplemented_reason = "built without fallocate() support"
};
#endif
