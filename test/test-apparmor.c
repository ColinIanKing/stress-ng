/*
 * Copyright (C) 2013-2017 Canonical, Ltd.
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
