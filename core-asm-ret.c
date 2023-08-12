/*
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
#include "core-arch.h"
#include "core-asm-ret.h"

stress_ret_opcode_t stress_ret_opcode =
#if defined(STRESS_ARCH_ALPHA)
        { 4, 4, "ret", { 0x01, 0x80, 0xfa, 0x6b } };
#elif defined(STRESS_ARCH_ARM) && defined(__aarch64__)
	{ 4, 4, "ret", { 0xc0, 0x03, 0x5f, 0xd6 } };
#elif defined(STRESS_ARCH_HPPA)
	{ 8, 8, "bv,n r0(rp); nop", { 0xe8, 0x40, 0xc0, 0x02, 0x08, 0x00, 0x02, 0x40 } };
#elif defined(STRESS_ARCH_M68K)
	{ 2, 2, "rts", { 0x4e, 0x75 } };
#elif defined(STRESS_ARCH_MIPS) && defined(STRESS_ARCH_LE)
	{ 8, 8, "jr ra; nop", { 0x08, 0x00, 0xe0, 0x03, 0x00, 0x00, 0x00, 0x00 } };
#elif defined(STRESS_ARCH_MIPS) && defined(STRESS_ARCH_BE)
	{ 8, 8, "jr ra; nop", { 0x03, 0xe0, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00 } };
#elif defined(STRESS_ARCH_PPC64) && defined(STRESS_ARCH_LE)
	{ 8, 8, "blr; nop", { 0x20, 0x00, 0x80, 0x4e, 0x00, 0x00, 0x00, 0x60 } };
#elif defined(STRESS_ARCH_RISCV)
	{ 2, 2, "ret", { 0x82, 0x080 } };
#elif defined(STRESS_ARCH_S390)
	{ 2, 2, "br %r14", { 0x07, 0xfe } };
#elif defined(STRESS_ARCH_SH4)
	{ 4, 4, "rts; nop", { 0x0b, 0x00, 0x09, 0x00 } };
#elif defined(STRESS_ARCH_SPARC)
	{ 8, 8, "retl; add %o7, %l7, %l7", { 0x81, 0xc3, 0xe0, 0x08, 0xae, 0x03, 0xc0, 0x17 } };
#elif defined(STRESS_ARCH_X86)
	{ 1, 1, "ret", { 0xc3 } };
#else
	{ 0, 0, "", { 0x00 } };
#endif

int stress_asm_ret_supported(const char *name)
{
	if (stress_ret_opcode.len > 0)
		return 0;
	pr_inf("%s: architecture not supported\n", name);
	return -1;
}
