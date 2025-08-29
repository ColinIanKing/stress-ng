/*
 * Copyright (C) 2022-2025 Colin Ian King
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
#ifndef CORE_TARGET_CLONES_H
#define CORE_TARGET_CLONES_H

#include "core-arch.h"

#if defined(HAVE_COMPILER_ICC)
#undef HAVE_TARGET_CLONES
#endif

/* GCC5.0+ target_clones attribute, x86 */
#if defined(STRESS_ARCH_X86) &&	\
    defined(HAVE_TARGET_CLONES)

#if defined(HAVE_TARGET_CLONES_MMX)
#define TARGET_CLONE_MMX	"mmx",
#define TARGET_CLONE_USE
#else
#define TARGET_CLONE_MMX
#endif

#if defined(HAVE_TARGET_CLONES_AVX)
#define TARGET_CLONE_AVX	"avx",
#define TARGET_CLONE_USE
#else
#define TARGET_CLONE_AVX
#endif

#if defined(HAVE_TARGET_CLONES_AVX2)
#define TARGET_CLONE_AVX2	"avx2",
#define TARGET_CLONE_USE
#else
#define TARGET_CLONE_AVX2
#endif

#if defined(HAVE_TARGET_CLONES_SSE)
#define TARGET_CLONE_SSE	"sse",
#define TARGET_CLONE_USE
#else
#define TARGET_CLONE_SSE
#endif

#if defined(HAVE_TARGET_CLONES_SSE2)
#define TARGET_CLONE_SSE2	"sse2",
#define TARGET_CLONE_USE
#else
#define TARGET_CLONE_SSE2
#endif

#if defined(HAVE_TARGET_CLONES_SSE3)
#define TARGET_CLONE_SSE3	"sse3",
#define TARGET_CLONE_USE
#else
#define TARGET_CLONE_SSE3
#endif

#if defined(HAVE_TARGET_CLONES_SSSE3)
#define TARGET_CLONE_SSSE3	"ssse3",
#define TARGET_CLONE_USE
#else
#define TARGET_CLONE_SSSE3
#endif

#if defined(HAVE_TARGET_CLONES_SSE4_1)
#define TARGET_CLONE_SSE4_1	"sse4.1",
#define TARGET_CLONE_USE
#else
#define TARGET_CLONE_SSE4_1
#endif

#if defined(HAVE_TARGET_CLONES_SSE4_2)
#define TARGET_CLONE_SSE4_2	"sse4.2",
#define TARGET_CLONE_USE
#else
#define TARGET_CLONE_SSE4_2
#endif

#if defined(HAVE_TARGET_CLONES_SKYLAKE_AVX512)
#define TARGET_CLONE_SKYLAKE_AVX512	"arch=skylake-avx512",
#define TARGET_CLONE_USE
#else
#define TARGET_CLONE_SKYLAKE_AVX512
#endif

#if defined(HAVE_TARGET_CLONES_COOPERLAKE)
#define TARGET_CLONE_COOPERLAKE	"arch=cooperlake",
#define TARGET_CLONE_USE
#else
#define TARGET_CLONE_COOPERLAKE
#endif

#if defined(HAVE_TARGET_CLONES_TIGERLAKE)
#define TARGET_CLONE_TIGERLAKE	"arch=tigerlake",
#define TARGET_CLONE_USE
#else
#define TARGET_CLONE_TIGERLAKE
#endif

#if defined(HAVE_TARGET_CLONES_SAPPHIRERAPIDS)
#define TARGET_CLONE_SAPPHIRERAPIDS "arch=sapphirerapids",
#define TARGET_CLONE_USE
#else
#define TARGET_CLONE_SAPPHIRERAPIDS
#endif

#if defined(HAVE_TARGET_CLONES_ALDERLAKE)
#define TARGET_CLONE_ALDERLAKE	"arch=alderlake",
#define TARGET_CLONE_USE
#else
#define TARGET_CLONE_ALDERLAKE
#endif

#if defined(HAVE_TARGET_CLONES_ROCKETLAKE)
#define TARGET_CLONE_ROCKETLAKE	"arch=rocketlake",
#define TARGET_CLONE_USE
#else
#define TARGET_CLONE_ROCKETLAKE
#endif

#if defined(HAVE_TARGET_CLONES_GRANITERAPIDS) &&	\
    defined(HAVE_COMPILER_GCC_OR_MUSL)
#define TARGET_CLONE_GRANITERAPIDS "arch=graniterapids",
#define TARGET_CLONE_USE
#else
#define TARGET_CLONE_GRANITERAPIDS
#endif

#if defined(HAVE_TARGET_CLONES_DIAMONDRAPIDS) &&	\
    defined(HAVE_COMPILER_GCC_OR_MUSL)
#define TARGET_CLONE_DIAMONDRAPIDS "arch=diamondrapids",
#define TARGET_CLONE_USE
#else
#define TARGET_CLONE_DIAMONDRAPIDS
#endif

#if defined(HAVE_TARGET_CLONES_PANTHERLAKE) &&	\
    defined(HAVE_COMPILER_GCC_OR_MUSL)
#define TARGET_CLONE_PANTHERLAKE "arch=pantherlake",
#define TARGET_CLONE_USE
#else
#define TARGET_CLONE_PANTHERLAKE
#endif

#if defined(HAVE_TARGET_CLONES_ARROWLAKE) &&		\
    defined(HAVE_COMPILER_GCC_OR_MUSL)
#define TARGET_CLONE_ARROWLAKE "arch=arrowlake",
#define TARGET_CLONE_USE
#else
#define TARGET_CLONE_ARROWLAKE
#endif

#if defined(HAVE_TARGET_CLONES_LUNARLAKE) &&		\
    defined(HAVE_COMPILER_GCC_OR_MUSL)
#define TARGET_CLONE_LUNARLAKE "arch=lunarlake",
#define TARGET_CLONE_USE
#else
#define TARGET_CLONE_LUNARLAKE
#endif

#if defined(HAVE_TARGET_CLONES_ZNVER1) &&		\
    defined(HAVE_COMPILER_GCC_OR_MUSL)
#define TARGET_CLONE_ZNVER1 "arch=znver1",
#define TARGET_CLONE_USE
#else
#define TARGET_CLONE_ZNVER1
#endif

#if defined(HAVE_TARGET_CLONES_ZNVER2) &&		\
    defined(HAVE_COMPILER_GCC_OR_MUSL)
#define TARGET_CLONE_ZNVER2 "arch=znver2",
#define TARGET_CLONE_USE
#else
#define TARGET_CLONE_ZNVER2
#endif

#if defined(HAVE_TARGET_CLONES_ZNVER3) &&		\
    defined(HAVE_COMPILER_GCC_OR_MUSL)
#define TARGET_CLONE_ZNVER3 "arch=znver3",
#define TARGET_CLONE_USE
#else
#define TARGET_CLONE_ZNVER3
#endif

#if defined(HAVE_TARGET_CLONES_ZNVER4) &&		\
    defined(HAVE_COMPILER_GCC_OR_MUSL)
#define TARGET_CLONE_ZNVER4 "arch=znver4",
#define TARGET_CLONE_USE
#else
#define TARGET_CLONE_ZNVER4
#endif

#if defined(HAVE_TARGET_CLONES_ZNVER5) &&		\
    defined(HAVE_COMPILER_GCC_OR_MUSL)
#define TARGET_CLONE_ZNVER5 "arch=znver5",
#define TARGET_CLONE_USE
#else
#define TARGET_CLONE_ZNVER5
#endif

#define TARGET_CLONES_ALL		\
	TARGET_CLONE_AVX		\
	TARGET_CLONE_AVX2 		\
	TARGET_CLONE_MMX 		\
	TARGET_CLONE_SSE		\
	TARGET_CLONE_SSE2 		\
	TARGET_CLONE_SSE3		\
	TARGET_CLONE_SSSE3 		\
	TARGET_CLONE_SSE4_1		\
	TARGET_CLONE_SSE4_2		\
	TARGET_CLONE_SKYLAKE_AVX512	\
	TARGET_CLONE_COOPERLAKE		\
	TARGET_CLONE_TIGERLAKE		\
	TARGET_CLONE_SAPPHIRERAPIDS	\
	TARGET_CLONE_ALDERLAKE		\
	TARGET_CLONE_ROCKETLAKE		\
	TARGET_CLONE_GRANITERAPIDS	\
	TARGET_CLONE_ARROWLAKE		\
	TARGET_CLONE_LUNARLAKE		\
	TARGET_CLONE_PANTHERLAKE	\
	TARGET_CLONE_DIAMONDRAPIDS	\
	TARGET_CLONE_ZNVER1		\
	TARGET_CLONE_ZNVER2		\
	TARGET_CLONE_ZNVER3		\
	TARGET_CLONE_ZNVER4		\
	TARGET_CLONE_ZNVER5		\
	"default"

#if defined(TARGET_CLONE_USE)
#define TARGET_CLONES	__attribute__((target_clones(TARGET_CLONES_ALL)))
#endif
#endif

/* GCC5.0+ target_clones attributes, ppc64 */

#if defined(STRESS_ARCH_PPC64) &&	\
    defined(HAVE_TARGET_CLONES)

#if defined(HAVE_TARGET_CLONES_POWER9) &&	\
    defined(HAVE_BUILTIN_CPU_IS_POWER10)
#define TARGET_CLONE_POWER9 "cpu=power9",
#define TARGET_CLONE_USE
#else
#define TARGET_CLONE_POWER9
#endif

#if defined(HAVE_TARGET_CLONES_POWER10)	&&	\
    defined(HAVE_BUILTIN_CPU_IS_POWER10)
#define TARGET_CLONE_POWER10 "cpu=power10",
#define TARGET_CLONE_USE
#else
#define TARGET_CLONE_POWER10
#endif

#if defined(HAVE_TARGET_CLONES_POWER11) &&	\
    defined(HAVE_BUILTIN_CPU_IS_POWER11)
#define TARGET_CLONE_POWER11 "cpu=power11",
#define TARGET_CLONE_USE
#else
#define TARGET_CLONE_POWER11
#endif

#define TARGET_CLONES_ALL	\
	TARGET_CLONE_POWER9	\
	TARGET_CLONE_POWER10	\
	TARGET_CLONE_POWER11	\
	"default"

#if defined(TARGET_CLONE_USE)
#define TARGET_CLONES	__attribute__((target_clones(TARGET_CLONES_ALL)))
#endif
#endif

#if !defined(TARGET_CLONES)
#define TARGET_CLONES
#endif

#endif
