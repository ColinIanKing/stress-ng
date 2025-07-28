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
#include "core-builtin.h"

#if defined(HAVE_CRYPT_H)
#include <crypt.h>
#endif

static const stress_help_t help[] = {
	{ NULL,	"crypt N",	  "start N workers performing password encryption" },
	{ NULL, "crypt-method M", "select encryption method [ all | MD5 | NT | SHA-1 | SHA-256 | SHA-512 | scrypt | SunMD5 | yescrypt]" },
	{ NULL,	"crypt-ops N",	  "stop after N bogo crypt operations" },
	{ NULL,	NULL,		  NULL }
};

typedef struct {
	const char *prefix;
	const size_t prefix_len;
	const char *method;
} crypt_method_t;

static const crypt_method_t crypt_methods[] = {
	{ NULL,		0,	"all" },
	{ "$2b$",	4,	"bcrypt" },
	{ "_",		1,	"bsdicrypt" },
	{ "",		0,	"descrypt" },
	{ "$gy$",	4,	"gost-yescrypt" },
	{ "$1$",	3,	"MD5" },
	{ "$3$",	3,	"NT" },
	{ "$7$",	3,	"scrypt" },
	{ "$sha1",	5,	"SHA-1" },
	{ "$5$",	3,	"SHA-256" },
	{ "$6$",	3,	"SHA-512" },
	{ "$md5",	4,	"SunMD5" },
	{ "$y$",	3,	"yescrypt" },
};

static const char * stress_crypt_method(const size_t i)
{
	return (i < SIZEOF_ARRAY(crypt_methods)) ? crypt_methods[i].method : NULL;
}

static const stress_opt_t opts[] = {
	{ OPT_crypt_method, "crypt-method", TYPE_ID_SIZE_T_METHOD, 0, 0, stress_crypt_method },
	END_OPT,
};

#if defined(HAVE_LIB_CRYPT) &&	\
    (defined(HAVE_CRYPT_H) || 	\
     defined(__FreeBSD__))
static stress_metrics_t *crypt_metrics;

/*
 *  stress_crypt_id()
 *	crypt a password with given seed and id
 */
static int stress_crypt_id(
	stress_args_t *args,
	const size_t i,
#if defined(HAVE_CRYPT_R)
	struct crypt_data *data
#else
	const char *phrase,
	const char *setting
#endif
	)
{
	const char *encrypted;
	double t1, t2;
	errno = 0;

#if defined(HAVE_CRYPT_R)
	t1 = stress_time_now();
	encrypted = crypt_r(data->input, data->setting, data);
	t2 = stress_time_now();
#else
	t1 = stress_time_now();
	encrypted = crypt(phrase, setting);
	t2 = stress_time_now();
#endif
	if (UNLIKELY(!encrypted)) {
		switch (errno) {
		case 0:
			break;
		case EINVAL:
			break;
#if defined(ENOENT)
		case ENOENT:
			break;
#endif
#if defined(ENOSYS)
		case ENOSYS:
			break;
#endif
#if defined(EOPNOTSUPP)
		case EOPNOTSUPP:
#endif
			break;
		default:
			pr_fail("%s: cannot encrypt with %s, errno=%d (%s)\n",
				args->name, crypt_methods[i].method, errno, strerror(errno));
			return -1;
		}
	} else {
		crypt_metrics[i].duration += (t2 - t1);
		crypt_metrics[i].count += 1.0;
	}
	return 0;
}

/*
 *  stress_crypt()
 *	stress libc crypt
 */
static int stress_crypt(stress_args_t *args)
{
	register size_t i, j;
	size_t crypt_method = 0;	/* all */
#if defined(HAVE_CRYPT_R)
	static struct crypt_data data;
#endif

	(void)stress_get_setting("crypt-method", &crypt_method);

	crypt_metrics = (stress_metrics_t *)calloc(SIZEOF_ARRAY(crypt_methods), sizeof(*crypt_metrics));
	if (!crypt_metrics) {
		pr_inf_skip("%s: cannot allocate crypt metrics "
			"array%s, skipping stressor\n",
			args->name, stress_get_memfree_str());
		return EXIT_NO_RESOURCE;
	}
	stress_zero_metrics(crypt_metrics, SIZEOF_ARRAY(crypt_methods));
#if defined(HAVE_CRYPT_R)
	(void)shim_memset(&data, 0, sizeof(data));
#endif
	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		static const char seedchars[64] ALIGN64 NONSTRING =
			"./0123456789ABCDEFGHIJKLMNOPQRST"
			"UVWXYZabcdefghijklmnopqrstuvwxyz";
#if defined(HAVE_CRYPT_R)
		char *const phrase = data.input;
		char *const setting = data.setting;
#else
		char phrase[16];
		char setting[12];
#endif
		char orig_setting[12];
		char orig_phrase[16];
		const crypt_method_t *cm = crypt_methods;
		size_t failed = 0;
		int ret;
		size_t n = cm->prefix_len + 8;

		setting[0] = '$';
		setting[1] = 'x';
		setting[2] = '$';

		n = (n > sizeof(orig_setting)) ? sizeof(orig_setting) : n;

		for (i = 0; i < n; i++)
			orig_setting[i] = seedchars[stress_mwc8() & 0x3f];
		orig_setting[n - 1] = '\0';

		for (i = 0; i < sizeof(orig_phrase) - 1; i++)
			orig_phrase[i] = seedchars[stress_mwc8() & 0x3f];
		orig_phrase[i] = '\0';

		if (crypt_method == 0) {
			for (i = 1; LIKELY(stress_continue(args) && (i < SIZEOF_ARRAY(crypt_methods))); i++, cm++) {
				(void)shim_memcpy(setting + cm->prefix_len, orig_setting, n);
				(void)shim_memcpy(setting, cm->prefix, cm->prefix_len);
				(void)shim_memcpy(phrase, orig_phrase, sizeof(orig_phrase));

#if defined (HAVE_CRYPT_R)
				data.initialized = 0;
#endif
				ret = stress_crypt_id(args, i,
#if defined (HAVE_CRYPT_R)
					&data);
#else
					phrase, setting);
#endif
				if (UNLIKELY(ret < 0))
					failed++;
				else
					stress_bogo_inc(args);
			}
		} else {
			(void)shim_strscpy(setting, orig_setting, sizeof(orig_setting));
			(void)shim_memcpy(setting, cm->prefix, cm->prefix_len);
			(void)shim_memcpy(phrase, orig_phrase, sizeof(orig_phrase));
#if defined (HAVE_CRYPT_R)
			data.initialized = 0;
#endif

			ret = stress_crypt_id(args, crypt_method,
#if defined (HAVE_CRYPT_R)
				&data);
#else
				phrase, setting);
#endif
			if (UNLIKELY(ret < 0))
				failed++;
			else
				stress_bogo_inc(args);
		}
		if (failed)
			break;
	} while (stress_continue(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	for (i = 1, j = 0; i < SIZEOF_ARRAY(crypt_methods); i++) {
		const double duration = crypt_metrics[i].duration;
		const double rate = duration > 0 ? crypt_metrics[i].count / duration : 0.0;

		if (rate > 0.0) {
			char str[40];

			(void)snprintf(str, sizeof(str), "%s encrypts per sec", crypt_methods[i].method);
			stress_metrics_set(args, j, str, rate, STRESS_METRIC_HARMONIC_MEAN);
			j++;
		}
	}

	free(crypt_metrics);

	return EXIT_SUCCESS;
}

const stressor_info_t stress_crypt_info = {
	.stressor = stress_crypt,
	.classifier = CLASS_CPU,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
const stressor_info_t stress_crypt_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_CPU | CLASS_COMPUTE,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without crypt library"
};
#endif
