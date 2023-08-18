// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
 *
 */
#include "stress-ng.h"

static const stress_help_t help[] = {
	{ NULL,	"mmapmany N",		"start N workers stressing many mmaps and munmaps" },
	{ NULL, "mmapmany-mlock",	"attempt to mlock pages into memory" },
	{ NULL,	"mmapmany-ops N",	"stop after N mmapmany bogo operations" },
	{ NULL,	NULL,		  	NULL }
};

#define MMAP_MAX	(256 * 1024)

static int stress_set_mmapmany_mlock(const char *opt)
{
	return stress_set_setting_true("mmapmany-mlock", opt);
}

#if defined(__linux__)
static void stress_mmapmany_read_proc_file(const char *path)
{
	int fd;
	char buf[4096];

	fd = open(path, O_RDONLY);
	if (fd < 0)
		return;
	while (read(fd, buf, sizeof(buf)) > 0) {
		if (!stress_continue_flag())
			break;
	}
	(void)close(fd);
}
#endif

static int stress_mmapmany_child(const stress_args_t *args, void *context)
{
	const size_t page_size = args->page_size;
	long max = STRESS_MAXIMUM(sysconf(_SC_MAPPED_FILES), MMAP_MAX);
	uint64_t **mappings;
	const uint64_t pattern0 = stress_mwc64();
	const uint64_t pattern1 = stress_mwc64();
	const size_t offset2pages = (page_size * 2) / sizeof(uint64_t);
	bool mmapmany_mlock = false;

	(void)context;

	(void)stress_get_setting("mmapmany-mlock", &mmapmany_mlock);

	mappings = calloc((size_t)max, sizeof(*mappings));
	if (!mappings) {
		pr_fail("%s: malloc failed, out of memory\n", args->name);
		return EXIT_NO_RESOURCE;
	}

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		size_t i, n;

		for (n = 0; stress_continue_flag() && (n < (size_t)max); n++) {
			uint64_t *ptr;

			if (!stress_continue(args))
				break;

			ptr = (uint64_t *)mmap(NULL, page_size * 3, PROT_READ | PROT_WRITE,
				MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
			if (ptr == MAP_FAILED)
				break;
			if (mmapmany_mlock)
				(void)shim_mlock(ptr, page_size * 3);
			mappings[n] = ptr;
			*ptr = pattern0 ^ (uint64_t)n;
			ptr += offset2pages;
			*ptr = pattern1 ^ (uint64_t)n;

			if (stress_munmap_retry_enomem((void *)(((uintptr_t)mappings[n]) + page_size), page_size) < 0)
				break;
			stress_bogo_inc(args);
		}

#if defined(__linux__)
		/* Exercise map traversal */
		stress_mmapmany_read_proc_file("/proc/self/smaps");
		stress_mmapmany_read_proc_file("/proc/self/maps");
#endif

		for (i = 0; i < n; i++) {
			uint64_t *ptr, val;

			ptr = (uint64_t *)mappings[i];
			val = (uint64_t)i ^ pattern0;
			if (*ptr != val) {
				pr_fail("%s: failed: mapping %zd at %p was %" PRIx64 " and not %" PRIx64 "\n",
					args->name, i, (void *)ptr, *ptr, val);
			}
			ptr += offset2pages;
			val = (uint64_t)i ^ pattern1;
			if (*ptr != val) {
				pr_fail("%s: failed: mapping %zd at %p was %" PRIx64 " and not %" PRIx64 "\n",
					args->name, i, (void *)ptr, *ptr, val);
			}

			(void)stress_munmap_retry_enomem((void *)mappings[i], page_size);
			(void)stress_munmap_retry_enomem((void *)(((uintptr_t)mappings[i]) + page_size), page_size);
			(void)stress_munmap_retry_enomem((void *)(((uintptr_t)mappings[i]) + page_size + page_size), page_size);
		}
	} while (stress_continue(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	free(mappings);
	return EXIT_SUCCESS;
}

/*
 *  stress_mmapmany()
 *	stress mmap with many pages being mapped
 */
static int stress_mmapmany(const stress_args_t *args)
{
	return stress_oomable_child(args, NULL, stress_mmapmany_child, STRESS_OOMABLE_NORMAL);
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_mmapmany_mlock,	stress_set_mmapmany_mlock },
	{ 0,			NULL }
};

stressor_info_t stress_mmapmany_info = {
	.stressor = stress_mmapmany,
	.class = CLASS_VM | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.verify = VERIFY_ALWAYS,
	.help = help
};
