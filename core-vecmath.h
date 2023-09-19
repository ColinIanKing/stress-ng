/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2022-2023 Colin Ian King.
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
#if defined(STRESS_ARCH_PPC64) && \
    defined(HAVE_COMPILER_GCC) && \
    __GNUC__ < 6
#undef HAVE_VECMATH
#endif

#endif
