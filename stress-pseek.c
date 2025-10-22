/*
 * Copyright (C)      2024 Colin Ian King.
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
#include "core-killpid.h"
#include "core-mmap.h"
#include "core-pragma.h"
#include "core-pthread.h"
#include "core-target-clones.h"

#define MIN_PSEEKIO_BYTES	(1 * MB)
#define MAX_PSEEKIO_BYTES	(MAX_FILE_LIMIT)
#define DEFAULT_PSEEKIO_BYTES	(1 * GB)

#define MIN_PSEEKIO_IO_SIZE	(1)
#define MAX_PSEEKIO_IO_SIZE	(1 * MB)
#define DEFAULT_PSEEKIO_IO_SIZE	(1024)

#define MIN_PSEEKIO_PROCS	(2)
#define MAX_PSEEKIO_PROCS	(16)
#define DEFAULT_PSEEKIO_PROCS	(5)

#define PSEEKIO_CHUNK_SCALE	(8)

/* io-mode */
#define IO_MODE_UNKNOWN		(0)
#define IO_MODE_SEEK_WR_RD	(1)
#define IO_MODE_P_WR_RD		(2)

/*
 *  General stressor information
 */
typedef struct {
	const char *fs_type;		/* file system type */
	int fd;				/* file descriptor */
	bool pseek_rand;		/* random seek option */
	uint64_t pseek_io_size;		/* write/read I/O size */
	pid_t parent_pid;		/* stressor ped */
} stress_peekio_info_t;

/*
 *  Per child/pthread process information
 */
typedef struct {
	stress_args_t *args;		/* stressor args */
	stress_peekio_info_t *info;	/* info */
	int proc_num;			/* process instance */
	int io_mode;			/* IO_MODE_* */
	uint8_t *buf;			/* I/O buffer */
#if defined(HAVE_LIB_PTHREAD)
        pthread_t pthread;		/* pthread */
	int pthread_ret;		/* pthread_create return */
#endif
	int ret;			/* exerciser return */
	pid_t pid;			/* pid if forked */
	double writes;			/* bytes written */
	double writes_duration;		/* seek+write duration */
	double reads;			/* bytes read */
	double reads_duration;		/* seek+read duration */
} stress_peekio_proc_t;

static const stress_help_t help[] = {
	{ "d N","pseek N",		"start N workers spinning on seek/write/seek/read" },
	{ NULL, "pseek-rand",		"perform random seeks rather than fixed seeks" },
	{ NULL,	"pseek-io-size N",	"set the default write/read I/O size to N bytes" },
	{ NULL, NULL,			NULL }
};

/*
 *  data_value()
 *	generate 8 bit data value for offsets and instance # into a test file
 */
static inline ALWAYS_INLINE uint8_t CONST OPTIMIZE3 data_value(
	const size_t i,
	const size_t j,
	const int proc_num)
{
	register const size_t sum = i + j;

	return (uint8_t)((sum >> 9) + sum + proc_num);
}

static void OPTIMIZE3 TARGET_CLONES pseek_fill_buf(
	uint8_t *buf,
	const size_t buf_size,
	const size_t i,
	const int proc_num)
{
	register size_t j;

	for (j = 0; j < buf_size; j++) {
		buf[j] = data_value(i, j, proc_num);
	}
}

/*
 *  stress_pseek_write_offset()
 *	write at a given offset
 */
static int stress_pseek_write_offset(
	stress_args_t *args,
	stress_peekio_info_t *info,
	stress_peekio_proc_t *proc,
	const off_t offset)
{
	ssize_t ret = -1;
	double t;

	pseek_fill_buf(proc->buf, info->pseek_io_size, offset, proc->proc_num);

	t = stress_time_now();
retry:
	errno = 0;
	if (proc->io_mode == IO_MODE_SEEK_WR_RD) {
		off_t new_offset;

		if (UNLIKELY(lseek(info->fd, offset, SEEK_SET) < 0)) {
			pr_fail("%s: lseek failed, set offset at %" PRIdMAX ", errno=%d (%s)\n",
				args->name, (intmax_t)offset, errno, strerror(errno));
			return -1;
		}

		new_offset = lseek(info->fd, 0, SEEK_CUR);
		if (UNLIKELY(new_offset != offset)) {
			pr_fail("%s: lseek failed, set offset at %" PRIdMAX
				", current offset at %" PRIdMAX "\n",
				args->name, (intmax_t)offset, (intmax_t)new_offset);
			return -1;
		}
		ret = write(info->fd, proc->buf, info->pseek_io_size);
	} else {
		ret = pwrite(info->fd, proc->buf, info->pseek_io_size, offset);
	}

	/* successful write */
	if (ret == (ssize_t)info->pseek_io_size) {
		proc->writes_duration += stress_time_now() - t;
		proc->writes += (double)ret;

		if (proc->proc_num == 0)
			stress_bogo_inc(args);
		return ret;
	}

	switch (errno) {
	case EAGAIN:
	case EINTR:
		if (LIKELY(stress_continue(args)))
			goto retry;
		return 0;
	case ENOSPC:
		return 0;
	case 0:
		pr_fail("%s: write of %" PRIu64 " bytes only wrote %zd bytes\n",
			args->name, info->pseek_io_size, ret);
		return -1;
	}
	pr_fail("%s: write failed, errno=%d (%s)%s\n",
		args->name, errno, strerror(errno), info->fs_type);
	return -1;
}

/*
 *  stress_pseek_read_offset()
 *	read at a given offset
 */
static int stress_pseek_read_offset(
	stress_args_t *args,
	stress_peekio_info_t *info,
	stress_peekio_proc_t *proc,
	const off_t offset)
{
	ssize_t ret;
	double t;

	t = stress_time_now();
retry:
	if (proc->io_mode == IO_MODE_SEEK_WR_RD) {
		off_t new_offset;

		if (UNLIKELY(lseek(info->fd, offset, SEEK_SET) < 0)) {
			pr_fail("%s: lseek failed, set offset at %" PRIdMAX ", errno=%d (%s)\n",
				args->name, (intmax_t)offset, errno, strerror(errno));
			return -1;
		}

		new_offset = lseek(info->fd, 0, SEEK_CUR);
		if (UNLIKELY(new_offset != offset)) {
			pr_fail("%s: lseek failed, set offset at %" PRIdMAX
				", current offset at %" PRIdMAX "\n",
				args->name, (intmax_t)offset, (intmax_t)new_offset);
			return -1;
		}
		ret = read(info->fd, proc->buf, (size_t)info->pseek_io_size);
		if (ret > 0) {
			proc->reads_duration += stress_time_now() - t;
			proc->reads += (double)ret;
			return ret;
		}
	} else {
		ret = pread(info->fd, proc->buf, (size_t)info->pseek_io_size, offset);
	}

	/* successful read */
	if (ret == (ssize_t)info->pseek_io_size) {
		register size_t j, baddata = 0, sz = (size_t)ret;

		proc->reads_duration += stress_time_now() - t;
		proc->reads += (double)ret;

PRAGMA_UNROLL_N(4)
		for (j = 0; j < sz; j++) {
			register const uint8_t v = data_value((size_t)offset, j, proc->proc_num);

			baddata += (proc->buf[j] != v);
		}
		if (baddata) {
			pr_fail("%s: read failed, %zu of %zd bytes incorrect\n",
				args->name, baddata, ret);
			return -1;
		}
		return ret;
	}

	switch (errno) {
	case EAGAIN:
	case EINTR:
		if (LIKELY(stress_continue(args)))
			goto retry;
		return 0;
	case ENOSPC: /* e.g. on vfat */
		return 0;
	case 0:
		pr_fail("%s: read of %" PRIu64 " bytes only read %zd bytes\n",
			args->name, info->pseek_io_size, ret);
		return -1;
	}
	pr_fail("%s: read failed, errno=%d (%s)%s\n",
		args->name, errno, strerror(errno), info->fs_type);
	return -1;
}

static void stress_peekio_exercise(stress_peekio_proc_t *proc)
{
	stress_args_t *args = proc->args;
	stress_peekio_info_t *info = proc->info;

	for (;;) {
		off_t offset;

		if (info->pseek_rand) {
			offset = (size_t)proc->proc_num * info->pseek_io_size * PSEEKIO_CHUNK_SCALE;
			offset += info->pseek_io_size * (size_t)stress_mwc8modn(PSEEKIO_CHUNK_SCALE - 1);
		} else {
			offset = (size_t)proc->proc_num * info->pseek_io_size * PSEEKIO_CHUNK_SCALE;
		}
		if (UNLIKELY(!stress_continue(args)))
			break;
		if (stress_pseek_write_offset(args, info, proc, offset) < 0) {
			(void)kill(info->parent_pid, SIGALRM);
			proc->ret = -1;
			return;
		}
		if (UNLIKELY(!stress_continue(args)))
			break;
		if (stress_pseek_read_offset(args, info, proc, offset) < 0) {
			(void)kill(info->parent_pid, SIGALRM);
			proc->ret = -1;
			return ;
		}
		(void)shim_sched_yield();
	}
	proc->ret = 0;
}

#if defined(HAVE_LIB_PTHREAD)
static void *stress_peekio_pthread(void *parg)
{
	stress_peekio_proc_t *proc = (stress_peekio_proc_t *)parg;

	stress_random_small_sleep();
	stress_peekio_exercise(proc);
	return &g_nowt;
}
#endif

static int stress_pseek_spawn(stress_args_t *args, stress_peekio_proc_t *proc)
{
	pid_t pid;

#if defined(HAVE_LIB_PTHREAD)
	if (proc->proc_num & 1) {
		proc->pthread_ret = -1;
		proc->pthread_ret =
			pthread_create(&proc->pthread, NULL,
				stress_peekio_pthread, proc);
		if (proc->pthread_ret != 0) {
			pr_inf("%s: failed to create pthread, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			return -1;
		}
		return 0;
	}
#endif
	pid = fork();
	if (pid < 0) {
		pr_inf("%s: failed to fork process, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return -1;
	} else if (pid == 0) {
		/* Child */
		stress_set_proc_state(args->name, STRESS_STATE_RUN);
		stress_peekio_exercise(proc);
		_exit(0);
	} else {
		proc->pid = pid;

	}
	return 0;
}

static void stress_pseek_kill(
	stress_args_t *args,
	stress_peekio_proc_t *proc)
{
#if defined(HAVE_LIB_PTHREAD)
	if (proc->proc_num & 1) {
		if (proc->pthread_ret == 0) {
			(void)pthread_cancel(proc->pthread);
		}
		return;
	}
#endif
	if (proc->pid > 1) {
		stress_kill_and_wait(args, proc->pid, SIGKILL, true);
	}
}

/*
 *  stress_pseek
 *	stress I/O via writes
 */
static int stress_pseek(stress_args_t *args)
{
	int rc = EXIT_SUCCESS;
	ssize_t ret;
	char filename[PATH_MAX];
	size_t pseek_bytes;
	static stress_peekio_info_t info;

	size_t i;
	size_t pseek_procs = DEFAULT_PSEEKIO_PROCS;
	stress_peekio_proc_t *procs;
	const size_t procs_size = sizeof(*procs) * pseek_procs;
	double total_writes = 0.0, total_reads = 0.0;
	double total_writes_duration = 0.0, total_reads_duration = 0.0;
	double rate;

	procs = stress_mmap_populate(NULL, procs_size, PROT_READ | PROT_WRITE,
					MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (procs == MAP_FAILED) {
		pr_inf_skip("%s: failed to mmap %zu byte procs array%s, "
			"errno=%d (%s), skipping stressor\n",
			args->name, procs_size,
			stress_get_memfree_str(), errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}
	stress_set_vma_anon_name(procs, procs_size, "process-state");

	info.fd = -1;
	info.pseek_rand = false;
	info.pseek_io_size = DEFAULT_PSEEKIO_IO_SIZE;
	info.parent_pid = getpid();

	if (!stress_get_setting("pseek-rand", &info.pseek_rand)) {
		if (g_opt_flags & OPT_FLAGS_AGGRESSIVE)
			info.pseek_rand = true;
	}
	if (!stress_get_setting("pseek-io-size", &info.pseek_io_size)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			info.pseek_io_size = MAX_PSEEKIO_IO_SIZE;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			info.pseek_io_size = MIN_PSEEKIO_IO_SIZE;
	}
	if (info.pseek_io_size < MIN_PSEEKIO_IO_SIZE) {
		info.pseek_io_size = MIN_PSEEKIO_IO_SIZE;
		if (stress_instance_zero(args))
			pr_inf("%s: --pseek-io-size too small, using %" PRIu64 " instead\n",
				args->name, info.pseek_io_size);
	}
	if (info.pseek_io_size > MAX_PSEEKIO_IO_SIZE) {
		info.pseek_io_size = MAX_PSEEKIO_IO_SIZE;
		if (stress_instance_zero(args))
			pr_inf("%s: --pseek-io-size too large, using %" PRIu64 " instead\n",
				args->name, info.pseek_io_size);
	}

	for (i = 0; i < pseek_procs; i++) {
		procs[i].args = args;
		procs[i].info = &info;
		procs[i].buf = NULL;
		procs[i].pid = -1;
		procs[i].writes = 0.0;
		procs[i].reads = 0.0;
		procs[i].io_mode = (i == 0) ? IO_MODE_SEEK_WR_RD : IO_MODE_P_WR_RD;
		procs[i].proc_num = i;
#if defined(HAVE_LIB_PTHREAD)
		procs[i].pthread_ret = -1;
		shim_memset(&procs[i].pthread, 0, sizeof(procs[i].pthread));
#endif
	}

	for (i = 0; i < pseek_procs; i++) {
		procs[i].buf = (uint8_t *)stress_mmap_populate(NULL, (size_t)info.pseek_io_size,
						PROT_READ | PROT_WRITE,
						MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		if (procs[i].buf == MAP_FAILED) {
			size_t j;

			pr_inf_skip("%s: failed to mmap buffer of %" PRIu64 " bytes%s, "
				"errno=%d (%s), skipping stressor\n",
				args->name, info.pseek_io_size,
				stress_get_memfree_str(), errno, strerror(errno));
			for (j = 0; j < i; j++) {
				(void)munmap((void *)procs[j].buf, (size_t)info.pseek_io_size);
			}
			rc = EXIT_NO_RESOURCE;
			goto tidy_munmap_procs;
		}
		stress_set_vma_anon_name(procs[i].buf, (size_t)info.pseek_io_size, "pseek-buffer");
		(void)shim_memset(procs[i].buf, stress_mwc8(), (size_t)info.pseek_io_size);
	}

	ret = stress_temp_dir_mk_args(args);
	if (ret < 0) {
		rc = EXIT_NO_RESOURCE;
		goto tidy_munmap_bufs;
	}

	(void)stress_temp_filename_args(args,
		filename, sizeof(filename), stress_mwc32());

	if ((info.fd = open(filename, O_CREAT | O_RDWR | O_TRUNC, S_IRUSR | S_IWUSR)) < 0) {
		pr_fail("%s: open %s failed, errno=%d (%s)\n",
			args->name, filename, errno, strerror(errno));
		goto tidy_unlink;
	}
	info.fs_type = stress_get_fs_type(filename);

	pseek_bytes = PSEEKIO_CHUNK_SCALE * pseek_procs * info.pseek_io_size;
	if (ftruncate(info.fd, (off_t)pseek_bytes) < 0) {
		pr_fail("%s: ftruncate '%s' to %zu bytes failed, errno=%d (%s)\n",
			args->name, filename, pseek_bytes, errno, strerror(errno));
		rc = EXIT_NO_RESOURCE;
		goto tidy_unlink;
	}
	(void)shim_unlink(filename);

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	for (i = 1; i < pseek_procs; i++) {
		if (stress_pseek_spawn(args, &procs[i]) < 0) {
			size_t j;

			for (j = 1; j < i; j++) {
				stress_pseek_kill(args, &procs[j]);
			}
			rc = EXIT_NO_RESOURCE;
			goto tidy_unlink;
		}
	}

	stress_peekio_exercise(&procs[0]);

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	for (i = 1; i < pseek_procs; i++) {
		stress_pseek_kill(args, &procs[i]);
	}

	for (i = 0; i < pseek_procs; i++) {
		if (procs[i].ret != 0) {
			rc = EXIT_FAILURE;
		}
		total_writes += procs[i].writes;
		total_writes_duration += procs[i].writes_duration;

		total_reads += procs[i].reads;
		total_reads_duration += procs[i].reads_duration;
	}

	rate = (total_reads_duration > 0.0) ? total_writes / total_reads_duration : 0.0;
	stress_metrics_set(args, 0, "MB/sec write rate",
		rate / (double)MB, STRESS_METRIC_HARMONIC_MEAN);
	rate = (total_writes_duration > 0.0) ? total_reads / total_writes_duration : 0.0;
	stress_metrics_set(args, 1, "MB/sec read rate",
		rate / (double)MB, STRESS_METRIC_HARMONIC_MEAN);

	(void)close(info.fd);
tidy_unlink:
	(void)shim_unlink(filename);
	(void)stress_temp_dir_rm_args(args);
tidy_munmap_bufs:
	for (i = 0; i < pseek_procs; i++)
		(void)munmap((void *)procs[i].buf, (size_t)info.pseek_io_size);
tidy_munmap_procs:
	(void)munmap((void *)procs, procs_size);

	return rc;
}

static const stress_opt_t opts[] = {
	{ OPT_pseek_rand,    "pseek-seek-rand",  TYPE_ID_BOOL, 0, 1, NULL },
	{ OPT_pseek_io_size, "pseek-io-size",    TYPE_ID_UINT64_BYTES_FS, MIN_PSEEKIO_IO_SIZE, MAX_PSEEKIO_IO_SIZE, NULL },
	END_OPT,
};

const stressor_info_t stress_pseek_info = {
	.stressor = stress_pseek,
	.classifier = CLASS_IO | CLASS_FILESYSTEM | CLASS_OS,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help
};
