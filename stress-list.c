/*
 * Copyright (C) 2016-2021 Canonical, Ltd.
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
	{ NULL,	"list-ops N",	 "stop after N bogo list operations" },
	{ NULL,	"list-method M", "select tlistmethod, all,circleq,insque,list,slist,stailq,tailq" },
	{ NULL,	"list-size N",	 "N is the number of items in the list" },
	{ NULL,	NULL,		 NULL }
};

#if defined(HAVE_SYS_QUEUE_H)

static volatile bool do_jmp = true;
static sigjmp_buf jmp_env;

struct list_entry {
	uint64_t value;
	union {
		CIRCLEQ_ENTRY(list_entry) circleq_entries;
		LIST_ENTRY(list_entry) list_entries;
		SLIST_ENTRY(list_entry) slist_entries;
		STAILQ_ENTRY(list_entry) stailq_entries;
		TAILQ_ENTRY(list_entry) tailq_entries;
		struct list_entry *next;
	} u;
};

LIST_HEAD(listhead, list_entry);
SLIST_HEAD(slisthead, list_entry);
CIRCLEQ_HEAD(circleqhead, list_entry);
STAILQ_HEAD(stailhead, list_entry);
TAILQ_HEAD(tailhead, list_entry);

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
			pr_err("%s: slistt entry #%zd not found\n",
				args->name, i);
	}
	while (head) {
		register struct list_entry *next = head->u.next;

		head->u.next = NULL;
		head = next;
	}
}


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
			pr_err("%s: list entry #%zd not found\n",
				args->name, i);
	}
	while (!LIST_EMPTY(&head)) {
		entry = LIST_FIRST(&head);
		LIST_REMOVE(entry, u.list_entries);
	}
	LIST_INIT(&head);
}


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
			pr_err("%s: slist entry #%zd not found\n",
				args->name, i);
	}
	while (!SLIST_EMPTY(&head)) {
		SLIST_REMOVE_HEAD(&head, u.slist_entries);
	}
	SLIST_INIT(&head);
}

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
			pr_err("%s: circleq entry #%zd not found\n",
				args->name, i);
	}
	while ((entry = CIRCLEQ_FIRST(&head)) != (struct list_entry *)&head) {
		CIRCLEQ_REMOVE(&head, entry, u.circleq_entries);
	}
	CIRCLEQ_INIT(&head);
}

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
			pr_err("%s: stailq entry #%zd not found\n",
				args->name, i);
	}
	while ((entry = STAILQ_FIRST(&head)) != NULL) {
		STAILQ_REMOVE(&head, entry, list_entry, u.stailq_entries);
	}
	STAILQ_INIT(&head);
}


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
			pr_err("%s: tailq entry #%zd not found\n",
				args->name, i);
	}
	while ((entry = TAILQ_FIRST(&head)) != NULL) {
		TAILQ_REMOVE(&head, entry, u.tailq_entries);
	}
	TAILQ_INIT(&head);
}

static void stress_list_all(
	const stress_args_t *args,
	const size_t n,
	struct list_entry *data)
{
	stress_list_circleq(args, n, data);
	stress_list_list(args, n, data);
	stress_list_slist(args, n, data);
	stress_list_stailq(args, n, data);
	stress_list_tailq(args, n, data);
}
#endif

/*
 * Table of list stress methods
 */
static const stress_list_method_info_t list_methods[] = {
#if defined(HAVE_SYS_QUEUE_H)
	{ "all",	stress_list_all },
	{ "circleq",	stress_list_circleq },
	{ "list",	stress_list_list },
	{ "slist",	stress_list_slist },
	{ "slistt",	stress_list_slistt },
	{ "stailq",	stress_list_stailq },
	{ "tailq",	stress_list_tailq },
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
	struct list_entry *entrys, *entry;
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

	if ((entrys = calloc(n, sizeof(struct list_entry))) == NULL) {
		pr_fail("%s: malloc failed, out of memory\n", args->name);
		return EXIT_NO_RESOURCE;
	}

	if (stress_sighandler(args->name, SIGALRM, stress_list_handler, &old_action) < 0) {
		free(entrys);
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
	for (entry = entrys, i = 0, bit = 0; i < n; i++, entry++) {
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

		info->func(args, n, entrys);

		rnd = stress_mwc64();
		for (entry = entrys, i = 0; i < n; i++, entry++)
			entry->value = ror64(entry->value ^ rnd);

		inc_counter(args);
	} while (keep_stressing(args));

	do_jmp = false;
	(void)stress_sigrestore(args->name, SIGALRM, &old_action);
tidy:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	free(entrys);

	return EXIT_SUCCESS;
}

stressor_info_t stress_list_info = {
	.stressor = stress_list,
	.class = CLASS_CPU_CACHE | CLASS_CPU | CLASS_MEMORY,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
#else
stressor_info_t stress_list_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_CPU_CACHE | CLASS_CPU | CLASS_MEMORY,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
#endif
