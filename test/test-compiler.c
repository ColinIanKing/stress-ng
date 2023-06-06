/*
 * Copyright (C)      2023 Colin Ian King
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
#include <stdio.h>
#include <string.h>

int main(void)
{
/* Intel ICC compiler */
#if defined(__ICC) &&			\
    defined(__INTEL_COMPILER) &&	\
    !defined(HAVE_COMPILER)
	printf("HAVE_COMPILER_ICC\n");
	return 0;
#endif


/* Portable C compiler */
#if defined(__PCC__) &&			\
    !defined(HAVE_COMPILER)
	printf("HAVE_COMPILER_PCC\n");
	return 0;
#endif

/* Tiny C Compiler */
#if defined(__TINYC__) &&		\
    !defined(HAVE_COMPILER)
	printf("HAVE_COMPILER_TCC\n");
	return 0;
#endif

/* Intel ICX compiler */
#if defined(__clang__) && 		\
   (defined(__INTEL_CLANG_COMPILER) || defined(__INTEL_LLVM_COMPILER))
	printf("HAVE_COMPILER_ICX\n");
	return 0;
#endif

/* clang */
#if defined(__clang__) &&	\
    !defined(HAVE_COMPILER)
	printf("HAVE_COMPILER_CLANG\n");
	return 0;
#endif

/* GNU C compiler */
#if defined(__GNUC__) &&	\
    !defined(HAVE_COMPILER)
	printf("HAVE_COMPILER_GCC\n");
	return 0;
#endif
	printf("HAVE_COMPILER_UNKNOWN\n");
	return 0;
}
