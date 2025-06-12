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
#ifndef CORE_NT_LOAD_H
#define CORE_NT_LOAD_H

/* Non-temporal loads */

/*
 *  128 bit non-temporal loads
 */
#if defined(HAVE_INT128_T) &&				\
    defined(HAVE_BUILTIN_SUPPORTS) &&			\
    defined(HAVE_BUILTIN_NONTEMPORAL_LOAD)
/* Clang non-temporal loads */
static inline __uint128_t ALWAYS_INLINE stress_nt_load128(__uint128_t *addr)
{
	return __builtin_nontemporal_load(addr);
}
#define HAVE_NT_LOAD128
#endif


/*
 *  64 bit non-temporal loads
 */
#if defined(HAVE_BUILTIN_SUPPORTS) &&	\
    defined(HAVE_BUILTIN_NONTEMPORAL_LOAD)
/* Clang non-temporal load */
static inline uint64_t ALWAYS_INLINE stress_nt_load64(uint64_t *addr)
{
	return __builtin_nontemporal_load(addr);
}
#define HAVE_NT_LOAD64
#endif

/*
 *  32 bit non-temporal loads
 */
#if defined(HAVE_BUILTIN_SUPPORTS) &&			\
    defined(HAVE_BUILTIN_NONTEMPORAL_LOAD)
/* Clang non-temporal load */
static inline uint32_t ALWAYS_INLINE stress_nt_load32(uint32_t *addr)
{
	return __builtin_nontemporal_load(addr);
}
#define HAVE_NT_LOAD32
#endif

/*
 *  double precision float non-temporal loads
 */
#if defined(HAVE_BUILTIN_SUPPORTS) &&			\
    defined(HAVE_BUILTIN_NONTEMPORAL_LOAD)
/* Clang non-temporal load */
static inline double ALWAYS_INLINE stress_nt_load_double(double *addr)
{
	return __builtin_nontemporal_load(addr);
}
#define HAVE_NT_LOAD_DOUBLE
#endif

#endif
