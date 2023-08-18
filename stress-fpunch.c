// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
 *
 */
#include "stress-ng.h"
#include "core-builtin.h"

static const stress_help_t help[] = {
	{ NULL,	"fpunch N",	"start N workers punching holes in a 16MB file" },
	{ NULL,	"fpunch-ops N",	"stop after N punch bogo operations" },
	{ NULL,	NULL,		NULL }
};

#if defined(HAVE_FALLOCATE)

#define DEFAULT_FPUNCH_LENGTH		(16 * MB)
#define PROC_FPUNCH_OFFSET		(2 * MB)
#define BUF_SIZE			(4096)
#define STRESS_PUNCH_PIDS		(4)

typedef struct {
	const int mode;			/* fallocate mode */
	const bool write_before;	/* write data before fallocate op */
	const bool write_after;		/* write data after fallocate op */
	const bool check_zero;		/* check for zero'd data */
} stress_fallocate_modes_t;

typedef struct {
	char buf_before[BUF_SIZE];
	char buf_after[BUF_SIZE];
	char buf_read[BUF_SIZE];
} stress_punch_buf_t;

static const stress_fallocate_modes_t modes[] = {
	{ 0,						false, true, false },
#if defined(FALLOC_FL_KEEP_SIZE)
	{ FALLOC_FL_KEEP_SIZE,				true, false, false },
#endif
#if defined(FALLOC_FL_KEEP_SIZE) &&     \
    defined(FALLOC_FL_PUNCH_HOLE)
	{ FALLOC_FL_KEEP_SIZE | FALLOC_FL_PUNCH_HOLE,	false, true, false },
#endif
#if defined(FALLOC_FL_ZERO_RANGE)
	{ FALLOC_FL_ZERO_RANGE,				true, true, true },
#endif
#if defined(FALLOC_FL_COLLAPSE_RANGE)
        { FALLOC_FL_COLLAPSE_RANGE,			true, true, false },
#endif
#if defined(FALLOC_FL_INSERT_RANGE)
	{ FALLOC_FL_INSERT_RANGE,			false, true, false },
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
	char *data,
	const int fd,
	const size_t size,
	const off_t offset)
{
#if defined(HAVE_PWRITEV)
	if (!stress_continue(args))
		return 0;
	return pwrite(fd, data, size, offset);
#else
	if (!stress_continue(args))
		return 0;
	if (lseek(fd, offset, SEEK_SET) < (off_t)-1)
		return 0;

	if (!stress_continue(args))
		return 0;
	return write(fd, data, size);
#endif
}

/*
 *  stress_punch_check_zero()
 *	verify data in file is zero'd
 */
static inline int stress_punch_check_zero(
	const stress_args_t *args,
	char *data,
	const int fd,
	const off_t offset,
	const size_t size)
{
	ssize_t ret;
	register char *ptr, *ptr_end;

#if defined(HAVE_PREADV)
	ret = pread(fd, data, size, offset);
#else
	if (lseek(fd, offset, SEEK_SET) < (off_t)-1)
		return 0;
	ret = read(fd, data, size);
#endif
	if (ret < 0)
		return 0;

	ptr = data;
	ptr_end = data + ret;
	while (ptr < ptr_end) {
		if (*ptr) {
			pr_inf("%s: data at file offset 0x%" PRIxMAX " was 0x%2.2x and not zero\n",
				args->name, offset + (ptr_end - ptr), *ptr & 0xff);
			return -1;
		}
		ptr++;
	}
	return 0;
}

/*
 *  stress_punch_action()
 *	perform a fallocate of a given mode. Where necessary
 *	pre-write data (if a hole is to be punched) or post-write
 *	data.
 */
static int stress_punch_action(
	const stress_args_t *args,
	stress_punch_buf_t *buf,
	const stress_fallocate_modes_t *mode,
	const size_t instance,
	const int fd,
	const off_t offset,
	const size_t size)
{
	static size_t prev_size = ~(size_t)0;
	static off_t prev_offset = ~(off_t)0;
	const bool verify = !!(g_opt_flags & OPT_FLAGS_VERIFY);

	if (!stress_continue(args))
		return 0;

	/* Don't duplicate writes to previous location */
	if ((mode->write_before) &&
	    (prev_size == size) && (prev_offset == offset))
		(void)stress_punch_pwrite(args, buf->buf_before, fd, size, offset);
	if (!stress_continue(args))
		return 0;
	(void)shim_fallocate(fd, mode->mode, offset, (off_t)size);
	if (verify &&
	    (instance == 0) &&
	    (offset < (off_t)(PROC_FPUNCH_OFFSET - size)) &&
	    (mode->check_zero)) {
		if (stress_punch_check_zero(args, buf->buf_read, fd, offset, size) < 0)
			return -1;
	}
	if (!stress_continue(args))
		return 0;

	if (mode->write_after)
		(void)stress_punch_pwrite(args, buf->buf_after, fd, size, offset);
	if (!stress_continue(args))
		return 0;

	prev_size = size;
	prev_offset = offset;

	return 0;
}

/*
 *  stress_punch_file()
 *	exercise fallocate punching operations
 */
static int stress_punch_file(
	const stress_args_t *args,
	stress_punch_buf_t *buf,
	const size_t instance,
	const int fd)
{
	const off_t offset_min = PROC_FPUNCH_OFFSET * instance;
	off_t offset = offset_min;
	int rc = 0;

	do {
		size_t i;

		/*
		 *  Various actions at various offsets, some will
		 *  fail as these are not naturally aligned or sized
		 *  to the requirements of the underlying filesystem
		 *  and we just ignore failures. Aim is to thrash
		 *  the fallocate hole punching and filling.
		 */
		for (i = 0; i < SIZEOF_ARRAY(modes); i++) {
			if (stress_punch_action(args, buf, &modes[i], instance, fd, offset + 511, 512) < 0) {
				rc = -1;
				break;
			}
		}

		for (i = 0; i < SIZEOF_ARRAY(modes); i++) {
			if (stress_punch_action(args, buf, &modes[i], instance, fd, offset + 1, 512) < 0) {
				rc = -1;
				break;
			}
		}

		for (i = 0; i < SIZEOF_ARRAY(modes); i++) {
			if (stress_punch_action(args, buf, &modes[i], instance, fd, offset, 512) < 0) {
				rc = -1;
				break;
			}
		}

		/*
		 * FALLOC_FL_COLLAPSE_RANGE may need 4K sized for ext4 to work
		 */
		for (i = 0; i < SIZEOF_ARRAY(modes); i++) {
			if (stress_punch_action(args, buf, &modes[i], instance, fd, offset, 4096) < 0) {
				rc = -1;
				break;
			}
		}

#if defined(FALLOC_FL_PUNCH_HOLE)
		/* Create some holes to make more extents */

		(void)shim_fallocate(fd, FALLOC_FL_PUNCH_HOLE, offset, 16);
		if (!stress_continue(args))
			break;
		(void)shim_fallocate(fd, FALLOC_FL_PUNCH_HOLE, offset + 128, 16);
		if (!stress_continue(args))
			break;
		(void)shim_fallocate(fd, FALLOC_FL_PUNCH_HOLE, (off_t)stress_mwc32modn(DEFAULT_FPUNCH_LENGTH), 16);
		if (!stress_continue(args))
			break;
#endif
		offset += (256 * (instance + 1));
		if (offset + 4096 > (off_t)DEFAULT_FPUNCH_LENGTH)
			offset = offset_min;

		VOID_RET(int, ftruncate(fd, (off_t)DEFAULT_FPUNCH_LENGTH));

		stress_bogo_inc(args);
	} while ((rc == 0) && stress_continue(args));

	return rc;
}

/*
 *  stress_fpunch
 *	stress punching holes in files
 */
static int stress_fpunch(const stress_args_t *args)
{
	int fd = -1, ret, rc = EXIT_SUCCESS;
	char filename[PATH_MAX];
	off_t offset;
	pid_t pids[STRESS_PUNCH_PIDS];
	size_t i, extents, n;
	const size_t stride = (size_t)BUF_SIZE << 1;
	const size_t max_punches = (size_t)(DEFAULT_FPUNCH_LENGTH / (off_t)stride);
	stress_punch_buf_t *buf;

	buf = (stress_punch_buf_t *)mmap(NULL, sizeof(*buf), PROT_READ | PROT_WRITE,
			MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (buf == MAP_FAILED) {
		pr_inf("%s: failed to mmap %zd sized buffer, errno=%d (%s), skipping stressor\n",
			args->name, sizeof(*buf), errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}
	ret = stress_temp_dir_mk_args(args);
	if (ret < 0) {
		rc = stress_exit_status(-ret);
		goto tidy_buf;
	}

	(void)stress_temp_filename_args(args,
		filename, sizeof(filename), stress_mwc32());
	if ((fd = open(filename, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR)) < 0) {
		rc = stress_exit_status(errno);
		pr_fail("%s: open %s failed, errno=%d (%s)\n",
			args->name, filename, errno, strerror(errno));
		goto tidy_temp;
	}
	(void)shim_unlink(filename);

	stress_file_rw_hint_short(fd);

	(void)shim_memset(&buf->buf_before, 0xff, sizeof(buf->buf_before));
	(void)shim_memset(&buf->buf_after, 0xa5, sizeof(buf->buf_after));

	/*
	 *  Create file with lots of holes and extents by populating
	 *  it with 50% data and 50% holes by writing it backwards
	 *  and skipping over stride sized hunks.
	 */
	offset = DEFAULT_FPUNCH_LENGTH;
	n = 0;
	for (i = 0; stress_continue(args) && (i < max_punches); i++) {
		ssize_t r;

		offset -= stride;
		r = stress_punch_pwrite(args, buf->buf_before, fd, sizeof(buf->buf_before), offset);
		n += (r > 0) ? (size_t)r : 0;
	}

	if (!stress_continue(args))
		goto tidy;

	/* Zero sized file is a bit concerning, so abort */
	if (n == 0) {
		pr_inf_skip("%s: cannot allocate file of %" PRIu64 " bytes, skipping stressor\n",
			args->name, (uint64_t)DEFAULT_FPUNCH_LENGTH);
		rc = EXIT_NO_RESOURCE;
		goto tidy;
	}

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	(void)shim_memset(pids, 0, sizeof(pids));
	for (i = 0; i < STRESS_PUNCH_PIDS; i++) {
		pids[i] = fork();
		if (pids[i] == 0) {
			VOID_RET(int, stress_sighandler(args->name, SIGALRM, stress_fpunch_child_handler, NULL));
			if (stress_punch_file(args, buf, i, fd) < 0)
				_exit(EXIT_FAILURE);
			_exit(EXIT_SUCCESS);
		}
	}

	/* Wait for test run duration to complete */
	(void)sleep((unsigned int)g_opt_timeout);

	if (stress_kill_and_wait_many(args, pids, STRESS_PUNCH_PIDS, SIGALRM, true) != EXIT_SUCCESS)
		rc = EXIT_FAILURE;

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	extents = stress_get_extents(fd);
	stress_metrics_set(args, 0, "extents per file", (double)extents);

tidy:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	if (fd != -1)
		(void)close(fd);
tidy_temp:
	(void)stress_temp_dir_rm_args(args);
tidy_buf:
	(void)munmap((void *)buf, sizeof(*buf));

	return rc;
}

stressor_info_t stress_fpunch_info = {
	.stressor = stress_fpunch,
	.class = CLASS_FILESYSTEM | CLASS_OS,
	.verify = VERIFY_OPTIONAL,
	.help = help
};
#else
stressor_info_t stress_fpunch_info = {
	.stressor = stress_unimplemented,
	.class = CLASS_FILESYSTEM | CLASS_OS,
	.verify = VERIFY_OPTIONAL,
	.help = help,
	.unimplemented_reason = "built without fallocate() support"
};
#endif
