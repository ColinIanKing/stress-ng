/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
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
			pr_inf("%s: ioperm out of memory, skipping stressor\n", name);
			return -1;
		case EPERM:
			pr_inf("%s has insufficient privilege, invoke with CAP_SYS_RAWIO privilege, skipping stressor\n", name);
			return -1;
		case EINVAL:
		case EIO:
		default:
			pr_inf("%s cannot access port 0x%x, not skipping stressor\n",
				name, IO_PORT);
			return -1;
		}
	}
	(void)ioperm(IO_PORT, 1, 0);
	return 0;
}

/*
 *  stress_ioport()
 *	stress performs I/O port I/O transactions
 */
static int stress_ioport(const stress_args_t *args)
{
	int ret, fd;
	uint32_t flag = 0;
	unsigned char v;

	(void)stress_get_setting("ioport-opts", &flag);
	if (!flag)
		flag = IOPORT_OPT_IN | IOPORT_OPT_OUT;

	ret = ioperm(IO_PORT, 1, 1);
	if (ret < 0) {
		pr_err("%s: cannot access port 0x%x, errno = %d (%s)\n",
			args->name, IO_PORT, errno, strerror(errno));
		return EXIT_FAILURE;
	}

	fd = open("/dev/port", O_RDWR);

	v = inb(IO_PORT);

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		if (flag & IOPORT_OPT_IN) {
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
		}
		if (flag & IOPORT_OPT_OUT) {
			outb(v + 15, IO_PORT);
			outb(v + 14, IO_PORT);
			outb(v + 13, IO_PORT);
			outb(v + 12, IO_PORT);
			outb(v + 11, IO_PORT);
			outb(v + 10, IO_PORT);
			outb(v + 9, IO_PORT);
			outb(v + 8, IO_PORT);
			outb(v + 7, IO_PORT);
			outb(v + 6, IO_PORT);
			outb(v + 5, IO_PORT);
			outb(v + 4, IO_PORT);
			outb(v + 3, IO_PORT);
			outb(v + 2, IO_PORT);
			outb(v + 1, IO_PORT);
			outb(v, IO_PORT);
		}

		if (fd >= 0) {
			off_t offset = IO_PORT, offret;

			offret = lseek(fd, offset, SEEK_SET);
			if (offret != (off_t)-1) {
				ssize_t n;
				unsigned char val;

				n = read(fd, &val, sizeof(val));
				if (n == sizeof(val)) {
					offret = lseek(fd, offset, SEEK_SET);
					if (offret != (off_t)-1) {
						val = ~v;
						n = write(fd, &val, sizeof(val));
						(void)n;
					}

					offret = lseek(fd, offset, SEEK_SET);
					if (offret != (off_t)-1) {
						val = v;
						n = write(fd, &val, sizeof(val));
						(void)n;
					}
				}
			}

		}

		/*
		 *  Exercise invalid ioperm settings
		 */
		ret = ioperm(IO_PORT, 0, 1);
		(void)ret;
		ret = ioperm(~0, 1, 1);
		(void)ret;
		ret = ioperm(IO_PORT, 1, 1);
		(void)ret;

#if defined(HAVE_IOPL)
		{
			static const int levels[] = {
				99, -1, 0, 1, 2, 3
			};
			size_t i;

			/*
			 *  Exercise various valud and invalid
			 *  iopl settings
			 */
			for (i = 0; i < SIZEOF_ARRAY(levels); i++) {
				ret = iopl(levels[i]);
				(void)ret;
			}
		}
#endif
		inc_counter(args);
	} while (keep_stressing(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	if (fd >= 0)
		(void)close(fd);

	(void)ioperm(IO_PORT, 1, 0);

	return EXIT_SUCCESS;
}

stressor_info_t stress_ioport_info = {
	.stressor = stress_ioport,
	.supported = stress_ioport_supported,
	.class = CLASS_CPU,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
#else
stressor_info_t stress_ioport_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_CPU,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
#endif
