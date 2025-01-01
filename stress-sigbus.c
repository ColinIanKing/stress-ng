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

static sigjmp_buf jmp_env;
#if defined(SA_SIGINFO)
static volatile void *fault_addr;
static volatile void *expected_addr;
static volatile int signo;
static volatile int code;
#endif

static const stress_help_t help[] = {
	{ NULL,	"sigbus N",	"start N workers generating bus faults" },
	{ NULL,	"sigbus-ops N",	"stop after N bogo bus faults" },
	{ NULL,	NULL,		NULL }
};

/*
 *  stress_bushandler()
 *	SIGBUS handler
 */
#if defined(SA_SIGINFO)
static void NORETURN MLOCKED_TEXT stress_bushandler(
	int num,
	siginfo_t *info,
	void *ucontext)
{
	(void)num;
	(void)ucontext;

	fault_addr = info->si_addr;
	signo = info->si_signo;
	code = info->si_code;

	siglongjmp(jmp_env, 1);		/* Ugly, bounce back */
}
#else
static void NORETURN MLOCKED_TEXT stress_bushandler(int signum)
{
	(void)signum;

	siglongjmp(jmp_env, 1);		/* Ugly, bounce back */
}
#endif

/*
 *  stress_sigbus
 *	stress by generating segmentation faults by
 *	writing to a read only page
 */
static int stress_sigbus(stress_args_t *args)
{
	int ret, fd;
	char filename[PATH_MAX];
	const char *fs_type;
	NOCLOBBER uint8_t *ptr;
	NOCLOBBER int rc = EXIT_FAILURE;
	const size_t page_size = args->page_size;
#if defined(SA_SIGINFO)
	const bool verify = !!(g_opt_flags & OPT_FLAGS_VERIFY);
#endif

	ret = stress_temp_dir_mk_args(args);
	if (ret < 0)
		return stress_exit_status(-ret);
	(void)stress_temp_filename_args(args, filename, sizeof(filename), stress_mwc32());
	if ((fd = open(filename, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR)) < 0) {
		rc  = stress_exit_status(errno);
		pr_fail("%s: open %s failed, errno=%d (%s)\n",
			args->name, filename, errno, strerror(errno));
		goto tidy_dir;
        }
	fs_type = stress_get_fs_type(filename);
	(void)shim_unlink(filename);

	ret = shim_posix_fallocate(fd, 0, page_size * 2);
	if (ret != 0) {
		if (ret != EINTR) {
			pr_inf_skip("%s: posix_fallocate failed, no free space, errno=%d (%s)%s, skipping stressor\n",
				args->name, ret, strerror(ret), fs_type);
		}
		rc = EXIT_NO_RESOURCE;
		goto tidy_close;
	}

	/* Allocate 2 pages */
	ptr = (uint8_t *)stress_mmap_populate(NULL, page_size * 2,
		PROT_READ | PROT_WRITE,
		MAP_SHARED, fd, 0);
	if (ptr == MAP_FAILED) {
		pr_inf_skip("%s: mmap of read only page failed: "
			"errno=%d (%s), skipping stressor\n",
			args->name, errno, strerror(errno));
		rc = EXIT_NO_RESOURCE;
		goto tidy_close;
	}
	/* And remove last page on backing file */
	ret = ftruncate(fd, page_size);
	if (ret < 0) {
		pr_fail("%s: ftruncate file to a single page failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		rc = EXIT_FAILURE;
		goto tidy_mmap;
	}

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	for (;;) {
		struct sigaction action;

		(void)shim_memset(&action, 0, sizeof action);
#if defined(SA_SIGINFO)
		action.sa_sigaction = stress_bushandler;
#else
		action.sa_handler = stress_bushandler;
#endif
		(void)sigemptyset(&action.sa_mask);
#if defined(SA_SIGINFO)
		action.sa_flags = SA_SIGINFO;
#endif
		ret = sigaction(SIGBUS, &action, NULL);
		if (ret < 0) {
			pr_fail("%s: sigaction SIGBUS: errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			goto tidy_mmap;
		}
		/* Some systems generate SIGSEGV rather than SIGBUS.. */
		ret = sigaction(SIGSEGV, &action, NULL);
		if (ret < 0) {
			pr_fail("%s: sigaction SIGSEGV: errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			goto tidy_mmap;
		}

		ret = sigsetjmp(jmp_env, 1);
		/*
		 * We return here if we get a SIGBUS, so
		 * first check if we need to terminate
		 */
		if (!stress_continue(args))
			break;

		if (ret) {
			/* Signal was tripped */
#if defined(SA_SIGINFO)
			if (verify && expected_addr && fault_addr && (fault_addr != expected_addr)) {
				pr_fail("%s: expecting fault address %p, got %p instead\n",
					args->name, (volatile void *)expected_addr, fault_addr);
			}
			/* We may also have SIGSEGV on some system as well as SIGBUS */
			if (verify &&
			    (signo != -1) &&
			    (signo != SIGBUS) &&
			    (signo != SIGSEGV)) {
				pr_fail("%s: expecting SIGBUS, got %s instead\n",
					args->name, strsignal(signo));
			}
			/* Just verify SIGBUS signals */
			if (verify && (signo == SIGBUS)) {
				switch (code) {
#if defined(BUS_ADRALN)
				case BUS_ADRALN:
					break;
#endif
#if defined(BUS_ADRERR)
				case BUS_ADRERR:
					break;
#endif
#if defined(BUS_OBJERR)
				case BUS_OBJERR:
					break;
#endif
				default:
					pr_fail("%s: unexpecting SIGBUS si_code %d\n",
						args->name, code);
					break;
				}
			}
#endif
			stress_bogo_inc(args);
		} else {
#if defined(SA_SIGINFO)
			signo = -1;
			code = -1;
			fault_addr = NULL;
			expected_addr = NULL;
#endif
			/*
			 *  Some architectures generate SIGBUS on misaligned writes, so
			 *  try these for 50% of the time. If these fail on systems that
			 *  support misaligned access we fall through to accessing an
			 *  unbacked file mapping
			 */
			if (stress_mwc1()) {
				static uint64_t data[2];
				uint8_t *ptr8 = (uint8_t *)data;
				uint64_t *ptr64 = (uint64_t *)(ptr8 + 1);
				uint32_t *ptr32 = (uint32_t *)(ptr8 + 1);
				uint16_t *ptr16 = (uint16_t *)(ptr8 + 1);

				(*ptr64)++;
				(*ptr32)++;
				(*ptr16)++;
			}

			/* Access un-backed file mmapping */
			(*(ptr + page_size))++;
		}
	}
	rc = EXIT_SUCCESS;
tidy_mmap:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	(void)munmap((void *)ptr, args->page_size);
tidy_close:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	(void)close(fd);
tidy_dir:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	(void)stress_temp_dir_rm_args(args);
	return rc;
}

const stressor_info_t stress_sigbus_info = {
	.stressor = stress_sigbus,
	.class = CLASS_SIGNAL | CLASS_OS,
#if defined(SA_SIGINFO)
	.verify = VERIFY_OPTIONAL,
#endif
	.help = help
};
