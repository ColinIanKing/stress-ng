/*
 * Copyright (C) 2023      Colin Ian King.
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
#ifndef CORE_ASM_PPC64_H
#define CORE_ASM_PPC64_H

#if defined(STRESS_ARCH_PPC64)

#if defined(HAVE_ASM_PPC64_DARN)
static inline uint64_t stress_asm_ppc64_darn(void)
{
	uint64_t val;

	/* Unconditioned raw deliver a raw number */
	__asm__ __volatile__("darn %0, 0\n" : "=r"(val) :);
	return val;
}
#endif

/* #if defined(STRESS_ARCH_PPC64) */
#endif

/* #ifndef CORE_ASM_PPC64_H */
#endif
