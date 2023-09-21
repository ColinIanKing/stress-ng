/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2023      Colin Ian King
 *
 */
#ifndef CORE_ATTRIBUTE_H
#define CORE_ATTRIBUTE_H

/* warn unused attribute */
#if (defined(HAVE_COMPILER_GCC) && NEED_GNUC(4, 2, 0)) ||	\
    (defined(HAVE_COMPILER_CLANG) && NEED_CLANG(3, 0, 0))
#define WARN_UNUSED	__attribute__((warn_unused_result))
#else
#define WARN_UNUSED
#endif

/* gcc 7.0 and later support __attribute__((fallthrough)); */
#if defined(HAVE_ATTRIBUTE_FALLTHROUGH)
#define CASE_FALLTHROUGH __attribute__((fallthrough))
#else
#define CASE_FALLTHROUGH
#endif

#if defined(HAVE_ATTRIBUTE_FAST_MATH) &&		\
    !defined(HAVE_COMPILER_ICC) &&			\
    defined(HAVE_COMPILER_GCC) &&			\
    NEED_GNUC(10, 0, 0)
#define OPTIMIZE_FAST_MATH __attribute__((optimize("fast-math")))
#else
#define OPTIMIZE_FAST_MATH
#endif

/* no return hint */
#if (defined(HAVE_COMPILER_GCC) && NEED_GNUC(2, 5, 0)) || 	\
    (defined(HAVE_COMPILER_CLANG) && NEED_CLANG(3, 0, 0))
#define NORETURN 	__attribute__((noreturn))
#else
#define NORETURN
#endif

/* weak attribute */
#if (defined(HAVE_COMPILER_GCC) && NEED_GNUC(4, 0, 0)) || 	\
    (defined(HAVE_COMPILER_CLANG) && NEED_CLANG(3, 4, 0))
#define WEAK		__attribute__((weak))
#define HAVE_WEAK_ATTRIBUTE
#else
#define WEAK
#endif

#if defined(ALWAYS_INLINE)
#undef ALWAYS_INLINE
#endif
/* force inlining hint */
#if (defined(HAVE_COMPILER_GCC) && NEED_GNUC(3, 4, 0) 				\
     && ((!defined(__s390__) && !defined(__s390x__)) || NEED_GNUC(6, 0, 1))) ||	\
    (defined(HAVE_COMPILER_CLANG) && NEED_CLANG(3, 0, 0))
#define ALWAYS_INLINE	__attribute__((always_inline))
#else
#define ALWAYS_INLINE
#endif

/* force no inlining hint */
#if (defined(HAVE_COMPILER_GCC) && NEED_GNUC(3, 4, 0)) ||	\
    (defined(HAVE_COMPILER_CLANG) && NEED_CLANG(3, 0, 0))
#define NOINLINE	__attribute__((noinline))
#else
#define NOINLINE
#endif

/* -O3 attribute support */
#if defined(HAVE_COMPILER_GCC) &&	\
    !defined(HAVE_COMPILER_CLANG) &&	\
    !defined(HAVE_COMPILER_ICC) &&	\
    NEED_GNUC(4, 6, 0)
#define OPTIMIZE3 	__attribute__((optimize("-O3")))
#else
#define OPTIMIZE3
#endif

/* -O2 attribute support */
#if defined(HAVE_COMPILER_GCC) &&	\
    !defined(HAVE_COMPILER_CLANG) &&	\
    !defined(HAVE_COMPILER_ICC) &&	\
    NEED_GNUC(4, 6, 0)
#define OPTIMIZE2 	__attribute__((optimize("-O2")))
#else
#define OPTIMIZE2
#endif

/* -O1 attribute support */
#if defined(HAVE_COMPILER_GCC) &&	\
    !defined(HAVE_COMPILER_CLANG) &&	\
    !defined(HAVE_COMPILER_ICC) &&	\
    NEED_GNUC(4, 6, 0)
#define OPTIMIZE1 	__attribute__((optimize("-O1")))
#else
#define OPTIMIZE1
#endif

/* -O0 attribute support */
#if defined(HAVE_COMPILER_GCC) &&	\
    !defined(HAVE_COMPILER_ICC) &&	\
    NEED_GNUC(4, 6, 0)
#define OPTIMIZE0 	__attribute__((optimize("-O0")))
#elif (defined(HAVE_COMPILER_CLANG) && NEED_CLANG(10, 0, 0))
#define OPTIMIZE0	__attribute__((optnone))
#else
#define OPTIMIZE0
#endif

#if ((defined(HAVE_COMPILER_GCC) && NEED_GNUC(3, 3, 0)) ||	\
     (defined(HAVE_COMPILER_CLANG) && NEED_CLANG(3, 0, 0)) ||	\
     (defined(HAVE_COMPILER_ICC) && NEED_ICC(2021, 0, 0))) &&	\
    !defined(HAVE_COMPILER_PCC) &&				\
    !defined(__minix__)
#define ALIGNED(a)	__attribute__((aligned(a)))
#else
#define ALIGNED(a)
#endif

/* Force alignment macros */
#define ALIGN128	ALIGNED(128)
#define ALIGN64		ALIGNED(64)


#if (defined(HAVE_COMPILER_GCC) && NEED_GNUC(4, 6, 0)) ||	\
    (defined(HAVE_COMPILER_CLANG) && NEED_CLANG(3, 0, 0))
#if (defined(__APPLE__) && defined(__MACH__))
#define SECTION(s)	__attribute__((__section__(# s "," # s)))
#else
#define SECTION(s)	__attribute__((__section__(# s)))
#endif
#else
#define SECTION(s)
#endif

/* GCC hot attribute */
#if (defined(HAVE_COMPILER_GCC) && NEED_GNUC(4, 6, 0)) ||	\
    (defined(HAVE_COMPILER_CLANG) && NEED_CLANG(3, 3, 0))
#define HOT		__attribute__((hot))
#else
#define HOT
#endif

/* GCC mlocked data and data section attribute */
#if ((defined(HAVE_COMPILER_GCC) && NEED_GNUC(4, 6, 0) ||	\
     (defined(HAVE_COMPILER_CLANG) && NEED_CLANG(3, 0, 0)))) &&	\
    !defined(__sun__) &&					\
    !defined(__APPLE__) &&					\
    !defined(BUILD_STATIC)
#define MLOCKED_TEXT	__attribute__((__section__("mlocked_text")))
#define MLOCKED_SECTION	(1)
#else
#define MLOCKED_TEXT
#endif

/* print format attribute */
#if ((defined(HAVE_COMPILER_GCC) && NEED_GNUC(3, 2, 0)) ||	\
     (defined(HAVE_COMPILER_CLANG) && NEED_CLANG(3, 0, 0)))
#define FORMAT(func, a, b) __attribute__((format(func, a, b)))
#else
#define FORMAT(func, a, b)
#endif

#endif
