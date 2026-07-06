/*
 * Copyright (C) 2026      SpacemiT, Ltd.
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

/*
 *  Check if the assembler knows the cbo.zero, cbo.clean and cbo.flush
 *  mnemonics. The ".option arch" directive turns on the extensions just
 *  for this code, so the global -march does not need them.
 *  Old assemblers fail this check and stress-ng uses the raw .4byte encoding.
 */

static void cbo_zero(char *addr)
{
	__asm__ __volatile__(
	".option push\n"
	".option arch, +zicboz\n"
	"cbo.zero (%0)\n"
	".option pop\n"
	: : "r" (addr) : "memory");
}

static void cbo_clean(const void *addr)
{
	__asm__ __volatile__(
	".option push\n"
	".option arch, +zicbom\n"
	"cbo.clean (%0)\n"
	".option pop\n"
	: : "r" (addr) : "memory");
}

static void cbo_flush(const void *addr)
{
	__asm__ __volatile__(
	".option push\n"
	".option arch, +zicbom\n"
	"cbo.flush (%0)\n"
	".option pop\n"
	: : "r" (addr) : "memory");
}

static char mem[64] __attribute__((aligned(64)));

#if defined(__riscv) || \
    defined(__riscv__)
int main(void)
{
	cbo_zero(mem);
	cbo_clean(mem);
	cbo_flush(mem);

	return 0;
}
#else
#error not RISC-V so no cache block operation instructions
#endif
