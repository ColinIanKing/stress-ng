/*
 * Copyright (C) 2016-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
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

#if defined(HAVE_SYS_QUEUE_H)
#include <sys/queue.h>
#endif

#define MIN_LIST_SIZE		(64)
#define MAX_LIST_SIZE		(1000000)
#define DEFAULT_LIST_SIZE	(5000)

struct list_entry;

typedef void (*stress_list_func)(const stress_args_t *args,
				 const size_t n,
				 struct list_entry *data);

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

#if defined(HAVE_SYS_QUEUE_H)

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
    defined(CIRCLEQ_REMOVE)
#define HAVE_SYS_QUEUE_CIRCLEQ
#endif

#if defined(LIST_ENTRY) &&		\
    defined(LIST_HEAD) &&		\
    defined(LIST_INIT) &&		\
    defined(LIST_INSERT_HEAD) &&	\
    defined(LIST_FOREACH) &&		\
    defined(LIST_EMPTY) &&		\
    defined(LIST_FIRST) &&		\
    defined(LIST_REMOVE)
#define HAVE_SYS_QUEUE_LIST
#endif

#if defined(SLIST_ENTRY) &&		\
    defined(SLIST_HEAD) &&		\
    defined(SLIST_INIT) &&		\
    defined(SLIST_INSERT_HEAD) &&	\
    defined(SLIST_FOREACH) &&		\
    defined(SLIST_EMPTY) &&		\
    defined(SLIST_REMOVE_HEAD)
#define HAVE_SYS_QUEUE_SLIST
#endif

#if defined(STAILQ_ENTRY) &&		\
    defined(STAILQ_HEAD) &&		\
    defined(STAILQ_INIT) &&		\
    defined(STAILQ_INSERT_TAIL) &&	\
    defined(STAILQ_FOREACH) &&		\
    defined(STAILQ_FIRST) &&		\
    defined(STAILQ_REMOVE)
#define HAVE_SYS_QUEUE_STAILQ
#endif

#if defined(TAILQ_ENTRY) &&		\
    defined(TAILQ_HEAD) &&		\
    defined(TAILQ_INIT) &&		\
    defined(TAILQ_INSERT_TAIL) &&	\
    defined(TAILQ_FOREACH) &&		\
    defined(TAILQ_FIRST) &&		\
    defined(TAILQ_REMOVE)
#define HAVE_SYS_QUEUE_TAILQ
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

#if defined(HAVE_SYS_QUEUE_CIRCLEQ)
CIRCLEQ_HEAD(circleqhead, list_entry);
#endif
#if defined(HAVE_SYS_QUEUE_LIST)
LIST_HEAD(listhead, list_entry);
#endif
#if defined(HAVE_SYS_QUEUE_SLIST)
SLIST_HEAD(slisthead, list_entry);
#endif
#if defined(HAVE_SYS_QUEUE_STAILQ)
STAILQ_HEAD(stailhead, list_entry);
#endif
#if defined(HAVE_SYS_QUEUE_TAILQ)
TAILQ_HEAD(tailhead, list_entry);
#endif

#endif

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

#if defined(HAVE_SYS_QUEUE_H)

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

static void OPTIMIZE3 stress_list_slistt(
	const stress_args_t *args,
	const size_t n,
	struct list_entry *data)
{
	size_t i;
	register struct list_entry *entry, *head, *tail;
	bool found = false;

	entry = data;
	head = entry;
	tail = entry;
	entry++;
	for (i = 1; i < n; i++, entry++) {
		tail->u.next = entry;
		tail = entry;
	}

	for (entry = head, i = 0; i < n; i++, entry++) {
		struct list_entry *find;

		for (find = head; find; find = find->u.next) {
			if (find == entry) {
				found = true;
				break;
			}
		}

		if (!found)
			pr_fail("%s: slistt entry #%zd not found\n",
				args->name, i);
	}
	while (head) {
		register struct list_entry *next = head->u.next;

		head->u.next = NULL;
		head = next;
	}
}

#if defined(HAVE_SYS_QUEUE_LIST)
static void stress_list_list(
	const stress_args_t *args,
	const size_t n,
	struct list_entry *data)
{
	size_t i;
	struct list_entry *entry;
	struct listhead head;
	bool found = false;

	LIST_INIT(&head);

	for (entry = data, i = 0; i < n; i++, entry++) {
		LIST_INSERT_HEAD(&head, entry, u.list_entries);
	}

	for (entry = data, i = 0; i < n; i++, entry++) {
		struct list_entry *find;

		LIST_FOREACH(find, &head, u.list_entries) {
			if (find == entry) {
				found = true;
				break;
			}
		}

		if (!found)
			pr_fail("%s: list entry #%zd not found\n",
				args->name, i);
	}
	while (!LIST_EMPTY(&head)) {
		entry = LIST_FIRST(&head);
		LIST_REMOVE(entry, u.list_entries);
	}
	LIST_INIT(&head);
}
#endif

#if defined(HAVE_SYS_QUEUE_SLIST)
static void stress_list_slist(
	const stress_args_t *args,
	const size_t n,
	struct list_entry *data)
{
	size_t i;
	struct list_entry *entry;
	struct slisthead head;
	bool found = false;

	SLIST_INIT(&head);

	for (entry = data, i = 0; i < n; i++, entry++) {
		SLIST_INSERT_HEAD(&head, entry, u.slist_entries);
	}

	for (entry = data, i = 0; i < n; i++, entry++) {
		struct list_entry *find;

		SLIST_FOREACH(find, &head, u.slist_entries) {
			if (find == entry) {
				found = true;
				break;
			}
		}

		if (!found)
			pr_fail("%s: slist entry #%zd not found\n",
				args->name, i);
	}
	while (!SLIST_EMPTY(&head)) {
		SLIST_REMOVE_HEAD(&head, u.slist_entries);
	}
	SLIST_INIT(&head);
}
#endif

#if defined(HAVE_SYS_QUEUE_CIRCLEQ)
static void stress_list_circleq(
	const stress_args_t *args,
	const size_t n,
	struct list_entry *data)
{
	size_t i;
	struct list_entry *entry;
	struct circleqhead head;
	bool found = false;

	CIRCLEQ_INIT(&head);

	for (entry = data, i = 0; i < n; i++, entry++) {
		CIRCLEQ_INSERT_TAIL(&head, entry, u.circleq_entries);
	}

	for (entry = data, i = 0; i < n; i++, entry++) {
		struct list_entry *find;

		CIRCLEQ_FOREACH(find, &head, u.circleq_entries) {
			if (find == entry) {
				found = true;
				break;
			}
		}

		if (!found)
			pr_fail("%s: circleq entry #%zd not found\n",
				args->name, i);
	}
	while ((entry = CIRCLEQ_FIRST(&head)) != (struct list_entry *)&head) {
		CIRCLEQ_REMOVE(&head, entry, u.circleq_entries);
	}
	CIRCLEQ_INIT(&head);
}
#endif

#if defined(HAVE_SYS_QUEUE_STAILQ)
static void stress_list_stailq(
	const stress_args_t *args,
	const size_t n,
	struct list_entry *data)
{
	size_t i;
	struct list_entry *entry;
	struct stailhead head;
	bool found = false;

	STAILQ_INIT(&head);

	for (entry = data, i = 0; i < n; i++, entry++) {
		STAILQ_INSERT_TAIL(&head, entry, u.stailq_entries);
	}

	for (entry = data, i = 0; i < n; i++, entry++) {
		struct list_entry *find;

		STAILQ_FOREACH(find, &head, u.stailq_entries) {
			if (find == entry) {
				found = true;
				break;
			}
		}

		if (!found)
			pr_fail("%s: stailq entry #%zd not found\n",
				args->name, i);
	}
	while ((entry = STAILQ_FIRST(&head)) != NULL) {
		STAILQ_REMOVE(&head, entry, list_entry, u.stailq_entries);
	}
	STAILQ_INIT(&head);
}
#endif

#if defined(HAVE_SYS_QUEUE_TAILQ)
static void stress_list_tailq(
	const stress_args_t *args,
	const size_t n,
	struct list_entry *data)
{
	size_t i;
	struct list_entry *entry;
	struct tailhead head;
	bool found = false;

	TAILQ_INIT(&head);

	for (entry = data, i = 0; i < n; i++, entry++) {
		TAILQ_INSERT_TAIL(&head, entry, u.tailq_entries);
	}

	for (entry = data, i = 0; i < n; i++, entry++) {
		struct list_entry *find;

		TAILQ_FOREACH(find, &head, u.tailq_entries) {
			if (find == entry) {
				found = true;
				break;
			}
		}

		if (!found)
			pr_fail("%s: tailq entry #%zd not found\n",
				args->name, i);
	}
	while ((entry = TAILQ_FIRST(&head)) != NULL) {
		TAILQ_REMOVE(&head, entry, u.tailq_entries);
	}
	TAILQ_INIT(&head);
}
#endif

static void stress_list_all(
	const stress_args_t *args,
	const size_t n,
	struct list_entry *data)
{
	(void)args;
	(void)n;
	(void)data;
#if defined(HAVE_SYS_QUEUE_CIRCLEQ)
	stress_list_circleq(args, n, data);
#endif
#if defined(HAVE_SYS_QUEUE_LIST)
	stress_list_list(args, n, data);
#endif
#if defined(HAVE_SYS_QUEUE_SLIST)
	stress_list_slist(args, n, data);
#endif
	stress_list_slistt(args, n, data);
#if defined(HAVE_SYS_QUEUE_STAILQ)
	stress_list_stailq(args, n, data);
#endif
#if defined(HAVE_SYS_QUEUE_TAILQ)
	stress_list_tailq(args, n, data);
#endif
}
#endif

/*
 * Table of list stress methods
 */
static const stress_list_method_info_t list_methods[] = {
#if defined(HAVE_SYS_QUEUE_H)
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
#endif
	{ NULL,		NULL },
};

/*
 *  stress_set_list_method()
 *	set the default funccal stress method
 */
static int stress_set_list_method(const char *name)
{
	stress_list_method_info_t const *info;

	for (info = list_methods; info->func; info++) {
		if (!strcmp(info->name, name)) {
			stress_set_setting("list-method", TYPE_ID_UINTPTR_T, &info);
			return 0;
		}
	}

	(void)fprintf(stderr, "list-method must be one of:");
	for (info = list_methods; info->func; info++) {
		(void)fprintf(stderr, " %s", info->name);
	}
	(void)fprintf(stderr, "\n");

	return -1;
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_list_method,	stress_set_list_method },
	{ OPT_list_size,	stress_set_list_size },
	{ 0,			NULL }
};

#if defined(HAVE_SYS_QUEUE_H)
/*
 *  Rotate right a 64 bit value, compiler
 *  optimizes this down to a rotate and store
 */
static inline uint64_t ror64(const uint64_t val)
{
	register uint64_t tmp = val;
	register const uint64_t bit0 = (tmp & 1) << 63;

	tmp >>= 1;
	return (tmp | bit0);
}

/*
 *  stress_list()
 *	stress list
 */
static int stress_list(const stress_args_t *args)
{
	uint64_t v, list_size = DEFAULT_LIST_SIZE;
	struct list_entry *entries, *entry;
	size_t n, i, bit;
	struct sigaction old_action;
	int ret;
	stress_list_method_info_t const *info = &list_methods[0];

	(void)stress_get_setting("list-method", &info);

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

	if (stress_sighandler(args->name, SIGALRM, stress_list_handler, &old_action) < 0) {
		free(entries);
		return EXIT_FAILURE;
	}

	ret = sigsetjmp(jmp_env, 1);
	if (ret) {
		/*
		 * We return here if SIGALRM jmp'd back
		 */
		(void)stress_sigrestore(args->name, SIGALRM, &old_action);
		goto tidy;
	}

	v = 0;
	for (entry = entries, i = 0, bit = 0; i < n; i++, entry++) {
		if (!bit) {
			v = stress_mwc64();
			bit = 1;
		} else {
			v ^= bit;
			bit <<= 1;
		}
		entry->value = v;
		v = ror64(v);
	}

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		uint64_t rnd;

		info->func(args, n, entries);

		rnd = stress_mwc64();
		for (entry = entries, i = 0; i < n; i++, entry++)
			entry->value = ror64(entry->value ^ rnd);

		inc_counter(args);
	} while (keep_stressing(args));

	do_jmp = false;
	(void)stress_sigrestore(args->name, SIGALRM, &old_action);
tidy:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	free(entries);

	return EXIT_SUCCESS;
}

stressor_info_t stress_list_info = {
	.stressor = stress_list,
	.class = CLASS_CPU_CACHE | CLASS_CPU | CLASS_MEMORY,
	.opt_set_funcs = opt_set_funcs,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
stressor_info_t stress_list_info = {
	.stressor = stress_unimplemented,
	.class = CLASS_CPU_CACHE | CLASS_CPU | CLASS_MEMORY,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
#endif
