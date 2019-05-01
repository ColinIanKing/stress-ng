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

static const help_t help[] = {
	{ NULL,	"dynlib N",	"start N workers exercising dlopen/dlclose" },
	{ NULL,	"dynlib-ops N",	"stop after N dlopen/dlclose bogo operations" },
	{ NULL,	NULL,		NULL }
};

#if defined(HAVE_LIB_DL) && !defined(BUILD_STATIC)

static sigjmp_buf jmp_env;

typedef struct {
	const char *library;
	const char *symbol;
} lib_info_t;

static const lib_info_t libnames[] = {
#if defined(LIBANL_SO)
	{ LIBANL_SO, "gai_error" },
#endif
#if defined(LIBBROKENLOCALE_SO)
	{ LIBBROKENLOCALE_SO, "nl_langinfo" },
#endif
#if defined(LIBCIDN_SO)
	{ LIBCIDN_SO, "idna_to_ascii_lz" },
#endif
#if defined(LIBCRYPT_SO)
	{ LIBCRYPT_SO, "crypt" },
#endif
#if defined(LIBGCC_S_SO)
	{ LIBGCC_S_SO, "__clear_cache" },
#endif
#if defined(LIBMVEC_SO)
	{ LIBMVEC_SO, "_ZGVbN4v_logf" },
#endif
#if defined(LIBM_SO)
	{ LIBM_SO, "cos" },
#endif
#if defined(LIBNSL_SO)
	{ LIBNSL_SO, "yp_match" },
#endif
#if defined(LIBNSS_COMPAT_SO)
	{ LIBNSS_COMPAT_SO, "_nss_compat_endspent" },
#endif
#if defined(LIBNSS_DNS_SO)
	{ LIBNSS_DNS_SO, "_nss_dns_gethostbyaddr_r" },
#endif
#if defined(LIBNSS_HESIOD_SO)
	{ LIBNSS_HESIOD_SO, "_nss_hesiod_getpwnam_r" },
#endif
#if defined(LIBNSS_NISPLUS_SO)
	{ LIBNSS_NISPLUS_SO, "_nss_nisplus_getnetent_r" },
#endif
#if defined(LIBNSS_NIS_SO)
	{ LIBNSS_NIS_SO, "_nss_nis_setetherent" },
#endif
#if defined(LIBPTHREAD_SO)
	{ LIBPTHREAD_SO, "pthread_cancel" },
#endif
#if defined(LIBRESOLV_SO)
	{ LIBRESOLV_SO, "ns_name_ntol" },
#endif
#if defined(LIBRT_SO)
	{ LIBRT_SO, "timer_create" },
#endif
#if defined(LIBTHREAD_DB_SO)
	{ LIBTHREAD_DB_SO, "td_thr_clear_event" },
#endif
#if defined(LIBUTIL_SO)
	{ LIBUTIL_SO, "openpty" },
#endif
};

/*
 *  stress_segvhandler()
 *      SEGV handler
 */
static void MLOCKED_TEXT stress_segvhandler(int signum)
{
	(void)signum;

	siglongjmp(jmp_env, 1);
}

/*
 *  stress_dynlib()
 *	stress that does lots of not a lot
 */
static int stress_dynlib(const args_t *args)
{
	const size_t n = SIZEOF_ARRAY(libnames);
	void *handles[n];

	(void)memset(handles, 0, sizeof(handles));

	if (stress_sighandler(args->name, SIGSEGV, stress_segvhandler, NULL) < 0)
		return EXIT_NO_RESOURCE;

	do {
		size_t i;
		int ret;

		ret = sigsetjmp(jmp_env, 1);
		if (!keep_stressing())
			break;
		if (ret)
			goto tidy;

		for (i = 0; i < n; i++) {
			int flags;

			flags = mwc1() ? RTLD_LAZY : RTLD_NOW;
#if defined(RTLD_GLOBAL) && defined(RTLD_LOCAL)
			flags |= mwc1() ? RTLD_GLOBAL : RTLD_LOCAL;
#endif
			handles[i] = dlopen(libnames[i].library, flags);
			(void)dlerror();
		}

		for (i = 0; i < n; i++) {
			if (handles[i]) {
				uint8_t *ptr;

				(void)dlerror();
				ptr = dlsym(handles[i], libnames[i].symbol);
				/*
				 * The function pointer should be readable,
				 * however, we have a SIGSEGV handler that
				 * will perfom tidy up if not
				 */
				if (ptr)
					uint8_put(*ptr);
			}
		}
tidy:
		for (i = 0; i < n; i++) {
			if (handles[i])
				(void)dlclose(handles[i]);
			handles[i] = NULL;
		}
		inc_counter(args);
	} while (keep_stressing());

	return EXIT_SUCCESS;
}
stressor_info_t stress_dynlib_info = {
	.stressor = stress_dynlib,
	.class = CLASS_OS,
	.help = help
};
#else
stressor_info_t stress_dynlib_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_OS,
	.help = help
};
#endif
