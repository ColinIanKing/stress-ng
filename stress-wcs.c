/*
 * Copyright (C) 2015 Christian Ehrhardt.
 * Copyright (C) 2015-2021 Canonical, Ltd.
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
#include "core-arch.h"

#if defined(HAVE_BSD_WCHAR_H)
#include <bsd/wchar.h>
#define HAVE_WCHAR
#endif

#if defined(HAVE_WCHAR_H)
#include <wchar.h>
#define HAVE_WCHAR
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

typedef struct stress_wcs_args {
	void *libc_func;
	const char *name;
	wchar_t *str1;
	size_t len1;
	wchar_t *str2;
	size_t len2;
	wchar_t *strdst;
	size_t strdstlen;
	bool failed;
} stress_wcs_args_t;

/*
 *  the wide string stress test has different classes of stressors
 */
typedef size_t (*stress_wcs_func)(stress_args_t *args, stress_wcs_args_t *info);

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
	static const wchar_t letters[32] ALIGN64 = {
		L'a', L'b', L'c', L'd', L'e', L'f', L'g', L'h',
		L'i', L'j', L'k', L'l', L'm', L'n', L'o', L'p',
		L'q', L'r', L's', L't', L'u', L'v', L'w', L'x',
		L'y', L'z', L'~', L'+', L'!', L'#', L'*', L'+',
	};

	for (i = 0; i < (len - 1); i++) {
		*wcstr++ = letters[stress_mwc8() & 31];
	}
	*wcstr = L'\0';
}

#if (defined(HAVE_WCSCASECMP) || 	\
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
     defined(HAVE_WCSXFRM)) &&		\
    defined(HAVE_WCHAR)

static void wcschk(
	stress_wcs_args_t *info,
	const int ok,
	const char *msg)
{
	if ((g_opt_flags & OPT_FLAGS_VERIFY) && (!ok)) {
		pr_fail("%s: %s did not return expected result\n",
			info->name, msg);
		info->failed = true;
	}
}
#endif

#define STR(x)	# x

#define WCSCHK(info, test)	wcschk(info, test, STR(test))

#if defined(HAVE_WCSCASECMP) &&	\
    defined(HAVE_WCHAR)
/*
 *  stress_wcscasecmp()
 *	stress on wcscasecmp
 */
static size_t stress_wcscasecmp(stress_args_t *args, stress_wcs_args_t *info)
{
	typedef int (*test_wcscasecmp_t)(const wchar_t *s1, const wchar_t *s2);

	const test_wcscasecmp_t test_wcscasecmp = (test_wcscasecmp_t)info->libc_func;
	const wchar_t *str1 = info->str1;
	const wchar_t *str2 = info->str2;
	const size_t len1 = info->len1;
	register size_t i;

	for (i = 1; LIKELY(stress_continue_flag() && (i < len1)); i++) {
		WCSCHK(info, 0 == test_wcscasecmp(str1, str1));
		WCSCHK(info, 0 == test_wcscasecmp(str2, str2));

		WCSCHK(info, 0 != test_wcscasecmp(str2, str1));
		WCSCHK(info, 0 != test_wcscasecmp(str1, str2));

		WCSCHK(info, 0 != test_wcscasecmp(str1 + i, str1));
		WCSCHK(info, 0 != test_wcscasecmp(str1, str1 + i));
		WCSCHK(info, 0 == test_wcscasecmp(str1 + i, str1 + i));

		WCSCHK(info, 0 != test_wcscasecmp(str1 + i, str2));
		WCSCHK(info, 0 != test_wcscasecmp(str2, str1 + i));
	}
	stress_bogo_add(args, 9);
	return i * 9;
}
#endif

#if defined(HAVE_WCSNCASECMP) &&	\
    defined(HAVE_WCHAR)
/*
 *  stress_wcsncasecmp()
 *	stress on wcsncasecmp
 */
static size_t stress_wcsncasecmp(stress_args_t *args, stress_wcs_args_t *info)
{
	typedef int (*test_wcsncasecmp_t)(const wchar_t *s1, const wchar_t *s2, size_t n);

	const test_wcsncasecmp_t test_wcsncasecmp = (test_wcsncasecmp_t)info->libc_func;
	const wchar_t *str1 = info->str1;
	const wchar_t *str2 = info->str2;
	const size_t len1 = info->len1;
	const size_t len2 = info->len2;
	register size_t i;

	for (i = 1; LIKELY(stress_continue_flag() && (i < len1)); i++) {
		WCSCHK(info, 0 == test_wcsncasecmp(str1, str1, len1));
		WCSCHK(info, 0 == test_wcsncasecmp(str2, str2, len2));

		WCSCHK(info, 0 != test_wcsncasecmp(str2, str1, len2));
		WCSCHK(info, 0 != test_wcsncasecmp(str1, str2, len1));

		WCSCHK(info, 0 != test_wcsncasecmp(str1 + i, str1, len1));
		WCSCHK(info, 0 != test_wcsncasecmp(str1, str1 + i, len1));
		WCSCHK(info, 0 == test_wcsncasecmp(str1 + i, str1 + i, len1));

		WCSCHK(info, 0 != test_wcsncasecmp(str1 + i, str2, len1));
		WCSCHK(info, 0 != test_wcsncasecmp(str2, str1 + i, len2));
	}
	stress_bogo_add(args, 9);
	return i * 9;
}
#endif

#if defined(HAVE_WCSLCPY) &&		\
    defined(HAVE_WCSLEN) &&		\
    defined(HAVE_WCHAR) &&		\
    !defined(HAVE_COMPILER_PCC) &&	\
    !defined(BUILD_STATIC)
/*
 *  stress_wcslcpy()
 *	stress on wcslcpy
 */
static size_t stress_wcslcpy(stress_args_t *args, stress_wcs_args_t *info)
{
	typedef size_t (*test_wcslcpy_t)(wchar_t *dest, const wchar_t *src, size_t len);

	const test_wcslcpy_t test_wcslcpy = (test_wcslcpy_t)info->libc_func;
	const wchar_t *str1 = info->str1;
	const wchar_t *str2 = info->str2;
	wchar_t *strdst = info->strdst;
	const size_t len1 = info->len1;
	const size_t strdstlen = info->strdstlen;
	const size_t strlen1 = wcslen(str1);
	const size_t strlen2 = wcslen(str2);
	register size_t i;

	for (i = 0; LIKELY(stress_continue_flag() && (i < len1 - 1)); i++) {
		WCSCHK(info, strlen1 == test_wcslcpy(strdst, str1, strdstlen));
		WCSCHK(info, strlen2 == test_wcslcpy(strdst, str2, strdstlen));
	}
	stress_bogo_add(args, 2);
	return i * 2;
}
#elif defined(HAVE_WCSCPY) &&	\
      defined(HAVE_WCHAR)
/*
 *  stress_wcscpy()
 *	stress on wcscpy
 */
static size_t stress_wcscpy(stress_args_t *args, stress_wcs_args_t *info)
{
	typedef wchar_t * (*test_wcscpy_t)(wchar_t *dest, const wchar_t *src);

	const test_wcscpy_t test_wcscpy = (test_wcscpy_t)info->libc_func;
	wchar_t *str1 = info->str1;
	wchar_t *str2 = info->str2;
	wchar_t *strdst = info->strdst;
	const size_t len1 = info->len1;
	register size_t i;

	for (i = 0; LIKELY(stress_continue_flag() && (i < len1 - 1)); i++) {
		WCSCHK(info, strdst == test_wcscpy(strdst, str1));
		WCSCHK(info, strdst == test_wcscpy(strdst, str2));
	}
	stress_bogo_add(args, 2);
	return i * 2;
}
#endif

#if defined(HAVE_WCSLCAT) &&		\
    defined(HAVE_WCSLEN) &&		\
    defined(HAVE_WCHAR) &&		\
    !defined(HAVE_COMPILER_PCC) &&	\
    !defined(BUILD_STATIC)
/*
 *  stress_wcslcat()
 *	stress on wcslcat
 */
static size_t stress_wcslcat(stress_args_t *args, stress_wcs_args_t *info)
{
	typedef size_t (*test_wcslcat_t)(wchar_t *dest, const wchar_t *src, size_t len);

	const test_wcslcat_t test_wcslcat = (test_wcslcat_t)info->libc_func;
	const wchar_t *str1 = info->str1;
	const wchar_t *str2 = info->str2;
	wchar_t *strdst = info->strdst;
	const size_t len1 = info->len1;
	const size_t str1_len = wcslen(str1);
	const size_t str2_len = wcslen(str2);
	const size_t str_len = str1_len + str2_len;
	const size_t strdstlen = info->strdstlen;
	register size_t i;

	for (i = 0; LIKELY(stress_continue_flag() && (i < len1 - 1)); i++) {
		*strdst = L'\0';
		WCSCHK(info, str1_len == test_wcslcat(strdst, str1, strdstlen));
		*strdst = L'\0';
		WCSCHK(info, str2_len == test_wcslcat(strdst, str2, strdstlen));
		*strdst = L'\0';
		WCSCHK(info, str1_len == test_wcslcat(strdst, str1, strdstlen));
		WCSCHK(info, str_len  == test_wcslcat(strdst, str2, strdstlen));
		*strdst = L'\0';
		WCSCHK(info, str2_len == test_wcslcat(strdst, str2, strdstlen));
		WCSCHK(info, str_len  == test_wcslcat(strdst, str1, strdstlen));
	}
	stress_bogo_add(args, 6);
	return i * 6;
}
#elif defined(HAVE_WCSCAT) &&	\
      defined(HAVE_WCHAR)
/*
 *  stress_wcscat()
 *	stress on wcscat
 */
static size_t stress_wcscat(stress_args_t *args, stress_wcs_args_t *info)
{
	typedef wchar_t * (*test_wcscat_t)(wchar_t *dest, const wchar_t *src);

	const test_wcscat_t test_wcscat = (test_wcscat_t)info->libc_func;
	const wchar_t *str1 = info->str1;
	const wchar_t *str2 = info->str2;
	wchar_t *strdst = info->strdst;
	const size_t len1 = info->len1;
	register size_t i;

	for (i = 0; LIKELY(stress_continue_flag() && (i < len1 - 1)); i++) {
		*strdst = L'\0';
		WCSCHK(info, strdst == test_wcscat(strdst, str1));
		*strdst = L'\0';
		WCSCHK(info, strdst == test_wcscat(strdst, str2));
		*strdst = L'\0';
		WCSCHK(info, strdst == test_wcscat(strdst, str1));
		WCSCHK(info, strdst == test_wcscat(strdst, str2));
		*strdst = L'\0';
		WCSCHK(info, strdst == test_wcscat(strdst, str2));
		WCSCHK(info, strdst == test_wcscat(strdst, str1));
	}
	stress_bogo_add(args, 6);
	return i * 6;
}
#endif

#if defined(HAVE_WCSNCAT) &&	\
    defined(HAVE_WCHAR)
/*
 *  stress_wcsncat()
 *	stress on wcsncat
 */
static size_t stress_wcsncat(stress_args_t *args, stress_wcs_args_t *info)
{
	typedef wchar_t * (*test_wcsncat_t)(wchar_t *dest, const wchar_t *src, size_t n);

	const test_wcsncat_t test_wcsncat = (test_wcsncat_t)info->libc_func;
	const wchar_t *str1 = info->str1;
	const wchar_t *str2 = info->str2;
	wchar_t *strdst = info->strdst;
	const size_t len1 = info->len1;
	const size_t len2 = info->len2;
	register size_t i;

	for (i = 0; LIKELY(stress_continue_flag() && (i < len1 - 1)); i++) {
		*strdst = '\0';
		WCSCHK(info, strdst == test_wcsncat(strdst, str1, len1));
		*strdst = '\0';
		WCSCHK(info, strdst == test_wcsncat(strdst, str2, len2));
		*strdst = '\0';
		WCSCHK(info, strdst == test_wcsncat(strdst, str1, len1));
		WCSCHK(info, strdst == test_wcsncat(strdst, str2, len1 + len2));
		*strdst = '\0';
		WCSCHK(info, strdst == test_wcsncat(strdst, str2, i));
		WCSCHK(info, strdst == test_wcsncat(strdst, str1, i));
	}
	stress_bogo_add(args, 6);
	return i * 6;
}
#endif

#if defined(HAVE_WCSCHR) &&	\
    defined(HAVE_WCHAR)
/*
 *  stress_wcschr()
 *	stress on wcschr
 */
static size_t stress_wcschr(stress_args_t *args, stress_wcs_args_t *info)
{
	typedef wchar_t * (*test_wcschr_t)(const wchar_t *wcs, wchar_t wc);

	const test_wcschr_t test_wcschr = (test_wcschr_t)info->libc_func;
	const wchar_t *str1 = info->str1;
	const wchar_t *str2 = info->str2;
	const size_t len1 = info->len1;
	register size_t i;

	for (i = 0; LIKELY(stress_continue_flag() && (i < len1 - 1)); i++) {
		WCSCHK(info, NULL == test_wcschr(str1, '_'));
		WCSCHK(info, NULL != test_wcschr(str1, str1[0]));

		WCSCHK(info, NULL == test_wcschr(str2, '_'));
		WCSCHK(info, NULL != test_wcschr(str2, str2[0]));
	}
	stress_bogo_add(args, 4);
	return i * 4;
}
#endif

#if defined(HAVE_WCSRCHR) &&	\
    defined(HAVE_WCHAR)
/*
 *  stress_wcsrchr()
 *	stress on wcsrchr
 */
static size_t stress_wcsrchr(stress_args_t *args, stress_wcs_args_t *info)
{
	typedef wchar_t * (*test_wcsrchr_t)(const wchar_t *wcs, wchar_t wc);

	const test_wcsrchr_t test_wcsrchr = (test_wcsrchr_t)info->libc_func;
	const wchar_t *str1 = info->str1;
	const wchar_t *str2 = info->str2;
	const size_t len1 = info->len1;
	register size_t i;

	for (i = 0; LIKELY(stress_continue_flag() && (i < len1 - 1)); i++) {
		WCSCHK(info, NULL == test_wcsrchr(str1, '_'));
		WCSCHK(info, NULL != test_wcsrchr(str1, str1[0]));

		WCSCHK(info, NULL == test_wcsrchr(str2, '_'));
		WCSCHK(info, NULL != test_wcsrchr(str2, str2[0]));
	}
	stress_bogo_add(args, 4);
	return i * 4;
}
#endif

#if defined(HAVE_WCSCMP) &&	\
    defined(HAVE_WCHAR) &&	\
    !defined(STRESS_ARCH_M68K)
/*
 *  stress_wcscmp()
 *	stress on wcscmp
 */
static size_t stress_wcscmp(stress_args_t *args, stress_wcs_args_t *info)
{
	typedef int * (*test_wcscmp_t)(const wchar_t *s1, const wchar_t *s2);

	const test_wcscmp_t test_wcscmp = (test_wcscmp_t)info->libc_func;
	const wchar_t *str1 = info->str1;
	const wchar_t *str2 = info->str2;
	const size_t len1 = info->len1;
	register size_t i;

	for (i = 1; LIKELY(stress_continue_flag() && (i < len1)); i++) {
		WCSCHK(info, 0 == test_wcscmp(str1, str1));
		WCSCHK(info, 0 == test_wcscmp(str2, str2));

		WCSCHK(info, 0 != test_wcscmp(str2, str1));
		WCSCHK(info, 0 != test_wcscmp(str1, str2));

		WCSCHK(info, 0 != test_wcscmp(str1 + i, str1));
		WCSCHK(info, 0 != test_wcscmp(str1, str1 + i));
		WCSCHK(info, 0 == test_wcscmp(str1 + i, str1 + i));

		WCSCHK(info, 0 != test_wcscmp(str1 + i, str2));
		WCSCHK(info, 0 != test_wcscmp(str2, str1 + i));
	}
	stress_bogo_add(args, 9);
	return i * 9;
}
#endif

#if defined(HAVE_WCSNCMP) &&	\
    defined(HAVE_WCHAR)
/*
 *  stress_wcsncmp()
 *	stress on wcsncmp
 */
static size_t stress_wcsncmp(stress_args_t *args, stress_wcs_args_t *info)
{
	typedef int (*test_wcsncmp_t)(const wchar_t *s1, const wchar_t *s2, size_t n);

	const test_wcsncmp_t test_wcsncmp = (test_wcsncmp_t)info->libc_func;
	const wchar_t *str1 = info->str1;
	const wchar_t *str2 = info->str2;
	const size_t len1 = info->len1;
	const size_t len2 = info->len2;
	register size_t i;

	for (i = 1; LIKELY(stress_continue_flag() && (i < len1)); i++) {
		WCSCHK(info, 0 == test_wcsncmp(str1, str1, len1));
		WCSCHK(info, 0 == test_wcsncmp(str2, str2, len2));

		WCSCHK(info, 0 != test_wcsncmp(str2, str1, len2));
		WCSCHK(info, 0 != test_wcsncmp(str1, str2, len1));

		WCSCHK(info, 0 != test_wcsncmp(str1 + i, str1, len1));
		WCSCHK(info, 0 != test_wcsncmp(str1, str1 + i, len1));
		WCSCHK(info, 0 == test_wcsncmp(str1 + i, str1 + i, len1));

		WCSCHK(info, 0 != test_wcsncmp(str1 + i, str2, len2));
		WCSCHK(info, 0 != test_wcsncmp(str2, str1 + i, len2));
	}
	stress_bogo_add(args, 9);
	return i * 9;
}
#endif

#if defined(HAVE_WCSLEN) &&	\
    defined(HAVE_WCHAR)
/*
 *  stress_wcslen()
 *	stress on wcslen
 */
static size_t stress_wcslen(stress_args_t *args, stress_wcs_args_t *info)
{
	typedef size_t (*test_wcslen_t)(const wchar_t *s);

	const test_wcslen_t test_wcslen = (test_wcslen_t)info->libc_func;
	const wchar_t *str1 = info->str1;
	const wchar_t *str2 = info->str2;
	const size_t len1 = info->len1;
	const size_t len2 = info->len2;
	register size_t i;

	for (i = 0; LIKELY(stress_continue_flag() && (i < len1 - 1)); i++) {
		WCSCHK(info, len1 - 1 == test_wcslen(str1));
		WCSCHK(info, len1 - 1 - i == test_wcslen(str1 + i));
	}

	for (i = 0; LIKELY(stress_continue_flag() && (i < len2 - 1)); i++) {
		WCSCHK(info, len2 - 1 == test_wcslen(str2));
		WCSCHK(info, len2 - 1 - i == test_wcslen(str2 + i));
	}
	stress_bogo_add(args, 4);
	return i * 4;
}
#endif

#if defined(HAVE_WCSCOLL) &&	\
    defined(HAVE_WCHAR)
/*
 *  stress_wcscoll()
 *	stress on wcscoll
 */
static size_t stress_wcscoll(stress_args_t *args, stress_wcs_args_t *info)
{
	typedef int (*test_wcscoll_t)(const wchar_t *ws1, const wchar_t *ws2);

	const test_wcscoll_t test_wcscoll = (test_wcscoll_t)info->libc_func;
	const wchar_t *str1 = info->str1;
	const wchar_t *str2 = info->str2;
	const size_t len1 = info->len1;
	register size_t i;

	for (i = 1; LIKELY(stress_continue_flag() && (i < len1)); i++) {
		WCSCHK(info, 0 == test_wcscoll(str1, str1));
		WCSCHK(info, 0 == test_wcscoll(str2, str2));

		WCSCHK(info, 0 != test_wcscoll(str2, str1));
		WCSCHK(info, 0 != test_wcscoll(str1, str2));

		WCSCHK(info, 0 != test_wcscoll(str1 + i, str1));
		WCSCHK(info, 0 != test_wcscoll(str1, str1 + i));
		WCSCHK(info, 0 == test_wcscoll(str1 + i, str1 + i));

		WCSCHK(info, 0 != test_wcscoll(str1 + i, str2));
		WCSCHK(info, 0 != test_wcscoll(str2, str1 + i));
	}
	stress_bogo_add(args, 9);
	return i * 9;
}
#endif

#if defined(HAVE_WCSXFRM) &&	\
    defined(HAVE_WCHAR)
/*
 *  stress_wcsxfrm()
 *	stress on wcsxfrm
 */
static size_t stress_wcsxfrm(stress_args_t *args, stress_wcs_args_t *info)
{
	typedef size_t (*test_wcsxfrm_t)(wchar_t* destination, const wchar_t* source, size_t num);

	const test_wcsxfrm_t test_wcsxfrm = (test_wcsxfrm_t)info->libc_func;
	const wchar_t *str1 = info->str1;
	const wchar_t *str2 = info->str2;
	wchar_t *strdst = info->strdst;
	const size_t len1 = info->len1;
	const size_t strdstlen = info->strdstlen;
	register size_t i;

	for (i = 0; LIKELY(stress_continue_flag() && (i < len1 - 1)); i++) {
		*strdst = '\0';
		WCSCHK(info, 0 != test_wcsxfrm(strdst, str1, strdstlen));
		*strdst = '\0';
		WCSCHK(info, 0 != test_wcsxfrm(strdst, str2, strdstlen));
		*strdst = '\0';
		WCSCHK(info, 0 != test_wcsxfrm(strdst, str1, strdstlen));
		WCSCHK(info, 0 != test_wcsxfrm(strdst, str2, strdstlen));
		*strdst = '\0';
		WCSCHK(info, 0 != test_wcsxfrm(strdst, str2, strdstlen));
		WCSCHK(info, 0 != test_wcsxfrm(strdst, str1, strdstlen));
	}
	stress_bogo_add(args, 6);
	return i * 6;
}
#endif

static size_t stress_wcs_all(stress_args_t *args, stress_wcs_args_t *info);

/*
 * Table of wcs stress methods
 */
static const stress_wcs_method_info_t wcs_methods[] = {
	{ "all",		stress_wcs_all,		NULL },	/* Special "all" test */
#if defined(HAVE_WCSCASECMP) &&	\
    defined(HAVE_WCHAR)
	{ "wcscasecmp",		stress_wcscasecmp,	(void *)wcscasecmp },
#endif
#if defined(HAVE_WCSLCAT) &&		\
    defined(HAVE_WCSLEN) &&		\
    defined(HAVE_WCHAR) &&		\
    !defined(HAVE_COMPILER_PCC) &&	\
    !defined(BUILD_STATIC)
	{ "wcslcat",		stress_wcslcat,		(void *)wcslcat },
#elif defined(HAVE_WCSCAT) &&	\
      defined(HAVE_WCHAR)
	{ "wcscat",		stress_wcscat,		(void *)wcscat },
#endif
#if defined(HAVE_WCSCHR) &&	\
    defined(HAVE_WCHAR)
	{ "wcschr",		stress_wcschr,		(void *)wcschr },
#endif
#if defined(HAVE_WCSCMP) &&	\
    defined(HAVE_WCHAR) &&	\
    !defined(STRESS_ARCH_M68K)
	{ "wcscmp",		stress_wcscmp,		(void *)wcscmp },
#endif
#if defined(HAVE_WCSLCPY) &&		\
    defined(HAVE_WCSLEN) &&		\
    defined(HAVE_WCHAR) &&		\
    !defined(HAVE_COMPILER_PCC) &&	\
    !defined(BUILD_STATIC)
	{ "wcslcpy",		stress_wcslcpy,		(void *)wcslcpy },
#elif defined(HAVE_WCSCPY) &&	\
      defined(HAVE_WCHAR)
	{ "wcscpy",		stress_wcscpy,		(void *)wcscpy },
#endif
#if defined(HAVE_WCSLEN) &&	\
    defined(HAVE_WCHAR)
	{ "wcslen",		stress_wcslen,		(void *)wcslen },
#endif
#if defined(HAVE_WCSNCASECMP) &&	\
    defined(HAVE_WCHAR)
	{ "wcsncasecmp",	stress_wcsncasecmp,	(void *)wcsncasecmp },
#endif
#if defined(HAVE_WCSNCAT) &&	\
    defined(HAVE_WCHAR)
	{ "wcsncat",		stress_wcsncat,		(void *)wcsncat },
#endif
#if defined(HAVE_WCSNCMP) &&	\
    defined(HAVE_WCHAR)
	{ "wcsncmp",		stress_wcsncmp,		(void *)wcsncmp },
#endif
#if defined(HAVE_WCSRCHR) &&	\
    defined(HAVE_WCHAR)
	{ "wcsrchr",		stress_wcsrchr,		(void *)wcschr },
#endif
#if defined(HAVE_WCSCOLL) &&	\
    defined(HAVE_WCHAR)
	{ "wcscoll",		stress_wcscoll,		(void *)wcscoll },
#endif
#if defined(HAVE_WCSXFRM) &&	\
    defined(HAVE_WCHAR)
	{ "wcsxfrm",		stress_wcsxfrm,		(void *)wcsxfrm },
#endif
};

static stress_metrics_t metrics[SIZEOF_ARRAY(wcs_methods)];

/*
 *  stress_wcs_all()
 *	iterate over all wcs stressors
 */
static size_t stress_wcs_all(stress_args_t *args, stress_wcs_args_t *info)
{
	static size_t i = 1;	/* Skip over stress_wcs_all */
	stress_wcs_args_t info_all = *info;
	double t;

	if (UNLIKELY(SIZEOF_ARRAY(wcs_methods) < 2))
		return 0;

	info_all.libc_func = wcs_methods[i].libc_func;

	t = stress_time_now();
	metrics[i].count += (double)wcs_methods[i].func(args, &info_all);
	metrics[i].duration += (stress_time_now() - t);
	i++;
	if (i >= SIZEOF_ARRAY(wcs_methods))
		i = 1;

	info->failed = info_all.failed;
	return 0;
}

/*
 *  stress_wcs()
 *	stress CPU by doing wide character string ops
 */
static int stress_wcs(stress_args_t *args)
{
	size_t i, j, wcs_method = 0;
	const stress_wcs_method_info_t *wcs_method_info;
	wchar_t ALIGN64 str1[STR1LEN], ALIGN64 str2[STR2LEN];
	wchar_t strdst[STRDSTLEN];
	stress_wcs_args_t info;
	int metrics_count = 0;

	/* No wcs* functions available on this system? */
	if (SIZEOF_ARRAY(wcs_methods) < 2)
		return stress_unimplemented(args);

	(void)stress_get_setting("wcs-method", &wcs_method);
	wcs_method_info = &wcs_methods[wcs_method];
	info.libc_func = wcs_method_info->libc_func;
	info.str1 = str1;
	info.len1 = STR1LEN;
	info.str2 = str2;
	info.len2 = STR2LEN;
	info.strdst = strdst;
	info.strdstlen = STRDSTLEN;
	info.failed = false;
	info.name = args->name;

	stress_wcs_fill(info.str1, info.len1);
	stress_zero_metrics(metrics, SIZEOF_ARRAY(metrics));

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		register wchar_t *tmpptr;
		register size_t tmplen;

		stress_wcs_fill(info.str2, info.len2);
		if (UNLIKELY(metrics_count++ > 1000)) {
			double t;

			metrics_count = 0;
			t = stress_time_now();
			metrics[wcs_method].count += (double)wcs_method_info->func(args, &info);
			metrics[wcs_method].duration += (stress_time_now() - t);
		} else {
			(void)wcs_method_info->func(args, &info);
		}

		tmpptr = info.str1;
		info.str1 = info.str2;
		info.str2 = tmpptr;

		tmplen = info.len1;
		info.len1 = info.len2;
		info.len2 = tmplen;

		stress_bogo_inc(args);
	} while (stress_continue(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	/* dump metrics of methods except for first "all" method */
	for (i = 1, j = 0; i < SIZEOF_ARRAY(metrics); i++) {
		if (metrics[i].duration > 0.0) {
			char msg[64];
			const double rate = metrics[i].count / metrics[i].duration;

			(void)snprintf(msg, sizeof(msg), "%s calls per sec", wcs_methods[i].name);
			stress_metrics_set(args, j, msg,
				rate, STRESS_METRIC_HARMONIC_MEAN);
			j++;
		}
	}

	return info.failed ? EXIT_FAILURE : EXIT_SUCCESS;
}

static const char *stress_wcs_method(const size_t i)
{
	return (i < SIZEOF_ARRAY(wcs_methods)) ? wcs_methods[i].name : NULL;
}

static const stress_opt_t opts[] = {
	{ OPT_wcs_method, "wcs-method", TYPE_ID_SIZE_T_METHOD, 0, 0, stress_wcs_method },
	END_OPT,
};

const stressor_info_t stress_wcs_info = {
	.stressor = stress_wcs,
	.classifier = CLASS_CPU | CLASS_CPU_CACHE | CLASS_MEMORY,
	.opts = opts,
	.verify = VERIFY_OPTIONAL,
	.help = help,
	.unimplemented_reason = "built without wchar.h or bsd/wchar.h"
};
