/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
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
#include "core-target-clones.h"

#define ALIGN_SIZE	(64)

static const stress_help_t help[] = {
	{ NULL,	"memcpy N",	   "start N workers performing memory copies" },
	{ NULL,	"memcpy-method M", "set memcpy method (M = all, libc, builtin, naive..)" },
	{ NULL,	"memcpy-ops N",	   "stop after N memcpy bogo operations" },
	{ NULL,	NULL,		   NULL }
};

static const char *s_args_name = "";
static char *s_method_name = "";

typedef struct {
	uint8_t buffer[STR_SHARED_SIZE + ALIGN_SIZE];
} stress_buffer_t;

typedef void (*stress_memcpy_func)(stress_buffer_t *b, uint8_t *b_str, uint8_t *str_shared, uint8_t *aligned_buf);

typedef struct {
	const char *name;
	const stress_memcpy_func func;
} stress_memcpy_method_info_t;

typedef void * (*memcpy_func_t)(void *dest, const void *src, size_t n);
typedef void * (*memmove_func_t)(void *dest, const void *src, size_t n);

typedef void * (*memcpy_check_func_t)(memcpy_func_t func, void *dest, const void *src, size_t n);
typedef void * (*memmove_check_func_t)(memmove_func_t func, void *dest, const void *src, size_t n);

static memcpy_check_func_t memcpy_check;
static memmove_check_func_t memmove_check;

static OPTIMIZE3 void *memcpy_check_func(memcpy_func_t func, void *dest, const void *src, size_t n)
{
	void *ptr = func(dest, src, n);

	if (memcmp(dest, src, n)) {
		pr_fail("%s: %s: memcpy content is different than expected\n", s_args_name, s_method_name);
	}
	if (ptr != dest) {
		pr_fail("%s: %s: memcpy return was %p and not %p as expected\n", s_args_name, s_method_name, ptr, dest);
	}
	return ptr;
}

static OPTIMIZE3 void *memcpy_no_check_func(memcpy_func_t func, void *dest, const void *src, size_t n)
{
	return func(dest, src, n);
}

static OPTIMIZE3 void *memmove_check_func(memcpy_func_t func, void *dest, const void *src, size_t n)
{
	void *ptr = func(dest, src, n);

	if (memcmp(dest, src, n)) {
		pr_fail("%s: %s: memmove content is different than expected\n", s_args_name, s_method_name);
	}
	if (ptr != dest) {
		pr_fail("%s: %s: memmove return was %p and not %p as expected\n", s_args_name, s_method_name, ptr, dest);
	}
	return ptr;
}

static OPTIMIZE3 void *memmove_no_check_func(memcpy_func_t func, void *dest, const void *src, size_t n)
{
	return func(dest, src, n);
}

#define TEST_NAIVE_MEMCPY(name, hint)					\
static hint void *name(void *dest, const void *src, size_t n)		\
{									\
	register size_t i;						\
	register char *cdest = (char *)dest;				\
	register const char *csrc = (const char *)src;			\
									\
	for (i = 0; i < n; i++)						\
		*(cdest++) = *(csrc++);					\
	return dest;							\
}									\

TEST_NAIVE_MEMCPY(test_naive_memcpy, NOINLINE)
TEST_NAIVE_MEMCPY(test_naive_memcpy_o0, NOINLINE OPTIMIZE0)
TEST_NAIVE_MEMCPY(test_naive_memcpy_o1, NOINLINE OPTIMIZE1)
TEST_NAIVE_MEMCPY(test_naive_memcpy_o2, NOINLINE OPTIMIZE2)
TEST_NAIVE_MEMCPY(test_naive_memcpy_o3, NOINLINE OPTIMIZE3)

#define TEST_NAIVE_MEMMOVE(name, hint)					\
static hint void *name(void *dest, const void *src, size_t n)		\
{									\
	register size_t i;						\
	register char *cdest = (char *)dest;				\
	register const char *csrc = (const char *)src;			\
									\
	if (dest < src) {						\
		for (i = 0; i < n; i++)					\
			*(cdest++) = *(csrc++);				\
	} else {							\
		csrc += n;						\
		cdest += n;						\
									\
		for (i = 0; i < n; i++)					\
			*(--cdest) = *(--csrc);				\
	}								\
	return dest;							\
}

TEST_NAIVE_MEMMOVE(test_naive_memmove, NOINLINE)
TEST_NAIVE_MEMMOVE(test_naive_memmove_o0, NOINLINE OPTIMIZE0)
TEST_NAIVE_MEMMOVE(test_naive_memmove_o1, NOINLINE OPTIMIZE1)
TEST_NAIVE_MEMMOVE(test_naive_memmove_o2, NOINLINE OPTIMIZE2)
TEST_NAIVE_MEMMOVE(test_naive_memmove_o3, NOINLINE OPTIMIZE3)

static NOINLINE void stress_memcpy_libc(
	stress_buffer_t *b,
	uint8_t *b_str,
	uint8_t *str_shared,
	uint8_t *aligned_buf)
{
	s_method_name = "libc";

	(void)memcpy_check(memcpy, aligned_buf, str_shared, STR_SHARED_SIZE);
	(void)memcpy_check(memcpy, str_shared, aligned_buf, STR_SHARED_SIZE / 2);
	(void)memmove_check(memmove, aligned_buf, aligned_buf + 64, STR_SHARED_SIZE - 64);
	(void)memcpy_check(memcpy, b_str, b, STR_SHARED_SIZE);
	(void)memmove_check(memmove, aligned_buf + 64, aligned_buf, STR_SHARED_SIZE - 64);
	(void)memcpy_check(memcpy, b, b_str, STR_SHARED_SIZE);
	(void)memmove_check(memmove, aligned_buf + 1, aligned_buf, STR_SHARED_SIZE - 1);
	(void)memmove_check(memmove, aligned_buf, aligned_buf + 1, STR_SHARED_SIZE - 1);
}

#if defined(HAVE_BUILTIN_MEMCPY) &&	\
    defined(HAVE_BUILTIN_MEMMOVE)
static void *stress_builtin_memcpy_wrapper(void *restrict dst, const void *restrict src, size_t n)
{
	return __builtin_memcpy(dst, src, n);
}

static void *stress_builtin_memmove_wrapper(void *dst, const void *src, size_t n)
{
	return __builtin_memmove(dst, src, n);
}
#endif

static NOINLINE void stress_memcpy_builtin(
	stress_buffer_t *b,
	uint8_t *b_str,
	uint8_t *str_shared,
	uint8_t *aligned_buf)
{
#if defined(HAVE_BUILTIN_MEMCPY) &&	\
    defined(HAVE_BUILTIN_MEMMOVE)

	s_method_name = "builtin";

	(void)memcpy_check(stress_builtin_memcpy_wrapper, aligned_buf, str_shared, STR_SHARED_SIZE);
	(void)memcpy_check(stress_builtin_memcpy_wrapper, str_shared, aligned_buf, STR_SHARED_SIZE / 2);
	(void)memmove_check(stress_builtin_memmove_wrapper, aligned_buf, aligned_buf + 64, STR_SHARED_SIZE - 64);
	(void)memcpy_check(stress_builtin_memcpy_wrapper, b_str, b, STR_SHARED_SIZE);
	(void)memmove_check(stress_builtin_memmove_wrapper, aligned_buf + 64, aligned_buf, STR_SHARED_SIZE - 64);
	(void)memcpy_check(stress_builtin_memcpy_wrapper, b, b_str, STR_SHARED_SIZE);
	(void)memmove_check(stress_builtin_memmove_wrapper, aligned_buf + 1, aligned_buf, STR_SHARED_SIZE - 1);
	(void)memmove_check(stress_builtin_memmove_wrapper, aligned_buf, aligned_buf + 1, STR_SHARED_SIZE - 1);
#else
	/*
	 *  Compiler may fall back to turning these into inline'd
	 *  optimized versions even if there are no explicit built-in
	 *  versions, so use these.
	 */

	s_method_name = "builtin (libc)";

	(void)memcpy_check(memcpy, aligned_buf, str_shared, STR_SHARED_SIZE);
	(void)memcpy_check(memcpy, str_shared, aligned_buf, STR_SHARED_SIZE / 2);
	(void)memmove_check(memmove, aligned_buf, aligned_buf + 64, STR_SHARED_SIZE - 64);
	(void)memcpy_check(memcpy, b_str, b, STR_SHARED_SIZE);
	(void)memmove_check(memmove, aligned_buf + 64, aligned_buf, STR_SHARED_SIZE - 64);
	(void)memcpy_check(memcpy, b, b_str, STR_SHARED_SIZE);
	(void)memmove_check(memmove, aligned_buf + 1, aligned_buf, STR_SHARED_SIZE - 1);
	(void)memmove_check(memmove, aligned_buf, aligned_buf + 1, STR_SHARED_SIZE - 1);
#endif
}

#define STRESS_MEMCPY_NAIVE(method, name, cpy, move)					\
static NOINLINE void name(								\
	stress_buffer_t *b,								\
	uint8_t *b_str,									\
	uint8_t *str_shared,								\
	uint8_t *aligned_buf)								\
{											\
	s_method_name = method;								\
											\
	(void)memcpy_check(cpy, aligned_buf, str_shared, STR_SHARED_SIZE);		\
	(void)memcpy_check(cpy, str_shared, aligned_buf, STR_SHARED_SIZE / 2);		\
	(void)memmove_check(move, aligned_buf, aligned_buf + 64, STR_SHARED_SIZE - 64);	\
	(void)memcpy_check(cpy, b_str, b, STR_SHARED_SIZE);				\
	(void)memmove_check(move, aligned_buf + 64, aligned_buf, STR_SHARED_SIZE - 64);	\
	(void)memcpy_check(cpy, b, b_str, STR_SHARED_SIZE);				\
	(void)memmove_check(move, aligned_buf + 1, aligned_buf, STR_SHARED_SIZE - 1);	\
	(void)memmove_check(move, aligned_buf, aligned_buf + 1, STR_SHARED_SIZE - 1);	\
}

STRESS_MEMCPY_NAIVE("naive", stress_memcpy_naive, test_naive_memcpy, test_naive_memmove)
STRESS_MEMCPY_NAIVE("naive_o0", stress_memcpy_naive_o0, test_naive_memcpy_o0, test_naive_memmove_o0)
STRESS_MEMCPY_NAIVE("naive_o1", stress_memcpy_naive_o1, test_naive_memcpy_o1, test_naive_memmove_o1)
STRESS_MEMCPY_NAIVE("naive_o2", stress_memcpy_naive_o2, test_naive_memcpy_o2, test_naive_memmove_o2)
STRESS_MEMCPY_NAIVE("naive_o3", stress_memcpy_naive_o3, test_naive_memcpy_o3, test_naive_memmove_o3)

static NOINLINE void stress_memcpy_all(
	stress_buffer_t *b,
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
	case 2:
		whence++;
		stress_memcpy_naive(b, b_str, str_shared, aligned_buf);
		return;
	case 3:
		whence++;
		stress_memcpy_naive_o0(b, b_str, str_shared, aligned_buf);
		return;
	default:
		stress_memcpy_naive_o3(b, b_str, str_shared, aligned_buf);
		whence = 0;
		return;
	}
}

static const stress_memcpy_method_info_t stress_memcpy_methods[] = {
	{ "all",	stress_memcpy_all },
	{ "libc",	stress_memcpy_libc },
	{ "builtin",	stress_memcpy_builtin },
	{ "naive",      stress_memcpy_naive },
	{ "naive_o0",	stress_memcpy_naive_o0 },
	{ "naive_o1",	stress_memcpy_naive_o1 },
	{ "naive_o2",	stress_memcpy_naive_o2 },
	{ "naive_o3",	stress_memcpy_naive_o3 },
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
			stress_set_setting("memcpy-method", TYPE_ID_UINTPTR_T, &info);
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
	stress_set_memcpy_method("all");
}

/*
 *  stress_memcpy()
 *	stress memory copies
 */
static int stress_memcpy(const stress_args_t *args)
{
	static stress_buffer_t b;
	uint8_t *b_str = g_shared->str_shared;
	uint8_t *str_shared = g_shared->str_shared;
	uint8_t *aligned_buf = stress_align_address(b.buffer, ALIGN_SIZE);
	const stress_memcpy_method_info_t *memcpy_method = &stress_memcpy_methods[0];

	s_args_name = args->name;

	if (g_opt_flags & OPT_FLAGS_VERIFY) {
		memcpy_check = memcpy_check_func;
		memmove_check = memmove_check_func;
	} else {
		memcpy_check = memcpy_no_check_func;
		memmove_check = memmove_no_check_func;
	}

	(void)stress_get_setting("memcpy-method", &memcpy_method);

	stress_rndbuf(aligned_buf, ALIGN_SIZE);

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		memcpy_method->func(&b, b_str, str_shared, aligned_buf);
		inc_counter(args);
	} while (keep_stressing(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return EXIT_SUCCESS;
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_memcpy_method,	stress_set_memcpy_method },
	{ 0,			NULL }
};

stressor_info_t stress_memcpy_info = {
	.stressor = stress_memcpy,
	.set_default = stress_memcpy_set_default,
	.class = CLASS_CPU_CACHE | CLASS_MEMORY,
	.opt_set_funcs = opt_set_funcs,
	.verify = VERIFY_OPTIONAL,
	.help = help
};
