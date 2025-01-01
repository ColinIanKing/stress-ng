/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2025 Colin Ian King
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
		const struct kmod_module *mod = kmod_module_get_module(l);
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
