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
#include "core-module.h"

#if defined(HAVE_LIBKMOD_H) &&	\
    defined(HAVE_LIB_KMOD) && 	\
    defined(__linux__)

#include <libkmod.h>

/*
 *  stress_module_load()
 *	load a linux kernel module
 */
int stress_module_load(
	const char *name,	/* name of stressor */
	const char *alias,	/* name of module */
	const char *options,	/* module options */
	bool *already_loaded)	/* set to true if already loaded */
{
	int ret;
	unsigned int flags = 0;
	struct kmod_ctx *ctx;
	struct kmod_list const *l;
	struct kmod_list *list = NULL;

	void (*show_func)(struct kmod_module *m, bool install,
			  const char *options) = NULL;

	*already_loaded = false;

	ctx = kmod_new(NULL, NULL);
	if (!ctx)
		return -1;
	ret = kmod_module_new_from_lookup(ctx, alias, &list);
	if (!list || ret < 0) {
		kmod_unref(ctx);
		return -1;
	}

	kmod_list_foreach(l, list) {
		struct kmod_module *mod = kmod_module_get_module(l);

		if (!mod)
			continue;
		ret = kmod_module_probe_insert_module(mod, flags, options,
			NULL, NULL, show_func);
		if (ret < 0) {
			if (errno == -EEXIST) {
				*already_loaded = true;
				kmod_unref(ctx);
				return 0;
			}
			pr_inf("%s: failed to load module %s, errno=%d (%s)\n",
				name, alias,
				errno, strerror(errno));
			ret = -1;
			break;
		}
		kmod_module_unref(mod);
	}

	kmod_module_unref_list(list);
	kmod_unref(ctx);

	return ret;
}

/*
 *  stress_module_unload_mod_and_deps()
 *	recursively unload module and dependencies
 */
static int stress_module_unload_mod_and_deps(struct kmod_module *mod)
{
	int ret;
	struct kmod_list *deps,	*l;

	ret = kmod_module_remove_module(mod, 0);

	deps = kmod_module_get_dependencies(mod);
	if (!deps)
		return ret;

	kmod_list_foreach(l, deps) {
		struct kmod_module *dep_mod = kmod_module_get_module(l);

		if (kmod_module_get_refcnt(dep_mod) == 0)
			stress_module_unload_mod_and_deps(dep_mod);

		kmod_module_unref_list(deps);
	}
	return ret;

}

/*
 *  stress_module_unload()
 *	unload a linux kernel module
 */
int stress_module_unload(
	const char *name,		/* name of stressor */
	const char *alias,		/* name of module */
	const bool already_loaded)	/* don't unload if true */
{
	int ret;
	struct kmod_list *l, *list = NULL;
	struct kmod_ctx *ctx;

	if (already_loaded)
		return 0;

	ctx = kmod_new(NULL, NULL);
	if (!ctx)
		return -1;
	ret = kmod_module_new_from_lookup(ctx, alias, &list);
	if (ret < 0) {
		kmod_unref(ctx);
		return -1;
	}
	if (!list) {
		pr_dbg("%s: module %s not found\n",
			name, name);
		kmod_unref(ctx);
		return -1;
	}

	kmod_list_foreach(l, list) {
		struct kmod_module *mod = kmod_module_get_module(l);
		const char *module_name = kmod_module_get_name(mod);

		if (!mod)
			continue;
		ret = kmod_module_get_initstate(mod);
		if (ret < 0)
			continue;
		if (ret == KMOD_MODULE_BUILTIN)
			continue;
		ret = kmod_module_get_refcnt(mod);
		if (ret > 0) {
			pr_dbg("%s: cannot unload %s, it is in use\n",
				name, module_name);
		}
		VOID_RET(int, stress_module_unload_mod_and_deps(mod));
	}

	kmod_module_unref_list(list);
	kmod_unref(ctx);

	return 0;
}

#else

int stress_module_load(
	const char *name,
	const char *alias,
	const char *options,
	bool *already_loaded)
{
	(void)alias;
	(void)options;
	(void)already_loaded;

	pr_dbg("%s: module loading not supported\n", name);

	return -1;
}

int stress_module_unload(
	const char *name,
	const char *alias,
	const bool already_loaded)
{
	(void)alias;
	(void)already_loaded;

	pr_dbg("%s: module unloading not supported\n", name);

	return -1;
}

UNEXPECTED

#endif
