/*
 * Copyright (C) 2014-2019 Canonical, Ltd.
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

bool cpu_is_x86(void)
{
#if defined(HAVE_CPUID_H) &&	\
    defined(STRESS_X86) && 	\
    defined(HAVE_CPUID) &&	\
    NEED_GNUC(4,6,0)
	uint32_t eax, ebx, ecx, edx;

	/* Intel CPU? */
	__cpuid(0, eax, ebx, ecx, edx);
	if ((memcmp(&ebx, "Genu", 4) == 0) &&
	    (memcmp(&edx, "ineI", 4) == 0) &&
	    (memcmp(&ecx, "ntel", 4) == 0))
		return true;
	else
		return false;
#else
	return false;
#endif
}
