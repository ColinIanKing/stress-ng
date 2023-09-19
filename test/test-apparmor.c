// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
 *
 */
#include <unistd.h>
#include <sys/apparmor.h>

int main(void)
{
	int ret;
	aa_kernel_interface *kern_if;

	ret = aa_is_enabled();
	if (ret < 0)
		return ret;

	ret = aa_kernel_interface_new(&kern_if, NULL, NULL);
	if (ret < 0)
		return ret;

	ret = aa_kernel_interface_load_policy(kern_if, NULL, 0);
	if (ret < 0)
		return ret;

	ret = aa_kernel_interface_replace_policy(kern_if, NULL, 0);
	if (ret < 0)
		return ret;

	ret = aa_kernel_interface_remove_policy(kern_if, "dummy");
	if (ret < 0)
		return ret;

	aa_kernel_interface_unref(kern_if);

	return ret;
}
