/*
 * Copyright (C)      2021 Canonical, Ltd.
 * Copyright (C)      2022 Colin Ian King.
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
	{ NULL,	"pci N",	"start N workers that read and mmap PCI regions" },
	{ NULL,	"pci-ops N",	"stop after N PCI bogo operations" },
	{ NULL,	NULL,		NULL }
};

#if defined(__linux__)

static sigjmp_buf jmp_env;

typedef struct stress_pci_info {
	char *path;			/* PCI /sysfs path name */
	bool	ignore;			/* true = ignore the entry */
	struct stress_pci_info	*next;	/* next in list */
} stress_pci_info_t;

/*
 *  stress_pci_filter()
 *	scandir filter on /sys/devices/pci[0-9]*
 */
static int stress_pci_filter(const struct dirent *d)
{
	if (strlen(d->d_name) < 4)
		return 0;
	if (strncmp(d->d_name, "pci", 3))
		return 0;
	if (isdigit((int)d->d_name[3]))
		return 1;
	return 0;
}

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
	stress_pci_info_t *pi = pci_info_list, *next;

	while (pi) {
		next = pi->next;

		free(pi->path);
		free(pi);
		pi = next;
	}
}

/*
 *  stress_pci_info_get()
 *	get a list of PCI device paths of stress_pci_info items
 */
static stress_pci_info_t *stress_pci_info_get(void)
{
	struct dirent **list = NULL;
	static const char sys_devices[] = "/sys/devices";
	int i, n;
	stress_pci_info_t *pci_info_list = NULL;

	n = scandir(sys_devices, &list, stress_pci_filter, alphasort);
	for (i = 0; i < n; i++) {
		int j, n_devs;
		char path[PATH_MAX];
		struct dirent **pci_list = NULL;

		(void)snprintf(path, sizeof(path), "%s/%s", sys_devices, list[i]->d_name);
		n_devs = scandir(path, &pci_list, stress_pci_dev_filter, alphasort);

		for (j = 0; j < n_devs; j++) {
			stress_pci_info_t *pi;

			pi = calloc(1, sizeof(*pi));
			if (pi) {
				char pci_path[PATH_MAX];

				(void)snprintf(pci_path, sizeof(pci_path),
					"%s/%s/%s", sys_devices,
					list[i]->d_name, pci_list[j]->d_name);
				pi->path = strdup(pci_path);
				if (!pi->path) {
					free(pi);
					continue;
				}
				pi->ignore = false;
				pi->next = pci_info_list;
				pci_info_list = pi;
			}
			free(pci_list[j]);
		}
		free(pci_list);
		free(list[i]);
	}
	free(list);

	return pci_info_list;
}

/*
 *  stress_pci_exercise_file()
 *	exercise a PCI file,
 */
static void stress_pci_exercise_file(
	stress_pci_info_t *pi,
	const char *name,
	const bool rd,
	const bool map,
	const bool rom)
{
	char path[PATH_MAX];
	int fd;

	(void)snprintf(path, sizeof(path), "%s/%s", pi->path, name);
	fd = open(path, rom ? O_RDWR : O_RDONLY);
	if (fd >= 0) {
		void *ptr;
		size_t sz = 4096;
		ssize_t n;
		char buf[sz];
		struct stat statbuf;

		if (fstat(fd, &statbuf) < 0)
			goto err;
		if (!S_ISREG(statbuf.st_mode))
			goto err;

		sz = STRESS_MINIMUM(sz, (size_t)statbuf.st_size);

		if (rom) {
			n = write(fd, "1\n", 2);
			(void)n;
		}

		if (map) {
			ptr = mmap(NULL, sz, PROT_READ, MAP_SHARED, fd, 0);
			if (ptr != MAP_FAILED)
				(void)munmap(ptr, sz);
		}
		if (rd) {
			n = read(fd, buf, sizeof(buf));
			(void)n;
		}
		if (rom) {
			n = write(fd, "0\n", 2);
			(void)n;
		}
err:
		(void)close(fd);
	}
}

/*
 *  stress_pci_exercise()
 *	exercise all PCI files in a given PCI info path
 */
static void stress_pci_exercise(stress_pci_info_t *pi)
{
	int i, n;
	struct dirent **list = NULL;

	n = scandir(pi->path, &list, stress_pci_dot_filter, alphasort);
	if (n == 0)
		pi->ignore = true;

	for (i = 0; i < n; i++) {
		const char *name = list[i]->d_name;
		bool map = (!strcmp(name, "config") ||
		            !strncmp(name, "resource", 8));
		bool rom = false;

		if (!strcmp(name, "rom")) {
			map = true;
			rom = true; 
		}

		stress_pci_exercise_file(pi, name, true, map, rom);
		free(list[i]);
	}
	free(list);
}

/*
 *  stress_pci_handler()
 *	handle unexpected SIGSEGV or SIGBUS errors when mmaping
 *	or reading PCI data
 */
static void MLOCKED_TEXT stress_pci_handler(int signum)
{
	(void)signum;

	siglongjmp(jmp_env, 1);
}

/*
 *  stress_pci()
 *	stress /sysfs PCI files with open/read/close and mmap where possible
 */
static int stress_pci(const stress_args_t *args)
{
	NOCLOBBER stress_pci_info_t *pci_info_list;
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
		NOCLOBBER stress_pci_info_t *pi;

		for (pi = pci_info_list; pi; pi = pi->next) {
			ret = sigsetjmp(jmp_env, 1);

			if (ret) {
				pi->ignore = true;
				continue;
			}

			if (pi->ignore)
				continue;
			stress_pci_exercise(pi);
			inc_counter(args);
		}
	} while (keep_stressing(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

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
	.stressor = stress_not_implemented,
	.class = CLASS_OS,
	.help = help
};
#endif
