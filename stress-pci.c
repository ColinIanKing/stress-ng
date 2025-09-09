/*
 * Copyright (C)      2021 Canonical, Ltd.
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
#include "core-attribute.h"
#include "core-builtin.h"

static const stress_help_t help[] = {
	{ NULL,	"pci N",	 "start N workers that read and mmap PCI regions" },
	{ NULL,	"pci-dev name ", "specify the pci device 'xxxx:xx:xx.x' to exercise" },
	{ NULL,	"pci-ops N",	 "stop after N PCI bogo operations" },
	{ NULL,	NULL,		 NULL }
};

static const stress_opt_t opts[] = {
	{ OPT_pci_dev,      "pci-dev",      TYPE_ID_STR, 0, 0, NULL },
	{ OPT_pci_ops_rate, "pci-ops-rate", TYPE_ID_UINT32, 1, 1000000, NULL },
	END_OPT,
};

#if defined(__linux__)

static sigjmp_buf jmp_env;

#define PCI_METRICS_CONFIG		(0)
#define PCI_METRICS_RESOURCE		(1)
#define PCI_METRICS_MAX			(2)

typedef struct stress_pci_info {
	char *path;				/* PCI /sysfs path name */
	char *name;				/* PCI dev name */
	bool ignore;				/* true = ignore the entry */
	stress_metrics_t metrics[PCI_METRICS_MAX]; /* PCI read rate metrics */
	struct stress_pci_info	*next;		/* next in list */
} stress_pci_info_t;

/*
 *  stress_pci_dev_filter()
 *	scandir filter on /sys/devicex/pci[0-9]/xxxx:xx:xx.x where x is a hex
 */
static int CONST stress_pci_dev_filter(const struct dirent *d)
{
	unsigned int dummy[4];

	/* Check it meets format 0000:00:00.0 */
	if (sscanf(d->d_name, "%x:%x:%x.%x",
		&dummy[0], &dummy[1], &dummy[2], &dummy[3]) == 4)
		return 1;
	return 0;
}

/*
 *  stress_pci_dot_filter()
 *	scandir filter out dot filenames
 */
static int CONST stress_pci_dot_filter(const struct dirent *d)
{
	if (d->d_name[0] == '.')
		return 0;
	return 1;
}

/*
 *  stress_pci_info_free()
 *	free list of stress_pci_info_t items
 */
static void stress_pci_info_free(stress_pci_info_t *pci_info_list)
{
	stress_pci_info_t *pci_info = pci_info_list;

	while (pci_info) {
		stress_pci_info_t *next = pci_info->next;

		free(pci_info->path);
		free(pci_info->name);
		free(pci_info);
		pci_info = next;
	}
}

static int stress_pci_rev_sort(const struct dirent **a, const struct dirent **b)
{
	return strcmp((*b)->d_name, (*a)->d_name);
}

static const char sys_pci_devices[] = "/sys/bus/pci/devices";

/*
 *  stress_pci_info_get_by_name()
 *	find PCI info with given name, add to pci_info_list
 */
static void stress_pci_info_get_by_name(stress_pci_info_t **pci_info_list, const char *name)
{
	stress_pci_info_t *pci_info = NULL;

	pci_info = (stress_pci_info_t *)calloc(1, sizeof(*pci_info));
	if (LIKELY(pci_info != NULL)) {
		char pci_path[PATH_MAX];

		(void)snprintf(pci_path, sizeof(pci_path),
			"%s/%s", sys_pci_devices, name);
		pci_info->path = shim_strdup(pci_path);
		if (UNLIKELY(!pci_info->path)) {
			free(pci_info);
			return;
		}
		pci_info->name = shim_strdup(name);
		if (UNLIKELY(!pci_info->name)) {
			free(pci_info->path);
			free(pci_info);
			return;
		}

		stress_zero_metrics(pci_info->metrics, PCI_METRICS_MAX);
		pci_info->ignore = false;
		pci_info->next = *pci_info_list;
		*pci_info_list = pci_info;
	}
}

/*
 *  stress_pci_info_get()
 *	get a list of PCI device paths of stress_pci_info items
 */
static stress_pci_info_t *stress_pci_info_get(void)
{
	stress_pci_info_t *pci_info_list = NULL;

	struct dirent **pci_list = NULL;
	char *pci_dev = NULL;

	(void)stress_get_setting("pci-dev", &pci_dev);

	if (pci_dev) {
		char pci_path[PATH_MAX];
		struct stat statbuf;

		(void)snprintf(pci_path, sizeof(pci_path),
			"%s/%s", sys_pci_devices, pci_dev);
		if (shim_stat(pci_path, &statbuf) == 0) {
			stress_pci_info_get_by_name(&pci_info_list, pci_dev);
		}
	} else {
		int n_devs, i;

		n_devs = scandir(sys_pci_devices, &pci_list, stress_pci_dev_filter, stress_pci_rev_sort);
		for (i = 0; i < n_devs; i++) {
			stress_pci_info_get_by_name(&pci_info_list, pci_list[i]->d_name);
			free(pci_list[i]);
		}
		free(pci_list);
	}

	return pci_info_list;
}

/*
 *  stress_pci_exercise_file()
 *	exercise a PCI file,
 */
static void stress_pci_exercise_file(
	stress_pci_info_t *pci_info,
	const char *name,
	const bool config,
	const bool resource,
	const bool rom)
{
	char path[PATH_MAX];
	int fd;

	(void)snprintf(path, sizeof(path), "%s/%s", pci_info->path, name);
	fd = open(path, rom ? O_RDWR : O_RDONLY);
	if (fd >= 0) {
		void *ptr;
		char buf[4096];
		size_t sz;
		struct stat statbuf;
		size_t n_left, n_read;
		double t;

		if (shim_fstat(fd, &statbuf) < 0)
			goto err;
		if (!S_ISREG(statbuf.st_mode))
			goto err;

		sz = STRESS_MINIMUM(sizeof(buf), (size_t)statbuf.st_size);
		if (UNLIKELY(rom)) {
			VOID_RET(ssize_t, write(fd, "1\n", 2));
		}

		ptr = mmap(NULL, sz, PROT_READ, MAP_SHARED, fd, 0);
		if (ptr != MAP_FAILED)
			(void)munmap(ptr, sz);

		/*
		 *  PCI ROM reads on some systems cause issues because
		 *  the ROM sizes are incorrectly reported, so don't
		 *  read memory for ROMs at the moment, cf.:
		 *  https://github.com/ColinIanKing/stress-ng/issues/255
		 */
		if (LIKELY(!rom)) {
			n_left = sz;
			n_read = 0;
			t = stress_time_now();
			while (n_left != 0) {
				const size_t n_cpy = n_left > sizeof(buf) ? sizeof(buf) : n_left;
				ssize_t n;

				n = read(fd, buf, n_cpy);
				if (n <= 0)
					break;
				n_left -= n;
				n_read += n;
			}
			if (LIKELY(n_read > 0)) {
				if (config) {
					pci_info->metrics[PCI_METRICS_CONFIG].duration += stress_time_now() - t;
					pci_info->metrics[PCI_METRICS_CONFIG].count += n_read;
				} else if (resource) {
					pci_info->metrics[PCI_METRICS_RESOURCE].duration += stress_time_now() - t;
					pci_info->metrics[PCI_METRICS_RESOURCE].count += n_read;
				}
			}
		} else {
			VOID_RET(ssize_t, write(fd, "0\n", 2));
		}
err:
		(void)close(fd);
	}
}

/*
 *  stress_pci_exercise()
 *	exercise all PCI files in a given PCI info path
 */
static void stress_pci_exercise(stress_args_t *args, stress_pci_info_t *pci_info)
{
	int i, n;
	struct dirent **list = NULL;

	n = scandir(pci_info->path, &list, stress_pci_dot_filter, alphasort);
	if (n == 0)
		pci_info->ignore = true;

	for (i = 0; LIKELY(stress_continue(args) && (i < n)); i++) {
		const char *name = list[i]->d_name;
		const bool config = !strcmp(name, "config");
		const bool resource = !strncmp(name, "resource", 8);
		const bool rom = !strcmp(name, "rom");

		stress_pci_exercise_file(pci_info, name, config, resource, rom);
	}

	for (i = 0; i < n; i++) {
		free(list[i]);
		list[i] = NULL;
	}
	free(list);
}

/*
 *  stress_pci_handler()
 *	handle unexpected SIGSEGV or SIGBUS errors when mmaping
 *	or reading PCI data
 */
static void NORETURN MLOCKED_TEXT stress_pci_handler(int signum)
{
	(void)signum;

	siglongjmp(jmp_env, 1);
	stress_no_return();
}

static void stress_pci_rate(const stress_metrics_t *metrics, char *str, const size_t len)
{
	if (metrics->duration > 0.0)
		(void)snprintf(str, len, "%8.2f", (metrics->count / metrics->duration) / MB);
	else
		(void)snprintf(str, len, "%8s", "untested");
}

/*
 *  stress_pci()
 *	stress /sysfs PCI files with open/read/close and mmap where possible
 */
static int stress_pci(stress_args_t *args)
{
	NOCLOBBER stress_pci_info_t *pci_info_list;
	NOCLOBBER stress_pci_info_t *pci_info;
	int ret;
	uint32_t pci_ops_rate = 0;	/* zero = unlimited */
	double t_start;
	NOCLOBBER double t_delta;

	(void)stress_get_setting("pci-ops-rate", &pci_ops_rate);
	t_delta = pci_ops_rate > 0 ? (double)args->instances / (double)pci_ops_rate : 0.0;

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	ret = sigsetjmp(jmp_env, 1);
	if (ret) {
		pr_inf("%s: unexpected SIGSEGV/SIGBUS, aborting\n", args->name);
		return EXIT_FAILURE;
	}

	if (stress_sighandler(args->name, SIGSEGV, stress_pci_handler, NULL) < 0)
		return EXIT_FAILURE;
	if (stress_sighandler(args->name, SIGBUS, stress_pci_handler, NULL) < 0)
		return EXIT_FAILURE;

	pci_info_list = stress_pci_info_get();
	if (!pci_info_list) {
		pr_inf_skip("%s: no PCI sysfs entries found, skipping stressor\n",
			args->name);
		return EXIT_NO_RESOURCE;
	}

	t_start = stress_time_now();
	do {
		for (pci_info = pci_info_list; pci_info; pci_info = pci_info->next) {
			if (UNLIKELY(!stress_continue(args)))
				break;
			ret = sigsetjmp(jmp_env, 1);

			if (ret) {
				pci_info->ignore = true;
				continue;
			}

			if (pci_info->ignore)
				continue;

			stress_pci_exercise(args, pci_info);
			stress_bogo_inc(args);
			if (pci_ops_rate > 0) {
				const double t_next = t_start + (stress_bogo_get(args) * t_delta);
				const double t_sleep = t_next - stress_time_now();

				if (LIKELY(t_sleep > 0.0)) {
					const uint64_t ns = (uint64_t)(t_sleep * STRESS_DBL_NANOSECOND);

					shim_nanosleep_uint64(ns);
				}
			}
		}
	} while (stress_continue(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	if (stress_instance_zero(args)) {
		pr_block_begin();
		pr_inf("%s: PCI space read rates in MB per sec for stressor instance 0:\n", args->name);
		pr_inf("%s: PCI Device     Config Resource\n", args->name);

		for (pci_info = pci_info_list; pci_info; pci_info = pci_info->next) {
			char rate_config[9], rate_resource[9];

			stress_pci_rate(&pci_info->metrics[PCI_METRICS_CONFIG], rate_config, sizeof(rate_config));
			stress_pci_rate(&pci_info->metrics[PCI_METRICS_RESOURCE], rate_resource, sizeof(rate_resource));
			pr_inf("%s: %s %8s %8s\n", args->name, pci_info->name, rate_config, rate_resource);
		}
		pr_block_end();
	}

	stress_pci_info_free(pci_info_list);

	return EXIT_SUCCESS;
}

const stressor_info_t stress_pci_info = {
	.stressor = stress_pci,
	.classifier = CLASS_OS,
	.opts = opts,
	.help = help
};
#else
const stressor_info_t stress_pci_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_OS,
	.opts = opts,
	.help = help,
	.unimplemented_reason = "only supported on Linux"
};
#endif
