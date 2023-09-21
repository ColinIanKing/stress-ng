// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
 *
 */
#include "stress-ng.h"
#include "core-mmap.h"
#include "core-out-of-memory.h"

static const stress_help_t help[] = {
	{ NULL,	"mmaphuge N",		"start N workers stressing mmap with huge mappings" },
	{ NULL,	"mmaphuge-mlock",	"attempt to mlock pages into memory" },
	{ NULL, "mmaphuge-mmaps N",	"select number of memory mappings per iteration" },
	{ NULL,	"mmaphuge-ops N",	"stop after N mmaphuge bogo operations" },
	{ NULL,	NULL,			NULL }
};

static int stress_set_mmaphuge_mlock(const char *opt)
{
	return stress_set_setting_true("mmaphuge-mlock", opt);
}

/*
 *  stress_set_mmaphuge_mmaps()
 *      set number of huge memory mappings to make per loop
 */
static int stress_set_mmaphuge_mmaps(const char *opt)
{
	size_t mmaphuge_mmaps;

	mmaphuge_mmaps = (size_t)stress_get_uint64(opt);
	stress_check_range("mmaphuge-mmaps", mmaphuge_mmaps,
		1, 65536 );
	return stress_set_setting("mmaphuge-mmaps", TYPE_ID_SIZE_T, &mmaphuge_mmaps);
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_mmaphuge_mlock,	stress_set_mmaphuge_mlock },
	{ OPT_mmaphuge_mmaps,	stress_set_mmaphuge_mmaps },
	{ 0,                    NULL }
};

#if defined(MAP_HUGETLB)

#define MAX_MMAP_BUFS	(8192)

#if !defined(MAP_HUGE_2MB) && defined(MAP_HUGE_SHIFT)
#define MAP_HUGE_2MB    (21 << MAP_HUGE_SHIFT)
#endif

#if !defined(MAP_HUGE_1GB) && defined(MAP_HUGE_SHIFT)
#define MAP_HUGE_1GB    (30 << MAP_HUGE_SHIFT)
#endif

typedef struct {
	uint8_t	*buf;		/* mapping start */
	size_t	sz;		/* mapping size */
} stress_mmaphuge_buf_t;

typedef struct {
	const int	flags;	/* MMAP flag */
	const size_t	sz;	/* MMAP size */
} stress_mmaphuge_setting_t;

typedef struct {
	stress_mmaphuge_buf_t	*bufs;	/* mmap'd buffers */
	size_t mmaphuge_mmaps;	/* number of mmap'd buffers */
} stress_mmaphuge_context_t;

static const stress_mmaphuge_setting_t stress_mmap_settings[] =
{
#if defined(MAP_HUGE_2MB)
	{ MAP_HUGETLB | MAP_HUGE_2MB,	2 * MB },
#endif
#if defined(MAP_HUGE_1GB)
	{ MAP_HUGETLB | MAP_HUGE_1GB,	1 * GB },
#endif
	{ MAP_HUGETLB, 1 * GB },
	{ MAP_HUGETLB, 16 * MB },	/* ppc64 */
	{ MAP_HUGETLB, 2 * MB },
	{ 0, 1 * GB },			/* for THP */
	{ 0, 16 * MB },			/* for THP */
	{ 0, 2 * MB },			/* for THP */
};

static int stress_mmaphuge_child(const stress_args_t *args, void *v_ctxt)
{
	stress_mmaphuge_context_t *ctxt = (stress_mmaphuge_context_t *)v_ctxt;
	const size_t page_size = args->page_size;
	stress_mmaphuge_buf_t *bufs = (stress_mmaphuge_buf_t *)ctxt->bufs;
	size_t idx = 0;
	int rc = EXIT_SUCCESS;
	bool mmaphuge_mlock = false;

	(void)stress_get_setting("mmaphuge-mlock", &mmaphuge_mlock);

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		size_t i;

		for (i = 0; i < ctxt->mmaphuge_mmaps; i++)
			bufs[i].buf = MAP_FAILED;

		for (i = 0; stress_continue(args) && (i < ctxt->mmaphuge_mmaps); i++) {
			size_t shmall, freemem, totalmem, freeswap, totalswap, last_freeswap, last_totalswap;
			size_t j;

			stress_get_memlimits(&shmall, &freemem, &totalmem, &last_freeswap, &last_totalswap);

			for (j = 0; j < SIZEOF_ARRAY(stress_mmap_settings); j++) {
				uint8_t *buf;
				const size_t sz = stress_mmap_settings[idx].sz;
				int flags = MAP_ANONYMOUS;

				flags |= (stress_mwc1() ? MAP_PRIVATE : MAP_SHARED);
				flags |= stress_mmap_settings[idx].flags;

				if ((g_opt_flags & OPT_FLAGS_OOM_AVOID) && stress_low_memory(page_size))
					break;

				bufs[i].sz = sz;
				buf = (uint8_t *)mmap(NULL, sz,
							PROT_READ | PROT_WRITE,
							flags, -1, 0);
				bufs[i].buf = buf;
				idx++;
				if (idx >= SIZEOF_ARRAY(stress_mmap_settings))
					idx = 0;

				if (buf != MAP_FAILED) {
					register uint64_t val = stress_mwc64();
					register size_t k;

					if (mmaphuge_mlock)
						(void)shim_mlock(buf, sz);

					/* Touch every other 64 pages.. */
					for (k = 0; k < sz; k += page_size * 64) {
						register uint64_t *ptr64 = (uint64_t *)&buf[k];

						*ptr64 = val + k;
					}
					/* ..and sanity check */
					for (k = 0; stress_continue(args) && (k < sz); k += page_size * 64) {
						register uint64_t *ptr64 = (uint64_t *)&buf[k];

						if (*ptr64 != val + k) {
							pr_fail("%s: memory %p at offset 0x%zx check error, "
								"got 0x%" PRIx64 ", expecting 0x%" PRIx64 "\n",
								args->name, buf, k, *ptr64, val + k);
							rc = EXIT_FAILURE;
						}
					}

					stress_bogo_inc(args);
					break;
				}
			}
			stress_get_memlimits(&shmall, &freemem, &totalmem, &freeswap, &totalswap);

			/* Check if we eat into swap */
			if (last_freeswap > freeswap)
				break;
		}

		for (i = 0; stress_continue(args) && (i < ctxt->mmaphuge_mmaps); i++) {
			if (bufs[i].buf != MAP_FAILED)
				continue;
			/* Try Transparent Huge Pages THP */
#if defined(MADV_HUGEPAGE)
			(void)shim_madvise(bufs[i].buf, bufs[i].sz, MADV_NOHUGEPAGE);
#endif
#if defined(MADV_HUGEPAGE)
			(void)shim_madvise(bufs[i].buf, bufs[i].sz, MADV_HUGEPAGE);
#endif
		}

		for (i = 0; i < ctxt->mmaphuge_mmaps; i++) {
			uint8_t *buf = bufs[i].buf;
			size_t sz;

			if (buf == MAP_FAILED)
				continue;

			sz = bufs[i].sz;
			if (page_size < sz) {
				uint8_t *end_page = buf + (sz - page_size);
				int ret;

				*buf = stress_mwc8();
				*end_page = stress_mwc8();
				/* Unmapping small page may fail on huge pages */
				ret = stress_munmap_retry_enomem((void *)end_page, page_size);
				if (ret == 0)
					ret = stress_munmap_retry_enomem((void *)buf, sz - page_size);
				if (ret != 0)
					(void)stress_munmap_retry_enomem((void *)buf, sz);
			} else {
				*buf = stress_mwc8();
				(void)stress_munmap_retry_enomem((void *)buf, sz);
			}
			bufs[i].buf = MAP_FAILED;
		}
	} while (stress_continue(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return rc;
}

/*
 *  stress_mmaphuge()
 *	stress huge page mmappings and unmappings
 */
static int stress_mmaphuge(const stress_args_t *args)
{
	stress_mmaphuge_context_t ctxt;

	int ret;

	ctxt.mmaphuge_mmaps = MAX_MMAP_BUFS;
	(void)stress_get_setting("mmaphuge-mmaps", &ctxt.mmaphuge_mmaps);

	ctxt.bufs = calloc(ctxt.mmaphuge_mmaps, sizeof(*ctxt.bufs));
	if (!ctxt.bufs) {
		pr_inf_skip("%s: cannot allocate buffer array, skipping stressor\n",
			args->name);
		return EXIT_NO_RESOURCE;
	}

	ret = stress_oomable_child(args, (void *)&ctxt, stress_mmaphuge_child, STRESS_OOMABLE_QUIET);
	free(ctxt.bufs);

	return ret;
}

stressor_info_t stress_mmaphuge_info = {
	.stressor = stress_mmaphuge,
	.class = CLASS_VM | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.verify = VERIFY_ALWAYS,
	.help = help
};

#else

stressor_info_t stress_mmaphuge_info = {
	.stressor = stress_unimplemented,
	.class = CLASS_VM | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without mmap() MAP_HUGETLB support"
};

#endif
