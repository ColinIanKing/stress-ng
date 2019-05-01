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

#define ALIGN_SIZE	(64)

static const help_t help[] = {
	{ NULL,	"memcpy N",	   "start N workers performing memory copies" },
	{ NULL,	"memcpy-ops N",	   "stop after N memcpy bogo operations" },
	{ NULL,	"memcpy-method M", "set memcpy method (M = all, libc, builtin, naive)" },
	{ NULL,	NULL,		   NULL }
};

typedef struct {
	uint8_t buffer[STR_SHARED_SIZE + ALIGN_SIZE];
} buffer_t;

typedef void (*stress_memcpy_func)(buffer_t *b, uint8_t *b_str, uint8_t *str_shared, uint8_t *aligned_buf);

typedef struct {
        const char *name;
        const stress_memcpy_func func;
} stress_memcpy_method_info_t;

static NOINLINE void *__memcpy(void *dest, const void *src, size_t n)
{
	return memcpy(dest, src, n);
}

static NOINLINE void *__memmove(void *dest, const void *src, size_t n)
{
	return memmove(dest, src, n);
}

static inline void *__naive_memcpy(void *dest, const void *src, size_t n)
{
	register size_t i;
	register char *cdest = (char *)dest;
	register const char *csrc = (const char *)src;

	for (i = 0; i < n; i++)
		*(cdest++) = *(csrc++);
	return dest;
}

static inline void *__naive_memmove(void *dest, const void *src, size_t n)
{
	register size_t i;
	register char *cdest = (char *)dest;
	register const char *csrc = (const char *)src;

	if (dest < src) {
		for (i = 0; i < n; i++)
			*(cdest++) = *(csrc++);
	} else {
		csrc += n;
		cdest += n;

		for (i = 0; i < n; i++)
			*(--cdest) = *(--csrc);
	}
	return dest;
}

static NOINLINE void stress_memcpy_libc(
	buffer_t *b,
	uint8_t *b_str,
	uint8_t *str_shared,
	uint8_t *aligned_buf)
{
	(void)__memcpy(aligned_buf, str_shared, STR_SHARED_SIZE);
	(void)__memcpy(str_shared, aligned_buf, STR_SHARED_SIZE / 2);
	(void)__memmove(aligned_buf, aligned_buf + 64, STR_SHARED_SIZE - 64);
	(void)__memcpy(b_str, b, STR_SHARED_SIZE);
	(void)__memmove(aligned_buf + 64, aligned_buf, STR_SHARED_SIZE - 64);
	(void)__memcpy(b, b_str, STR_SHARED_SIZE);
	(void)__memmove(aligned_buf + 1, aligned_buf, STR_SHARED_SIZE - 1);
	(void)__memmove(aligned_buf, aligned_buf + 1, STR_SHARED_SIZE - 1);
}

static NOINLINE void stress_memcpy_builtin(
	buffer_t *b,
	uint8_t *b_str,
	uint8_t *str_shared,
	uint8_t *aligned_buf)
{
#if defined(HAVE_BUILTIN_MEMCPY) &&	\
    defined(HAVE_BUILTIN_MEMMOVE)
	(void)__builtin_memcpy(aligned_buf, str_shared, STR_SHARED_SIZE);
	(void)__builtin_memcpy(str_shared, aligned_buf, STR_SHARED_SIZE / 2);
	(void)__builtin_memmove(aligned_buf, aligned_buf + 64, STR_SHARED_SIZE - 64);
	(void)__builtin_memcpy(b_str, b, STR_SHARED_SIZE);
	(void)__builtin_memmove(aligned_buf + 64, aligned_buf, STR_SHARED_SIZE - 64);
	(void)__builtin_memcpy(b, b_str, STR_SHARED_SIZE);
	(void)__builtin_memmove(aligned_buf + 1, aligned_buf, STR_SHARED_SIZE - 1);
	(void)__builtin_memmove(aligned_buf, aligned_buf + 1, STR_SHARED_SIZE - 1);
#else
	/*
	 *  Compiler may fall back to turning these into inline'd
	 *  optimized versions even if there are no explicit built-in
	 *  versions, so use these.
	 */
	(void)memcpy(aligned_buf, str_shared, STR_SHARED_SIZE);
	(void)memcpy(str_shared, aligned_buf, STR_SHARED_SIZE / 2);
	(void)memmove(aligned_buf, aligned_buf + 64, STR_SHARED_SIZE - 64);
	(void)memcpy(b_str, b, STR_SHARED_SIZE);
	(void)memmove(aligned_buf + 64, aligned_buf, STR_SHARED_SIZE - 64);
	(void)memcpy(b, b_str, STR_SHARED_SIZE);
	(void)memmove(aligned_buf + 1, aligned_buf, STR_SHARED_SIZE - 1);
	(void)memmove(aligned_buf, aligned_buf + 1, STR_SHARED_SIZE - 1);
#endif
}

static NOINLINE void stress_memcpy_naive(
	buffer_t *b,
	uint8_t *b_str,
	uint8_t *str_shared,
	uint8_t *aligned_buf)
{
	(void)__naive_memcpy(aligned_buf, str_shared, STR_SHARED_SIZE);
	(void)__naive_memcpy(str_shared, aligned_buf, STR_SHARED_SIZE / 2);
	(void)__naive_memmove(aligned_buf, aligned_buf + 64, STR_SHARED_SIZE - 64);
	(void)__naive_memcpy(b_str, b, STR_SHARED_SIZE);
	(void)__naive_memmove(aligned_buf + 64, aligned_buf, STR_SHARED_SIZE - 64);
	(void)__naive_memcpy(b, b_str, STR_SHARED_SIZE);
	(void)__naive_memmove(aligned_buf + 1, aligned_buf, STR_SHARED_SIZE - 1);
	(void)__naive_memmove(aligned_buf, aligned_buf + 1, STR_SHARED_SIZE - 1);
}

static NOINLINE void stress_memcpy_all(
	buffer_t *b,
	uint8_t *b_str,
	uint8_t *str_shared,
	uint8_t *aligned_buf)
{
	static int whence;

	switch (whence) {
	case 0:
		whence++;
		stress_memcpy_libc(b, b_str, str_shared, aligned_buf);
		return;
	case 1:
		whence++;
		stress_memcpy_builtin(b, b_str, str_shared, aligned_buf);
		return;
	default:
		whence = 0;
		stress_memcpy_naive(b, b_str, str_shared, aligned_buf);
		return;
	}
}

static const stress_memcpy_method_info_t stress_memcpy_methods[] = {
	{ "all",	stress_memcpy_all },
        { "libc",	stress_memcpy_libc },
        { "builtin",	stress_memcpy_builtin },
        { "naive",      stress_memcpy_naive },
        { NULL,         NULL }
};

/*
 *  stress_set_memcpy_method()
 *      set default memcpy stress method
 */
static int stress_set_memcpy_method(const char *name)
{
	stress_memcpy_method_info_t const *info;

	for (info = stress_memcpy_methods; info->func; info++) {
		if (!strcmp(info->name, name)) {
			set_setting("memcpy-method", TYPE_ID_UINTPTR_T, &info);
			return 0;
		}
	}

	(void)fprintf(stderr, "memcpy-method must be one of:");
	for (info = stress_memcpy_methods; info->func; info++) {
		(void)fprintf(stderr, " %s", info->name);
	}
	(void)fprintf(stderr, "\n");

	return -1;
}

static void stress_memcpy_set_default(void)
{
        stress_set_memcpy_method("libc");
}

/*
 *  stress_memcpy()
 *	stress memory copies
 */
static int stress_memcpy(const args_t *args)
{
	static buffer_t b;
	uint8_t *b_str = g_shared->str_shared;
	uint8_t *str_shared = g_shared->str_shared;
	uint8_t *aligned_buf = stress_align_address(b.buffer, ALIGN_SIZE);
	const stress_memcpy_method_info_t *memcpy_method = &stress_memcpy_methods[1];

	(void)get_setting("memcpy-method", &memcpy_method);

	stress_strnrnd((char *)aligned_buf, ALIGN_SIZE);

	do {
		memcpy_method->func(&b, b_str, str_shared, aligned_buf);
		inc_counter(args);
	} while (keep_stressing());

	return EXIT_SUCCESS;
}

static const opt_set_func_t opt_set_funcs[] = {
	{ OPT_memcpy_method,	stress_set_memcpy_method },
	{ 0,			NULL }
};

stressor_info_t stress_memcpy_info = {
	.stressor = stress_memcpy,
	.set_default = stress_memcpy_set_default,
	.class = CLASS_CPU_CACHE | CLASS_MEMORY,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
