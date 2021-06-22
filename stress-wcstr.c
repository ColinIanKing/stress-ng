/*
 * Copyright (C) 2015 Christian Ehrhardt
 * Copyright (C) 2015-2021 Canonical, Ltd.
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

#define STR1LEN 256
#define STR2LEN 128

static const stress_help_t help[] = {
	{ NULL,	"wcs N",	   "start N workers on lib C wide char string functions" },
	{ NULL,	"wcs-method func", "specify the wide character string function to stress" },
	{ NULL,	"wcs-ops N",	   "stop after N bogo wide character string operations" },
	{ NULL,	NULL,		   NULL }
};

/*
 *  the wide string stress test has different classes of stressors
 */
typedef void (*stress_wcs_func)(
	const void *libc_func,
	const char *name,
	wchar_t *str1,
	const size_t len1,
	wchar_t *str2,
	const size_t len2,
	bool *failed);

typedef struct {
	const char		*name;	/* human readable form of stressor */
	const stress_wcs_func	func;	/* the wcs method function */
	const void		*libc_func;
} stress_wcs_method_info_t;

static const stress_wcs_method_info_t wcs_methods[];

/*
 *  stress_wcs_fill
 */
static void stress_wcs_fill(wchar_t *wcstr, const size_t len)
{
	register size_t i;

	for (i = 0; i < (len-1); i++) {
		*wcstr++ = (stress_mwc8() % 26) + L'a';
	}
	*wcstr = L'\0';
}

#if defined(HAVE_WCSCASECMP) || 	\
    defined(HAVE_WCSNCASECMP) || 	\
    (defined(HAVE_WCSLCPY) && defined(HAVE_WCSLEN)) || \
    defined(HAVE_WCSCPY)  || 		\
    (defined(HAVE_WCSLCAT) && defined(HAVE_WCSLEN)) || \
    defined(HAVE_WCSCAT)  || 		\
    defined(HAVE_WCSNCAT) || 		\
    defined(HAVE_WCSCHR)  || 		\
    defined(HAVE_WCSRCHR) || 		\
    defined(HAVE_WCSCMP)  || 		\
    defined(HAVE_WCSNCMP) || 		\
    defined(HAVE_WCSLEN)  || 		\
    defined(HAVE_WCSCOLL) ||		\
    defined(HAVE_WCSXFRM)

static inline void wcschk(
	const char *name,
	const int ok,
	const char *msg,
	bool *failed)
{
	if ((g_opt_flags & OPT_FLAGS_VERIFY) && (!ok)) {
		pr_fail("%s: %s did not return expected result\n",
			name, msg);
		*failed = true;
	}
}
#endif

#define STR(x)	# x

#define WCSCHK(name, test, failed)	\
	wcschk(name, test, STR(test), failed)

#if defined(HAVE_WCSCASECMP)
/*
 *  stress_wcscasecmp()
 *	stress on wcscasecmp
 */
static void stress_wcscasecmp(
	const void *libc_func,
	const char *name,
	wchar_t *str1,
	const size_t len1,
	wchar_t *str2,
	const size_t len2,
	bool *failed)
{
	register size_t i;
	int (*test_wcscasecmp)(const wchar_t *s1, const wchar_t *s2) = libc_func;

	(void)len2;

	for (i = 1; keep_stressing_flag() && (i < len1); i++) {
		WCSCHK(name, 0 == test_wcscasecmp(str1, str1), failed);
		WCSCHK(name, 0 == test_wcscasecmp(str2, str2), failed);

		WCSCHK(name, 0 != test_wcscasecmp(str2, str1), failed);
		WCSCHK(name, 0 != test_wcscasecmp(str1, str2), failed);

		WCSCHK(name, 0 != test_wcscasecmp(str1 + i, str1), failed);
		WCSCHK(name, 0 != test_wcscasecmp(str1, str1 + i), failed);
		WCSCHK(name, 0 == test_wcscasecmp(str1 + i, str1 + i), failed);

		WCSCHK(name, 0 != test_wcscasecmp(str1 + i, str2), failed);
		WCSCHK(name, 0 != test_wcscasecmp(str2, str1 + i), failed);
	}
}
#endif

#if defined(HAVE_WCSNCASECMP)
/*
 *  stress_wcsncasecmp()
 *	stress on wcsncasecmp
 */
static void stress_wcsncasecmp(
	const void *libc_func,
	const char *name,
	wchar_t *str1,
	const size_t len1,
	wchar_t *str2,
	const size_t len2,
	bool *failed)
{
	register size_t i;
	int (*test_wcsncasecmp)(const wchar_t *s1, const wchar_t *s2, size_t n) = libc_func;

	(void)len2;

	for (i = 1; keep_stressing_flag() && (i < len1); i++) {
		WCSCHK(name, 0 == test_wcsncasecmp(str1, str1, len1), failed);
		WCSCHK(name, 0 == test_wcsncasecmp(str2, str2, len2), failed);

		WCSCHK(name, 0 != test_wcsncasecmp(str2, str1, len2), failed);
		WCSCHK(name, 0 != test_wcsncasecmp(str1, str2, len1), failed);

		WCSCHK(name, 0 != test_wcsncasecmp(str1 + i, str1, len1), failed);
		WCSCHK(name, 0 != test_wcsncasecmp(str1, str1 + i, len1), failed);
		WCSCHK(name, 0 == test_wcsncasecmp(str1 + i, str1 + i, len1), failed);

		WCSCHK(name, 0 != test_wcsncasecmp(str1 + i, str2, len1), failed);
		WCSCHK(name, 0 != test_wcsncasecmp(str2, str1 + i, len2), failed);
	}
}
#endif

#if defined(HAVE_WCSLCPY) &&	\
    defined(HAVE_WCSLEN) &&	\
    !defined(__PCC__) &&	\
    !defined(BUILD_STATIC)
/*
 *  stress_wcslcpy()
 *	stress on wcslcpy
 */
static void stress_wcslcpy(
	const void *libc_func,
	const char *name,
	wchar_t *str1,
	const size_t len1,
	wchar_t *str2,
	const size_t len2,
	bool *failed)
{
	register size_t i;
	size_t (*test_wcslcpy)(wchar_t *dest, const wchar_t *src, size_t len) = libc_func;
	wchar_t buf[len1 + len2 + 1];
	const size_t buf_len = sizeof(buf);
	const size_t str1_len = wcslen(str1);
	const size_t str2_len = wcslen(str2);

	for (i = 0; keep_stressing_flag() && (i < len1 - 1); i++) {
		WCSCHK(name, str1_len == test_wcslcpy(buf, str1, buf_len), failed);
		WCSCHK(name, str2_len == test_wcslcpy(buf, str2, buf_len), failed);
	}
}
#elif defined(HAVE_WCSCPY)
/*
 *  stress_wcscpy()
 *	stress on wcscpy
 */
static void stress_wcscpy(
	const void *libc_func,
	const char *name,
	wchar_t *str1,
	const size_t len1,
	wchar_t *str2,
	const size_t len2,
	bool *failed)
{
	register size_t i;
	wchar_t * (*test_wcscpy)(wchar_t *dest, const wchar_t *src) = libc_func;
	wchar_t buf[len1 + len2 + 1];

	for (i = 0; keep_stressing_flag() && (i < len1 - 1); i++) {
		WCSCHK(name, buf == test_wcscpy(buf, str1), failed);
		WCSCHK(name, buf == test_wcscpy(buf, str2), failed);
	}
}
#endif

#if defined(HAVE_WCSLCAT) &&	\
    defined(HAVE_WCSLEN) &&	\
    !defined(__PCC__) &&	\
    !defined(BUILD_STATIC)
/*
 *  stress_wcslcat()
 *	stress on wcslcat
 */
static void stress_wcslcat(
	const void *libc_func,
	const char *name,
	wchar_t *str1,
	const size_t len1,
	wchar_t *str2,
	const size_t len2,
	bool *failed)
{
	register size_t i;
	size_t (*test_wcslcat)(wchar_t *dest, const wchar_t *src, size_t len) = libc_func;
	wchar_t buf[len1 + len2 + 1];
	const size_t buf_len = sizeof(buf);
	const size_t str1_len = wcslen(str1);
	const size_t str2_len = wcslen(str2);
	const size_t str_len = str1_len + str2_len;

	for (i = 0; keep_stressing_flag() && (i < len1 - 1); i++) {
		*buf = L'\0';
		WCSCHK(name, str1_len == test_wcslcat(buf, str1, buf_len), failed);
		*buf = L'\0';
		WCSCHK(name, str2_len == test_wcslcat(buf, str2, buf_len), failed);
		*buf = L'\0';
		WCSCHK(name, str1_len == test_wcslcat(buf, str1, buf_len), failed);
		WCSCHK(name, str_len  == test_wcslcat(buf, str2, buf_len), failed);
		*buf = L'\0';
		WCSCHK(name, str2_len == test_wcslcat(buf, str2, buf_len), failed);
		WCSCHK(name, str_len  == test_wcslcat(buf, str1, buf_len), failed);
	}
}
#elif defined(HAVE_WCSCAT)
/*
 *  stress_wcscat()
 *	stress on wcscat
 */
static void stress_wcscat(
	const void *libc_func,
	const char *name,
	wchar_t *str1,
	const size_t len1,
	wchar_t *str2,
	const size_t len2,
	bool *failed)
{
	register size_t i;
	wchar_t * (*test_wcscat)(wchar_t *dest, const wchar_t *src) = libc_func;
	wchar_t buf[len1 + len2 + 1];

	for (i = 0; keep_stressing_flag() && (i < len1 - 1); i++) {
		*buf = L'\0';
		WCSCHK(name, buf == test_wcscat(buf, str1), failed);
		*buf = L'\0';
		WCSCHK(name, buf == test_wcscat(buf, str2), failed);
		*buf = L'\0';
		WCSCHK(name, buf == test_wcscat(buf, str1), failed);
		WCSCHK(name, buf == test_wcscat(buf, str2), failed);
		*buf = L'\0';
		WCSCHK(name, buf == test_wcscat(buf, str2), failed);
		WCSCHK(name, buf == test_wcscat(buf, str1), failed);
	}
}
#endif

#if defined(HAVE_WCSNCAT)
/*
 *  stress_wcsncat()
 *	stress on wcsncat
 */
static void stress_wcsncat(
	const void *libc_func,
	const char *name,
	wchar_t *str1,
	const size_t len1,
	wchar_t *str2,
	const size_t len2,
	bool *failed)
{
	register size_t i;
	wchar_t * (*test_wcsncat)(wchar_t *dest, const wchar_t *src, size_t n) = libc_func;
	wchar_t buf[len1 + len2 + 1];

	for (i = 0; keep_stressing_flag() && (i < len1 - 1); i++) {
		*buf = '\0';
		WCSCHK(name, buf == test_wcsncat(buf, str1, len1), failed);
		*buf = '\0';
		WCSCHK(name, buf == test_wcsncat(buf, str2, len2), failed);
		*buf = '\0';
		WCSCHK(name, buf == test_wcsncat(buf, str1, len1), failed);
		WCSCHK(name, buf == test_wcsncat(buf, str2, len1 + len2), failed);
		*buf = '\0';
		WCSCHK(name, buf == test_wcsncat(buf, str2, i), failed);
		WCSCHK(name, buf == test_wcsncat(buf, str1, i), failed);
	}
}
#endif

#if defined(HAVE_WCSCHR)
/*
 *  stress_wcschr()
 *	stress on wcschr
 */
static void stress_wcschr(
	const void *libc_func,
	const char *name,
	wchar_t *str1,
	const size_t len1,
	wchar_t *str2,
	const size_t len2,
	bool *failed)
{
	register size_t i;
	wchar_t * (*test_wcschr)(const wchar_t *wcs, wchar_t wc) = libc_func;

	(void)len2;

	for (i = 0; keep_stressing_flag() && (i < len1 - 1); i++) {
		WCSCHK(name, NULL == test_wcschr(str1, '_'), failed);
		WCSCHK(name, NULL != test_wcschr(str1, str1[0]), failed);

		WCSCHK(name, NULL == test_wcschr(str2, '_'), failed);
		WCSCHK(name, NULL != test_wcschr(str2, str2[0]), failed);
	}
}
#endif

#if defined(HAVE_WCSRCHR)
/*
 *  stress_wcsrchr()
 *	stress on wcsrchr
 */
static void stress_wcsrchr(
	const void *libc_func,
	const char *name,
	wchar_t *str1,
	const size_t len1,
	wchar_t *str2,
	const size_t len2,
	bool *failed)
{
	register size_t i;
	wchar_t * (*test_wcsrchr)(const wchar_t *wcs, wchar_t wc) = libc_func;

	(void)len2;

	for (i = 0; keep_stressing_flag() && (i < len1 - 1); i++) {
		WCSCHK(name, NULL == test_wcsrchr(str1, '_'), failed);
		WCSCHK(name, NULL != test_wcsrchr(str1, str1[0]), failed);

		WCSCHK(name, NULL == test_wcsrchr(str2, '_'), failed);
		WCSCHK(name, NULL != test_wcsrchr(str2, str2[0]), failed);
	}
}
#endif

#if defined(HAVE_WCSCMP) &&	\
    !defined(STRESS_ARCH_M68K)
/*
 *  stress_wcscmp()
 *	stress on wcscmp
 */
static void stress_wcscmp(
	const void *libc_func,
	const char *name,
	wchar_t *str1,
	const size_t len1,
	wchar_t *str2,
	const size_t len2,
	bool *failed)
{
	register size_t i;
	int * (*test_wcscmp)(const wchar_t *s1, const wchar_t *s2) = libc_func;

	(void)len2;

	for (i = 1; keep_stressing_flag() && (i < len1); i++) {
		WCSCHK(name, 0 == test_wcscmp(str1, str1), failed);
		WCSCHK(name, 0 == test_wcscmp(str2, str2), failed);

		WCSCHK(name, 0 != test_wcscmp(str2, str1), failed);
		WCSCHK(name, 0 != test_wcscmp(str1, str2), failed);

		WCSCHK(name, 0 != test_wcscmp(str1 + i, str1), failed);
		WCSCHK(name, 0 != test_wcscmp(str1, str1 + i), failed);
		WCSCHK(name, 0 == test_wcscmp(str1 + i, str1 + i), failed);

		WCSCHK(name, 0 != test_wcscmp(str1 + i, str2), failed);
		WCSCHK(name, 0 != test_wcscmp(str2, str1 + i), failed);
	}
}
#endif

#if defined(HAVE_WCSNCMP)
/*
 *  stress_wcsncmp()
 *	stress on wcsncmp
 */
static void stress_wcsncmp(
	const void *libc_func,
	const char *name,
	wchar_t *str1,
	const size_t len1,
	wchar_t *str2,
	const size_t len2,
	bool *failed)
{
	register size_t i;
	int (*test_wcsncmp)(const wchar_t *s1, const wchar_t *s2, size_t n) = libc_func;

	for (i = 1; keep_stressing_flag() && (i < len1); i++) {
		WCSCHK(name, 0 == test_wcsncmp(str1, str1, len1), failed);
		WCSCHK(name, 0 == test_wcsncmp(str2, str2, len2), failed);

		WCSCHK(name, 0 != test_wcsncmp(str2, str1, len2), failed);
		WCSCHK(name, 0 != test_wcsncmp(str1, str2, len1), failed);

		WCSCHK(name, 0 != test_wcsncmp(str1 + i, str1, len1), failed);
		WCSCHK(name, 0 != test_wcsncmp(str1, str1 + i, len1), failed);
		WCSCHK(name, 0 == test_wcsncmp(str1 + i, str1 + i, len1), failed);

		WCSCHK(name, 0 != test_wcsncmp(str1 + i, str2, len2), failed);
		WCSCHK(name, 0 != test_wcsncmp(str2, str1 + i, len2), failed);
	}
}
#endif

#if defined(HAVE_WCSLEN)
/*
 *  stress_wcslen()
 *	stress on wcslen
 */
static void stress_wcslen(
	const void *libc_func,
	const char *name,
	wchar_t *str1,
	const size_t len1,
	wchar_t *str2,
	const size_t len2,
	bool *failed)
{
	register size_t i;
	size_t (*test_wcslen)(const wchar_t *s) = libc_func;

	for (i = 0; keep_stressing_flag() && (i < len1 - 1); i++) {
		WCSCHK(name, len1 - 1 == test_wcslen(str1), failed);
		WCSCHK(name, len1 - 1 - i == test_wcslen(str1 + i), failed);
	}

	for (i = 0; keep_stressing_flag() && (i < len2 - 1); i++) {
		WCSCHK(name, len2 - 1 == test_wcslen(str2), failed);
		WCSCHK(name, len2 - 1 - i == test_wcslen(str2 + i), failed);
	}
}
#endif

#if defined(HAVE_WCSCOLL)
/*
 *  stress_wcscoll()
 *	stress on wcscoll
 */
static void stress_wcscoll(
	const void *libc_func,
	const char *name,
	wchar_t *str1,
	const size_t len1,
	wchar_t *str2,
	const size_t len2,
	bool *failed)
{
	register size_t i;
	int (*test_wcscoll)(const wchar_t *ws1, const wchar_t *ws2) = libc_func;

	(void)len2;

	for (i = 1; keep_stressing_flag() && (i < len1); i++) {
		WCSCHK(name, 0 == test_wcscoll(str1, str1), failed);
		WCSCHK(name, 0 == test_wcscoll(str2, str2), failed);

		WCSCHK(name, 0 != test_wcscoll(str2, str1), failed);
		WCSCHK(name, 0 != test_wcscoll(str1, str2), failed);

		WCSCHK(name, 0 != test_wcscoll(str1 + i, str1), failed);
		WCSCHK(name, 0 != test_wcscoll(str1, str1 + i), failed);
		WCSCHK(name, 0 == test_wcscoll(str1 + i, str1 + i), failed);

		WCSCHK(name, 0 != test_wcscoll(str1 + i, str2), failed);
		WCSCHK(name, 0 != test_wcscoll(str2, str1 + i), failed);
	}
}
#endif

#if defined(HAVE_WCSXFRM)
/*
 *  stress_wcsxfrm()
 *	stress on wcsxfrm
 */
static void stress_wcsxfrm(
	const void *libc_func,
	const char *name,
	wchar_t *str1,
	const size_t len1,
	wchar_t *str2,
	const size_t len2,
	bool *failed)
{
	register size_t i;
	size_t (*test_wcsxfrm)(wchar_t* destination, const wchar_t* source, size_t num) = libc_func;
	wchar_t buf[len1 + len2];

	for (i = 0; keep_stressing_flag() && (i < len1 - 1); i++) {
		*buf = '\0';
		WCSCHK(name, 0 != test_wcsxfrm(buf, str1, sizeof(buf)), failed);
		*buf = '\0';
		WCSCHK(name, 0 != test_wcsxfrm(buf, str2, sizeof(buf)), failed);
		*buf = '\0';
		WCSCHK(name, 0 != test_wcsxfrm(buf, str1, sizeof(buf)), failed);
		WCSCHK(name, 0 != test_wcsxfrm(buf, str2, sizeof(buf)), failed);
		*buf = '\0';
		WCSCHK(name, 0 != test_wcsxfrm(buf, str2, sizeof(buf)), failed);
		WCSCHK(name, 0 != test_wcsxfrm(buf, str1, sizeof(buf)), failed);
	}
}
#endif

/*
 *  stress_wcs_all()
 *	iterate over all wcs stressors
 */
static void stress_wcs_all(
	const void *libc_func,
	const char *name,
	wchar_t *str1,
	const size_t len1,
	wchar_t *str2,
	const size_t len2,
	bool *failed)
{
	static int i = 1;	/* Skip over stress_wcs_all */

	(void)libc_func;

	wcs_methods[i].func(wcs_methods[i].libc_func, name, str1, len1, str2, len2, failed);
	i++;
	if (!wcs_methods[i].func)
		i = 1;
}

/*
 * Table of wcs stress methods
 */
static const stress_wcs_method_info_t wcs_methods[] = {
	{ "all",		stress_wcs_all,		NULL },	/* Special "all" test */
#if defined(HAVE_WCSCASECMP)
	{ "wcscasecmp",		stress_wcscasecmp,	wcscasecmp },
#endif
#if defined(HAVE_WCSLCAT) &&	\
    defined(HAVE_WCSLEN) &&	\
    !defined(__PCC__) &&	\
    !defined(BUILD_STATIC)
	{ "wcslcat",		stress_wcslcat,		wcslcat },
#elif defined(HAVE_WCSCAT)
	{ "wcscat",		stress_wcscat,		wcscat },
#endif
#if defined(HAVE_WCSCHR)
	{ "wcschr",		stress_wcschr,		wcschr },
#endif
#if defined(HAVE_WCSCMP) &&	\
    !defined(STRESS_ARCH_M68K)
	{ "wcscmp",		stress_wcscmp,		wcscmp },
#endif
#if defined(HAVE_WCSLCPY) &&	\
    defined(HAVE_WCSLEN) &&	\
    !defined(__PCC__) &&	\
    !defined(BUILD_STATIC)
	{ "wcslcpy",		stress_wcslcpy,		wcslcpy },
#elif defined(HAVE_WCSCPY)
	{ "wcscpy",		stress_wcscpy,		wcscpy },
#endif
#if defined(HAVE_WCSLEN)
	{ "wcslen",		stress_wcslen,		wcslen },
#endif
#if defined(HAVE_WCSNCASECMP)
	{ "wcsncasecmp",	stress_wcsncasecmp,	wcsncasecmp },
#endif
#if defined(HAVE_WCSNCAT)
	{ "wcsncat",		stress_wcsncat,		wcsncat },
#endif
#if defined(HAVE_WCSNCMP)
	{ "wcsncmp",		stress_wcsncmp,		wcsncmp },
#endif
#if defined(HAVE_WCSRCHR)
	{ "wcsrchr",		stress_wcsrchr,		wcschr },
#endif
#if defined(HAVE_WCSCOLL)
	{ "wcscoll",		stress_wcscoll,		wcscoll },
#endif
#if defined(HAVE_WCSXFRM)
	{ "wcsxfrm",		stress_wcsxfrm,		wcsxfrm },
#endif
	{ NULL,			NULL,			NULL }
};

/*
 *  stress_set_wcs_method()
 *	set the specified wcs stress method
 */
static int stress_set_wcs_method(const char *name)
{
	stress_wcs_method_info_t const *info;

	for (info = wcs_methods; info->func; info++) {
		if (!strcmp(info->name, name)) {
			stress_set_setting("wcs-method", TYPE_ID_UINTPTR_T, &info);
			return 0;
		}
	}

	(void)fprintf(stderr, "wcs-method must be one of:");
	for (info = wcs_methods; info->func; info++) {
		(void)fprintf(stderr, " %s", info->name);
	}
	(void)fprintf(stderr, "\n");

	return -1;
}

/*
 *  stress_wcs()
 *	stress CPU by doing wide character string ops
 */
static int stress_wcs(const stress_args_t *args)
{
	stress_wcs_method_info_t const *wcs_method = &wcs_methods[0];
	stress_wcs_func func;
	const void *libc_func;
	bool failed = false;
	wchar_t ALIGN64 str1[STR1LEN], ALIGN64 str2[STR2LEN];
	register wchar_t *ptr1, *ptr2;
	size_t len1, len2;

	/* No wcs* functions available on this system? */
	if (SIZEOF_ARRAY(wcs_methods) <= 2)
		return stress_not_implemented(args);

	(void)stress_get_setting("wcs-method", &wcs_method);
	func = wcs_method->func;
	libc_func = wcs_method->libc_func;

	ptr1 = str1;
	len1 = STR1LEN;
	ptr2 = str2;
	len2 = STR2LEN;

	stress_wcs_fill(ptr1, len1);

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		register wchar_t *tmpptr;
		register size_t tmplen;

		stress_wcs_fill(ptr2, len2);
		(void)func(libc_func, args->name, ptr1, len1, ptr2, len2, &failed);

		tmpptr = ptr1;
		ptr1 = ptr2;
		ptr2 = tmpptr;

		tmplen = len1;
		len1 = len2;
		len2 = tmplen;

		inc_counter(args);
	} while (keep_stressing(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}

static void stress_wcs_set_default(void)
{
	stress_set_wcs_method("all");
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_wcs_method,	stress_set_wcs_method },
	{ 0,			NULL }
};

stressor_info_t stress_wcs_info = {
	.stressor = stress_wcs,
	.set_default = stress_wcs_set_default,
	.class = CLASS_CPU | CLASS_CPU_CACHE | CLASS_MEMORY,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
