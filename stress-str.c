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

/*
 *  the STR stress test has different classes of string stressors
 */
typedef void (*stress_str_func)(
	const void *libc_func,
	const char *name,
	char *str1,
	const size_t len1,
	char *str2,
	const size_t len2,
	bool *failed);

typedef struct {
	const char 		*name;	/* human readable form of stressor */
	const stress_str_func	func;	/* the stressor function */
	const void 		*libc_func;
} stress_str_method_info_t;

static const help_t help[] = {
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
	const void *libc_func,
	const char *name,
	char *str1,
	const size_t len1,
	char *str2,
	const size_t len2,
	bool *failed)
{
	register size_t i;
	int (*__strcasecmp)(const char *s1, const char *s2) = libc_func;

	(void)len2;

	for (i = 1; g_keep_stressing_flag && (i < len1); i++) {
		STRCHK(name, 0 == __strcasecmp(str1, str1), failed);
		STRCHK(name, 0 == __strcasecmp(str2, str2), failed);

		STRCHK(name, 0 != __strcasecmp(str2, str1), failed);
		STRCHK(name, 0 != __strcasecmp(str1, str2), failed);

		STRCHK(name, 0 != __strcasecmp(str1 + i, str1), failed);
		STRCHK(name, 0 != __strcasecmp(str1, str1 + i), failed);
		STRCHK(name, 0 == __strcasecmp(str1 + i, str1 + i), failed);

		STRCHK(name, 0 != __strcasecmp(str1 + i, str2), failed);
		STRCHK(name, 0 != __strcasecmp(str2, str1 + i), failed);
	}
}
#endif

#if defined(HAVE_STRINGS_H)
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
	const size_t len2,
	bool *failed)
{
	register size_t i;
	int (*__strncasecmp)(const char *s1, const char *s2, size_t n) = libc_func;

	(void)len2;

	for (i = 1; g_keep_stressing_flag && (i < len1); i++) {
		STRCHK(name, 0 == __strncasecmp(str1, str1, len1), failed);
		STRCHK(name, 0 == __strncasecmp(str2, str2, len2), failed);

		STRCHK(name, 0 != __strncasecmp(str2, str1, len2), failed);
		STRCHK(name, 0 != __strncasecmp(str1, str2, len1), failed);

		STRCHK(name, 0 != __strncasecmp(str1 + i, str1, len1), failed);
		STRCHK(name, 0 != __strncasecmp(str1, str1 + i, len1), failed);
		STRCHK(name, 0 == __strncasecmp(str1 + i, str1 + i, len1), failed);

		STRCHK(name, 0 != __strncasecmp(str1 + i, str2, len1), failed);
		STRCHK(name, 0 != __strncasecmp(str2, str1 + i, len2), failed);
	}
}
#endif

#if defined(HAVE_STRINGS_H)
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
	const size_t len2,
	bool *failed)
{
	register size_t i;
	char * (*__index)(const char *s, int c) = libc_func;

	(void)len2;

	for (i = 0; g_keep_stressing_flag && (i < len1 - 1); i++) {
		STRCHK(name, NULL == __index(str1, '_'), failed);
		STRCHK(name, NULL != __index(str1, str1[0]), failed);

		STRCHK(name, NULL == __index(str2, '_'), failed);
		STRCHK(name, NULL != __index(str2, str2[0]), failed);
	}
}
#endif

#if defined(HAVE_STRINGS_H)
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
	const size_t len2,
	bool *failed)
{
	register size_t i;
	char * (*__rindex)(const char *s, int c) = libc_func;

	(void)len2;

	for (i = 0; g_keep_stressing_flag && (i < len1 - 1); i++) {
		STRCHK(name, NULL == __rindex(str1, '_'), failed);
		STRCHK(name, NULL != __rindex(str1, str1[0]), failed);

		STRCHK(name, NULL == __rindex(str2, '_'), failed);
		STRCHK(name, NULL != __rindex(str2, str2[0]), failed);
	}
}
#endif

#if defined(HAVE_STRLCPY)
/*
 *  stress_strlcpy()
 *	stress on strlcpy
 */
static void stress_strlcpy(
	const void *libc_func,
	const char *name,
	char *str1,
	const size_t len1,
	char *str2,
	const size_t len2,
	bool *failed)
{
	register size_t i;
	size_t (*__strlcpy)(char *dest, const char *src, size_t len) = libc_func;

	char buf[len1 + len2 + 1];
	const size_t buf_len = sizeof(buf);
	const size_t str_len1 = strlen(str1);
	const size_t str_len2 = strlen(str2);

	for (i = 0; g_keep_stressing_flag && (i < len1 - 1); i++) {
		STRCHK(name, str_len1 == __strlcpy(buf, str1, buf_len), failed);
		STRCHK(name, str_len2 == __strlcpy(buf, str2, buf_len), failed);
	}
}
#else
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
	const size_t len2,
	bool *failed)
{
	register size_t i;
	char * (*__strcpy)(char *dest, const char *src) = libc_func;

	char buf[len1 + len2 + 1];

	for (i = 0; g_keep_stressing_flag && (i < len1 - 1); i++) {
		STRCHK(name, buf == __strcpy(buf, str1), failed);
		STRCHK(name, buf == __strcpy(buf, str2), failed);
	}
}
#endif


#if defined(HAVE_STRLCAT)
/*
 *  stress_strlcat()
 *	stress on strlcat
 */
static void stress_strlcat(
	const void *libc_func,
	const char *name,
	char *str1,
	const size_t len1,
	char *str2,
	const size_t len2,
	bool *failed)
{
	register size_t i;
	size_t (*__strlcat)(char *dest, const char *src, size_t len) = libc_func;

	char buf[len1 + len2 + 1];
	const size_t buf_len = sizeof(buf);
	const size_t str_len1 = strlen(str1);
	const size_t str_len2 = strlen(str2);
	const size_t str_len = str_len1 + str_len2;

	for (i = 0; g_keep_stressing_flag && (i < len1 - 1); i++) {
		*buf = '\0';
		STRCHK(name, str_len1 == __strlcat(buf, str1, buf_len), failed);
		*buf = '\0';
		STRCHK(name, str_len2 == __strlcat(buf, str2, buf_len), failed);
		*buf = '\0';
		STRCHK(name, str_len1 == __strlcat(buf, str1, buf_len), failed);
		STRCHK(name, str_len  == __strlcat(buf, str2, buf_len), failed);
		*buf = '\0';
		STRCHK(name, str_len2 == __strlcat(buf, str2, buf_len), failed);
		STRCHK(name, str_len  == __strlcat(buf, str1, buf_len), failed);
	}
}
#else
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
	const size_t len2,
	bool *failed)
{
	register size_t i;
	char * (*__strcat)(char *dest, const char *src) = libc_func;

	char buf[len1 + len2 + 1];

	for (i = 0; g_keep_stressing_flag && (i < len1 - 1); i++) {
		*buf = '\0';
		STRCHK(name, buf == __strcat(buf, str1), failed);
		*buf = '\0';
		STRCHK(name, buf == __strcat(buf, str2), failed);
		*buf = '\0';
		STRCHK(name, buf == __strcat(buf, str1), failed);
		STRCHK(name, buf == __strcat(buf, str2), failed);
		*buf = '\0';
		STRCHK(name, buf == __strcat(buf, str2), failed);
		STRCHK(name, buf == __strcat(buf, str1), failed);
	}
}
#endif

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
	const size_t len2,
	bool *failed)
{
	register size_t i;
	char * (*__strncat)(char *dest, const char *src, size_t n) = libc_func;
	char buf[len1 + len2 + 1];

	for (i = 0; g_keep_stressing_flag && (i < len1 - 1); i++) {
		*buf = '\0';
		STRCHK(name, buf == __strncat(buf, str1, len1), failed);
		*buf = '\0';
		STRCHK(name, buf == __strncat(buf, str2, len2), failed);
		*buf = '\0';
		STRCHK(name, buf == __strncat(buf, str1, len1), failed);
		STRCHK(name, buf == __strncat(buf, str2, len1 + len2), failed);
		*buf = '\0';
		STRCHK(name, buf == __strncat(buf, str2, i), failed);
		STRCHK(name, buf == __strncat(buf, str1, i), failed);
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
	const size_t len2,
	bool *failed)
{
	register size_t i;
	char * (*__strchr)(const char *s, int c) = libc_func;

	(void)len2;

	for (i = 0; g_keep_stressing_flag && (i < len1 - 1); i++) {
		STRCHK(name, NULL == __strchr(str1, '_'), failed);
		STRCHK(name, NULL != __strchr(str1, str1[0]), failed);

		STRCHK(name, NULL == __strchr(str2, '_'), failed);
		STRCHK(name, NULL != __strchr(str2, str2[0]), failed);
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
	const size_t len2,
	bool *failed)
{
	register size_t i;
	char * (*__strrchr)(const char *s, int c) = libc_func;

	(void)len2;

	for (i = 0; g_keep_stressing_flag && (i < len1 - 1); i++) {
		STRCHK(name, NULL == __strrchr(str1, '_'), failed);
		STRCHK(name, NULL != __strrchr(str1, str1[0]), failed);

		STRCHK(name, NULL == __strrchr(str2, '_'), failed);
		STRCHK(name, NULL != __strrchr(str2, str2[0]), failed);
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
	const size_t len2,
	bool *failed)
{
	register size_t i;
	int (*__strcmp)(const char *s1, const char *s2) = libc_func;

	(void)len2;

	for (i = 1; g_keep_stressing_flag && (i < len1); i++) {
		STRCHK(name, 0 == __strcmp(str1, str1), failed);
		STRCHK(name, 0 == __strcmp(str2, str2), failed);

		STRCHK(name, 0 != __strcmp(str2, str1), failed);
		STRCHK(name, 0 != __strcmp(str1, str2), failed);

		STRCHK(name, 0 != __strcmp(str1 + i, str1), failed);
		STRCHK(name, 0 != __strcmp(str1, str1 + i), failed);
		STRCHK(name, 0 == __strcmp(str1 + i, str1 + i), failed);

		STRCHK(name, 0 != __strcmp(str1 + i, str2), failed);
		STRCHK(name, 0 != __strcmp(str2, str1 + i), failed);
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
	const size_t len2,
	bool *failed)
{
	register size_t i;
	int (*__strncmp)(const char *s1, const char *s2, size_t n) = libc_func;

	for (i = 1; g_keep_stressing_flag && (i < len1); i++) {
		STRCHK(name, 0 == __strncmp(str1, str1, len1), failed);
		STRCHK(name, 0 == __strncmp(str2, str2, len2), failed);

		STRCHK(name, 0 != __strncmp(str2, str1, len2), failed);
		STRCHK(name, 0 != __strncmp(str1, str2, len1), failed);

		STRCHK(name, 0 != __strncmp(str1 + i, str1, len1), failed);
		STRCHK(name, 0 != __strncmp(str1, str1 + i, len1), failed);
		STRCHK(name, 0 == __strncmp(str1 + i, str1 + i, len1), failed);

		STRCHK(name, 0 != __strncmp(str1 + i, str2, len2), failed);
		STRCHK(name, 0 != __strncmp(str2, str1 + i, len2), failed);
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
	const size_t len2,
	bool *failed)
{
	register size_t i;
	int (*__strcoll)(const char *s1, const char *s2) = libc_func;

	(void)len2;

	for (i = 1; g_keep_stressing_flag && (i < len1); i++) {
		STRCHK(name, 0 == __strcoll(str1, str1), failed);
		STRCHK(name, 0 == __strcoll(str2, str2), failed);

		STRCHK(name, 0 != __strcoll(str2, str1), failed);
		STRCHK(name, 0 != __strcoll(str1, str2), failed);

		STRCHK(name, 0 != __strcoll(str1 + i, str1), failed);
		STRCHK(name, 0 != __strcoll(str1, str1 + i), failed);
		STRCHK(name, 0 == __strcoll(str1 + i, str1 + i), failed);

		STRCHK(name, 0 != __strcoll(str1 + i, str2), failed);
		STRCHK(name, 0 != __strcoll(str2, str1 + i), failed);
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
	const size_t len2,
	bool *failed)
{
	register size_t i;
	size_t (*__strlen)(const char *s) = libc_func;


	for (i = 0; g_keep_stressing_flag && (i < len1 - 1); i++) {
		STRCHK(name, len1 - 1 == __strlen(str1), failed);
		STRCHK(name, len1 - 1 - i == __strlen(str1 + i), failed);
	}

	for (i = 0; g_keep_stressing_flag && (i < len2 - 1); i++) {
		STRCHK(name, len2 - 1 == __strlen(str2), failed);
		STRCHK(name, len2 - 1 - i == __strlen(str2 + i), failed);
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
	const size_t len2,
	bool *failed)
{
	register size_t i;
	char buf[len1 + len2];
	size_t (*__strxfrm)(char *dest, const char *src, size_t n) = libc_func;

	for (i = 0; g_keep_stressing_flag && (i < len1 - 1); i++) {
		*buf = '\0';
		STRCHK(name, 0 != __strxfrm(buf, str1, sizeof(buf)), failed);
		*buf = '\0';
		STRCHK(name, 0 != __strxfrm(buf, str2, sizeof(buf)), failed);
		*buf = '\0';
		STRCHK(name, 0 != __strxfrm(buf, str1, sizeof(buf)), failed);
		STRCHK(name, 0 != __strxfrm(buf, str2, sizeof(buf)), failed);
		*buf = '\0';
		STRCHK(name, 0 != __strxfrm(buf, str2, sizeof(buf)), failed);
		STRCHK(name, 0 != __strxfrm(buf, str1, sizeof(buf)), failed);
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
	const size_t len2,
	bool *failed)
{
	static int i = 1;	/* Skip over stress_str_all */

	(void)libc_func;

	str_methods[i].func(str_methods[i].libc_func, name, str1, len1, str2, len2, failed);
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
	{ "index",		stress_index,		index },
	{ "rindex",		stress_rindex,		rindex  },
	{ "strcasecmp",		stress_strcasecmp,	strcasecmp },
#endif
#if defined(HAVE_STRLCAT)
	{ "strlcat",		stress_strlcat,		strlcat },
#else
	{ "strcat",		stress_strcat,		strcat },
#endif
	{ "strchr",		stress_strchr,		strchr },
	{ "strcoll",		stress_strcoll,		strcoll },
	{ "strcmp",		stress_strcmp,		strcmp },
#if defined(HAVE_STRLCPY)
	{ "strlcpy",		stress_strlcpy,		strlcpy },
#else
	{ "strcpy",		stress_strcpy,		strcpy },
#endif
	{ "strlen",		stress_strlen,		strlen },
#if defined(HAVE_STRINGS_H)
	{ "strncasecmp",	stress_strncasecmp,	strncasecmp },
#endif
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
static int stress_set_str_method(const char *name)
{
	stress_str_method_info_t const *info;

	for (info = str_methods; g_keep_stressing_flag && info->func; info++) {
		if (!strcmp(info->name, name)) {
			set_setting("str-method", TYPE_ID_UINTPTR_T, &info);
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
static int stress_str(const args_t *args)
{
	const stress_str_method_info_t *str_method = &str_methods[0];
	stress_str_func func;
	const void *libc_func;
	bool failed = false;
	char ALIGN64 str1[256], ALIGN64 str2[128];
	register char *ptr1, *ptr2;
	register size_t len1, len2;
	const char *name = args->name;

	(void)get_setting("str-method", &str_method);
	func = str_method->func;
	libc_func = str_method->libc_func;

	ptr1 = str1;
	len1 = sizeof(str1);
	ptr2 = str2;
	len2 = sizeof(str2);

	stress_strnrnd(ptr1, len1);

	do {
		register char *tmpptr;
		register size_t tmplen;

		stress_strnrnd(ptr2, len2);
		(void)func(libc_func, name, ptr1, len1, ptr2, len2, &failed);

		tmpptr = ptr1;
		ptr1 = ptr2;
		ptr2 = tmpptr;

		tmplen = len1;
		len1 = len2;
		len2 = tmplen;

		inc_counter(args);
	} while (keep_stressing());

	return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}

static void stress_str_set_default(void)
{
	stress_set_str_method("all");
}

static const opt_set_func_t opt_set_funcs[] = {
	{ OPT_str_method,	stress_set_str_method },
	{ 0,			NULL }
};

stressor_info_t stress_str_info = {
	.stressor = stress_str,
	.set_default = stress_str_set_default,
	.class = CLASS_CPU | CLASS_CPU_CACHE | CLASS_MEMORY,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
