/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C)      2022 Colin Ian King.
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

/*
 *  the STR stress test has different classes of string stressors
 */
typedef void (*stress_str_func)(
	void *libc_func,
	const char *name,
	char *str1,
	const size_t len1,
	char *str2,
	const size_t len2,
	char *strdst,
	const size_t strdstlen,
	bool *failed);

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

static const stress_str_method_info_t str_methods[];

static inline void strchk(
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

#define STR(x)	# x

#define STRCHK(name, test, failed)	\
	strchk(name, test, STR(test), failed)

#if defined(HAVE_STRINGS_H)
/*
 *  stress_strcasecmp()
 *	stress on strcasecmp
 */
static void stress_strcasecmp(
	void *libc_func,
	const char *name,
	char *str1,
	const size_t len1,
	char *str2,
	const size_t len2,
	char *strdst,
	const size_t strdstlen,
	bool *failed)
{
	typedef int (*test_strcasecmp_t)(const char *s1, const char *s2);

	register size_t i;
	test_strcasecmp_t test_strcasecmp = (test_strcasecmp_t)libc_func;

	(void)len2;
	(void)strdst;
	(void)strdstlen;

	for (i = 1; keep_stressing_flag() && (i < len1); i++) {
		STRCHK(name, 0 == test_strcasecmp(str1, str1), failed);
		STRCHK(name, 0 == test_strcasecmp(str2, str2), failed);

		STRCHK(name, 0 != test_strcasecmp(str2, str1), failed);
		STRCHK(name, 0 != test_strcasecmp(str1, str2), failed);

		STRCHK(name, 0 != test_strcasecmp(str1 + i, str1), failed);
		STRCHK(name, 0 != test_strcasecmp(str1, str1 + i), failed);
		STRCHK(name, 0 == test_strcasecmp(str1 + i, str1 + i), failed);

		STRCHK(name, 0 != test_strcasecmp(str1 + i, str2), failed);
		STRCHK(name, 0 != test_strcasecmp(str2, str1 + i), failed);
	}
}
#endif

#if defined(HAVE_STRINGS_H)
/*
 *  stress_strncasecmp()
 *	stress on strncasecmp
 */
static void stress_strncasecmp(
	void *libc_func,
	const char *name,
	char *str1,
	const size_t len1,
	char *str2,
	const size_t len2,
	char *strdst,
	const size_t strdstlen,
	bool *failed)
{
	typedef int (*test_strncasecmp_t)(const char *s1, const char *s2, size_t n);

	register size_t i;
	test_strncasecmp_t test_strncasecmp = (test_strncasecmp_t)libc_func;

	(void)len2;
	(void)strdst;
	(void)strdstlen;

	for (i = 1; keep_stressing_flag() && (i < len1); i++) {
		STRCHK(name, 0 == test_strncasecmp(str1, str1, len1), failed);
		STRCHK(name, 0 == test_strncasecmp(str2, str2, len2), failed);

		STRCHK(name, 0 != test_strncasecmp(str2, str1, len2), failed);
		STRCHK(name, 0 != test_strncasecmp(str1, str2, len1), failed);

		STRCHK(name, 0 != test_strncasecmp(str1 + i, str1, len1), failed);
		STRCHK(name, 0 != test_strncasecmp(str1, str1 + i, len1), failed);
		STRCHK(name, 0 == test_strncasecmp(str1 + i, str1 + i, len1), failed);

		STRCHK(name, 0 != test_strncasecmp(str1 + i, str2, len1), failed);
		STRCHK(name, 0 != test_strncasecmp(str2, str1 + i, len2), failed);
	}
}
#endif

#if defined(HAVE_STRINGS_H)
/*
 *  stress_index()
 *	stress on index
 */
static void stress_index(
	void *libc_func,
	const char *name,
	char *str1,
	const size_t len1,
	char *str2,
	const size_t len2,
	char *strdst,
	const size_t strdstlen,
	bool *failed)
{
	typedef char * (*test_index_t)(const char *s, int c);

	register size_t i;
	test_index_t test_index = (test_index_t)libc_func;

	(void)len2;
	(void)strdst;
	(void)strdstlen;

	for (i = 0; keep_stressing_flag() && (i < len1 - 1); i++) {
		STRCHK(name, NULL == test_index(str1, '_'), failed);
		STRCHK(name, NULL != test_index(str1, str1[0]), failed);

		STRCHK(name, NULL == test_index(str2, '_'), failed);
		STRCHK(name, NULL != test_index(str2, str2[0]), failed);
	}
}
#endif

#if defined(HAVE_STRINGS_H)
/*
 *  stress_rindex()
 *	stress on rindex
 */
static void stress_rindex(
	void *libc_func,
	const char *name,
	char *str1,
	const size_t len1,
	char *str2,
	const size_t len2,
	char *strdst,
	const size_t strdstlen,
	bool *failed)
{
	typedef char * (*test_rindex_t)(const char *s, int c);

	register size_t i;
	test_rindex_t test_rindex = (test_rindex_t)libc_func;

	(void)len2;
	(void)strdst;
	(void)strdstlen;

	for (i = 0; keep_stressing_flag() && (i < len1 - 1); i++) {
		STRCHK(name, NULL == test_rindex(str1, '_'), failed);
		STRCHK(name, NULL != test_rindex(str1, str1[0]), failed);

		STRCHK(name, NULL == test_rindex(str2, '_'), failed);
		STRCHK(name, NULL != test_rindex(str2, str2[0]), failed);
	}
}
#endif

#if defined(HAVE_BSD_STRLCPY) &&	\
    !defined(BUILD_STATIC)
/*
 *  stress_strlcpy()
 *	stress on strlcpy
 */
static void stress_strlcpy(
	void *libc_func,
	const char *name,
	char *str1,
	const size_t len1,
	char *str2,
	const size_t len2,
	char *strdst,
	const size_t strdstlen,
	bool *failed)
{
	typedef size_t (*test_strlcpy_t)(char *dest, const char *src, size_t len);

	register size_t i;
	test_strlcpy_t test_strlcpy = (test_strlcpy_t)libc_func;

	const size_t str_len1 = strlen(str1);
	const size_t str_len2 = strlen(str2);

	(void)len2;

	for (i = 0; keep_stressing_flag() && (i < len1 - 1); i++) {
		STRCHK(name, str_len1 == test_strlcpy(strdst, str1, strdstlen), failed);
		STRCHK(name, str_len2 == test_strlcpy(strdst, str2, strdstlen), failed);
	}
}
#else
/*
 *  stress_strcpy()
 *	stress on strcpy
 */
static void stress_strcpy(
	void *libc_func,
	const char *name,
	char *str1,
	const size_t len1,
	char *str2,
	const size_t len2,
	char *strdst,
	const size_t strdstlen,
	bool *failed)
{
	register size_t i;
	char * (*test_strcpy)(char *dest, const char *src) = libc_func;

	(void)len2;
	(void)strdstlen;

	for (i = 0; keep_stressing_flag() && (i < len1 - 1); i++) {
		STRCHK(name, strdst == test_strcpy(strdst, str1), failed);
		STRCHK(name, strdst == test_strcpy(strdst, str2), failed);
	}
}
#endif


#if defined(HAVE_BSD_STRLCAT) &&	\
    !defined(BUILD_STATIC)
/*
 *  stress_strlcat()
 *	stress on strlcat
 */
static void stress_strlcat(
	void *libc_func,
	const char *name,
	char *str1,
	const size_t len1,
	char *str2,
	const size_t len2,
	char *strdst,
	const size_t strdstlen,
	bool *failed)
{
	typedef size_t (*test_strlcat_t)(char *dest, const char *src, size_t len);

	test_strlcat_t test_strlcat = (test_strlcat_t)libc_func;
	register size_t i;

	const size_t str_len1 = strlen(str1);
	const size_t str_len2 = strlen(str2);
	const size_t str_len = str_len1 + str_len2;

	(void)len2;

	for (i = 0; keep_stressing_flag() && (i < len1 - 1); i++) {
		*strdst = '\0';
		STRCHK(name, str_len1 == test_strlcat(strdst, str1, strdstlen), failed);
		*strdst = '\0';
		STRCHK(name, str_len2 == test_strlcat(strdst, str2, strdstlen), failed);
		*strdst = '\0';
		STRCHK(name, str_len1 == test_strlcat(strdst, str1, strdstlen), failed);
		STRCHK(name, str_len  == test_strlcat(strdst, str2, strdstlen), failed);
		*strdst = '\0';
		STRCHK(name, str_len2 == test_strlcat(strdst, str2, strdstlen), failed);
		STRCHK(name, str_len  == test_strlcat(strdst, str1, strdstlen), failed);
	}
}
#else
/*
 *  stress_strcat()
 *	stress on strcat
 */
static void stress_strcat(
	void *libc_func,
	const char *name,
	char *str1,
	const size_t len1,
	char *str2,
	const size_t len2,
	char *strdst,
	const size_t strdstlen,
	bool *failed)
{
	typedef char * (*test_strcat_t)(char *dest, const char *src);

	register size_t i;
	test_strcat_t test_strcat = (test_strcat_t)libc_func;

	(void)len2;
	(void)strdstlen;

	for (i = 0; keep_stressing_flag() && (i < len1 - 1); i++) {
		*strdst = '\0';
		STRCHK(name, strdst == test_strcat(strdst, str1), failed);
		*strdst = '\0';
		STRCHK(name, strdst == test_strcat(strdst, str2), failed);
		*strdst = '\0';
		STRCHK(name, strdst == test_strcat(strdst, str1), failed);
		STRCHK(name, strdst == test_strcat(strdst, str2), failed);
		*strdst = '\0';
		STRCHK(name, strdst == test_strcat(strdst, str2), failed);
		STRCHK(name, strdst == test_strcat(strdst, str1), failed);
	}
}
#endif

/*
 *  stress_strncat()
 *	stress on strncat
 */
static void stress_strncat(
	void *libc_func,
	const char *name,
	char *str1,
	const size_t len1,
	char *str2,
	const size_t len2,
	char *strdst,
	const size_t strdstlen,
	bool *failed)
{
	typedef char * (*test_strncat_t)(char *dest, const char *src, size_t n);

	register size_t i;
	test_strncat_t test_strncat = (test_strncat_t)libc_func;

	(void)strdstlen;

	for (i = 0; keep_stressing_flag() && (i < len1 - 1); i++) {
		*strdst = '\0';
		STRCHK(name, strdst == test_strncat(strdst, str1, len1), failed);
		*strdst = '\0';
		STRCHK(name, strdst == test_strncat(strdst, str2, len2), failed);
		*strdst = '\0';
		STRCHK(name, strdst == test_strncat(strdst, str1, len1), failed);
		STRCHK(name, strdst == test_strncat(strdst, str2, len1 + len2), failed);
		*strdst = '\0';
		STRCHK(name, strdst == test_strncat(strdst, str2, i), failed);
		STRCHK(name, strdst == test_strncat(strdst, str1, i), failed);
	}
}

/*
 *  stress_strchr()
 *	stress on strchr
 */
static void stress_strchr(
	void *libc_func,
	const char *name,
	char *str1,
	const size_t len1,
	char *str2,
	const size_t len2,
	char *strdst,
	const size_t strdstlen,
	bool *failed)
{
	typedef char * (*test_strchr_t)(const char *s, int c);

	register size_t i;
	test_strchr_t test_strchr = (test_strchr_t)libc_func;

	(void)len2;
	(void)strdst;
	(void)strdstlen;

	for (i = 0; keep_stressing_flag() && (i < len1 - 1); i++) {
		STRCHK(name, NULL == test_strchr(str1, '_'), failed);
		STRCHK(name, NULL != test_strchr(str1, str1[0]), failed);

		STRCHK(name, NULL == test_strchr(str2, '_'), failed);
		STRCHK(name, NULL != test_strchr(str2, str2[0]), failed);
	}
}

/*
 *  stress_strrchr()
 *	stress on strrchr
 */
static void stress_strrchr(
	void *libc_func,
	const char *name,
	char *str1,
	const size_t len1,
	char *str2,
	const size_t len2,
	char *strdst,
	const size_t strdstlen,
	bool *failed)
{
	typedef char * (*test_strrchr_t)(const char *s, int c);

	register size_t i;
	test_strrchr_t test_strrchr = (test_strrchr_t)libc_func;

	(void)len2;
	(void)strdst;
	(void)strdstlen;

	for (i = 0; keep_stressing_flag() && (i < len1 - 1); i++) {
		STRCHK(name, NULL == test_strrchr(str1, '_'), failed);
		STRCHK(name, NULL != test_strrchr(str1, str1[0]), failed);

		STRCHK(name, NULL == test_strrchr(str2, '_'), failed);
		STRCHK(name, NULL != test_strrchr(str2, str2[0]), failed);
	}
}

/*
 *  stress_strcmp()
 *	stress on strcmp
 */
static void stress_strcmp(
	void *libc_func,
	const char *name,
	char *str1,
	const size_t len1,
	char *str2,
	const size_t len2,
	char *strdst,
	const size_t strdstlen,
	bool *failed)
{
	typedef int (*test_strcmp_t)(const char *s1, const char *s2);

	register size_t i;
	test_strcmp_t test_strcmp = (test_strcmp_t)libc_func;

	(void)len2;
	(void)strdst;
	(void)strdstlen;

	for (i = 1; keep_stressing_flag() && (i < len1); i++) {
		STRCHK(name, 0 == test_strcmp(str1, str1), failed);
		STRCHK(name, 0 == test_strcmp(str2, str2), failed);

		STRCHK(name, 0 != test_strcmp(str2, str1), failed);
		STRCHK(name, 0 != test_strcmp(str1, str2), failed);

		STRCHK(name, 0 != test_strcmp(str1 + i, str1), failed);
		STRCHK(name, 0 != test_strcmp(str1, str1 + i), failed);
		STRCHK(name, 0 == test_strcmp(str1 + i, str1 + i), failed);

		STRCHK(name, 0 != test_strcmp(str1 + i, str2), failed);
		STRCHK(name, 0 != test_strcmp(str2, str1 + i), failed);
	}
}

/*
 *  stress_strncmp()
 *	stress on strncmp
 */
static void stress_strncmp(
	void *libc_func,
	const char *name,
	char *str1,
	const size_t len1,
	char *str2,
	const size_t len2,
	char *strdst,
	const size_t strdstlen,
	bool *failed)
{
	typedef int (*test_strncmp_t)(const char *s1, const char *s2, size_t n);

	register size_t i;
	test_strncmp_t test_strncmp = (test_strncmp_t)libc_func;

	(void)strdst;
	(void)strdstlen;

	for (i = 1; keep_stressing_flag() && (i < len1); i++) {
		STRCHK(name, 0 == test_strncmp(str1, str1, len1), failed);
		STRCHK(name, 0 == test_strncmp(str2, str2, len2), failed);

		STRCHK(name, 0 != test_strncmp(str2, str1, len2), failed);
		STRCHK(name, 0 != test_strncmp(str1, str2, len1), failed);

		STRCHK(name, 0 != test_strncmp(str1 + i, str1, len1), failed);
		STRCHK(name, 0 != test_strncmp(str1, str1 + i, len1), failed);
		STRCHK(name, 0 == test_strncmp(str1 + i, str1 + i, len1), failed);

		STRCHK(name, 0 != test_strncmp(str1 + i, str2, len2), failed);
		STRCHK(name, 0 != test_strncmp(str2, str1 + i, len2), failed);
	}
}
/*
 *  stress_strcoll()
 *	stress on strcoll
 */
static void stress_strcoll(
	void *libc_func,
	const char *name,
	char *str1,
	const size_t len1,
	char *str2,
	const size_t len2,
	char *strdst,
	const size_t strdstlen,
	bool *failed)
{
	typedef int (*test_strcoll_t)(const char *s1, const char *s2);

	register size_t i;
	test_strcoll_t test_strcoll = (test_strcoll_t)libc_func;

	(void)len2;
	(void)strdst;
	(void)strdstlen;

	for (i = 1; keep_stressing_flag() && (i < len1); i++) {
		STRCHK(name, 0 == test_strcoll(str1, str1), failed);
		STRCHK(name, 0 == test_strcoll(str2, str2), failed);

		STRCHK(name, 0 != test_strcoll(str2, str1), failed);
		STRCHK(name, 0 != test_strcoll(str1, str2), failed);

		STRCHK(name, 0 != test_strcoll(str1 + i, str1), failed);
		STRCHK(name, 0 != test_strcoll(str1, str1 + i), failed);
		STRCHK(name, 0 == test_strcoll(str1 + i, str1 + i), failed);

		STRCHK(name, 0 != test_strcoll(str1 + i, str2), failed);
		STRCHK(name, 0 != test_strcoll(str2, str1 + i), failed);
	}
}

/*
 *  stress_strlen()
 *	stress on strlen
 */
static void stress_strlen(
	void *libc_func,
	const char *name,
	char *str1,
	const size_t len1,
	char *str2,
	const size_t len2,
	char *strdst,
	const size_t strdstlen,
	bool *failed)
{
	typedef size_t (*test_strlen_t)(const char *s);

	register size_t i;
	test_strlen_t test_strlen = (test_strlen_t)libc_func;

	(void)strdst;
	(void)strdstlen;

	for (i = 0; keep_stressing_flag() && (i < len1 - 1); i++) {
		STRCHK(name, len1 - 1 == test_strlen(str1), failed);
		STRCHK(name, len1 - 1 - i == test_strlen(str1 + i), failed);
	}

	for (i = 0; keep_stressing_flag() && (i < len2 - 1); i++) {
		STRCHK(name, len2 - 1 == test_strlen(str2), failed);
		STRCHK(name, len2 - 1 - i == test_strlen(str2 + i), failed);
	}
}

/*
 *  stress_strxfrm()
 *	stress on strxfrm
 */
static void stress_strxfrm(
	void *libc_func,
	const char *name,
	char *str1,
	const size_t len1,
	char *str2,
	const size_t len2,
	char *strdst,
	const size_t strdstlen,
	bool *failed)
{
	typedef size_t (*test_strxfrm_t)(char *dest, const char *src, size_t n);

	register size_t i;
	test_strxfrm_t test_strxfrm = (test_strxfrm_t)libc_func;

	(void)len2;

	for (i = 0; keep_stressing_flag() && (i < len1 - 1); i++) {
		*strdst = '\0';
		STRCHK(name, 0 != test_strxfrm(strdst, str1, strdstlen), failed);
		*strdst = '\0';
		STRCHK(name, 0 != test_strxfrm(strdst, str2, strdstlen), failed);
		*strdst = '\0';
		STRCHK(name, 0 != test_strxfrm(strdst, str1, strdstlen), failed);
		STRCHK(name, 0 != test_strxfrm(strdst, str2, strdstlen), failed);
		*strdst = '\0';
		STRCHK(name, 0 != test_strxfrm(strdst, str2, strdstlen), failed);
		STRCHK(name, 0 != test_strxfrm(strdst, str1, strdstlen), failed);
	}
}


/*
 *  stress_str_all()
 *	iterate over all string stressors
 */
static void stress_str_all(
	void *libc_func,
	const char *name,
	char *str1,
	const size_t len1,
	char *str2,
	const size_t len2,
	char *strdst,
	const size_t strdstlen,
	bool *failed)
{
	static int i = 1;	/* Skip over stress_str_all */

	(void)libc_func;

	str_methods[i].func(str_methods[i].libc_func, name, str1, len1, str2, len2, strdst, strdstlen, failed);
	i++;
	if (!str_methods[i].func)
		i = 1;
}

/*
 * Table of string stress methods
 */
static const stress_str_method_info_t str_methods[] = {
	{ "all",		stress_str_all,		NULL },	/* Special "all test */

#if defined(HAVE_STRINGS_H)
	{ "index",		stress_index,		(void *)index },
	{ "rindex",		stress_rindex,		(void *)rindex  },
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
	{ NULL,			NULL,			NULL }
};

/*
 *  stress_set_str_method()
 *	set the default string stress method
 */
static int stress_set_str_method(const char *name)
{
	stress_str_method_info_t const *info;

	for (info = str_methods; keep_stressing_flag() && info->func; info++) {
		if (!strcmp(info->name, name)) {
			stress_set_setting("str-method", TYPE_ID_UINTPTR_T, &info);
			return 0;
		}
	}

	(void)fprintf(stderr, "str-method must be one of:");
	for (info = str_methods; info->func; info++) {
		(void)fprintf(stderr, " %s", info->name);
	}
	(void)fprintf(stderr, "\n");

	return -1;
}

/*
 *  stress_str()
 *	stress CPU by doing various string operations
 */
static int stress_str(const stress_args_t *args)
{
	const stress_str_method_info_t *str_method = &str_methods[0];
	stress_str_func func;
	void *libc_func;
	bool failed = false;
	char ALIGN64 str1[STR1LEN], ALIGN64 str2[STR2LEN];
	char ALIGN64 strdst[STRDSTLEN];
	register char *ptr1, *ptr2;
	register size_t len1, len2;
	const char *name = args->name;

	(void)stress_get_setting("str-method", &str_method);
	func = str_method->func;
	libc_func = str_method->libc_func;

	ptr1 = str1;
	len1 = sizeof(str1);
	ptr2 = str2;
	len2 = sizeof(str2);

	stress_strnrnd(ptr1, len1);

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		register char *tmpptr;
		register size_t tmplen;

		stress_strnrnd(ptr2, len2);
		(void)func(libc_func, name, ptr1, len1, ptr2, len2, strdst, STRDSTLEN, &failed);

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

static void stress_str_set_default(void)
{
	stress_set_str_method("all");
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_str_method,	stress_set_str_method },
	{ 0,			NULL }
};

stressor_info_t stress_str_info = {
	.stressor = stress_str,
	.set_default = stress_str_set_default,
	.class = CLASS_CPU | CLASS_CPU_CACHE | CLASS_MEMORY,
	.opt_set_funcs = opt_set_funcs,
	.verify = VERIFY_OPTIONAL,
	.help = help
};
