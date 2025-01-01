/*
 * Copyright (C) 2022-2025 Colin Ian King.
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
#ifndef CORE_VECMATH_H
#define CORE_VECMATH_H

#include "core-arch.h"

/*
 *  Clang 5.0 is the lowest version of clang that
 *  can build this without issues (clang 4.0 seems
 *  to spend forever optimizing this and causes the build
 *  to never complete)
 */
#if defined(HAVE_COMPILER_CLANG) && \
    defined(__clang_major__) && \
    __clang_major__ < 5
#undef HAVE_VECMATH
#endif

/*
 *  gcc 5.x or earlier breaks on 128 bit vector maths on
 *  PPC64 for some reason with some flavours of the toolchain
 *  so disable this test for now
 */
#if (defined(STRESS_ARCH_PPC64) ||		\
     defined(STRESS_ARCH_PPC)) && 		\
    defined(HAVE_COMPILER_GCC_OR_MUSL) &&	\
    __GNUC__ < 6
#undef HAVE_VECMATH
#endif

#endif
