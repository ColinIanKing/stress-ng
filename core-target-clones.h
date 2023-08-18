/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2022-2023 Colin Ian King
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

#if defined(HAVE_TARGET_CLONES_AVX) &&	\
    NEED_GNUC(8,0,0)
#define TARGET_CLONE_AVX	"avx",
#define TARGET_CLONE_USE
#else
#define TARGET_CLONE_AVX
#endif

#if defined(HAVE_TARGET_CLONES_AVX2) &&	\
    NEED_GNUC(8,0,0)
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

#if defined(HAVE_TARGET_CLONES_SKYLAKE_AVX512) &&	\
    NEED_GNUC(8,0,0)
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
    defined(HAVE_COMPILER_GCC)
#define TARGET_CLONE_GRANITERAPIDS "arch=graniterapids",
#define TARGET_CLONE_USE
#else
#define TARGET_CLONE_GRANITERAPIDS
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
	"default"

#if defined(TARGET_CLONE_USE)
#define TARGET_CLONES	__attribute__((target_clones(TARGET_CLONES_ALL)))
#endif
#endif

/* GCC5.0+ target_clones attribute, ppc64 */

#if defined(STRESS_ARCH_PPC64) &&	\
    defined(HAVE_TARGET_CLONES) && 	\
    defined(HAVE_TARGET_CLONES_POWER9)
#define TARGET_CLONES	__attribute__((target_clones("cpu=power9,default")))
#endif

#if !defined(TARGET_CLONES)
#define TARGET_CLONES
#endif

#endif
