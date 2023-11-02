// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2022-2023 Colin Ian King.
 *
 */
#include "stress-ng.h"
#include "core-arch.h"
#include "core-asm-ret.h"

#if defined(__BYTE_ORDER__) &&	\
    defined(__ORDER_LITTLE_ENDIAN__)
#if __BYTE_ORDER__  == __ORDER_LITTLE_ENDIAN__
#define STRESS_ARCH_LE
#endif
#endif

#if defined(__BYTE_ORDER__) &&	\
    defined(__ORDER_BIG_ENDIAN__)
#if __BYTE_ORDER__  == __ORDER_BIG_ENDIAN__
#define STRESS_ARCH_BE
#endif
#endif

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
	char tmp[64];

	if (stress_ret_opcode.len > 0)
		return 0;
	stress_munge_underscore(tmp, name, sizeof(tmp));
	pr_inf("%s: architecture not supported\n", tmp);
	return -1;
}
