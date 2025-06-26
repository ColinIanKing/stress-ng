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

#if defined(HAVE_LINUX_RANDOM_H)
#include <linux/random.h>
#endif

static const stress_help_t help[] = {
	{ NULL,	"getrandom N",	   "start N workers fetching random data via getrandom()" },
	{ NULL,	"getrandom-ops N", "stop after N getrandom bogo operations" },
	{ NULL, NULL,		   NULL }
};

#if defined(__OpenBSD__) || 	\
    defined(__APPLE__) || 	\
    defined(__FreeBSD__) ||	\
    (defined(__linux__) && defined(__NR_getrandom))

#if defined(__OpenBSD__) ||	\
    defined(__APPLE__)
#define RANDOM_BUFFER_SIZE	(256)
#else
#define RANDOM_BUFFER_SIZE	(8192)
#endif

/*
 *  stress_getrandom_supported()
 *      check if getrandom is supported
 */
static int stress_getrandom_supported(const char *name)
{
	int ret;
	char buffer[RANDOM_BUFFER_SIZE];

	ret = shim_getrandom(buffer, sizeof(buffer), 0);
	if ((ret < 0) && (errno == ENOSYS)) {
		pr_inf_skip("%s stressor will be skipped, getrandom() not supported\n", name);
		return -1;
	}
	return 0;
}

typedef struct {
	const unsigned int flag;
	const char *flag_str;
} getrandom_flags_t ;

#define GETRANDOM_FLAG_INFO(x)      { x, # x }

static const getrandom_flags_t getrandom_flags[] = {
	GETRANDOM_FLAG_INFO(0),
#if defined(GRND_NONBLOCK) &&	\
    defined(__linux__)
	GETRANDOM_FLAG_INFO(GRND_NONBLOCK),
#endif
#if defined(GRND_RANDOM) &&	\
    defined(__linux__)
	GETRANDOM_FLAG_INFO(GRND_RANDOM),
#endif
#if defined(GRND_INSECURE) &&	\
    defined(__linux__)
	GETRANDOM_FLAG_INFO(GRND_INSECURE),
#endif
#if defined(GRND_INSECURE) &&	\
    defined(__linux__)
	GETRANDOM_FLAG_INFO(GRND_INSECURE),
#endif
#if defined(GRND_INSECURE) &&	\
    defined(GRND_NONBLOCK) &&	\
    defined(__linux__)
	GETRANDOM_FLAG_INFO(GRND_NONBLOCK | GRND_INSECURE),
#endif
#if defined(GRND_NONBLOCK) &&	\
    defined(GRND_RANDOM) &&	\
    defined(__linux__)
	GETRANDOM_FLAG_INFO(GRND_NONBLOCK | GRND_RANDOM),
#endif
#if defined(GRND_INSECURE) &&	\
    defined(GRND_RANDOM) &&	\
    defined(__linux__)
	/* exercise invalid flag combination */
	GETRANDOM_FLAG_INFO(GRND_INSECURE | GRND_RANDOM),
#endif
#if defined(GRND_INSECURE) &&	\
    defined(GRND_RANDOM) &&	\
    defined(GRND_NONBLOCK) &&	\
    defined(__linux__)
	/* exercise invalid flag combination */
	GETRANDOM_FLAG_INFO(GRND_INSECURE | GRND_RANDOM | GRND_NONBLOCK),
#endif
	/* exercise all flags illegal flag combination */
	GETRANDOM_FLAG_INFO(~0U),
};

/*
 *  stress_getrandom
 *	stress reading random values using getrandom()
 */
static int stress_getrandom(stress_args_t *args)
{
	double duration = 0.0, bytes = 0.0, rate;

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		char buffer[RANDOM_BUFFER_SIZE];
		size_t i;
		double t;

		t = stress_time_now();
		for (i = 0; LIKELY(stress_continue(args) && (i < SIZEOF_ARRAY(getrandom_flags))); i++) {
			ssize_t ret;

			ret = shim_getrandom(buffer, sizeof(buffer), getrandom_flags[i].flag);
			if (UNLIKELY(ret < 0)) {
				if ((errno == EAGAIN) ||
				    (errno == EINTR) ||
				    (errno == EINVAL))
					continue;
				if (errno == ENOSYS) {
					/* Should not happen.. */
					if (stress_instance_zero(args))
						pr_inf_skip("%s: stressor will be skipped, "
							"getrandom() not supported\n",
							args->name);
					return EXIT_NOT_IMPLEMENTED;
				}
				pr_fail("%s: getrandom using flags %s failed, errno=%d (%s)\n",
					args->name, getrandom_flags[i].flag_str,
					errno, strerror(errno));
				return EXIT_FAILURE;
			} else {
				bytes += (double)ret;
			}
#if defined(HAVE_GETENTROPY)
			/*
			 *  getentropy() on Linux is implemented using
			 *  getrandom() but it's worth exercising it for
			 *  completeness sake and it's also available on
			 *  other systems such as OpenBSD.
			 */
			ret = getentropy(buffer, 1);
			if (ret > 0)
				bytes += (double)1.0;
#endif
			stress_bogo_inc(args);
		}
		duration += stress_time_now() - t;
	} while (stress_continue(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	rate = (duration > 0.0) ? (8.0 * bytes) / duration : 0.0;
	stress_metrics_set(args, 0, "getrandom bits per sec",
		rate, STRESS_METRIC_HARMONIC_MEAN);

	return EXIT_SUCCESS;
}

const stressor_info_t stress_getrandom_info = {
	.stressor = stress_getrandom,
	.supported = stress_getrandom_supported,
	.classifier = CLASS_OS | CLASS_CPU,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
const stressor_info_t stress_getrandom_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_OS | CLASS_CPU,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without getrandom() support"
};
#endif
