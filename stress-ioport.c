/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2024 Colin Ian King.
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
#include "core-arch.h"

#if defined(HAVE_SYS_IO_H)
#include <sys/io.h>
#endif

#define IO_PORT		0x80

#define IOPORT_OPT_IN	0x00000001
#define IOPORT_OPT_OUT	0x00000002

typedef struct {
	const char 	*opt;
	const uint32_t	flag;
} stress_ioport_opts_t;

static const stress_ioport_opts_t ioport_opts[] = {
	{ "in",		IOPORT_OPT_IN },
	{ "out",	IOPORT_OPT_OUT },
	{ "inout",	IOPORT_OPT_IN | IOPORT_OPT_OUT },
};

static const stress_help_t help[] = {
	{ NULL,	"ioport N",	"start N workers exercising port I/O" },
	{ NULL,	"ioport-ops N",	"stop ioport workers after N port bogo operations" },
	{ NULL,	NULL,		NULL }
};

static int stress_set_ioport_opts(const char *opts)
{
	size_t i;

	for (i = 0; i < SIZEOF_ARRAY(ioport_opts); i++) {
		if (!strcmp(opts, ioport_opts[i].opt)) {
			uint32_t flag = ioport_opts[i].flag;

			stress_set_setting("ioport-opts", TYPE_ID_UINT32, &flag);
			return 0;
		}
	}

	(void)fprintf(stderr, "ioport-opt option '%s' not known, options are:", opts);
	for (i = 0; i < SIZEOF_ARRAY(ioport_opts); i++) {
		(void)fprintf(stderr, "%s %s",
			i == 0 ? "" : ",", ioport_opts[i].opt);
	}
	(void)fprintf(stderr, "\n");
	return -1;
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_ioport_opts,	stress_set_ioport_opts },
	{ 0,			NULL }
};

#if defined(STRESS_ARCH_X86) && 	\
    defined(HAVE_IOPORT) &&	\
    defined(HAVE_SYS_IO_H)

static int stress_ioport_supported(const char *name)
{
	int ret;

	ret = ioperm(IO_PORT, 1, 1);
	if (ret < 0) {
		switch (errno) {
		case ENOMEM:
			pr_inf_skip("%s: ioperm out of memory, skipping stressor\n", name);
			return -1;
		case EPERM:
			pr_inf_skip("%s has insufficient privilege, invoke with CAP_SYS_RAWIO privilege, skipping stressor\n", name);
			return -1;
		case EINVAL:
		case EIO:
		default:
			pr_inf_skip("%s cannot access port 0x%x, not skipping stressor\n",
				name, IO_PORT);
			return -1;
		}
	}
	(void)ioperm(IO_PORT, 1, 0);
	return 0;
}

/*
 *  stress_ioport_ioperm()
 *	simple ioperm sanity check for invalid argument tests
 */
static int stress_ioport_ioperm(
	stress_args_t *args,
	unsigned long from,
	unsigned long num,
	int turn_on)
{
	if (ioperm(from, num, turn_on) == 0) {
		pr_fail("%s: ioperm(%lu, %lu, %d) unexpectedly succeeded, expected error EINVAL\n",
			args->name, from, num, turn_on);
		return -1;
	}
	return 0;
}

/*
 *  stress_ioport()
 *	stress performs I/O port I/O transactions
 */
static int stress_ioport(stress_args_t *args)
{
	int ret, fd, rc = EXIT_SUCCESS;
	uint32_t flag = 0;
	unsigned char v;
	double duration_in = 0.0, count_in = 0.0;
	double duration_out = 0.0, count_out = 0.0;
	double rate;
	char msg[40];

	(void)stress_get_setting("ioport-opts", &flag);
	if (!flag)
		flag = IOPORT_OPT_IN | IOPORT_OPT_OUT;

	ret = ioperm(IO_PORT, 1, 1);
	if (ret < 0) {
		pr_err("%s: cannot access port 0x%x, errno=%d (%s)\n",
			args->name, IO_PORT, errno, strerror(errno));
		return EXIT_FAILURE;
	}

	fd = open("/dev/port", O_RDWR);

	v = inb(IO_PORT);

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		double t;

		if (flag & IOPORT_OPT_IN) {
			t = stress_time_now();
			(void)inb(IO_PORT);
			(void)inb(IO_PORT);
			(void)inb(IO_PORT);
			(void)inb(IO_PORT);
			(void)inb(IO_PORT);
			(void)inb(IO_PORT);
			(void)inb(IO_PORT);
			(void)inb(IO_PORT);
			(void)inb(IO_PORT);
			(void)inb(IO_PORT);
			(void)inb(IO_PORT);
			(void)inb(IO_PORT);
			(void)inb(IO_PORT);
			(void)inb(IO_PORT);
			(void)inb(IO_PORT);
			(void)inb(IO_PORT);
			(void)inb(IO_PORT);
			(void)inb(IO_PORT);
			(void)inb(IO_PORT);
			(void)inb(IO_PORT);
			(void)inb(IO_PORT);
			(void)inb(IO_PORT);
			(void)inb(IO_PORT);
			(void)inb(IO_PORT);
			(void)inb(IO_PORT);
			(void)inb(IO_PORT);
			(void)inb(IO_PORT);
			(void)inb(IO_PORT);
			(void)inb(IO_PORT);
			(void)inb(IO_PORT);
			(void)inb(IO_PORT);
			(void)inb(IO_PORT);
			duration_in += stress_time_now() - t;
			count_in += 32.0;
		}
		if (flag & IOPORT_OPT_OUT) {
			t = stress_time_now();
			outb(v, IO_PORT);
			outb(v, IO_PORT);
			outb(v, IO_PORT);
			outb(v, IO_PORT);
			outb(v, IO_PORT);
			outb(v, IO_PORT);
			outb(v, IO_PORT);
			outb(v, IO_PORT);
			outb(v, IO_PORT);
			outb(v, IO_PORT);
			outb(v, IO_PORT);
			outb(v, IO_PORT);
			outb(v, IO_PORT);
			outb(v, IO_PORT);
			outb(v, IO_PORT);
			outb(v, IO_PORT);
			outb(v, IO_PORT);
			outb(v, IO_PORT);
			outb(v, IO_PORT);
			outb(v, IO_PORT);
			outb(v, IO_PORT);
			outb(v, IO_PORT);
			outb(v, IO_PORT);
			outb(v, IO_PORT);
			outb(v, IO_PORT);
			outb(v, IO_PORT);
			outb(v, IO_PORT);
			outb(v, IO_PORT);
			outb(v, IO_PORT);
			outb(v, IO_PORT);
			outb(v, IO_PORT);
			outb(v, IO_PORT);
			duration_out += stress_time_now() - t;
			count_out += 32;
		}

		if (fd >= 0) {
			const off_t offset = IO_PORT;
			off_t offret;

			offret = lseek(fd, offset, SEEK_SET);
			if (offret != (off_t)-1) {
				ssize_t n;
				unsigned char val;

				n = read(fd, &val, sizeof(val));
				if (n == sizeof(val)) {
					offret = lseek(fd, offset, SEEK_SET);
					if (offret != (off_t)-1) {
						val = ~v;
						VOID_RET(ssize_t, write(fd, &val, sizeof(val)));
					}

					offret = lseek(fd, offset, SEEK_SET);
					if (offret != (off_t)-1) {
						val = v;
						VOID_RET(ssize_t, write(fd, &val, sizeof(val)));
					}
				}
			}
		}

		/*
		 *  Exercise invalid ioperm settings
		 */
		if (stress_ioport_ioperm(args, IO_PORT, 0, 1) < 0) {
			rc = EXIT_FAILURE;
			break;
		}
		if (stress_ioport_ioperm(args, ~0UL, 1, 1) < 0) {
			rc = EXIT_FAILURE;
			break;
		}
		if (stress_ioport_ioperm(args, IO_PORT, ~0UL, 1) < 0) {
			rc = EXIT_FAILURE;
			break;
		}
		if (stress_ioport_ioperm(args, 0, ~0UL, 0) < 0) {
			rc = EXIT_FAILURE;
			break;
		}
		if (stress_ioport_ioperm(args, 0, 0, 0) < 0) {
			rc = EXIT_FAILURE;
			break;
		}
		if (stress_ioport_ioperm(args, ~0, 0, 0) < 0) {
			rc = EXIT_FAILURE;
			break;
		}

		/* iopl is deprecated, but exercise it anyhow */
#if defined(HAVE_IOPL)
		{
			static const int levels[] = {
				99, -1, 0, 1, 2, 3
			};
			size_t i;

			/*
			 *  Exercise various valid and invalid
			 *  iopl settings
			 */
			for (i = 0; i < SIZEOF_ARRAY(levels); i++) {
				VOID_RET(int, iopl(levels[i]));
			}
		}
#else
		UNEXPECTED
#endif
		stress_bogo_inc(args);
	} while (stress_continue(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	rate = count_in > 0.0 ? duration_in / count_in : 0.0;
	(void)snprintf(msg, sizeof(msg), "nanosecs per inb(0x%x) op", IO_PORT);
	stress_metrics_set(args, 0, msg,
		rate * STRESS_DBL_NANOSECOND, STRESS_METRIC_HARMONIC_MEAN);

	rate = count_out > 0.0 ? duration_out / count_out : 0.0;
	(void)snprintf(msg, sizeof(msg), "nanosecs per outb(0x%x) op", IO_PORT);
	stress_metrics_set(args, 1, msg,
		rate * STRESS_DBL_NANOSECOND, STRESS_METRIC_HARMONIC_MEAN);

	if (fd >= 0)
		(void)close(fd);

	(void)ioperm(IO_PORT, 1, 0);

	return rc;
}

stressor_info_t stress_ioport_info = {
	.stressor = stress_ioport,
	.supported = stress_ioport_supported,
	.class = CLASS_CPU,
	.opt_set_funcs = opt_set_funcs,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
stressor_info_t stress_ioport_info = {
	.stressor = stress_unimplemented,
	.class = CLASS_CPU,
	.opt_set_funcs = opt_set_funcs,
	.help = help,
	.verify = VERIFY_ALWAYS,
	.unimplemented_reason = "not x86 CPU and/or not built with ioport() support"
};
#endif
