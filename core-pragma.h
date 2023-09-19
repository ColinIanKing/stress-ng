/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */
#ifndef CORE_PRAGMA_H
#define CORE_PRAGMA_H

#define STRESS_PRAGMA_(x) _Pragma (#x)
#define STRESS_PRAGMA(x) STRESS_PRAGMA_(x)

#if defined(HAVE_PRAGMA_NO_HARD_DFP) &&	\
    defined(HAVE_COMPILER_GCC) &&	\
    defined(HAVE_PRAGMA)
#define STRESS_PRAGMA_NO_HARD_DFP	 _Pragma("GCC target (\"no-hard-dfp\")")
#endif

#if defined(HAVE_COMPILER_CLANG) &&	\
    NEED_CLANG(4, 0, 0) &&		\
    defined(HAVE_PRAGMA)
#define STRESS_PRAGMA_PUSH		_Pragma("GCC diagnostic push")
#define STRESS_PRAGMA_POP		_Pragma("GCC diagnostic pop")
#define STRESS_PRAGMA_WARN_OFF		_Pragma("GCC diagnostic ignored \"-Weverything\"")
#elif defined(HAVE_COMPILER_GCC) &&	\
      defined(HAVE_PRAGMA) &&		\
      NEED_GNUC(4, 6, 0)
#define STRESS_PRAGMA_PUSH		_Pragma("GCC diagnostic push")
#define STRESS_PRAGMA_POP		_Pragma("GCC diagnostic pop")
#define STRESS_PRAGMA_WARN_OFF		_Pragma("GCC diagnostic ignored \"-Wall\"") \
					_Pragma("GCC diagnostic ignored \"-Wextra\"") \
					_Pragma("GCC diagnostic ignored \"-Wdeprecated-declarations\"") \
					_Pragma("GCC diagnostic ignored \"-Wcast-qual\"") \
					_Pragma("GCC diagnostic ignored \"-Wnonnull\"")	\
					_Pragma("GCC diagnostic ignored \"-Wstringop-overflow\"")
#else
#define STRESS_PRAGMA_PUSH
#define STRESS_PRAGMA_POP
#define STRESS_PRAGMA_WARN_OFF
#endif

#if defined(HAVE_COMPILER_CLANG) &&	\
    NEED_CLANG(8, 0, 0) &&		\
    defined(HAVE_PRAGMA)
#define STRESS_PRAGMA_WARN_CPP_OFF	_Pragma("GCC diagnostic ignored \"-Wcpp\"")
#elif defined(HAVE_COMPILER_GCC) &&	\
      defined(HAVE_PRAGMA) &&		\
      NEED_GNUC(10, 0, 0)
#define STRESS_PRAGMA_WARN_CPP_OFF	_Pragma("GCC diagnostic ignored \"-Wcpp\"")
#else
#define STRESS_PRAGMA_WARN_CPP_OFF
#endif

#if defined(HAVE_COMPILER_ICC)
#define PRAGMA_UNROLL_N(n)	STRESS_PRAGMA(unroll)
#define PRAGMA_UNROLL		STRESS_PRAGMA(unroll)
#elif defined(HAVE_COMPILER_CLANG) &&	\
    NEED_CLANG(9, 0, 0)
#define PRAGMA_UNROLL_N(n)	STRESS_PRAGMA(unroll n)
#define PRAGMA_UNROLL		STRESS_PRAGMA(unroll)
#elif defined(HAVE_COMPILER_GCC) &&      \
    NEED_GNUC(10, 0, 0)
#define PRAGMA_UNROLL_N(n)	STRESS_PRAGMA(GCC unroll n)
#define PRAGMA_UNROLL		STRESS_PRAGMA(GCC unroll 8)
#else
#define PRAGMA_UNROLL_N(n)
#define PRAGMA_UNROLL
#endif

#endif
