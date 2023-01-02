/*
 * Copyright (C) 2015 Christian Ehrhardt.
 * Copyright (C) 2015-2021 Canonical, Ltd.
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
#include "core-arch.h"

#if defined(HAVE_BSD_WCHAR)
#include <bsd/wchar.h>
#endif

#if defined(HAVE_WCHAR)
#include <wchar.h>
#endif

#define STR1LEN 256
#define STR2LEN 128
#define STRDSTLEN (STR1LEN + STR2LEN + 1)

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
	void *libc_func,
	const char *name,
	wchar_t *str1,
	const size_t len1,
	wchar_t *str2,
	const size_t len2,
	wchar_t *strdst,
	const size_t strdstlen,
	bool *failed);

typedef struct {
	const char		*name;	/* human readable form of stressor */
	const stress_wcs_func	func;	/* the wcs method function */
	void			*libc_func;
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
	void *libc_func,
	const char *name,
	wchar_t *str1,
	const size_t len1,
	wchar_t *str2,
	const size_t len2,
	wchar_t *strdst,
	const size_t strdstlen,
	bool *failed)
{
	typedef int (*test_wcscasecmp_t)(const wchar_t *s1, const wchar_t *s2);

	register size_t i;
	const test_wcscasecmp_t test_wcscasecmp = (test_wcscasecmp_t)libc_func;

	(void)len2;
	(void)strdst;
	(void)strdstlen;

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
	void *libc_func,
	const char *name,
	wchar_t *str1,
	const size_t len1,
	wchar_t *str2,
	const size_t len2,
	wchar_t *strdst,
	const size_t strdstlen,
	bool *failed)
{
	typedef int (*test_wcsncasecmp_t)(const wchar_t *s1, const wchar_t *s2, size_t n);

	register size_t i;
	const test_wcsncasecmp_t test_wcsncasecmp = (test_wcsncasecmp_t)libc_func;

	(void)len2;
	(void)strdst;
	(void)strdstlen;

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
	void *libc_func,
	const char *name,
	wchar_t *str1,
	const size_t len1,
	wchar_t *str2,
	const size_t len2,
	wchar_t *strdst,
	const size_t strdstlen,
	bool *failed)
{
	register size_t i;
	typedef size_t (*test_wcslcpy_t)(wchar_t *dest, const wchar_t *src, size_t len);

	const test_wcslcpy_t test_wcslcpy = (test_wcslcpy_t)libc_func;
	const size_t str1_len = wcslen(str1);
	const size_t str2_len = wcslen(str2);

	(void)len2;

	for (i = 0; keep_stressing_flag() && (i < len1 - 1); i++) {
		WCSCHK(name, str1_len == test_wcslcpy(strdst, str1, strdstlen), failed);
		WCSCHK(name, str2_len == test_wcslcpy(strdst, str2, strdstlen), failed);
	}
}
#elif defined(HAVE_WCSCPY)
/*
 *  stress_wcscpy()
 *	stress on wcscpy
 */
static void stress_wcscpy(
	void *libc_func,
	const char *name,
	wchar_t *str1,
	const size_t len1,
	wchar_t *str2,
	const size_t len2,
	wchar_t *strdst,
	const size_t strdstlen,
	bool *failed)
{
	typedef wchar_t * (*test_wcscpy_t)(wchar_t *dest, const wchar_t *src);

	register size_t i;
	const test_wcscpy_t test_wcscpy = (test_wcscpy_t)libc_func;

	(void)len2;
	(void)strdstlen;

	for (i = 0; keep_stressing_flag() && (i < len1 - 1); i++) {
		WCSCHK(name, strdst == test_wcscpy(strdst, str1), failed);
		WCSCHK(name, strdst == test_wcscpy(strdst, str2), failed);
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
	void *libc_func,
	const char *name,
	wchar_t *str1,
	const size_t len1,
	wchar_t *str2,
	const size_t len2,
	wchar_t *strdst,
	const size_t strdstlen,
	bool *failed)
{
	typedef size_t (*test_wcslcat_t)(wchar_t *dest, const wchar_t *src, size_t len);

	register size_t i;
	const test_wcslcat_t test_wcslcat = (test_wcslcat_t)libc_func;
	const size_t str1_len = wcslen(str1);
	const size_t str2_len = wcslen(str2);
	const size_t str_len = str1_len + str2_len;

	(void)len2;

	for (i = 0; keep_stressing_flag() && (i < len1 - 1); i++) {
		*strdst = L'\0';
		WCSCHK(name, str1_len == test_wcslcat(strdst, str1, strdstlen), failed);
		*strdst = L'\0';
		WCSCHK(name, str2_len == test_wcslcat(strdst, str2, strdstlen), failed);
		*strdst = L'\0';
		WCSCHK(name, str1_len == test_wcslcat(strdst, str1, strdstlen), failed);
		WCSCHK(name, str_len  == test_wcslcat(strdst, str2, strdstlen), failed);
		*strdst = L'\0';
		WCSCHK(name, str2_len == test_wcslcat(strdst, str2, strdstlen), failed);
		WCSCHK(name, str_len  == test_wcslcat(strdst, str1, strdstlen), failed);
	}
}
#elif defined(HAVE_WCSCAT)
/*
 *  stress_wcscat()
 *	stress on wcscat
 */
static void stress_wcscat(
	void *libc_func,
	const char *name,
	wchar_t *str1,
	const size_t len1,
	wchar_t *str2,
	const size_t len2,
	wchar_t *strdst,
	const size_t strdstlen,
	bool *failed)
{
	typedef wchar_t * (*test_wcscat_t)(wchar_t *dest, const wchar_t *src);

	register size_t i;
	const test_wcscat_t test_wcscat = (test_wcscat_t)libc_func;

	(void)len2;
	(void)strdstlen;

	for (i = 0; keep_stressing_flag() && (i < len1 - 1); i++) {
		*strdst = L'\0';
		WCSCHK(name, strdst == test_wcscat(strdst, str1), failed);
		*strdst = L'\0';
		WCSCHK(name, strdst == test_wcscat(strdst, str2), failed);
		*strdst = L'\0';
		WCSCHK(name, strdst == test_wcscat(strdst, str1), failed);
		WCSCHK(name, strdst == test_wcscat(strdst, str2), failed);
		*strdst = L'\0';
		WCSCHK(name, strdst == test_wcscat(strdst, str2), failed);
		WCSCHK(name, strdst == test_wcscat(strdst, str1), failed);
	}
}
#endif

#if defined(HAVE_WCSNCAT)
/*
 *  stress_wcsncat()
 *	stress on wcsncat
 */
static void stress_wcsncat(
	void *libc_func,
	const char *name,
	wchar_t *str1,
	const size_t len1,
	wchar_t *str2,
	const size_t len2,
	wchar_t *strdst,
	const size_t strdstlen,
	bool *failed)
{
	typedef wchar_t * (*test_wcsncat_t)(wchar_t *dest, const wchar_t *src, size_t n);

	register size_t i;
	const test_wcsncat_t test_wcsncat = (test_wcsncat_t)libc_func;

	(void)strdst;
	(void)strdstlen;

	for (i = 0; keep_stressing_flag() && (i < len1 - 1); i++) {
		*strdst = '\0';
		WCSCHK(name, strdst == test_wcsncat(strdst, str1, len1), failed);
		*strdst = '\0';
		WCSCHK(name, strdst == test_wcsncat(strdst, str2, len2), failed);
		*strdst = '\0';
		WCSCHK(name, strdst == test_wcsncat(strdst, str1, len1), failed);
		WCSCHK(name, strdst == test_wcsncat(strdst, str2, len1 + len2), failed);
		*strdst = '\0';
		WCSCHK(name, strdst == test_wcsncat(strdst, str2, i), failed);
		WCSCHK(name, strdst == test_wcsncat(strdst, str1, i), failed);
	}
}
#endif

#if defined(HAVE_WCSCHR)
/*
 *  stress_wcschr()
 *	stress on wcschr
 */
static void stress_wcschr(
	void *libc_func,
	const char *name,
	wchar_t *str1,
	const size_t len1,
	wchar_t *str2,
	const size_t len2,
	wchar_t *strdst,
	const size_t strdstlen,
	bool *failed)
{
	typedef wchar_t * (*test_wcschr_t)(const wchar_t *wcs, wchar_t wc);

	register size_t i;
	const test_wcschr_t test_wcschr = (test_wcschr_t)libc_func;

	(void)len2;
	(void)strdst;
	(void)strdstlen;

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
	void *libc_func,
	const char *name,
	wchar_t *str1,
	const size_t len1,
	wchar_t *str2,
	const size_t len2,
	wchar_t *strdst,
	const size_t strdstlen,
	bool *failed)
{
	typedef wchar_t * (*test_wcsrchr_t)(const wchar_t *wcs, wchar_t wc);

	register size_t i;
	const test_wcsrchr_t test_wcsrchr = (test_wcsrchr_t)libc_func;

	(void)len2;
	(void)strdst;
	(void)strdstlen;

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
	void *libc_func,
	const char *name,
	wchar_t *str1,
	const size_t len1,
	wchar_t *str2,
	const size_t len2,
	wchar_t *strdst,
	const size_t strdstlen,
	bool *failed)
{
	typedef int * (*test_wcscmp_t)(const wchar_t *s1, const wchar_t *s2);

	register size_t i;
	const test_wcscmp_t test_wcscmp = (test_wcscmp_t)libc_func;

	(void)len2;
	(void)strdst;
	(void)strdstlen;

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
	void *libc_func,
	const char *name,
	wchar_t *str1,
	const size_t len1,
	wchar_t *str2,
	const size_t len2,
	wchar_t *strdst,
	const size_t strdstlen,
	bool *failed)
{
	typedef int (*test_wcsncmp_t)(const wchar_t *s1, const wchar_t *s2, size_t n);

	register size_t i;
	const test_wcsncmp_t test_wcsncmp = (test_wcsncmp_t)libc_func;

	(void)strdst;
	(void)strdstlen;

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
	void *libc_func,
	const char *name,
	wchar_t *str1,
	const size_t len1,
	wchar_t *str2,
	const size_t len2,
	wchar_t *strdst,
	const size_t strdstlen,
	bool *failed)
{
	typedef size_t (*test_wcslen_t)(const wchar_t *s);
	register size_t i;

	const test_wcslen_t test_wcslen = (test_wcslen_t)libc_func;

	(void)strdst;
	(void)strdstlen;

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
	void *libc_func,
	const char *name,
	wchar_t *str1,
	const size_t len1,
	wchar_t *str2,
	const size_t len2,
	wchar_t *strdst,
	const size_t strdstlen,
	bool *failed)
{
	typedef int (*test_wcscoll_t)(const wchar_t *ws1, const wchar_t *ws2);

	register size_t i;
	const test_wcscoll_t test_wcscoll = (test_wcscoll_t)libc_func;

	(void)len2;
	(void)strdst;
	(void)strdstlen;

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
	void *libc_func,
	const char *name,
	wchar_t *str1,
	const size_t len1,
	wchar_t *str2,
	const size_t len2,
	wchar_t *strdst,
	const size_t strdstlen,
	bool *failed)
{
	typedef size_t (*test_wcsxfrm_t)(wchar_t* destination, const wchar_t* source, size_t num);

	register size_t i;
	const test_wcsxfrm_t test_wcsxfrm = (test_wcsxfrm_t)libc_func;

	(void)len2;

	for (i = 0; keep_stressing_flag() && (i < len1 - 1); i++) {
		*strdst = '\0';
		WCSCHK(name, 0 != test_wcsxfrm(strdst, str1, strdstlen), failed);
		*strdst = '\0';
		WCSCHK(name, 0 != test_wcsxfrm(strdst, str2, strdstlen), failed);
		*strdst = '\0';
		WCSCHK(name, 0 != test_wcsxfrm(strdst, str1, strdstlen), failed);
		WCSCHK(name, 0 != test_wcsxfrm(strdst, str2, strdstlen), failed);
		*strdst = '\0';
		WCSCHK(name, 0 != test_wcsxfrm(strdst, str2, strdstlen), failed);
		WCSCHK(name, 0 != test_wcsxfrm(strdst, str1, strdstlen), failed);
	}
}
#endif

/*
 *  stress_wcs_all()
 *	iterate over all wcs stressors
 */
static void stress_wcs_all(
	void *libc_func,
	const char *name,
	wchar_t *str1,
	const size_t len1,
	wchar_t *str2,
	const size_t len2,
	wchar_t *strdst,
	const size_t strdstlen,
	bool *failed)
{
	static int i = 1;	/* Skip over stress_wcs_all */

	(void)libc_func;

	wcs_methods[i].func(wcs_methods[i].libc_func, name, str1, len1, str2, len2, strdst, strdstlen, failed);
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
	{ "wcscasecmp",		stress_wcscasecmp,	(void *)wcscasecmp },
#endif
#if defined(HAVE_WCSLCAT) &&	\
    defined(HAVE_WCSLEN) &&	\
    !defined(__PCC__) &&	\
    !defined(BUILD_STATIC)
	{ "wcslcat",		stress_wcslcat,		(void *)wcslcat },
#elif defined(HAVE_WCSCAT)
	{ "wcscat",		stress_wcscat,		(void *)wcscat },
#endif
#if defined(HAVE_WCSCHR)
	{ "wcschr",		stress_wcschr,		(void *)wcschr },
#endif
#if defined(HAVE_WCSCMP) &&	\
    !defined(STRESS_ARCH_M68K)
	{ "wcscmp",		stress_wcscmp,		(void *)wcscmp },
#endif
#if defined(HAVE_WCSLCPY) &&	\
    defined(HAVE_WCSLEN) &&	\
    !defined(__PCC__) &&	\
    !defined(BUILD_STATIC)
	{ "wcslcpy",		stress_wcslcpy,		(void *)wcslcpy },
#elif defined(HAVE_WCSCPY)
	{ "wcscpy",		stress_wcscpy,		(void *)wcscpy },
#endif
#if defined(HAVE_WCSLEN)
	{ "wcslen",		stress_wcslen,		(void *)wcslen },
#endif
#if defined(HAVE_WCSNCASECMP)
	{ "wcsncasecmp",	stress_wcsncasecmp,	(void *)wcsncasecmp },
#endif
#if defined(HAVE_WCSNCAT)
	{ "wcsncat",		stress_wcsncat,		(void *)wcsncat },
#endif
#if defined(HAVE_WCSNCMP)
	{ "wcsncmp",		stress_wcsncmp,		(void *)wcsncmp },
#endif
#if defined(HAVE_WCSRCHR)
	{ "wcsrchr",		stress_wcsrchr,		(void *)wcschr },
#endif
#if defined(HAVE_WCSCOLL)
	{ "wcscoll",		stress_wcscoll,		(void *)wcscoll },
#endif
#if defined(HAVE_WCSXFRM)
	{ "wcsxfrm",		stress_wcsxfrm,		(void *)wcsxfrm },
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
	void *libc_func;
	bool failed = false;
	wchar_t ALIGN64 str1[STR1LEN], ALIGN64 str2[STR2LEN];
	wchar_t strdst[STRDSTLEN];
	register wchar_t *ptr1, *ptr2;
	size_t len1, len2;

	/* No wcs* functions available on this system? */
	if (SIZEOF_ARRAY(wcs_methods) <= 2)
		return stress_unimplemented(args);

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
		(void)func(libc_func, args->name, ptr1, len1, ptr2, len2, strdst, STRDSTLEN, &failed);

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
	.verify = VERIFY_OPTIONAL,
	.help = help,
	.unimplemented_reason = "built without wchar.h or bsd/wchar.h"
};
