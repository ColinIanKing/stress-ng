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
#ifndef CORE_PRAGMA_H
#define CORE_PRAGMA_H

#define STRESS_PRAGMA_(x) _Pragma (#x)
#define STRESS_PRAGMA(x) STRESS_PRAGMA_(x)

#if defined(HAVE_PRAGMA_PREFETCH) &&		\
    defined(HAVE_COMPILER_GCC_OR_MUSL) &&  	\
    defined(HAVE_PRAGMA)
#define STRESS_PRAGMA_PREFETCH		_Pragma("GCC optimize (\"prefetch-loop-arrays\")")
#define STRESS_PRAGMA_NOPREFETCH	_Pragma("GCC optimize (\"no-prefetch-loop-arrays\")")
#else
#define STRESS_PRAGMA_PREFETCH
#define STRESS_PRAGMA_NOPREFETCH
#endif

#if defined(HAVE_PRAGMA_NO_HARD_DFP) &&		\
    defined(HAVE_COMPILER_GCC_OR_MUSL) &&	\
    defined(HAVE_PRAGMA)
#define STRESS_PRAGMA_NO_HARD_DFP	_Pragma("GCC target (\"no-hard-dfp\")")
#endif

#if defined(HAVE_COMPILER_CLANG) &&	\
    NEED_CLANG(4, 0, 0) &&		\
    defined(HAVE_PRAGMA)
#define STRESS_PRAGMA_PUSH		_Pragma("GCC diagnostic push")
#define STRESS_PRAGMA_POP		_Pragma("GCC diagnostic pop")
#define STRESS_PRAGMA_WARN_OFF		_Pragma("GCC diagnostic ignored \"-Weverything\"")
#elif defined(HAVE_COMPILER_GCC_OR_MUSL) &&	\
      defined(HAVE_PRAGMA) &&			\
      NEED_GNUC(9, 4, 0)
#define STRESS_PRAGMA_PUSH		_Pragma("GCC diagnostic push")
#define STRESS_PRAGMA_POP		_Pragma("GCC diagnostic pop")
#define STRESS_PRAGMA_WARN_OFF		_Pragma("GCC diagnostic ignored \"-Wall\"") \
					_Pragma("GCC diagnostic ignored \"-Wextra\"") \
					_Pragma("GCC diagnostic ignored \"-Wdeprecated-declarations\"") \
					_Pragma("GCC diagnostic ignored \"-Wcast-qual\"") \
					_Pragma("GCC diagnostic ignored \"-Wnonnull\"")	\
					_Pragma("GCC diagnostic ignored \"-Wstringop-overflow\"") \
					_Pragma("GCC diagnostic ignored \"-Waddress-of-packed-member\"")
#elif defined(HAVE_COMPILER_GCC_OR_MUSL) &&	\
      defined(HAVE_PRAGMA) &&			\
      NEED_GNUC(7, 5, 0)
#define STRESS_PRAGMA_PUSH		_Pragma("GCC diagnostic push")
#define STRESS_PRAGMA_POP		_Pragma("GCC diagnostic pop")
#define STRESS_PRAGMA_WARN_OFF		_Pragma("GCC diagnostic ignored \"-Wall\"") \
					_Pragma("GCC diagnostic ignored \"-Wextra\"") \
					_Pragma("GCC diagnostic ignored \"-Wdeprecated-declarations\"") \
					_Pragma("GCC diagnostic ignored \"-Wcast-qual\"") \
					_Pragma("GCC diagnostic ignored \"-Wnonnull\"")	\
					_Pragma("GCC diagnostic ignored \"-Wstringop-overflow\"")
#elif defined(HAVE_COMPILER_GCC_OR_MUSL) &&	\
      defined(HAVE_PRAGMA) &&			\
      NEED_GNUC(4, 6, 0)
#define STRESS_PRAGMA_PUSH		_Pragma("GCC diagnostic push")
#define STRESS_PRAGMA_POP		_Pragma("GCC diagnostic pop")
#define STRESS_PRAGMA_WARN_OFF		_Pragma("GCC diagnostic ignored \"-Wall\"") \
					_Pragma("GCC diagnostic ignored \"-Wextra\"") \
					_Pragma("GCC diagnostic ignored \"-Wdeprecated-declarations\"") \
					_Pragma("GCC diagnostic ignored \"-Wcast-qual\"") \
					_Pragma("GCC diagnostic ignored \"-Wnonnull\"")
#else
#define STRESS_PRAGMA_PUSH
#define STRESS_PRAGMA_POP
#define STRESS_PRAGMA_WARN_OFF
#endif

#if defined(HAVE_COMPILER_CLANG) &&	\
    NEED_CLANG(8, 0, 0) &&		\
    defined(HAVE_PRAGMA)
#define STRESS_PRAGMA_WARN_CPP_OFF	_Pragma("GCC diagnostic ignored \"-Wcpp\"")
#elif defined(HAVE_COMPILER_GCC_OR_MUSL) &&	\
      defined(HAVE_PRAGMA) &&			\
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
#elif defined(HAVE_COMPILER_GCC_OR_MUSL) &&      \
    NEED_GNUC(10, 0, 0)
#define PRAGMA_UNROLL_N(n)	STRESS_PRAGMA(GCC unroll n)
#define PRAGMA_UNROLL		STRESS_PRAGMA(GCC unroll 8)
#else
#define PRAGMA_UNROLL_N(n)
#define PRAGMA_UNROLL
#endif

#endif
