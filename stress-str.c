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
	const void *libc_func,
	const char *name,
	char *str1,
	const size_t len1,
	char *str2,
	const size_t len2);


typedef struct {
	const char 		*name;	/* human readable form of stressor */
	const stress_str_func	func;	/* the stressor function */
	const void 		*libc_func;
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
	const void *libc_func,
	const char *name,
	char *str1,
	const size_t len1,
	char *str2,
	const size_t len2)
{
	register size_t i;
	int (*__strcasecmp)(const char *s1, const char *s2) = libc_func;

	(void)len2;

	for (i = 1; i < len1; i++) {
		STRCHK(name, 0 == __strcasecmp(str1, str1));
		STRCHK(name, 0 == __strcasecmp(str2, str2));

		STRCHK(name, 0 != __strcasecmp(str2, str1));
		STRCHK(name, 0 != __strcasecmp(str1, str2));

		STRCHK(name, 0 != __strcasecmp(str1 + i, str1));
		STRCHK(name, 0 != __strcasecmp(str1, str1 + i));
		STRCHK(name, 0 == __strcasecmp(str1 + i, str1 + i));

		STRCHK(name, 0 != __strcasecmp(str1 + i, str2));
		STRCHK(name, 0 != __strcasecmp(str2, str1 + i));
	}
}

/*
 *  stress_strncasecmp()
 *	stress on strncasecmp
 */
static void stress_strncasecmp(
	const void *libc_func,
	const char *name,
	char *str1,
	const size_t len1,
	char *str2,
	const size_t len2)
{
	register size_t i;
	int (*__strncasecmp)(const char *s1, const char *s2, size_t n) = libc_func;

	(void)len2;

	for (i = 1; i < len1; i++) {
		STRCHK(name, 0 == __strncasecmp(str1, str1, len1));
		STRCHK(name, 0 == __strncasecmp(str2, str2, len2));

		STRCHK(name, 0 != __strncasecmp(str2, str1, len2));
		STRCHK(name, 0 != __strncasecmp(str1, str2, len1));

		STRCHK(name, 0 != __strncasecmp(str1 + i, str1, len1));
		STRCHK(name, 0 != __strncasecmp(str1, str1 + i, len1));
		STRCHK(name, 0 == __strncasecmp(str1 + i, str1 + i, len1));

		STRCHK(name, 0 != __strncasecmp(str1 + i, str2, len1));
		STRCHK(name, 0 != __strncasecmp(str2, str1 + i, len2));
	}
}

/*
 *  stress_index()
 *	stress on index
 */
static void stress_index(
	const void *libc_func,
	const char *name,
	char *str1,
	const size_t len1,
	char *str2,
	const size_t len2)
{
	register size_t i;
	char * (*__index)(const char *s, int c) = libc_func;

	(void)len2;

	for (i = 0; i < len1 - 1; i++) {
		STRCHK(name, NULL == __index(str1, '_'));
		STRCHK(name, NULL != __index(str1, str1[0]));

		STRCHK(name, NULL == __index(str2, '_'));
		STRCHK(name, NULL != __index(str2, str2[0]));
	}
}

/*
 *  stress_rindex()
 *	stress on rindex
 */
static void stress_rindex(
	const void *libc_func,
	const char *name,
	char *str1,
	const size_t len1,
	char *str2,
	const size_t len2)
{
	register size_t i;
	char * (*__rindex)(const char *s, int c) = libc_func;

	(void)len2;

	for (i = 0; i < len1 - 1; i++) {
		STRCHK(name, NULL == __rindex(str1, '_'));
		STRCHK(name, NULL != __rindex(str1, str1[0]));

		STRCHK(name, NULL == __rindex(str2, '_'));
		STRCHK(name, NULL != __rindex(str2, str2[0]));
	}
}

/*
 *  stress_strcpy()
 *	stress on strcpy
 */
static void stress_strcpy(
	const void *libc_func,
	const char *name,
	char *str1,
	const size_t len1,
	char *str2,
	const size_t len2)
{
	register size_t i;
	char * (*__strcpy)(char *dest, const char *src) = libc_func;

	char buf[len1 + len2];

	for (i = 0; i < len1 - 1; i++) {
		STRCHK(name, buf == __strcpy(buf, str1));
		STRCHK(name, buf == __strcpy(buf, str2));
	}
}

/*
 *  stress_strcat()
 *	stress on strcat
 */
static void stress_strcat(
	const void *libc_func,
	const char *name,
	char *str1,
	const size_t len1,
	char *str2,
	const size_t len2)
{
	register size_t i;
	char * (*__strcat)(char *dest, const char *src) = libc_func;

	char buf[len1 + len2 + 1];

	for (i = 0; i < len1 - 1; i++) {
		*buf = '\0';
		STRCHK(name, buf == __strcat(buf, str1));
		*buf = '\0';
		STRCHK(name, buf == __strcat(buf, str2));
		*buf = '\0';
		STRCHK(name, buf == __strcat(buf, str1));
		STRCHK(name, buf == __strcat(buf, str2));
		*buf = '\0';
		STRCHK(name, buf == __strcat(buf, str2));
		STRCHK(name, buf == __strcat(buf, str1));
	}
}

/*
 *  stress_strncat()
 *	stress on strncat
 */
static void stress_strncat(
	const void *libc_func,
	const char *name,
	char *str1,
	const size_t len1,
	char *str2,
	const size_t len2)
{
	register size_t i;
	char * (*__strncat)(char *dest, const char *src, size_t n) = libc_func;
	char buf[len1 + len2 + 1];

	for (i = 0; i < len1 - 1; i++) {
		*buf = '\0';
		STRCHK(name, buf == __strncat(buf, str1, len1));
		*buf = '\0';
		STRCHK(name, buf == __strncat(buf, str2, len2));
		*buf = '\0';
		STRCHK(name, buf == __strncat(buf, str1, len1));
		STRCHK(name, buf == __strncat(buf, str2, len1 + len2));
		*buf = '\0';
		STRCHK(name, buf == __strncat(buf, str2, i));
		STRCHK(name, buf == __strncat(buf, str1, i));
	}
}

/*
 *  stress_strchr()
 *	stress on strchr
 */
static void stress_strchr(
	const void *libc_func,
	const char *name,
	char *str1,
	const size_t len1,
	char *str2,
	const size_t len2)
{
	register size_t i;
	char * (*__strchr)(const char *s, int c) = libc_func;

	(void)len2;

	for (i = 0; i < len1 - 1; i++) {
		STRCHK(name, NULL == __strchr(str1, '_'));
		STRCHK(name, NULL != __strchr(str1, str1[0]));

		STRCHK(name, NULL == __strchr(str2, '_'));
		STRCHK(name, NULL != __strchr(str2, str2[0]));
	}
}

/*
 *  stress_strrchr()
 *	stress on strrchr
 */
static void stress_strrchr(
	const void *libc_func,
	const char *name,
	char *str1,
	const size_t len1,
	char *str2,
	const size_t len2)
{
	register size_t i;
	char * (*__strrchr)(const char *s, int c) = libc_func;

	(void)len2;

	for (i = 0; i < len1 - 1; i++) {
		STRCHK(name, NULL == __strrchr(str1, '_'));
		STRCHK(name, NULL != __strrchr(str1, str1[0]));

		STRCHK(name, NULL == __strrchr(str2, '_'));
		STRCHK(name, NULL != __strrchr(str2, str2[0]));
	}
}

/*
 *  stress_strcmp()
 *	stress on strcmp
 */
static void stress_strcmp(
	const void *libc_func,
	const char *name,
	char *str1,
	const size_t len1,
	char *str2,
	const size_t len2)
{
	register size_t i;
	int (*__strcmp)(const char *s1, const char *s2) = libc_func;

	(void)len2;

	for (i = 1; i < len1; i++) {
		STRCHK(name, 0 == __strcmp(str1, str1));
		STRCHK(name, 0 == __strcmp(str2, str2));

		STRCHK(name, 0 != __strcmp(str2, str1));
		STRCHK(name, 0 != __strcmp(str1, str2));

		STRCHK(name, 0 != __strcmp(str1 + i, str1));
		STRCHK(name, 0 != __strcmp(str1, str1 + i));
		STRCHK(name, 0 == __strcmp(str1 + i, str1 + i));

		STRCHK(name, 0 != __strcmp(str1 + i, str2));
		STRCHK(name, 0 != __strcmp(str2, str1 + i));
	}
}

/*
 *  stress_strncmp()
 *	stress on strncmp
 */
static void stress_strncmp(
	const void *libc_func,
	const char *name,
	char *str1,
	const size_t len1,
	char *str2,
	const size_t len2)
{
	register size_t i;
	int (*__strncmp)(const char *s1, const char *s2, size_t n) = libc_func;

	for (i = 1; i < len1; i++) {
		STRCHK(name, 0 == __strncmp(str1, str1, len1));
		STRCHK(name, 0 == __strncmp(str2, str2, len2));

		STRCHK(name, 0 != __strncmp(str2, str1, len2));
		STRCHK(name, 0 != __strncmp(str1, str2, len1));

		STRCHK(name, 0 != __strncmp(str1 + i, str1, len1));
		STRCHK(name, 0 != __strncmp(str1, str1 + i, len1));
		STRCHK(name, 0 == __strncmp(str1 + i, str1 + i, len1));

		STRCHK(name, 0 != __strncmp(str1 + i, str2, len2));
		STRCHK(name, 0 != __strncmp(str2, str1 + i, len2));
	}
}
/*
 *  stress_strcoll()
 *	stress on strcoll
 */
static void stress_strcoll(
	const void *libc_func,
	const char *name,
	char *str1,
	const size_t len1,
	char *str2,
	const size_t len2)
{
	register size_t i;
	int (*__strcoll)(const char *s1, const char *s2) = libc_func;

	(void)len2;

	for (i = 1; i < len1; i++) {
		STRCHK(name, 0 == __strcoll(str1, str1));
		STRCHK(name, 0 == __strcoll(str2, str2));

		STRCHK(name, 0 != __strcoll(str2, str1));
		STRCHK(name, 0 != __strcoll(str1, str2));

		STRCHK(name, 0 != __strcoll(str1 + i, str1));
		STRCHK(name, 0 != __strcoll(str1, str1 + i));
		STRCHK(name, 0 == __strcoll(str1 + i, str1 + i));

		STRCHK(name, 0 != __strcoll(str1 + i, str2));
		STRCHK(name, 0 != __strcoll(str2, str1 + i));
	}
}

/*
 *  stress_strlen()
 *	stress on strlen
 */
static void stress_strlen(
	const void *libc_func,
	const char *name,
	char *str1,
	const size_t len1,
	char *str2,
	const size_t len2)
{
	register size_t i;
	size_t (*__strlen)(const char *s) = libc_func;


	for (i = 0; i < len1 - 1; i++) {
		STRCHK(name, len1 - 1 == __strlen(str1));
		STRCHK(name, len1 - 1 - i == __strlen(str1 + i));
	}

	for (i = 0; i < len2 - 1; i++) {
		STRCHK(name, len2 - 1 == __strlen(str2));
		STRCHK(name, len2 - 1 - i == __strlen(str2 + i));
	}
}

/*
 *  stress_strxfrm()
 *	stress on strxfrm
 */
static void stress_strxfrm(
	const void *libc_func,
	const char *name,
	char *str1,
	const size_t len1,
	char *str2,
	const size_t len2)
{
	register size_t i;
	char buf[len1 + len2];
	size_t (*__strxfrm)(char *dest, const char *src, size_t n) = libc_func;

	for (i = 0; i < len1 - 1; i++) {
		*buf = '\0';
		STRCHK(name, 0 != __strxfrm(buf, str1, sizeof(buf)));
		*buf = '\0';
		STRCHK(name, 0 != __strxfrm(buf, str2, sizeof(buf)));
		*buf = '\0';
		STRCHK(name, 0 != __strxfrm(buf, str1, sizeof(buf)));
		STRCHK(name, 0 != __strxfrm(buf, str2, sizeof(buf)));
		*buf = '\0';
		STRCHK(name, 0 != __strxfrm(buf, str2, sizeof(buf)));
		STRCHK(name, 0 != __strxfrm(buf, str1, sizeof(buf)));
	}
}


/*
 *  stress_str_all()
 *	iterate over all string stressors
 */
static void stress_str_all(
	const void *libc_func,
	const char *name,
	char *str1,
	const size_t len1,
	char *str2,
	const size_t len2)
{
	static int i = 1;	/* Skip over stress_str_all */

	(void)libc_func;

	str_methods[i].func(str_methods[i].libc_func, name, str1, len1, str2, len2);
	i++;
	if (!str_methods[i].func)
		i = 1;
}

/*
 * Table of string stress methods
 */
static const stress_str_stressor_info_t str_methods[] = {
	{ "all",		stress_str_all,		NULL },	/* Special "all test */

	{ "index",		stress_index,		index },
	{ "rindex",		stress_rindex,		rindex  },
	{ "strcasecmp",		stress_strcasecmp,	strcasecmp },
	{ "strcat",		stress_strcat,		strcat },
	{ "strchr",		stress_strchr,		strchr },
	{ "strcoll",		stress_strcoll,		strcoll },
	{ "strcmp",		stress_strcmp,		strcmp },
	{ "strcpy",		stress_strcpy,		strcpy },
	{ "strlen",		stress_strlen,		strlen },
	{ "strncasecmp",	stress_strncasecmp,	strncasecmp },
	{ "strncat",		stress_strncat,		strncat },
	{ "strncmp",		stress_strncmp,		strncmp },
	{ "strrchr",		stress_strrchr,		strrchr },
	{ "strxfrm",		stress_strxfrm,		strxfrm },
	{ NULL,			NULL,			NULL }
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
	const void *libc_func = opt_str_stressor->libc_func;

	(void)instance;

	do {
		char str1[256], str2[128];

		stress_strnrnd(str1, sizeof(str1));
		stress_strnrnd(str2, sizeof(str2));

		(void)func(libc_func, name, str1, sizeof(str1), str2, sizeof(str2));
		(*counter)++;
	} while (opt_do_run && (!max_ops || *counter < max_ops));
	return EXIT_SUCCESS;
}
