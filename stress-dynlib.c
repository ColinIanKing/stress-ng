// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
 *
 */
#include "stress-ng.h"
#include "core-builtin.h"
#include "core-put.h"

#if defined(HAVE_LIB_DL)
#include <dlfcn.h>
#include <gnu/lib-names.h>
#endif

static const stress_help_t help[] = {
	{ NULL,	"dynlib N",	"start N workers exercising dlopen/dlclose" },
	{ NULL,	"dynlib-ops N",	"stop after N dlopen/dlclose bogo operations" },
	{ NULL,	NULL,		NULL }
};

#if defined(HAVE_LIB_DL) &&	\
    !defined(BUILD_STATIC)

static sigjmp_buf jmp_env;

typedef struct {
	const char *library;
	const char *symbol;
} stress_lib_info_t;

static const stress_lib_info_t libnames[] = {
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
	{ LIBM_SO, "sin" },
	{ LIBM_SO, "tan" },
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
	{ LIBRT_SO, "timer_delete" },
#endif
#if defined(LIBTHREAD_DB_SO)
	{ LIBTHREAD_DB_SO, "td_thr_clear_event" },
#endif
#if defined(LIBUTIL_SO)
	{ LIBUTIL_SO, "openpty" },
#endif
};

#define MAX_LIBNAMES	(SIZEOF_ARRAY(libnames))

/*
 *  stress_segvhandler()
 *      SEGV handler
 */
static void NORETURN MLOCKED_TEXT stress_segvhandler(int signum)
{
	(void)signum;

	siglongjmp(jmp_env, 1);
}

/*
 *  stress_dynlib()
 *	stress that does lots of not a lot
 */
static int stress_dynlib(const stress_args_t *args)
{
	void *handles[MAX_LIBNAMES];
	NOCLOBBER double count = 0.0, duration = 0.0;
	double rate;

	(void)shim_memset(handles, 0, sizeof(handles));

	if (stress_sighandler(args->name, SIGSEGV, stress_segvhandler, NULL) < 0)
		return EXIT_NO_RESOURCE;

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		size_t i;
		int ret;

		ret = sigsetjmp(jmp_env, 1);
		if (!stress_continue(args))
			break;
		if (ret)
			goto tidy;

		for (i = 0; i < MAX_LIBNAMES; i++) {
			int flags;

			flags = stress_mwc1() ? RTLD_LAZY : RTLD_NOW;
#if defined(RTLD_GLOBAL) &&	\
    defined(RTLD_LOCAL)
			flags |= stress_mwc1() ? RTLD_GLOBAL : RTLD_LOCAL;
#endif
			handles[i] = dlopen(libnames[i].library, flags);
			(void)dlerror();
		}

		for (i = 0; i < MAX_LIBNAMES; i++) {
			if (handles[i]) {
				uint8_t *ptr;
				double t;

				(void)dlerror();
				t = stress_time_now();
				ptr = dlsym(handles[i], libnames[i].symbol);
				/*
				 * The function pointer should be readable,
				 * however, we have a SIGSEGV handler that
				 * will perform tidy up if not
				 */
				duration += stress_time_now() - t;
				count += 1.0;
				if (ptr)
					stress_uint8_put(*ptr);
			}
		}
tidy:
		for (i = 0; i < MAX_LIBNAMES; i++) {
			if (handles[i])
				(void)dlclose(handles[i]);
			handles[i] = NULL;
		}
		stress_bogo_inc(args);
	} while (stress_continue(args));

	rate = (count > 0.0) ? duration / count : 0.0;
	stress_metrics_set(args, 0, "nanosecs per dlsym lookup", rate * STRESS_DBL_NANOSECOND);

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return EXIT_SUCCESS;
}
stressor_info_t stress_dynlib_info = {
	.stressor = stress_dynlib,
	.class = CLASS_OS,
	.help = help
};
#else
stressor_info_t stress_dynlib_info = {
	.stressor = stress_unimplemented,
	.class = CLASS_OS,
	.help = help,
	.unimplemented_reason = "built without dynamic library libdl support"
};
#endif
