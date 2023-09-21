// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
 *
 */
#include "stress-ng.h"
#include "core-put.h"

static sigjmp_buf jmp_env;
static volatile bool do_jmp = true;
static volatile int die_signum = -1;

static const stress_help_t help[] = {
	{ NULL,	"fault N",	"start N workers producing page faults" },
	{ NULL,	"fault-ops N",	"stop after N page fault bogo operations" },
	{ NULL,	NULL,		NULL }
};

/*
 *  stress_segvhandler()
 *	SEGV handler
 */
static void MLOCKED_TEXT stress_segvhandler(int signum)
{
	die_signum = signum;

	if (do_jmp)
		siglongjmp(jmp_env, 1);		/* Ugly, bounce back */
}

/*
 *  stress_fault()
 *	stress min and max page faulting
 */
static int stress_fault(const stress_args_t *args)
{
#if defined(HAVE_GETRUSAGE) &&		\
    defined(RUSAGE_SELF) &&		\
    defined(HAVE_RUSAGE_RU_MINFLT)
	struct rusage usage;
#endif
	char filename[PATH_MAX];
	int ret;
	NOCLOBBER int i;
	char *start, *end;
	const size_t len = stress_exec_text_addr(&start, &end);
	const size_t page_size = args->page_size;
	void *mapto;
#if defined(HAVE_GETRUSAGE) &&		\
    defined(RUSAGE_SELF) &&		\
    defined(HAVE_RUSAGE_RU_MINFLT)
	double t1 = 0.0, t2 = 0.0, dt;
#endif
	NOCLOBBER double duration = 0.0, count = 0.0;

	ret = stress_temp_dir_mk_args(args);
	if (ret < 0)
		return stress_exit_status(-ret);

	(void)stress_temp_filename_args(args,
		filename, sizeof(filename), stress_mwc32());

	if (stress_sighandler(args->name, SIGSEGV, stress_segvhandler, NULL) < 0)
		return EXIT_FAILURE;
	if (stress_sighandler(args->name, SIGBUS, stress_segvhandler, NULL) < 0)
		return EXIT_FAILURE;

	mapto = mmap(NULL, page_size, PROT_READ,
		MAP_ANONYMOUS | MAP_SHARED, -1, 0);

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

#if defined(HAVE_GETRUSAGE) &&		\
    defined(RUSAGE_SELF) &&		\
    defined(HAVE_RUSAGE_RU_MINFLT)
	t1 = stress_time_now();
#endif
	i = 0;
	do {
		int fd;
		uint8_t *ptr;
		double t;

		ret = sigsetjmp(jmp_env, 1);
		if (ret) {
			do_jmp = false;
			pr_fail("%s: unexpected %s, terminating early\n",
				args->name, stress_strsignal(die_signum));
			break;
		}

		fd = open(filename, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
		if (fd < 0) {
			if ((errno == ENOSPC) || (errno == ENOMEM))
				continue;	/* Try again */
			pr_fail("%s: open %s failed, errno=%d (%s)\n",
				args->name, filename, errno, strerror(errno));
			break;
		}
#if defined(HAVE_POSIX_FALLOCATE)
		if (shim_posix_fallocate(fd, 0, 1) < 0) {
			if ((errno == ENOSPC) || (errno == EINTR)) {
				(void)close(fd);
				continue;	/* Try again */
			}
			(void)close(fd);
			pr_fail("%s: posix_fallocate failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			break;
		}
#else
		{
			char buffer[1];

redo:
			if (stress_continue_flag() &&
			    (write(fd, buffer, sizeof(buffer)) < 0)) {
				if ((errno == EAGAIN) || (errno == EINTR))
					goto redo;
				if (errno == ENOSPC) {
					(void)close(fd);
					continue;
				}
				(void)close(fd);
				pr_fail("%s: write failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				break;
			}
		}
#endif
		ret = sigsetjmp(jmp_env, 1);
		if (ret) {
			if (!stress_continue(args))
				do_jmp = false;
			if (fd != -1)
				(void)close(fd);
			goto next;
		}

		/*
		 * Removing file here causes major fault when we touch
		 * ptr later
		 */
		if (i & 1)
			(void)shim_unlink(filename);

		ptr = (uint8_t *)mmap(NULL, 1, PROT_READ | PROT_WRITE,
			MAP_SHARED, fd, 0);
		(void)close(fd);

		if (ptr == MAP_FAILED) {
			if ((errno == EAGAIN) ||
			    (errno == ENOMEM) ||
			    (errno == ENFILE))
				goto next;
			pr_err("%s: mmap failed: errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			break;

		}
		t = stress_time_now();
		*ptr = 0;	/* Cause the page fault */
		duration += stress_time_now() - t;
		count += 1.0;

#if defined(HAVE_MADVISE) &&	\
    defined(MADV_DONTNEED)
		if (madvise((void *)ptr, page_size, MADV_DONTNEED) == 0) {
			t = stress_time_now();
			*ptr = 0;	/* Cause the page fault */
			duration += stress_time_now() - t;
			count += 1.0;
		}
#endif

#if defined(HAVE_MADVISE) &&	\
    defined(MADV_PAGEOUT)
		if (madvise((void *)ptr, page_size, MADV_PAGEOUT) == 0) {
			t = stress_time_now();
			*ptr = 0;	/* Cause the page fault */
			duration += stress_time_now() - t;
			count += 1.0;
		}
#endif
		if (stress_munmap_retry_enomem((void *)ptr, page_size) < 0) {
			pr_err("%s: munmap failed: errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			break;
		}

next:
		/* Remove file on-non major fault case */
		if (!(i & 1))
			(void)shim_unlink(filename);

		/*
		 *  Force a minor page fault by remapping an existing
		 *  page in the text segment onto page mapto and then
		 *  force reading a byte from the start of the page.
		 */
		if (len > (page_size << 1)) {
			if (mapto != MAP_FAILED) {
				ptr = (uint8_t *)mmap(mapto, page_size, PROT_READ,
					MAP_ANONYMOUS | MAP_SHARED, -1, 0);
				if (ptr != MAP_FAILED) {
					stress_uint8_put(*ptr);
#if defined(HAVE_MADVISE) &&	\
    defined(MADV_DONTNEED)
					if (madvise((void *)ptr, page_size, MADV_DONTNEED) == 0) {
						t = stress_time_now();
						stress_uint8_put(*ptr);
						duration += stress_time_now() - t;
						count += 1.0;
					}
#endif
					(void)stress_munmap_retry_enomem((void *)ptr, page_size);
				}
			}
		}
		i++;
		stress_bogo_inc(args);
	} while (stress_continue(args));

	(void)stress_sighandler_default(SIGBUS);
	(void)stress_sighandler_default(SIGSEGV);

#if defined(HAVE_GETRUSAGE) &&		\
    defined(RUSAGE_SELF) &&		\
    defined(HAVE_RUSAGE_RU_MINFLT)
	t2 = stress_time_now();
#endif
	/* Clean up, most times this is redundant */

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	if (mapto != MAP_FAILED)
		(void)munmap(mapto, page_size);
	(void)shim_unlink(filename);
	(void)stress_temp_dir_rm_args(args);

#if defined(HAVE_GETRUSAGE) &&		\
    defined(RUSAGE_SELF) &&		\
    defined(HAVE_RUSAGE_RU_MINFLT)
	if (!shim_getrusage(RUSAGE_SELF, &usage)) {
		pr_dbg("%s: page faults: minor: %lu, major: %lu\n",
			args->name, usage.ru_minflt, usage.ru_majflt);
	}
	dt = t2 - t1;
	if (dt > 0.0) {
		double average_duration;

		stress_metrics_set(args, 0, "minor page faults per sec",
			(double)usage.ru_minflt / dt);
		stress_metrics_set(args, 1, "major page faults per sec",
			(double)usage.ru_majflt / dt);
		average_duration = (count > 0.0) ? duration / count : 0.0;
		stress_metrics_set(args, 2, "nanosecs per page fault", average_duration * STRESS_DBL_NANOSECOND);
	}
#endif
	return EXIT_SUCCESS;
}

stressor_info_t stress_fault_info = {
	.stressor = stress_fault,
	.class = CLASS_INTERRUPT | CLASS_SCHEDULER | CLASS_OS,
	.help = help
};
