/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2022-2023 Colin Ian King
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
static inline __uint128_t ALWAYS_INLINE OPTIMIZE3 stress_nt_load128(__uint128_t *addr)
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
static inline uint64_t ALWAYS_INLINE OPTIMIZE3 stress_nt_load64(uint64_t *addr)
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
static inline uint32_t ALWAYS_INLINE OPTIMIZE3 stress_nt_load32(uint32_t *addr)
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
static inline double ALWAYS_INLINE OPTIMIZE3 stress_nt_load_double(double *addr)
{
	return __builtin_nontemporal_load(addr);
}
#define HAVE_NT_LOAD_DOUBLE
#endif

#endif
