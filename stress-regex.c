/*
 * Copyright (C) 2024-2025 Colin Ian King.
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

static const stress_help_t help[] = {
	{ NULL,	"regex N",	"start N workers exercise POSIX regular expressions" },
	{ NULL,	"regex-ops N",	"stop after N regular expression operations" },
	{ NULL,	NULL,		NULL }
};

#if defined(HAVE_REGEX_H) &&	\
    defined(HAVE_REGCOMP) &&	\
    defined(HAVE_REGERROR) &&	\
    defined(HAVE_REGEXEC) &&	\
    defined(HAVE_REGFREE)

#include <regex.h>

#define N_REGEXES	SIZEOF_ARRAY(stress_posix_regex)

typedef struct stress_posix_regex {
	const char *regex;
	const char *description;
} stress_posix_regex_t;

static const stress_posix_regex_t stress_posix_regex[] = {
	{ "^(((((((((((((([a-z])*)*)*)*)*)*)*)*)*)*)*)*)*)*", "devious alphas" },
	{ "^(((((((((((((([0-9])*)*)*)*)*)*)*)*)*)*)*)*)*)*", "devious digits" },
	{ "(([a-z])+.)+", "pathological" },
	{ "^.*$", "match all" },
	{ "^[0-9]*$", "positive integers" },
	/* { "([0-9][0-9]+?,[0-9]*?)", "lazy numbers" }, */
	{ "([0-9]+,[0-9]*)", "greedy numbers" },
	{ "^[+-]?[0-9]*$", "integers" },
	{ "^[-+]?[0-9]*\\.?[0-9]+([eE][-+]?[0-9]+)?.$", "floating point" },
	{ "^(([0-9]|[1-9][0-9]|1[0-9]{2}|2[0-4][0-9]|25[0-5])\\.){3}([0-9]|[1-9][0-9]|1[0-9]{2}|2[0-4][0-9]|25[0-5])$", "IP-address" },
	{ "^0x[0-9A-Fa-f]+", "hexadecimal" },
	{ "^[a-zA-Z0-9+/]+", "base64" },
	{ "^([Mm]on|[Tt]ues|[Ww]ednes|[Tt]hurs|[Ff]ri|[Ss]at|[Ss]un)day", "Days" },
	{ "^([01]?[0-9]|2[0-3]):[0-5]?[0-9]:([0-5]?[0-9])$", "HH:MM:SS" },
	{ "^([0-9][0-9][0-9][0-9])/(0[1-9]|1[0-2])/(0[1-9]|[12][0-9]|3[0-1])", "YYYY/MM/DD" },
};

static const char * const stress_regex_text[] = {
	"28742",
	"1984",
	"-1984",
	"0xc13eb9a621bd",
	"0x00000000000000000000000000000000000000000000000000000000000000001",
	"0x0123456789abcdef",
	"0x0123456789ABCDEF",
	"12,345",
	"12,345,678",
	"12,345,678,901",
	"12:45:57",
	"23:59:59",
	"24:00:00",
	"00:00:00",
	"17.9",
	"07919",
	"-3.14159265358979323846264338327950288419716939937510582097494459230781640628620899",
	"-12.4E23",
	"1.437676376e-12",
	"fred@somewhere.com",
	"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
	"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa!",
	"Proin dignissim, erat nec interdum commodo, nulla mi tempor dui, quis scelerisque odio nisi in tortor.",
	"1.1.1.1",
	"192.168.122.1",
	"255.255.255.0",
	"255.255.255.256",
	"2024/12/31",
	"2026/01/01",
	"2026/09/01",
	"2026/00/01",
	"2026/12/32",
	"Tuesday",
	"monday",
	"Friday",
	"FridaY",
	"example.sqltest.com",
	"bbc.co.uk",
	"google.com",
};

static double stress_regex_rate(double t[N_REGEXES], uint64_t c[N_REGEXES])
{
	size_t i;
	double t_total = 0.0;
	uint64_t c_total = 0;

	for (i = 0; i < N_REGEXES; i++) {
		t_total += t[i];
		c_total += c[i];
	}
	return (t_total > 0.0) ? (double)c_total / t_total : 0.0;
}

/*
 *  stress_regex()
 *	stress POSIX regular expressions
 */
static int stress_regex(stress_args_t *args)
{
	size_t i;

	double comp_times[N_REGEXES];
	double exec_times[N_REGEXES];
	uint64_t comp_count[N_REGEXES];
	uint64_t exec_count[N_REGEXES];
	bool failed[N_REGEXES];

	for (i = 0; i < N_REGEXES; i++) {
		comp_times[i] = 0.0;
		exec_times[i] = 0.0;
		comp_count[i] = 0;
		exec_count[i] = 0;
		failed[i] = false;
	}

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		int succeeded = 0;

		for (i = 0; LIKELY(i < SIZEOF_ARRAY(stress_posix_regex) && stress_continue(args)); i++) {
			double t;
			regex_t regex;
			int ret;

			if (UNLIKELY(failed[i]))
				continue;

			t = stress_time_now();
			ret = regcomp(&regex, stress_posix_regex[i].regex, REG_EXTENDED);
			if (UNLIKELY(ret != 0)) {
				if (stress_instance_zero(args) && (!failed[i])) {
					char errbuf[256];

					failed[i] = true;
					(void)regerror(ret, &regex, errbuf, sizeof(errbuf));
					pr_inf("%s: failed to compile %s regex '%s', error %s\n",
						args->name, stress_posix_regex[i].description,
						stress_posix_regex[i].regex, errbuf);

				}
			} else {
				size_t j;

				comp_times[i] += stress_time_now() - t;
				comp_count[i]++;
				succeeded++;

				for (j = 0; j < SIZEOF_ARRAY(stress_regex_text); j++) {
					regmatch_t regmatch[1];

					t = stress_time_now();
					ret = regexec(&regex, stress_regex_text[j], SIZEOF_ARRAY(regmatch), regmatch, 0);
					if (UNLIKELY(ret))
						continue;
					/* pr_inf("%s %s\n", stress_posix_regex[i].regex, stress_regex_text[j]); */
					exec_times[i] += stress_time_now() - t;
					exec_count[i]++;
				}
				regfree(&regex);
			}
			stress_bogo_inc(args);
		}
		if (UNLIKELY(succeeded == 0))
			break;
	} while (stress_continue(args));

	stress_metrics_set(args, 0, "regcomp per sec",
		stress_regex_rate(comp_times, comp_count), STRESS_METRIC_HARMONIC_MEAN);
	stress_metrics_set(args, 1, "regexec per sec",
		stress_regex_rate(exec_times, exec_count), STRESS_METRIC_HARMONIC_MEAN);

	for (i = 0; i < N_REGEXES; i++) {
		char str[64];
		double rate;

		rate = comp_times[i] > 0 ? comp_count[i] / comp_times[i] : 0.0;
		(void)snprintf(str, sizeof(str), "regcomp '%s' per sec", stress_posix_regex[i].description);
		stress_metrics_set(args, i + 2, str, rate, STRESS_METRIC_HARMONIC_MEAN);
	}

	return EXIT_SUCCESS;
}

const stressor_info_t stress_regex_info = {
	.stressor = stress_regex,
	.classifier = CLASS_CPU,
	.help = help
};
#else

const stressor_info_t stress_regex_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_CPU,
	.help = help,
	.unimplemented_reason = "no POSIX regex support"
};
#endif
