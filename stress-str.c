/*
 * Copyright (C) 2013-2016 Canonical, Ltd.
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
#include <strings.h>
#include "stress-ng.h"

/*
 *  the STR stress test has different classes of string stressors
 */
typedef void (*stress_str_func)(
	const char *name,
	char *str1,
	const size_t len1,
	char *str2,
	const size_t len2);

typedef struct {
	const char		*name;	/* human readable form of stressor */
	const stress_str_func	func;	/* the stressor function */
} stress_str_stressor_info_t;

static const stress_str_stressor_info_t *opt_str_stressor;
static const stress_str_stressor_info_t str_methods[];

static inline void strchk(
	const char *name,
	const int ok,
	const char *msg)
{
	if ((opt_flags & OPT_FLAGS_VERIFY) && (!ok))
		pr_fail(stderr, "%s: %s did not return expected result\n",
			name, msg);
}

#define STR(x)	# x

#define STRCHK(name, test)	\
	strchk(name, test, STR(test))


/*
 *  stress_strcasecmp()
 *	stress on strcasecmp
 */
static void stress_strcasecmp(
	const char *name,
	char *str1,
	const size_t len1,
	char *str2,
	const size_t len2)
{
	register size_t i;

	(void)len2;

	for (i = 1; i < len1; i++) {
		STRCHK(name, 0 == strcasecmp(str1, str1));
		STRCHK(name, 0 == strcasecmp(str2, str2));

		STRCHK(name, 0 != strcasecmp(str2, str1));
		STRCHK(name, 0 != strcasecmp(str1, str2));

		STRCHK(name, 0 != strcasecmp(str1 + i, str1));
		STRCHK(name, 0 != strcasecmp(str1, str1 + i));
		STRCHK(name, 0 == strcasecmp(str1 + i, str1 + i));

		STRCHK(name, 0 != strcasecmp(str1 + i, str2));
		STRCHK(name, 0 != strcasecmp(str2, str1 + i));
	}
}

/*
 *  stress_strncasecmp()
 *	stress on strncasecmp
 */
static void stress_strncasecmp(
	const char *name,
	char *str1,
	const size_t len1,
	char *str2,
	const size_t len2)
{
	register size_t i;

	(void)len2;

	for (i = 1; i < len1; i++) {
		STRCHK(name, 0 == strncasecmp(str1, str1, len1));
		STRCHK(name, 0 == strncasecmp(str2, str2, len2));

		STRCHK(name, 0 != strncasecmp(str2, str1, len2));
		STRCHK(name, 0 != strncasecmp(str1, str2, len1));

		STRCHK(name, 0 != strncasecmp(str1 + i, str1, len1));
		STRCHK(name, 0 != strncasecmp(str1, str1 + i, len1));
		STRCHK(name, 0 == strncasecmp(str1 + i, str1 + i, len1));

		STRCHK(name, 0 != strncasecmp(str1 + i, str2, len1));
		STRCHK(name, 0 != strncasecmp(str2, str1 + i, len2));
	}
}

/*
 *  stress_index()
 *	stress on index
 */
static void stress_index(
	const char *name,
	char *str1,
	const size_t len1,
	char *str2,
	const size_t len2)
{
	register size_t i;

	(void)len2;

	for (i = 0; i < len1 - 1; i++) {
		STRCHK(name, NULL == index(str1, '_'));
		STRCHK(name, NULL != index(str1, str1[0]));

		STRCHK(name, NULL == index(str2, '_'));
		STRCHK(name, NULL != index(str2, str2[0]));
	}
}

/*
 *  stress_rindex()
 *	stress on rindex
 */
static void stress_rindex(
	const char *name,
	char *str1,
	const size_t len1,
	char *str2,
	const size_t len2)
{
	register size_t i;

	(void)len2;

	for (i = 0; i < len1 - 1; i++) {
		STRCHK(name, NULL == rindex(str1, '_'));
		STRCHK(name, NULL != rindex(str1, str1[0]));

		STRCHK(name, NULL == rindex(str2, '_'));
		STRCHK(name, NULL != rindex(str2, str2[0]));
	}
}

/*
 *  stress_strcpy()
 *	stress on strcpy
 */
static void stress_strcpy(
	const char *name,
	char *str1,
	const size_t len1,
	char *str2,
	const size_t len2)
{
	register size_t i;
	char buf[len1 + len2];

	for (i = 0; i < len1 - 1; i++) {
		STRCHK(name, buf == strcpy(buf, str1));
		STRCHK(name, buf == strcpy(buf, str2));
	}
}

/*
 *  stress_strcat()
 *	stress on strcat
 */
static void stress_strcat(
	const char *name,
	char *str1,
	const size_t len1,
	char *str2,
	const size_t len2)
{
	register size_t i;
	char buf[len1 + len2 + 1];

	for (i = 0; i < len1 - 1; i++) {
		*buf = '\0';
		STRCHK(name, buf == strcat(buf, str1));
		*buf = '\0';
		STRCHK(name, buf == strcat(buf, str2));
		*buf = '\0';
		STRCHK(name, buf == strcat(buf, str1));
		STRCHK(name, buf == strcat(buf, str2));
		*buf = '\0';
		STRCHK(name, buf == strcat(buf, str2));
		STRCHK(name, buf == strcat(buf, str1));
	}
}

/*
 *  stress_strncat()
 *	stress on strncat
 */
static void stress_strncat(
	const char *name,
	char *str1,
	const size_t len1,
	char *str2,
	const size_t len2)
{
	register size_t i;
	char buf[len1 + len2 + 1];

	for (i = 0; i < len1 - 1; i++) {
		*buf = '\0';
		STRCHK(name, buf == strncat(buf, str1, len1));
		*buf = '\0';
		STRCHK(name, buf == strncat(buf, str2, len2));
		*buf = '\0';
		STRCHK(name, buf == strncat(buf, str1, len1));
		STRCHK(name, buf == strncat(buf, str2, len1 + len2));
		*buf = '\0';
		STRCHK(name, buf == strncat(buf, str2, i));
		STRCHK(name, buf == strncat(buf, str1, i));
	}
}

/*
 *  stress_strchr()
 *	stress on strchr
 */
static void stress_strchr(
	const char *name,
	char *str1,
	const size_t len1,
	char *str2,
	const size_t len2)
{
	register size_t i;

	(void)len2;

	for (i = 0; i < len1 - 1; i++) {
		STRCHK(name, NULL == strchr(str1, '_'));
		STRCHK(name, NULL != strchr(str1, str1[0]));

		STRCHK(name, NULL == strchr(str2, '_'));
		STRCHK(name, NULL != strchr(str2, str2[0]));
	}
}

/*
 *  stress_strrchr()
 *	stress on strrchr
 */
static void stress_strrchr(
	const char *name,
	char *str1,
	const size_t len1,
	char *str2,
	const size_t len2)
{
	register size_t i;

	(void)len2;

	for (i = 0; i < len1 - 1; i++) {
		STRCHK(name, NULL == strrchr(str1, '_'));
		STRCHK(name, NULL != strrchr(str1, str1[0]));

		STRCHK(name, NULL == strrchr(str2, '_'));
		STRCHK(name, NULL != strrchr(str2, str2[0]));
	}
}

/*
 *  stress_strcmp()
 *	stress on strcmp
 */
static void stress_strcmp(
	const char *name,
	char *str1,
	const size_t len1,
	char *str2,
	const size_t len2)
{
	register size_t i;

	(void)len2;

	for (i = 1; i < len1; i++) {
		STRCHK(name, 0 == strcmp(str1, str1));
		STRCHK(name, 0 == strcmp(str2, str2));

		STRCHK(name, 0 != strcmp(str2, str1));
		STRCHK(name, 0 != strcmp(str1, str2));

		STRCHK(name, 0 != strcmp(str1 + i, str1));
		STRCHK(name, 0 != strcmp(str1, str1 + i));
		STRCHK(name, 0 == strcmp(str1 + i, str1 + i));

		STRCHK(name, 0 != strcmp(str1 + i, str2));
		STRCHK(name, 0 != strcmp(str2, str1 + i));
	}
}

/*
 *  stress_strncmp()
 *	stress on strncmp
 */
static void stress_strncmp(
	const char *name,
	char *str1,
	const size_t len1,
	char *str2,
	const size_t len2)
{
	register size_t i;

	for (i = 1; i < len1; i++) {
		STRCHK(name, 0 == strncmp(str1, str1, len1));
		STRCHK(name, 0 == strncmp(str2, str2, len2));

		STRCHK(name, 0 != strncmp(str2, str1, len2));
		STRCHK(name, 0 != strncmp(str1, str2, len1));

		STRCHK(name, 0 != strncmp(str1 + i, str1, len1));
		STRCHK(name, 0 != strncmp(str1, str1 + i, len1));
		STRCHK(name, 0 == strncmp(str1 + i, str1 + i, len1));

		STRCHK(name, 0 != strncmp(str1 + i, str2, len2));
		STRCHK(name, 0 != strncmp(str2, str1 + i, len2));
	}
}
/*
 *  stress_strcoll()
 *	stress on strcoll
 */
static void stress_strcoll(
	const char *name,
	char *str1,
	const size_t len1,
	char *str2,
	const size_t len2)
{
	register size_t i;

	(void)len2;

	for (i = 1; i < len1; i++) {
		STRCHK(name, 0 == strcoll(str1, str1));
		STRCHK(name, 0 == strcoll(str2, str2));

		STRCHK(name, 0 != strcoll(str2, str1));
		STRCHK(name, 0 != strcoll(str1, str2));

		STRCHK(name, 0 != strcoll(str1 + i, str1));
		STRCHK(name, 0 != strcoll(str1, str1 + i));
		STRCHK(name, 0 == strcoll(str1 + i, str1 + i));

		STRCHK(name, 0 != strcoll(str1 + i, str2));
		STRCHK(name, 0 != strcoll(str2, str1 + i));
	}
}

/*
 *  stress_strlen()
 *	stress on strlen
 */
static void stress_strlen(
	const char *name,
	char *str1,
	const size_t len1,
	char *str2,
	const size_t len2)
{
	register size_t i;

	for (i = 0; i < len1 - 1; i++) {
		STRCHK(name, len1 - 1 == strlen(str1));
		STRCHK(name, len1 - 1 - i == strlen(str1 + i));
	}

	for (i = 0; i < len2 - 1; i++) {
		STRCHK(name, len2 - 1 == strlen(str2));
		STRCHK(name, len2 - 1 - i == strlen(str2 + i));
	}
}

/*
 *  stress_strxfrm()
 *	stress on strxfrm
 */
static void stress_strxfrm(
	const char *name,
	char *str1,
	const size_t len1,
	char *str2,
	const size_t len2)
{
	register size_t i;
	char buf[len1 + len2];

	for (i = 0; i < len1 - 1; i++) {
		*buf = '\0';
		STRCHK(name, 0 != strxfrm(buf, str1, sizeof(buf)));
		*buf = '\0';
		STRCHK(name, 0 != strxfrm(buf, str2, sizeof(buf)));
		*buf = '\0';
		STRCHK(name, 0 != strxfrm(buf, str1, sizeof(buf)));
		STRCHK(name, 0 != strxfrm(buf, str2, sizeof(buf)));
		*buf = '\0';
		STRCHK(name, 0 != strxfrm(buf, str2, sizeof(buf)));
		STRCHK(name, 0 != strxfrm(buf, str1, sizeof(buf)));
	}
}


/*
 *  stress_str_all()
 *	iterate over all string stressors
 */
static void stress_str_all(
	const char *name,
	char *str1,
	const size_t len1,
	char *str2,
	const size_t len2)
{
	static int i = 1;	/* Skip over stress_str_all */

	str_methods[i++].func(name, str1, len1, str2, len2);
	if (!str_methods[i].func)
		i = 1;
}

/*
 * Table of string stress methods
 */
static const stress_str_stressor_info_t str_methods[] = {
	{ "all",		stress_str_all },	/* Special "all test */

	{ "index",		stress_index },
	{ "rindex",		stress_rindex },
	{ "strcasecmp",		stress_strcasecmp },
	{ "strcat",		stress_strcat },
	{ "strchr",		stress_strchr },
	{ "strcoll",		stress_strcoll },
	{ "strcmp",		stress_strcmp },
	{ "strcpy",		stress_strcpy },
	{ "strlen",		stress_strlen },
	{ "strncasecmp",	stress_strncasecmp },
	{ "strncat",		stress_strncat },
	{ "strncmp",		stress_strncmp },
	{ "strrchr",		stress_strrchr },
	{ "strxfrm",		stress_strxfrm },
	{ NULL,			NULL }
};

/*
 *  stress_set_str_method()
 *	set the default string stress method
 */
int stress_set_str_method(const char *name)
{
	stress_str_stressor_info_t const *info = str_methods;


	for (info = str_methods; info->func; info++) {
		if (!strcmp(info->name, name)) {
			opt_str_stressor = info;
			return 0;
		}
	}

	fprintf(stderr, "str-method must be one of:");
	for (info = str_methods; info->func; info++) {
		fprintf(stderr, " %s", info->name);
	}
	fprintf(stderr, "\n");

	return -1;
}

/*
 *  stress_str()
 *	stress CPU by doing various string operations
 */
int stress_str(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	stress_str_func func = opt_str_stressor->func;

	(void)instance;

	do {
		char str1[256], str2[128];

		stress_strnrnd(str1, sizeof(str1));
		stress_strnrnd(str2, sizeof(str2));

		(void)func(name, str1, sizeof(str1), str2, sizeof(str2));
		(*counter)++;
	} while (opt_do_run && (!max_ops || *counter < max_ops));
	return EXIT_SUCCESS;
}
