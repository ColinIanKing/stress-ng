/*
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

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include "stress-ng.h"

#define STR1LEN 256
#define STR2LEN 128

/*
 *  the wide string stress test has different classes of stressors
 */
typedef void (*stress_wcs_func)(
	const char *name,
	wchar_t *str1,
	const size_t len1,
	wchar_t *str2,
	const size_t len2);

typedef struct {
	const char		*name;	/* human readable form of stressor */
	const stress_wcs_func	func;	/* the stressor function */
} stress_wcs_stressor_info_t;

static const stress_wcs_stressor_info_t *opt_wcs_stressor;
static const stress_wcs_stressor_info_t wcs_methods[];

/*
 *  stress_wcs_fill
 */
static void stress_wcs_fill(wchar_t *wcstr, const size_t len)
{
	register size_t i;

	for (i = 0; i < (len-1); i++) {
		*wcstr++ = (mwc8() % 26) + L'a';
	}
	*wcstr = L'\0';
}

static inline void wcschk(
	const char *name,
	const int ok,
	const char *msg)
{
	if ((opt_flags & OPT_FLAGS_VERIFY) && (!ok))
		pr_fail(stderr, "%s: %s did not return expected result\n",
			name, msg);
}

#define STR(x)	# x

#define WCSCHK(name, test)	\
	wcschk(name, test, STR(test))


/*
 *  stress_wcscasecmp()
 *	stress on wcscasecmp
 */
static void stress_wcscasecmp(
	const char *name,
	wchar_t *str1,
	const size_t len1,
	wchar_t *str2,
	const size_t len2)
{
	register size_t i;

	(void)len2;

	for (i = 1; i < len1; i++) {
		WCSCHK(name, 0 == wcscasecmp(str1, str1));
		WCSCHK(name, 0 == wcscasecmp(str2, str2));

		WCSCHK(name, 0 != wcscasecmp(str2, str1));
		WCSCHK(name, 0 != wcscasecmp(str1, str2));

		WCSCHK(name, 0 != wcscasecmp(str1 + i, str1));
		WCSCHK(name, 0 != wcscasecmp(str1, str1 + i));
		WCSCHK(name, 0 == wcscasecmp(str1 + i, str1 + i));

		WCSCHK(name, 0 != wcscasecmp(str1 + i, str2));
		WCSCHK(name, 0 != wcscasecmp(str2, str1 + i));
	}
}

/*
 *  stress_wcsncasecmp()
 *	stress on wcsncasecmp
 */
static void stress_wcsncasecmp(
	const char *name,
	wchar_t *str1,
	const size_t len1,
	wchar_t *str2,
	const size_t len2)
{
	register size_t i;

	(void)len2;

	for (i = 1; i < len1; i++) {
		WCSCHK(name, 0 == wcsncasecmp(str1, str1, len1));
		WCSCHK(name, 0 == wcsncasecmp(str2, str2, len2));

		WCSCHK(name, 0 != wcsncasecmp(str2, str1, len2));
		WCSCHK(name, 0 != wcsncasecmp(str1, str2, len1));

		WCSCHK(name, 0 != wcsncasecmp(str1 + i, str1, len1));
		WCSCHK(name, 0 != wcsncasecmp(str1, str1 + i, len1));
		WCSCHK(name, 0 == wcsncasecmp(str1 + i, str1 + i, len1));

		WCSCHK(name, 0 != wcsncasecmp(str1 + i, str2, len1));
		WCSCHK(name, 0 != wcsncasecmp(str2, str1 + i, len2));
	}
}

/*
 *  stress_wcscpy()
 *	stress on wcscpy
 */
static void stress_wcscpy(
	const char *name,
	wchar_t *str1,
	const size_t len1,
	wchar_t *str2,
	const size_t len2)
{
	register size_t i;
	wchar_t buf[len1 + len2];

	for (i = 0; i < len1 - 1; i++) {
		WCSCHK(name, buf == wcscpy(buf, str1));
		WCSCHK(name, buf == wcscpy(buf, str2));
	}
}

/*
 *  stress_wcscat()
 *	stress on wcscat
 */
static void stress_wcscat(
	const char *name,
	wchar_t *str1,
	const size_t len1,
	wchar_t *str2,
	const size_t len2)
{
	register size_t i;
	wchar_t buf[len1 + len2 + 1];


	for (i = 0; i < len1 - 1; i++) {
		*buf = L'\0';
		WCSCHK(name, buf == wcscat(buf, str1));
		*buf = L'\0';
		WCSCHK(name, buf == wcscat(buf, str2));
		*buf = L'\0';
		WCSCHK(name, buf == wcscat(buf, str1));
		WCSCHK(name, buf == wcscat(buf, str2));
		*buf = L'\0';
		WCSCHK(name, buf == wcscat(buf, str2));
		WCSCHK(name, buf == wcscat(buf, str1));
	}
}

/*
 *  stress_wcsncat()
 *	stress on wcsncat
 */
static void stress_wcsncat(
	const char *name,
	wchar_t *str1,
	const size_t len1,
	wchar_t *str2,
	const size_t len2)
{
	register size_t i;
	wchar_t buf[len1 + len2 + 1];


	for (i = 0; i < len1 - 1; i++) {
		*buf = '\0';
		WCSCHK(name, buf == wcsncat(buf, str1, len1));
		*buf = '\0';
		WCSCHK(name, buf == wcsncat(buf, str2, len2));
		*buf = '\0';
		WCSCHK(name, buf == wcsncat(buf, str1, len1));
		WCSCHK(name, buf == wcsncat(buf, str2, len1 + len2));
		*buf = '\0';
		WCSCHK(name, buf == wcsncat(buf, str2, i));
		WCSCHK(name, buf == wcsncat(buf, str1, i));
	}
}

/*
 *  stress_wcschr()
 *	stress on wcschr
 */
static void stress_wcschr(
	const char *name,
	wchar_t *str1,
	const size_t len1,
	wchar_t *str2,
	const size_t len2)
{
	register size_t i;

	(void)len2;

	for (i = 0; i < len1 - 1; i++) {
		WCSCHK(name, NULL == wcschr(str1, '_'));
		WCSCHK(name, NULL != wcschr(str1, str1[0]));

		WCSCHK(name, NULL == wcschr(str2, '_'));
		WCSCHK(name, NULL != wcschr(str2, str2[0]));
	}
}

/*
 *  stress_wcsrchr()
 *	stress on wcsrchr
 */
static void stress_wcsrchr(
	const char *name,
	wchar_t *str1,
	const size_t len1,
	wchar_t *str2,
	const size_t len2)
{
	register size_t i;

	(void)len2;

	for (i = 0; i < len1 - 1; i++) {
		WCSCHK(name, NULL == wcsrchr(str1, '_'));
		WCSCHK(name, NULL != wcsrchr(str1, str1[0]));

		WCSCHK(name, NULL == wcsrchr(str2, '_'));
		WCSCHK(name, NULL != wcsrchr(str2, str2[0]));
	}
}

/*
 *  stress_wcscmp()
 *	stress on wcscmp
 */
static void stress_wcscmp(
	const char *name,
	wchar_t *str1,
	const size_t len1,
	wchar_t *str2,
	const size_t len2)
{
	register size_t i;

	(void)len2;

	for (i = 1; i < len1; i++) {
		WCSCHK(name, 0 == wcscmp(str1, str1));
		WCSCHK(name, 0 == wcscmp(str2, str2));

		WCSCHK(name, 0 != wcscmp(str2, str1));
		WCSCHK(name, 0 != wcscmp(str1, str2));

		WCSCHK(name, 0 != wcscmp(str1 + i, str1));
		WCSCHK(name, 0 != wcscmp(str1, str1 + i));
		WCSCHK(name, 0 == wcscmp(str1 + i, str1 + i));

		WCSCHK(name, 0 != wcscmp(str1 + i, str2));
		WCSCHK(name, 0 != wcscmp(str2, str1 + i));
	}
}

/*
 *  stress_wcsncmp()
 *	stress on wcsncmp
 */
static void stress_wcsncmp(
	const char *name,
	wchar_t *str1,
	const size_t len1,
	wchar_t *str2,
	const size_t len2)
{
	register size_t i;

	for (i = 1; i < len1; i++) {
		WCSCHK(name, 0 == wcsncmp(str1, str1, len1));
		WCSCHK(name, 0 == wcsncmp(str2, str2, len2));

		WCSCHK(name, 0 != wcsncmp(str2, str1, len2));
		WCSCHK(name, 0 != wcsncmp(str1, str2, len1));

		WCSCHK(name, 0 != wcsncmp(str1 + i, str1, len1));
		WCSCHK(name, 0 != wcsncmp(str1, str1 + i, len1));
		WCSCHK(name, 0 == wcsncmp(str1 + i, str1 + i, len1));

		WCSCHK(name, 0 != wcsncmp(str1 + i, str2, len2));
		WCSCHK(name, 0 != wcsncmp(str2, str1 + i, len2));
	}
}

/*
 *  stress_wcslen()
 *	stress on wcslen
 */
static void stress_wcslen(
	const char *name,
	wchar_t *str1,
	const size_t len1,
	wchar_t *str2,
	const size_t len2)
{
	register size_t i;

	for (i = 0; i < len1 - 1; i++) {
		WCSCHK(name, len1 - 1 == wcslen(str1));
		WCSCHK(name, len1 - 1 - i == wcslen(str1 + i));
	}

	for (i = 0; i < len2 - 1; i++) {
		WCSCHK(name, len2 - 1 == wcslen(str2));
		WCSCHK(name, len2 - 1 - i == wcslen(str2 + i));
	}
}

/*
 *  stress_wcscoll()
 *	stress on wcscoll
 */
static void stress_wcscoll(
	const char *name,
	wchar_t *str1,
	const size_t len1,
	wchar_t *str2,
	const size_t len2)
{
	register size_t i;

	(void)len2;

	for (i = 1; i < len1; i++) {
		WCSCHK(name, 0 == wcscoll(str1, str1));
		WCSCHK(name, 0 == wcscoll(str2, str2));

		WCSCHK(name, 0 != wcscoll(str2, str1));
		WCSCHK(name, 0 != wcscoll(str1, str2));

		WCSCHK(name, 0 != wcscoll(str1 + i, str1));
		WCSCHK(name, 0 != wcscoll(str1, str1 + i));
		WCSCHK(name, 0 == wcscoll(str1 + i, str1 + i));

		WCSCHK(name, 0 != wcscoll(str1 + i, str2));
		WCSCHK(name, 0 != wcscoll(str2, str1 + i));
	}
}

/*
 *  stress_wcsxfrm()
 *	stress on wcsxfrm
 */
static void stress_wcsxfrm(
	const char *name,
	wchar_t *str1,
	const size_t len1,
	wchar_t *str2,
	const size_t len2)
{
	register size_t i;
	wchar_t buf[len1 + len2];

	for (i = 0; i < len1 - 1; i++) {
		*buf = '\0';
		WCSCHK(name, 0 != wcsxfrm(buf, str1, sizeof(buf)));
		*buf = '\0';
		WCSCHK(name, 0 != wcsxfrm(buf, str2, sizeof(buf)));
		*buf = '\0';
		WCSCHK(name, 0 != wcsxfrm(buf, str1, sizeof(buf)));
		WCSCHK(name, 0 != wcsxfrm(buf, str2, sizeof(buf)));
		*buf = '\0';
		WCSCHK(name, 0 != wcsxfrm(buf, str2, sizeof(buf)));
		WCSCHK(name, 0 != wcsxfrm(buf, str1, sizeof(buf)));
	}
}


/*
 *  stress_wcs_all()
 *	iterate over all wcs stressors
 */
static void stress_wcs_all(
	const char *name,
	wchar_t *str1,
	const size_t len1,
	wchar_t *str2,
	const size_t len2)
{
	static int i = 1;	/* Skip over stress_wcs_all */

	wcs_methods[i++].func(name, str1, len1, str2, len2);
	if (!wcs_methods[i].func)
		i = 1;
}

/*
 * Table of wcs stress methods
 */
static const stress_wcs_stressor_info_t wcs_methods[] = {
	{ "all",		stress_wcs_all },	/* Special "all" test */

	{ "wcscasecmp",		stress_wcscasecmp },
	{ "wcscat",		stress_wcscat },
	{ "wcschr",		stress_wcschr },
	{ "wcscmp",		stress_wcscmp },
	{ "wcscpy",		stress_wcscpy },
	{ "wcslen",		stress_wcslen },
	{ "wcsncasecmp",	stress_wcsncasecmp },
	{ "wcsncat",		stress_wcsncat },
	{ "wcsncmp",		stress_wcsncmp },
	{ "wcsrchr",		stress_wcsrchr },
	{ "wcscoll",		stress_wcscoll },
	{ "wcsxfrm",		stress_wcsxfrm },
	{ NULL,			NULL }
};

/*
 *  stress_set_wcs_method()
 *	set the specified wcs stress method
 */
int stress_set_wcs_method(const char *name)
{
	stress_wcs_stressor_info_t const *wcsfunction = wcs_methods;


	for (wcsfunction = wcs_methods; wcsfunction->func; wcsfunction++) {
		if (!strcmp(wcsfunction->name, name)) {
			opt_wcs_stressor = wcsfunction;
			return 0;
		}
	}

	fprintf(stderr, "wcs-method must be one of:");
	for (wcsfunction = wcs_methods; wcsfunction->func; wcsfunction++) {
		fprintf(stderr, " %s", wcsfunction->name);
	}
	fprintf(stderr, "\n");

	return -1;
}

/*
 *  stress_wcs()
 *	stress CPU by doing wide character string ops
 */
int stress_wcs(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	stress_wcs_func func = opt_wcs_stressor->func;

	(void)instance;

	do {
		wchar_t str1[STR1LEN], str2[STR2LEN];

		stress_wcs_fill(str1, STR1LEN);
		stress_wcs_fill(str2, STR2LEN);

		(void)func(name, str1, STR1LEN, str2, STR2LEN);
		(*counter)++;
	} while (opt_do_run && (!max_ops || *counter < max_ops));
	return EXIT_SUCCESS;
}
