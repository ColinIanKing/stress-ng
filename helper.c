/*
 * Copyright (C) 2014 Canonical, Ltd.
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
#include <stdint.h>

/*
 *  force stress-float to think the doubles are actually
 *  being used - this avoids the float loop from being
 *  over optimised out per iteration.
 */
void double_put(double a, double b, double c, double d)
{
	(void)a;
	(void)b;
	(void)c;
	(void)d;
}

/*
 *  force stress-int to think the uint64_t args are actually
 *  being used - this avoids the integer loop from being
 *  over optimised out per iteration.
 */
void uint64_put(uint64_t a, uint64_t b)
{
	(void)a;
	(void)b;
}
