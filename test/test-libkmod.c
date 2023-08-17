// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */
#define _GNU_SOURCE

#include <unistd.h>
#include <libkmod.h>

int main(void)
{
	struct kmod_list *l, *list = NULL;
	struct kmod_ctx *ctx;
	int ret;

	ctx = kmod_new(NULL, NULL);
	if (!ctx)
		return -1;
	ret = kmod_module_new_from_lookup(ctx, "snd", &list);
	if (ret < 0)
	return -1;

	kmod_list_foreach(l, list) {
		struct kmod_module *mod = kmod_module_get_module(l);
		const char *module_name = kmod_module_get_name(mod);

		(void)module_name;

		ret = kmod_module_get_initstate(mod);
		(void)ret;
		ret = kmod_module_get_refcnt(mod);
		(void)ret;
	}

	kmod_module_unref_list(list);

	return 0;
}
