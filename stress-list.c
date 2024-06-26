/*
 * Copyright (C) 2016-2021 Canonical, Ltd.
 * Copyright (C) 2022-2024 Colin Ian King.
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

#if defined(HAVE_SYS_QUEUE_H)
#include <sys/queue.h>
#endif

#define MIN_LIST_SIZE		(64)
#define MAX_LIST_SIZE		(1000000)
#define DEFAULT_LIST_SIZE	(5000)

struct list_entry;

typedef int (*stress_list_func)(stress_args_t *args,
				struct list_entry *entries,
				const struct list_entry *entries_end,
				stress_metrics_t *metrics);

typedef struct {
	const char              *name;  /* human readable form of stressor */
	const stress_list_func   func;	/* the list method function */
} stress_list_method_info_t;

static const stress_list_method_info_t list_methods[];

static const stress_help_t help[] = {
	{ NULL,	"list N",	 "start N workers that exercise list structures" },
	{ NULL,	"list-method M", "select list method: all, circleq, list, slist, slistt, stailq, tailq" },
	{ NULL,	"list-ops N",	 "stop after N bogo list operations" },
	{ NULL,	"list-size N",	 "N is the number of items in the list" },
	{ NULL,	NULL,		 NULL }
};

static volatile bool do_jmp = true;
static sigjmp_buf jmp_env;

/*
 *  Check if macros are defined from sys/queue.h
 *  before attempting to use them.
 */
#if defined(CIRCLEQ_ENTRY) && 		\
    defined(CIRCLEQ_HEAD) &&		\
    defined(CIRCLEQ_INIT) &&		\
    defined(CIRCLEQ_INSERT_TAIL) &&	\
    defined(CIRCLEQ_FOREACH) &&		\
    defined(CIRCLEQ_FIRST) &&		\
    defined(CIRCLEQ_REMOVE) &&		\
    defined(HAVE_SYS_QUEUE_H)
#define HAVE_SYS_QUEUE_CIRCLEQ
CIRCLEQ_HEAD(circleqhead, list_entry);
#endif

#if defined(LIST_ENTRY) &&		\
    defined(LIST_HEAD) &&		\
    defined(LIST_INIT) &&		\
    defined(LIST_INSERT_HEAD) &&	\
    defined(LIST_FOREACH) &&		\
    defined(LIST_EMPTY) &&		\
    defined(LIST_FIRST) &&		\
    defined(LIST_REMOVE) &&		\
    defined(HAVE_SYS_QUEUE_H)
#define HAVE_SYS_QUEUE_LIST
LIST_HEAD(listhead, list_entry);
#endif

#if defined(SLIST_ENTRY) &&		\
    defined(SLIST_HEAD) &&		\
    defined(SLIST_INIT) &&		\
    defined(SLIST_INSERT_HEAD) &&	\
    defined(SLIST_FOREACH) &&		\
    defined(SLIST_EMPTY) &&		\
    defined(SLIST_REMOVE_HEAD) &&	\
    defined(HAVE_SYS_QUEUE_H)
#define HAVE_SYS_QUEUE_SLIST
SLIST_HEAD(slisthead, list_entry);
#endif

#if defined(STAILQ_ENTRY) &&		\
    defined(STAILQ_HEAD) &&		\
    defined(STAILQ_INIT) &&		\
    defined(STAILQ_INSERT_TAIL) &&	\
    defined(STAILQ_FOREACH) &&		\
    defined(STAILQ_FIRST) &&		\
    defined(STAILQ_REMOVE) &&		\
    defined(HAVE_SYS_QUEUE_H)
#define HAVE_SYS_QUEUE_STAILQ
STAILQ_HEAD(stailhead, list_entry);
#endif

#if defined(TAILQ_ENTRY) &&		\
    defined(TAILQ_HEAD) &&		\
    defined(TAILQ_INIT) &&		\
    defined(TAILQ_INSERT_TAIL) &&	\
    defined(TAILQ_FOREACH) &&		\
    defined(TAILQ_FIRST) &&		\
    defined(TAILQ_REMOVE) &&		\
    defined(HAVE_SYS_QUEUE_H)
#define HAVE_SYS_QUEUE_TAILQ
TAILQ_HEAD(tailhead, list_entry);
#endif

struct list_entry {
	uint64_t value;
	union {
#if defined(HAVE_SYS_QUEUE_CIRCLEQ)
		CIRCLEQ_ENTRY(list_entry) circleq_entries;
#endif
#if defined(HAVE_SYS_QUEUE_LIST)
		LIST_ENTRY(list_entry) list_entries;
#endif
#if defined(HAVE_SYS_QUEUE_SLIST)
		SLIST_ENTRY(list_entry) slist_entries;
#endif
#if defined(HAVE_SYS_QUEUE_STAILQ)
		STAILQ_ENTRY(list_entry) stailq_entries;
#endif
#if defined(HAVE_SYS_QUEUE_TAILQ)
		TAILQ_ENTRY(list_entry) tailq_entries;
#endif
		struct list_entry *next;
	} u;
};

/*
 *  stress_set_list_size()
 *	set list size
 */
static int stress_set_list_size(const char *opt)
{
	uint64_t list_size;

	list_size = stress_get_uint64(opt);
	stress_check_range("list-size", list_size,
		MIN_LIST_SIZE, MAX_LIST_SIZE);
	return stress_set_setting("list-size", TYPE_ID_UINT64, &list_size);
}

/*
 *  stress_list_handler()
 *	SIGALRM generic handler
 */
static void MLOCKED_TEXT stress_list_handler(int signum)
{
	(void)signum;

	if (do_jmp) {
		do_jmp = false;
		siglongjmp(jmp_env, 1);		/* Ugly, bounce back */
	}
}

static int OPTIMIZE3 stress_list_slistt(
	stress_args_t *args,
	struct list_entry *entries,
	const struct list_entry *entries_end,
	stress_metrics_t *metrics)
{
	register struct list_entry *entry, *head, *tail;
	bool found = false;
	double t;
	int rc = EXIT_SUCCESS;

	head = entries;
	tail = entries;
	for (entry = entries + 1; entry < entries_end; entry++) {
		tail->u.next = entry;
		tail = entry;
	}

	t = stress_time_now();
	for (entry = head; entry < entries_end; entry++) {
		register struct list_entry *find;

		for (find = head; find; find = find->u.next) {
			if (UNLIKELY(find == entry)) {
				found = true;
				break;
			}
		}

		if (UNLIKELY(!found)) {
			pr_fail("%s: slistt entry #%zd not found\n",
				args->name, entry - entries);
			rc = EXIT_FAILURE;
			break;
		}
	}
	metrics->duration += stress_time_now() - t;
	metrics->count += (double)(entry - entries);

	while (head) {
		register struct list_entry *next = head->u.next;

		head->u.next = NULL;
		head = next;
	}
	return rc;
}

#if defined(HAVE_SYS_QUEUE_LIST)
static int OPTIMIZE3 stress_list_list(
	stress_args_t *args,
	struct list_entry *entries,
	const struct list_entry *entries_end,
	stress_metrics_t *metrics)
{
	register struct list_entry *entry;
	struct listhead head;
	bool found = false;
	double t;
	int rc = EXIT_SUCCESS;

	(void)shim_memset(&head, 0, sizeof(head));
	LIST_INIT(&head);

	for (entry = entries; entry < entries_end; entry++) {
		LIST_INSERT_HEAD(&head, entry, u.list_entries);
	}

	t = stress_time_now();
	for (entry = entries; entry < entries_end; entry++) {
		register struct list_entry *find;

		LIST_FOREACH(find, &head, u.list_entries) {
			if (UNLIKELY(find == entry)) {
				found = true;
				break;
			}
		}

		if (UNLIKELY(!found)) {
			pr_fail("%s: list entry #%zd not found\n",
				args->name, entry - entries);
			rc = EXIT_FAILURE;
			break;
		}
	}
	metrics->duration += stress_time_now() - t;
	metrics->count += (double)(entry - entries);

	while (!LIST_EMPTY(&head)) {
		entry = (struct list_entry *)LIST_FIRST(&head);
		LIST_REMOVE(entry, u.list_entries);
	}
	LIST_INIT(&head);

	return rc;
}
#endif

#if defined(HAVE_SYS_QUEUE_SLIST)
static int OPTIMIZE3 stress_list_slist(
	stress_args_t *args,
	struct list_entry *entries,
	const struct list_entry *entries_end,
	stress_metrics_t *metrics)
{
	register struct list_entry *entry;
	struct slisthead head;
	bool found = false;
	double t;
	int rc = EXIT_SUCCESS;

	(void)shim_memset(&head, 0, sizeof(head));
	SLIST_INIT(&head);

	for (entry = entries; entry < entries_end; entry++) {
		SLIST_INSERT_HEAD(&head, entry, u.slist_entries);
	}

	t = stress_time_now();
	for (entry = entries; entry < entries_end; entry++) {
		register struct list_entry *find;

		SLIST_FOREACH(find, &head, u.slist_entries) {
			if (UNLIKELY(find == entry)) {
				found = true;
				break;
			}
		}

		if (UNLIKELY(!found)) {
			pr_fail("%s: slist entry #%zd not found\n",
				args->name, entry - entries);
			rc = EXIT_FAILURE;
			break;
		}
	}
	metrics->duration += stress_time_now() - t;
	metrics->count += (double)(entry - entries);

	while (!SLIST_EMPTY(&head)) {
		SLIST_REMOVE_HEAD(&head, u.slist_entries);
	}
	SLIST_INIT(&head);

	return rc;
}
#endif

#if defined(HAVE_SYS_QUEUE_CIRCLEQ)
static int OPTIMIZE3 stress_list_circleq(
	stress_args_t *args,
	struct list_entry *entries,
	const struct list_entry *entries_end,
	stress_metrics_t *metrics)
{
	register struct list_entry *entry;
	struct circleqhead head;
	bool found = false;
	double t;
	int rc = EXIT_SUCCESS;

	(void)shim_memset(&head, 0, sizeof(head));
	CIRCLEQ_INIT(&head);

	for (entry = entries; entry < entries_end; entry++) {
		CIRCLEQ_INSERT_TAIL(&head, entry, u.circleq_entries);
	}

	t = stress_time_now();
	for (entry = entries; entry < entries_end; entry++) {
		register struct list_entry *find;

		CIRCLEQ_FOREACH(find, &head, u.circleq_entries) {
			if (UNLIKELY(find == entry)) {
				found = true;
				break;
			}
		}

		if (UNLIKELY(!found)) {
			pr_fail("%s: circleq entry #%zd not found\n",
				args->name, entry - entries);
			rc = EXIT_FAILURE;
			break;
		}
	}
	metrics->duration += stress_time_now() - t;
	metrics->count += (double)(entry - entries);

	while ((entry = (struct list_entry *)CIRCLEQ_FIRST(&head)) != (struct list_entry *)&head) {
		CIRCLEQ_REMOVE(&head, entry, u.circleq_entries);
	}
	CIRCLEQ_INIT(&head);

	return rc;
}
#endif

#if defined(HAVE_SYS_QUEUE_STAILQ)
static int OPTIMIZE3 stress_list_stailq(
	stress_args_t *args,
	struct list_entry *entries,
	const struct list_entry *entries_end,
	stress_metrics_t *metrics)
{
	register struct list_entry *entry;
	struct stailhead head;
	bool found = false;
	double t;
	int rc = EXIT_SUCCESS;

	(void)shim_memset(&head, 0, sizeof(head));
	STAILQ_INIT(&head);

	for (entry = entries; entry < entries_end; entry++) {
		STAILQ_INSERT_TAIL(&head, entry, u.stailq_entries);
	}

	t = stress_time_now();
	for (entry = entries; entry < entries_end; entry++) {
		register struct list_entry *find;

		STAILQ_FOREACH(find, &head, u.stailq_entries) {
			if (UNLIKELY(find == entry)) {
				found = true;
				break;
			}
		}

		if (UNLIKELY(!found)) {
			pr_fail("%s: stailq entry #%zd not found\n",
				args->name, entry - entries);
			rc = EXIT_FAILURE;
			break;
		}
	}
	metrics->duration += stress_time_now() - t;
	metrics->count += (double)(entry - entries);

	while ((entry = (struct list_entry *)STAILQ_FIRST(&head)) != NULL) {
		STAILQ_REMOVE(&head, entry, list_entry, u.stailq_entries);
	}
	STAILQ_INIT(&head);

	return rc;
}
#endif

#if defined(HAVE_SYS_QUEUE_TAILQ)
static int OPTIMIZE3 stress_list_tailq(
	stress_args_t *args,
	struct list_entry *entries,
	const struct list_entry *entries_end,
	stress_metrics_t *metrics)
{
	register struct list_entry *entry;
	struct tailhead head;
	bool found = false;
	double t;
	int rc = EXIT_SUCCESS;

	(void)shim_memset(&head, 0, sizeof(head));
	TAILQ_INIT(&head);

	for (entry = entries; entry < entries_end; entry++) {
		TAILQ_INSERT_TAIL(&head, entry, u.tailq_entries);
	}

	t = stress_time_now();
	for (entry = entries; entry < entries_end; entry++) {
		register struct list_entry *find;

		TAILQ_FOREACH(find, &head, u.tailq_entries) {
			if (UNLIKELY(find == entry)) {
				found = true;
				break;
			}
		}

		if (!found) {
			pr_fail("%s: tailq entry #%zd not found\n",
				args->name, entry - entries);
			rc = EXIT_FAILURE;
			break;
		}
	}
	metrics->duration += stress_time_now() - t;
	metrics->count += (double)(entry - entries);

	while ((entry = (struct list_entry *)TAILQ_FIRST(&head)) != NULL) {
		TAILQ_REMOVE(&head, entry, u.tailq_entries);
	}
	TAILQ_INIT(&head);

	return rc;
}
#endif

static int stress_list_all(
	stress_args_t *args,
	struct list_entry *entries,
	const struct list_entry *entries_end,
	stress_metrics_t *metrics);


/*
 * Table of list stress methods
 */
static const stress_list_method_info_t list_methods[] = {
	{ "all",	stress_list_all },
#if defined(HAVE_SYS_QUEUE_CIRCLEQ)
	{ "circleq",	stress_list_circleq },
#endif
#if defined(HAVE_SYS_QUEUE_LIST)
	{ "list",	stress_list_list },
#endif
#if defined(HAVE_SYS_QUEUE_SLIST)
	{ "slist",	stress_list_slist },
#endif
	{ "slistt",	stress_list_slistt },
#if defined(HAVE_SYS_QUEUE_STAILQ)
	{ "stailq",	stress_list_stailq },
#endif
#if defined(HAVE_SYS_QUEUE_TAILQ)
	{ "tailq",	stress_list_tailq },
#endif
};

static int stress_list_all(
	stress_args_t *args,
	struct list_entry *entries,
	const struct list_entry *entries_end,
	stress_metrics_t *metrics)
{
	static size_t idx = 1;
	int rc;

	rc = list_methods[idx].func(args, entries, entries_end, &metrics[idx]);
	idx++;
	if (idx >= SIZEOF_ARRAY(list_methods))
		idx = 1;

	return rc;
}

/*
 *  stress_set_list_method()
 *	set the default funccal stress method
 */
static int stress_set_list_method(const char *name)
{
	size_t i;

	for (i = 0; i < SIZEOF_ARRAY(list_methods); i++) {
		if (!strcmp(list_methods[i].name, name)) {
			stress_set_setting("list-method", TYPE_ID_SIZE_T, &i);
			return 0;
		}
	}

	(void)fprintf(stderr, "list-method must be one of:");
	for (i = 0; i < SIZEOF_ARRAY(list_methods); i++) {
		(void)fprintf(stderr, " %s", list_methods[i].name);
	}
	(void)fprintf(stderr, "\n");

	return -1;
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_list_method,	stress_set_list_method },
	{ OPT_list_size,	stress_set_list_size },
	{ 0,			NULL }
};

/*
 *  stress_list()
 *	stress list
 */
static int stress_list(stress_args_t *args)
{
	uint64_t v, list_size = DEFAULT_LIST_SIZE;
	struct list_entry *entries, *entry, *entries_end;
	size_t n, i, j, bit, list_method = 0;
	struct sigaction old_action;
	int ret;
	NOCLOBBER int rc = EXIT_SUCCESS;
	stress_metrics_t *metrics, list_metrics[SIZEOF_ARRAY(list_methods)];
	stress_list_func func;

	for (i = 0; i < SIZEOF_ARRAY(list_metrics); i++) {
		list_metrics[i].duration = 0.0;
		list_metrics[i].count = 0.0;
	}

	(void)stress_get_setting("list-method", &list_method);
	func = list_methods[list_method].func;
	metrics = &list_metrics[list_method];

	if (!stress_get_setting("list-size", &list_size)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			list_size = MAX_LIST_SIZE;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			list_size = MIN_LIST_SIZE;
	}
	n = (size_t)list_size;

	entries = calloc(n, sizeof(*entries));
	if (!entries) {
		pr_inf_skip("%s: malloc failed allocating %zu list entries, "
			"out of memory, skipping stressor\n", args->name, n);
		return EXIT_NO_RESOURCE;
	}
	entries_end = entries + n;

	ret = sigsetjmp(jmp_env, 1);
	if (ret) {
		/*
		 * We return here if SIGALRM jmp'd back
		 */
		(void)stress_sigrestore(args->name, SIGALRM, &old_action);
		goto tidy;
	}
	if (stress_sighandler(args->name, SIGALRM, stress_list_handler, &old_action) < 0) {
		free(entries);
		return EXIT_FAILURE;
	}

	v = 0;
	for (entry = entries, bit = 0; entry < entries_end; entry++) {
		if (!bit) {
			v = stress_mwc64();
			bit = 1;
		} else {
			v ^= bit;
			bit <<= 1;
		}
		entry->value = v;
		v = shim_ror64(v);
	}

	stress_set_proc_state(args->name, STRESS_STATE_RUN);
	stress_sync_start_wait(args);

	do {
		uint64_t rnd;

		if (func(args, entries, entries_end, metrics) == EXIT_FAILURE) {
			rc = EXIT_FAILURE;
			break;
		}

		rnd = stress_mwc64();
		for (entry = entries; entry < entries_end; entry++) {
			register uint64_t value = entry->value ^ rnd;

			entry->value = shim_ror64(value);
		}

		stress_bogo_inc(args);
	} while (stress_continue(args));

	do_jmp = false;
	(void)stress_sigrestore(args->name, SIGALRM, &old_action);
tidy:
	for (i = 0, j = 0; i < SIZEOF_ARRAY(list_metrics); i++) {
		if ((list_metrics[i].duration > 0.0) && (list_metrics[i].count > 0.0)) {
			char msg[64];
			const double rate = list_metrics[i].count / list_metrics[i].duration;

			(void)snprintf(msg, sizeof(msg), "%s searches per second", list_methods[i].name);
			stress_metrics_set(args, j, msg,
				rate, STRESS_METRIC_HARMONIC_MEAN);
			j++;
		}
	}

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	free(entries);

	return rc;
}

stressor_info_t stress_list_info = {
	.stressor = stress_list,
	.class = CLASS_CPU_CACHE | CLASS_CPU | CLASS_MEMORY | CLASS_SEARCH,
	.opt_set_funcs = opt_set_funcs,
	.verify = VERIFY_ALWAYS,
	.help = help
};
