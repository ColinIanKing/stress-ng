/*
 * Copyright (C) 2013-2019 Canonical, Ltd.
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

static sigjmp_buf jmp_env;
static volatile bool do_jmp = true;

static const help_t help[] = {
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
	(void)signum;

	if (do_jmp)
		siglongjmp(jmp_env, 1);		/* Ugly, bounce back */
}

/*
 *  stress_fault()
 *	stress min and max page faulting
 */
static int stress_fault(const args_t *args)
{
#if !defined(__HAIKU__)
	struct rusage usage;
#endif
	char filename[PATH_MAX];
	int ret;
	NOCLOBBER int i;
	char *start, *end;
	const size_t len = stress_text_addr(&start, &end);
	const size_t page_size = args->page_size;
	void *mapto;

	ret = stress_temp_dir_mk_args(args);
	if (ret < 0)
		return exit_status(-ret);

	(void)stress_temp_filename_args(args,
		filename, sizeof(filename), mwc32());
	i = 0;

	if (stress_sighandler(args->name, SIGSEGV, stress_segvhandler, NULL) < 0)
		return EXIT_FAILURE;
	if (stress_sighandler(args->name, SIGBUS, stress_segvhandler, NULL) < 0)
		return EXIT_FAILURE;

	mapto = mmap(NULL, page_size, PROT_READ,
		MAP_ANONYMOUS | MAP_SHARED, -1, 0);

	do {
		int fd;
		uint8_t *ptr;

		ret = sigsetjmp(jmp_env, 1);
		if (ret) {
			do_jmp = false;
			pr_err("%s: unexpected segmentation fault\n",
				args->name);
			break;
		}

		fd = open(filename, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
		if (fd < 0) {
			if ((errno == ENOSPC) || (errno == ENOMEM))
				continue;	/* Try again */
			pr_fail_err("open");
			break;
		}
#if defined(HAVE_POSIX_FALLOCATE)
		if (posix_fallocate(fd, 0, 1) < 0) {
			if (errno == ENOSPC) {
				(void)close(fd);
				continue;	/* Try again */
			}
			(void)close(fd);
			pr_fail_err("posix_fallocate");
			break;
		}
#else
		{
			char buffer[1];

redo:
			if (g_keep_stressing_flag &&
			    (write(fd, buffer, sizeof(buffer)) < 0)) {
				if ((errno == EAGAIN) || (errno == EINTR))
					goto redo;
				if (errno == ENOSPC) {
					(void)close(fd);
					continue;
				}
				(void)close(fd);
				pr_fail_err("write");
				break;
			}
		}
#endif
		ret = sigsetjmp(jmp_env, 1);
		if (ret) {
			if (!keep_stressing())
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
			(void)unlink(filename);

		ptr = (uint8_t *)mmap(NULL, 1, PROT_READ | PROT_WRITE,
			MAP_SHARED, fd, 0);
		(void)close(fd);
		fd = -1;
		(void)fd;

		if (ptr == MAP_FAILED) {
			if ((errno == EAGAIN) ||
			    (errno == ENOMEM) ||
			    (errno == ENFILE))
				goto next;
			pr_err("%s: mmap failed: errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			break;

		}
		*ptr = 0;	/* Cause the page fault */

		if (munmap((void *)ptr, 1) < 0) {
			pr_err("%s: munmap failed: errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			break;
		}

next:
		/* Remove file on-non major fault case */
		if (!(i & 1))
			(void)unlink(filename);

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
					uint8_put(*ptr);
					(void)munmap((void *)ptr, page_size);
				}
			}
		};
		i++;
		inc_counter(args);
	} while (keep_stressing());
	/* Clean up, most times this is redundant */

	if (mapto != MAP_FAILED)
		(void)munmap(mapto, page_size);
	(void)unlink(filename);
	(void)stress_temp_dir_rm_args(args);

#if !defined(__HAIKU__)
	if (!getrusage(RUSAGE_SELF, &usage)) {
		pr_dbg("%s: page faults: minor: %lu, major: %lu\n",
			args->name, usage.ru_minflt, usage.ru_majflt);
	}
#endif

	return EXIT_SUCCESS;
}

stressor_info_t stress_fault_info = {
	.stressor = stress_fault,
	.class = CLASS_INTERRUPT | CLASS_SCHEDULER | CLASS_OS,
	.help = help
};
