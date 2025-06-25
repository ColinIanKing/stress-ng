/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
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

#define STR1LEN 256
#define STR2LEN 128
#define STRDSTLEN (STR1LEN + STR2LEN + 1)

typedef struct stress_str_args {
	void *libc_func;
	const char *name;
	char *str1;
	size_t len1;
	char *str2;
	size_t len2;
	char *strdst;
	size_t strdstlen;
	bool failed;
} stress_str_args_t;

/*
 *  the STR stress test has different classes of string stressors
 */
typedef size_t (*stress_str_func)(stress_args_t *args, stress_str_args_t *info);

typedef struct {
	const char 		*name;	/* human readable form of stressor */
	const stress_str_func	func;	/* the stressor function */
	void 		*libc_func;
} stress_str_method_info_t;

static const stress_help_t help[] = {
	{ NULL,	"str N",	   "start N workers exercising lib C string functions" },
	{ NULL,	"str-method func", "specify the string function to stress" },
	{ NULL,	"str-ops N",	   "stop after N bogo string operations" },
	{ NULL,	NULL,		   NULL }
};

static inline void strchk(
	stress_str_args_t *info,
	const int ok,
	const char *msg)
{
	if (UNLIKELY((g_opt_flags & OPT_FLAGS_VERIFY) && (!ok))) {
		pr_fail("%s: %s did not return expected result\n",
			info->name, msg);
		info->failed = true;
	}
}

#define STR(x)	# x

#define STRCHK(info, test)	strchk(info, test, STR(test))

#if defined(HAVE_STRINGS_H)
/*
 *  stress_strcasecmp()
 *	stress on strcasecmp
 */
static size_t stress_strcasecmp(stress_args_t *args, stress_str_args_t *info)
{
	typedef int (*test_strcasecmp_t)(const char *s1, const char *s2);

	const test_strcasecmp_t test_strcasecmp = (test_strcasecmp_t)info->libc_func;
	const char *str1 = info->str1;
	const char *str2 = info->str2;
	const size_t len1 = info->len1;
	register size_t i;

	for (i = 1; LIKELY(stress_continue_flag() && (i < len1)); i++) {
		STRCHK(info, 0 == test_strcasecmp(str1, str1));
		STRCHK(info, 0 == test_strcasecmp(str2, str2));

		STRCHK(info, 0 != test_strcasecmp(str2, str1));
		STRCHK(info, 0 != test_strcasecmp(str1, str2));

		STRCHK(info, 0 != test_strcasecmp(str1 + i, str1));
		STRCHK(info, 0 != test_strcasecmp(str1, str1 + i));
		STRCHK(info, 0 == test_strcasecmp(str1 + i, str1 + i));

		STRCHK(info, 0 != test_strcasecmp(str1 + i, str2));
		STRCHK(info, 0 != test_strcasecmp(str2, str1 + i));
	}
	stress_bogo_add(args, 9);
	return i * 9;
}
#endif

#if defined(HAVE_STRINGS_H)
/*
 *  stress_strncasecmp()
 *	stress on strncasecmp
 */
static size_t stress_strncasecmp(stress_args_t *args, stress_str_args_t *info)
{
	typedef int (*test_strncasecmp_t)(const char *s1, const char *s2, size_t n);

	const test_strncasecmp_t test_strncasecmp = (test_strncasecmp_t)info->libc_func;
	const char *str1 = info->str1;
	const char *str2 = info->str2;
	const size_t len1 = info->len1;
	const size_t len2 = info->len2;
	register size_t i;

	for (i = 1; LIKELY(stress_continue_flag() && (i < len1)); i++) {
		STRCHK(info, 0 == test_strncasecmp(str1, str1, len1));
		STRCHK(info, 0 == test_strncasecmp(str2, str2, len2));

		STRCHK(info, 0 != test_strncasecmp(str2, str1, len2));
		STRCHK(info, 0 != test_strncasecmp(str1, str2, len1));

		STRCHK(info, 0 != test_strncasecmp(str1 + i, str1, len1));
		STRCHK(info, 0 != test_strncasecmp(str1, str1 + i, len1));
		STRCHK(info, 0 == test_strncasecmp(str1 + i, str1 + i, len1));

		STRCHK(info, 0 != test_strncasecmp(str1 + i, str2, len1));
		STRCHK(info, 0 != test_strncasecmp(str2, str1 + i, len2));
	}
	stress_bogo_add(args, 9);
	return i * 9;
}
#endif

#if defined(HAVE_STRINGS_H) &&	\
    defined(HAVE_INDEX)
/*
 *  stress_index()
 *	stress on index
 */
static size_t stress_index(stress_args_t *args, stress_str_args_t *info)
{
	typedef char * (*test_index_t)(const char *s, int c);

	const test_index_t test_index = (test_index_t)info->libc_func;
	const char *str1 = info->str1;
	const char *str2 = info->str2;
	const size_t len1 = info->len1;
	register size_t i;

	for (i = 0; LIKELY(stress_continue_flag() && (i < len1 - 1)); i++) {
		STRCHK(info, NULL == test_index(str1, '+'));
		STRCHK(info, NULL != test_index(str1, str1[0]));

		STRCHK(info, NULL == test_index(str2, '+'));
		STRCHK(info, NULL != test_index(str2, str2[0]));
	}
	stress_bogo_add(args, 4);
	return i * 4;
}
#endif

#if defined(HAVE_STRINGS_H) &&	\
    defined(HAVE_RINDEX)
/*
 *  stress_rindex()
 *	stress on rindex
 */
static size_t stress_rindex(stress_args_t *args, stress_str_args_t *info)
{
	typedef char * (*test_rindex_t)(const char *s, int c);

	const test_rindex_t test_rindex = (test_rindex_t)info->libc_func;
	const char *str1 = info->str1;
	const char *str2 = info->str2;
	const size_t len1 = info->len1;
	register size_t i;

	for (i = 0; LIKELY(stress_continue_flag() && (i < len1 - 1)); i++) {
		STRCHK(info, NULL == test_rindex(str1, '+'));
		STRCHK(info, NULL != test_rindex(str1, str1[0]));

		STRCHK(info, NULL == test_rindex(str2, '+'));
		STRCHK(info, NULL != test_rindex(str2, str2[0]));
	}
	stress_bogo_add(args, 4);
	return i * 4;
}
#endif

#if defined(HAVE_BSD_STRLCPY) &&	\
    !defined(BUILD_STATIC)
/*
 *  stress_strlcpy()
 *	stress on strlcpy
 */
static size_t stress_strlcpy(stress_args_t *args, stress_str_args_t *info)
{
	typedef size_t (*test_strlcpy_t)(char *dest, const char *src, size_t len);

	const test_strlcpy_t test_strlcpy = (test_strlcpy_t)info->libc_func;
	const char *str1 = info->str1;
	const char *str2 = info->str2;
	char *strdst = info->strdst;
	const size_t str_len1 = strlen(str1);
	const size_t str_len2 = strlen(str2);
	const size_t len1 = info->len1;
	const size_t strdstlen = info->strdstlen;
	register size_t i;

	for (i = 0; LIKELY(stress_continue_flag() && (i < len1 - 1)); i++) {
		STRCHK(info, str_len1 == test_strlcpy(strdst, str1, strdstlen));
		STRCHK(info, str_len2 == test_strlcpy(strdst, str2, strdstlen));
	}
	stress_bogo_add(args, 2);
	return i * 2;
}
#else
/*
 *  stress_strcpy()
 *	stress on strcpy
 */
static size_t stress_strcpy(stress_args_t *args, stress_str_args_t *info)
{
	typedef char * (*test_strcpy_t)(char *dest, const char *src);

	char * (*test_strcpy)(char *dest, const char *src) = (test_strcpy_t)info->libc_func;
	const char *str1 = info->str1;
	const char *str2 = info->str2;
	char *strdst = info->strdst;
	const size_t len1 = info->len1;
	register size_t i;

	for (i = 0; LIKELY(stress_continue_flag() && (i < len1 - 1)); i++) {
		STRCHK(info, strdst == test_strcpy(strdst, str1));
		STRCHK(info, strdst == test_strcpy(strdst, str2));
	}
	stress_bogo_add(args, 2);
	return i * 2;
}
#endif


#if defined(HAVE_BSD_STRLCAT) &&	\
    !defined(BUILD_STATIC)
/*
 *  stress_strlcat()
 *	stress on strlcat
 */
static size_t stress_strlcat(stress_args_t *args, stress_str_args_t *info)
{
	typedef size_t (*test_strlcat_t)(char *dest, const char *src, size_t len);

	const test_strlcat_t test_strlcat = (test_strlcat_t)info->libc_func;
	const char *str1 = info->str1;
	const char *str2 = info->str2;
	char *strdst = info->strdst;
	const size_t str_len1 = strlen(str1);
	const size_t str_len2 = strlen(str2);
	const size_t str_len = str_len1 + str_len2;
	const size_t len1 = info->len1;
	const size_t strdstlen = info->strdstlen;
	register size_t i;

	for (i = 0; LIKELY(stress_continue_flag() && (i < len1 - 1)); i++) {
		*strdst = '\0';
		STRCHK(info, str_len1 == test_strlcat(strdst, str1, strdstlen));
		*strdst = '\0';
		STRCHK(info, str_len2 == test_strlcat(strdst, str2, strdstlen));
		*strdst = '\0';
		STRCHK(info, str_len1 == test_strlcat(strdst, str1, strdstlen));
		STRCHK(info, str_len  == test_strlcat(strdst, str2, strdstlen));
		*strdst = '\0';
		STRCHK(info, str_len2 == test_strlcat(strdst, str2, strdstlen));
		STRCHK(info, str_len  == test_strlcat(strdst, str1, strdstlen));
	}
	stress_bogo_add(args, 6);
	return i * 6;
}
#else
/*
 *  stress_strcat()
 *	stress on strcat
 */
static size_t stress_strcat(stress_args_t *args, stress_str_args_t *info)
{
	typedef char * (*test_strcat_t)(char *dest, const char *src);

	const test_strcat_t test_strcat = (test_strcat_t)info->libc_func;
	const char *str1 = info->str1;
	const char *str2 = info->str2;
	char *strdst = info->strdst;
	const size_t len1 = info->len1;
	register size_t i;

	for (i = 0; LIKELY(stress_continue_flag() && (i < len1 - 1)); i++) {
		*strdst = '\0';
		STRCHK(info, strdst == test_strcat(strdst, str1));
		*strdst = '\0';
		STRCHK(info, strdst == test_strcat(strdst, str2));
		*strdst = '\0';
		STRCHK(info, strdst == test_strcat(strdst, str1));
		STRCHK(info, strdst == test_strcat(strdst, str2));
		*strdst = '\0';
		STRCHK(info, strdst == test_strcat(strdst, str2));
		STRCHK(info, strdst == test_strcat(strdst, str1));
	}
	stress_bogo_add(args, 6);
	return i * 6;
}
#endif

/*
 *  stress_strncat()
 *	stress on strncat
 */
static size_t stress_strncat(stress_args_t *args, stress_str_args_t *info)
{
	typedef char * (*test_strncat_t)(char *dest, const char *src, size_t n);

	const test_strncat_t test_strncat = (test_strncat_t)info->libc_func;
	const char *str1 = info->str1;
	const char *str2 = info->str2;
	char *strdst = info->strdst;
	const size_t len1 = info->len1;
	const size_t len2 = info->len2;
	register size_t i;

	for (i = 0; LIKELY(stress_continue_flag() && (i < len1 - 1)); i++) {
		*strdst = '\0';
		STRCHK(info, strdst == test_strncat(strdst, str1, len1));
		*strdst = '\0';
		STRCHK(info, strdst == test_strncat(strdst, str2, len2));
		*strdst = '\0';
		STRCHK(info, strdst == test_strncat(strdst, str1, len1));
		STRCHK(info, strdst == test_strncat(strdst, str2, len1 + len2));
		*strdst = '\0';
		STRCHK(info, strdst == test_strncat(strdst, str2, i));
		STRCHK(info, strdst == test_strncat(strdst, str1, i));
	}
	stress_bogo_add(args, 6);
	return i * 6;
}

/*
 *  stress_strchr()
 *	stress on strchr
 */
static size_t stress_strchr(stress_args_t *args, stress_str_args_t *info)
{
	typedef char * (*test_strchr_t)(const char *s, int c);

	const test_strchr_t test_strchr = (test_strchr_t)info->libc_func;
	const char *str1 = info->str1;
	const char *str2 = info->str2;
	const size_t len1 = info->len1;
	register size_t i;

	for (i = 0; LIKELY(stress_continue_flag() && (i < len1 - 1)); i++) {
		STRCHK(info, NULL == test_strchr(str1, '+'));
		STRCHK(info, NULL != test_strchr(str1, str1[0]));

		STRCHK(info, NULL == test_strchr(str2, '+'));
		STRCHK(info, NULL != test_strchr(str2, str2[0]));
	}
	stress_bogo_add(args, 4);
	return i * 4;
}

/*
 *  stress_strrchr()
 *	stress on strrchr
 */
static size_t stress_strrchr(stress_args_t *args, stress_str_args_t *info)
{
	typedef char * (*test_strrchr_t)(const char *s, int c);

	const test_strrchr_t test_strrchr = (test_strrchr_t)info->libc_func;
	const char *str1 = info->str1;
	const char *str2 = info->str2;
	const size_t len1 = info->len1;
	register size_t i;

	for (i = 0; LIKELY(stress_continue_flag() && (i < len1 - 1)); i++) {
		STRCHK(info, NULL == test_strrchr(str1, '+'));
		STRCHK(info, NULL != test_strrchr(str1, str1[0]));

		STRCHK(info, NULL == test_strrchr(str2, '+'));
		STRCHK(info, NULL != test_strrchr(str2, str2[0]));
	}
	stress_bogo_add(args, 4);
	return i * 4;
}

/*
 *  stress_strcmp()
 *	stress on strcmp
 */
static size_t stress_strcmp(stress_args_t *args, stress_str_args_t *info)
{
	typedef int (*test_strcmp_t)(const char *s1, const char *s2);

	const test_strcmp_t test_strcmp = (test_strcmp_t)info->libc_func;
	const char *str1 = info->str1;
	const char *str2 = info->str2;
	const size_t len1 = info->len1;
	register size_t i;

	for (i = 1; LIKELY(stress_continue_flag() && (i < len1)); i++) {
		STRCHK(info, 0 == test_strcmp(str1, str1));
		STRCHK(info, 0 == test_strcmp(str2, str2));

		STRCHK(info, 0 != test_strcmp(str2, str1));
		STRCHK(info, 0 != test_strcmp(str1, str2));

		STRCHK(info, 0 != test_strcmp(str1 + i, str1));
		STRCHK(info, 0 != test_strcmp(str1, str1 + i));
		STRCHK(info, 0 == test_strcmp(str1 + i, str1 + i));

		STRCHK(info, 0 != test_strcmp(str1 + i, str2));
		STRCHK(info, 0 != test_strcmp(str2, str1 + i));
	}
	stress_bogo_add(args, 9);
	return i * 9;
}

/*
 *  stress_strncmp()
 *	stress on strncmp
 */
static size_t stress_strncmp(stress_args_t *args, stress_str_args_t *info)
{
	typedef int (*test_strncmp_t)(const char *s1, const char *s2, size_t n);

	const test_strncmp_t test_strncmp = (test_strncmp_t)info->libc_func;
	const char *str1 = info->str1;
	const char *str2 = info->str2;
	const size_t len1 = info->len1;
	const size_t len2 = info->len2;
	register size_t i;

	for (i = 1; LIKELY(stress_continue_flag() && (i < len1)); i++) {
		STRCHK(info, 0 == test_strncmp(str1, str1, len1));
		STRCHK(info, 0 == test_strncmp(str2, str2, len2));

		STRCHK(info, 0 != test_strncmp(str2, str1, len2));
		STRCHK(info, 0 != test_strncmp(str1, str2, len1));

		STRCHK(info, 0 != test_strncmp(str1 + i, str1, len1));
		STRCHK(info, 0 != test_strncmp(str1, str1 + i, len1));
		STRCHK(info, 0 == test_strncmp(str1 + i, str1 + i, len1));

		STRCHK(info, 0 != test_strncmp(str1 + i, str2, len2));
		STRCHK(info, 0 != test_strncmp(str2, str1 + i, len2));
	}
	stress_bogo_add(args, 9);
	return i * 9;
}
/*
 *  stress_strcoll()
 *	stress on strcoll
 */
static size_t stress_strcoll(stress_args_t *args, stress_str_args_t *info)
{
	typedef int (*test_strcoll_t)(const char *s1, const char *s2);

	const test_strcoll_t test_strcoll = (test_strcoll_t)info->libc_func;
	const char *str1 = info->str1;
	const char *str2 = info->str2;
	const size_t len1 = info->len1;
	register size_t i;

	for (i = 1; LIKELY(stress_continue_flag() && (i < len1)); i++) {
		STRCHK(info, 0 == test_strcoll(str1, str1));
		STRCHK(info, 0 == test_strcoll(str2, str2));

		STRCHK(info, 0 != test_strcoll(str2, str1));
		STRCHK(info, 0 != test_strcoll(str1, str2));

		STRCHK(info, 0 != test_strcoll(str1 + i, str1));
		STRCHK(info, 0 != test_strcoll(str1, str1 + i));
		STRCHK(info, 0 == test_strcoll(str1 + i, str1 + i));

		STRCHK(info, 0 != test_strcoll(str1 + i, str2));
		STRCHK(info, 0 != test_strcoll(str2, str1 + i));
	}
	stress_bogo_add(args, 9);
	return i * 9;
}

/*
 *  stress_strlen()
 *	stress on strlen
 */
static size_t stress_strlen(stress_args_t *args, stress_str_args_t *info)
{
	typedef size_t (*test_strlen_t)(const char *s);

	const test_strlen_t test_strlen = (test_strlen_t)info->libc_func;
	const char *str1 = info->str1;
	const char *str2 = info->str2;
	const size_t len1 = info->len1;
	const size_t len2 = info->len2;
	register size_t i;
	size_t n;

	for (i = 0; LIKELY(stress_continue_flag() && (i < len1 - 1)); i++) {
		STRCHK(info, len1 - 1 == test_strlen(str1));
		STRCHK(info, len1 - 1 - i == test_strlen(str1 + i));
	}
	n = i * 2;

	for (i = 0; LIKELY(stress_continue_flag() && (i < len2 - 1)); i++) {
		STRCHK(info, len2 - 1 == test_strlen(str2));
		STRCHK(info, len2 - 1 - i == test_strlen(str2 + i));
	}
	n += i * 2;
	stress_bogo_add(args, 4);
	return n;
}

/*
 *  stress_strxfrm()
 *	stress on strxfrm
 */
static size_t stress_strxfrm(stress_args_t *args, stress_str_args_t *info)
{
	typedef size_t (*test_strxfrm_t)(char *dest, const char *src, size_t n);

	const test_strxfrm_t test_strxfrm = (test_strxfrm_t)info->libc_func;
	const char *str1 = info->str1;
	const char *str2 = info->str2;
	char *strdst = info->strdst;
	const size_t len1 = info->len1;
	const size_t strdstlen = info->strdstlen;
	register size_t i;

	for (i = 0; LIKELY(stress_continue_flag() && (i < len1 - 1)); i++) {
		*strdst = '\0';
		STRCHK(info, 0 != test_strxfrm(strdst, str1, strdstlen));
		*strdst = '\0';
		STRCHK(info, 0 != test_strxfrm(strdst, str2, strdstlen));
		*strdst = '\0';
		STRCHK(info, 0 != test_strxfrm(strdst, str1, strdstlen));
		STRCHK(info, 0 != test_strxfrm(strdst, str2, strdstlen));
		*strdst = '\0';
		STRCHK(info, 0 != test_strxfrm(strdst, str2, strdstlen));
		STRCHK(info, 0 != test_strxfrm(strdst, str1, strdstlen));
	}
	stress_bogo_add(args, 6);
	return i * 6;
}

static size_t stress_str_all(stress_args_t *args, stress_str_args_t *info);

/*
 * Table of string stress methods
 */
static const stress_str_method_info_t str_methods[] = {
	{ "all",		stress_str_all,		NULL },	/* Special "all test */

#if defined(HAVE_STRINGS_H) &&	\
    defined(HAVE_INDEX)
	{ "index",		stress_index,		(void *)index },
#endif
#if defined(HAVE_STRINGS_H) &&	\
    defined(HAVE_RINDEX)
	{ "rindex",		stress_rindex,		(void *)rindex  },
#endif
#if defined(HAVE_STRINGS_H)
	{ "strcasecmp",		stress_strcasecmp,	(void *)strcasecmp },
#endif
#if defined(HAVE_BSD_STRLCAT) &&	\
    !defined(BUILD_STATIC)
	{ "strlcat",		stress_strlcat,		(void *)strlcat },
#else
	{ "strcat",		stress_strcat,		(void *)strcat },
#endif
	{ "strchr",		stress_strchr,		(void *)strchr },
	{ "strcoll",		stress_strcoll,		(void *)strcoll },
	{ "strcmp",		stress_strcmp,		(void *)strcmp },
#if defined(HAVE_BSD_STRLCPY) &&	\
    !defined(BUILD_STATIC)
	{ "strlcpy",		stress_strlcpy,		(void *)strlcpy },
#else
	{ "strcpy",		stress_strcpy,		(void *)strcpy },
#endif
	{ "strlen",		stress_strlen,		(void *)strlen },
#if defined(HAVE_STRINGS_H)
	{ "strncasecmp",	stress_strncasecmp,	(void *)strncasecmp },
#endif
	{ "strncat",		stress_strncat,		(void *)strncat },
	{ "strncmp",		stress_strncmp,		(void *)strncmp },
	{ "strrchr",		stress_strrchr,		(void *)strrchr },
	{ "strxfrm",		stress_strxfrm,		(void *)strxfrm },
};

static stress_metrics_t metrics[SIZEOF_ARRAY(str_methods)];

/*
 *  stress_str_all()
 *	iterate over all string stressors
 */
static size_t stress_str_all(stress_args_t *args, stress_str_args_t *info)
{
	static size_t i = 1;	/* Skip over stress_str_all */
	stress_str_args_t info_all = *info;
	double t;

	info_all.libc_func = str_methods[i].libc_func;

	t = stress_time_now();
	metrics[i].count += (double)str_methods[i].func(args, &info_all);
	metrics[i].duration += (stress_time_now() - t);
	i++;
	if (UNLIKELY(i >= SIZEOF_ARRAY(str_methods)))
		i = 1;
	info->failed = info_all.failed;
	return 0;
}

/*
 *  stress_str()
 *	stress CPU by doing various string operations
 */
static int stress_str(stress_args_t *args)
{
	char ALIGN64 str1[STR1LEN], ALIGN64 str2[STR2LEN];
	char ALIGN64 strdst[STRDSTLEN];
	stress_str_args_t info;
	const stress_str_method_info_t *str_method_info;
	size_t i, j, str_method = 0;

	(void)stress_get_setting("str-method", &str_method);
	str_method_info = &str_methods[str_method];

	info.libc_func = str_method_info->libc_func;
	info.str1 = str1;
	info.len1 = sizeof(str1);
	info.str2 = str2;
	info.len2 = sizeof(str2);
	info.name = args->name;
	info.strdst = strdst;
	info.strdstlen = sizeof(strdst);
	info.failed = false;

	stress_rndstr(info.str1, info.len1);

	stress_zero_metrics(metrics, SIZEOF_ARRAY(metrics));

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		register char *tmpptr;
		register size_t tmplen;
		double t;

		stress_rndstr(info.str2, info.len2);

		t = stress_time_now();
		metrics[str_method].count += (double)str_method_info->func(args, &info);
		metrics[str_method].duration += (stress_time_now() - t);

		tmpptr = info.str1;
		info.str1 = info.str2;
		info.str2 = tmpptr;

		tmplen = info.len1;
		info.len1 = info.len2;
		info.len2 = tmplen;
	} while (stress_continue(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	/* dump metrics of methods except for first "all" method */
	for (i = 1, j = 0; i < SIZEOF_ARRAY(metrics); i++) {
		if (metrics[i].duration > 0.0) {
			char msg[64];
			const double rate = metrics[i].count / metrics[i].duration;

			(void)snprintf(msg, sizeof(msg), "%s calls per sec", str_methods[i].name);
			stress_metrics_set(args, j, msg,
				rate, STRESS_METRIC_HARMONIC_MEAN);
			j++;
		}
	}

	return info.failed ? EXIT_FAILURE : EXIT_SUCCESS;
}

static const char *stress_str_method(const size_t i)
{
	return (i < SIZEOF_ARRAY(str_methods)) ? str_methods[i].name : NULL;
}

static const stress_opt_t opts[] = {
	{ OPT_str_method, "str-method", TYPE_ID_SIZE_T_METHOD, 0, 0, stress_str_method },
	END_OPT,
};

const stressor_info_t stress_str_info = {
	.stressor = stress_str,
	.classifier = CLASS_CPU | CLASS_CPU_CACHE | CLASS_MEMORY,
	.verify = VERIFY_OPTIONAL,
	.opts = opts,
	.help = help
};
