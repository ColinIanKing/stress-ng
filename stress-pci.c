// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C)      2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
 *
 */
#include "stress-ng.h"

static const stress_help_t help[] = {
	{ NULL,	"pci N",	"start N workers that read and mmap PCI regions" },
	{ NULL,	"pci-ops N",	"stop after N PCI bogo operations" },
	{ NULL,	NULL,		NULL }
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
static int stress_pci_dev_filter(const struct dirent *d)
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
static int stress_pci_dot_filter(const struct dirent *d)
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
	stress_pci_info_t *pci_info = pci_info_list, *next;

	while (pci_info) {
		next = pci_info->next;

		free(pci_info->path);
		free(pci_info->name);
		free(pci_info);
		pci_info = next;
	}
}

static int stress_pci_rev_sort(const struct dirent **a, const struct dirent **b)
{
	return alphasort(b, a);
}

/*
 *  stress_pci_info_get()
 *	get a list of PCI device paths of stress_pci_info items
 */
static stress_pci_info_t *stress_pci_info_get(void)
{
	static const char sys_devices[] = "/sys/bus/pci/devices";
	stress_pci_info_t *pci_info_list = NULL;

	int n_devs, i;
	struct dirent **pci_list = NULL;

	n_devs = scandir(sys_devices, &pci_list, stress_pci_dev_filter, stress_pci_rev_sort);
	for (i = 0; i < n_devs; i++) {
		stress_pci_info_t *pci_info;

		pci_info = calloc(1, sizeof(*pci_info));
		if (pci_info) {
			char pci_path[PATH_MAX];
			int j;

			(void)snprintf(pci_path, sizeof(pci_path),
				"%s/%s", sys_devices, pci_list[i]->d_name);
			pci_info->path = strdup(pci_path);
			if (!pci_info->path) {
				free(pci_info);
				continue;
			}
			pci_info->name = strdup(pci_list[i]->d_name);
			if (!pci_info->name) {
				free(pci_info->path);
				free(pci_info);
				continue;
			}
			for (j = 0; j < PCI_METRICS_MAX; j++) {
				pci_info->metrics[j].duration = 0.0;
				pci_info->metrics[j].count = 0.0;
			}
			pci_info->ignore = false;
			pci_info->next = pci_info_list;
			pci_info_list = pci_info;
		}
		free(pci_list[i]);
	}
	free(pci_list);

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

		if (fstat(fd, &statbuf) < 0)
			goto err;
		if (!S_ISREG(statbuf.st_mode))
			goto err;

		sz = STRESS_MINIMUM(sizeof(buf), (size_t)statbuf.st_size);
		if (rom) {
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
		if (!rom) {
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
			if (n_read > 0) {
				if (config) {
					pci_info->metrics[PCI_METRICS_CONFIG].duration += stress_time_now() - t;
					pci_info->metrics[PCI_METRICS_CONFIG].count += n_read;
				} else if (resource) {
					pci_info->metrics[PCI_METRICS_RESOURCE].duration += stress_time_now() - t;
					pci_info->metrics[PCI_METRICS_RESOURCE].count += n_read;
				}
			}
		}
		if (rom) {
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
static void stress_pci_exercise(const stress_args_t *args, stress_pci_info_t *pci_info)
{
	int i, n;
	struct dirent **list = NULL;

	n = scandir(pci_info->path, &list, stress_pci_dot_filter, alphasort);
	if (n == 0)
		pci_info->ignore = true;

	for (i = 0; stress_continue(args) && (i < n); i++) {
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
}

static double stress_pci_rate(const stress_metrics_t *metrics)
{
	return metrics->duration > 0.0 ? metrics->count / metrics->duration : 0.0;
}

/*
 *  stress_pci()
 *	stress /sysfs PCI files with open/read/close and mmap where possible
 */
static int stress_pci(const stress_args_t *args)
{
	NOCLOBBER stress_pci_info_t *pci_info_list;
	NOCLOBBER stress_pci_info_t *pci_info;
	int ret;

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

	do {
		for (pci_info = pci_info_list; pci_info; pci_info = pci_info->next) {
			if (!stress_continue(args))
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
		}
	} while (stress_continue(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	if (args->instance == 0) {
		pr_lock();
		pr_inf("%s: PCI space read rates in MB per sec for stressor instance 0:\n", args->name);
		pr_inf("%s: PCI Device     Config Resource\n", args->name);
		for (pci_info = pci_info_list; pci_info; pci_info = pci_info->next) {
			pr_inf("%s: %s %8.2f %8.2f\n",
				args->name, pci_info->name,
				stress_pci_rate(&pci_info->metrics[PCI_METRICS_CONFIG]) / MB,
				stress_pci_rate(&pci_info->metrics[PCI_METRICS_RESOURCE]) / MB);
		}
		pr_unlock();
	}

	stress_pci_info_free(pci_info_list);

	return EXIT_SUCCESS;
}

stressor_info_t stress_pci_info = {
	.stressor = stress_pci,
	.class = CLASS_OS,
	.help = help
};
#else
stressor_info_t stress_pci_info = {
	.stressor = stress_unimplemented,
	.class = CLASS_OS,
	.help = help,
	.unimplemented_reason = "only supported on Linux"
};
#endif
