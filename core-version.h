/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
 *
 */
#if !defined(STRESS_VERSION_H)
#define STRESS_VERSION_H

#define STRESS_VERSION_NUMBER(major, minor, patchlevel)		\
	((major * 10000) + (minor * 100) + patchlevel)

#if defined(__GLIBC__) &&	\
    defined(__GLIBC_MINOR__)
#define NEED_GLIBC(major, minor, patchlevel) 			\
	STRESS_VERSION_NUMBER(major, minor, patchlevel) <=	\
	STRESS_VERSION_NUMBER(__GLIBC__, __GLIBC_MINOR__, 0)
#else
#define NEED_GLIBC(major, minor, patchlevel) 	(0)
#endif

#if defined(__GNUC__) &&	\
    defined(__GNUC_MINOR__)
#if defined(__GNUC_PATCHLEVEL__)
#define NEED_GNUC(major, minor, patchlevel) 			\
	STRESS_VERSION_NUMBER(major, minor, patchlevel) <= 	\
	STRESS_VERSION_NUMBER(__GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__)
#else
#define NEED_GNUC(major, minor, patchlevel) 			\
	STRESS_VERSION_NUMBER(major, minor, patchlevel) <=	\
	STRESS_VERSION_NUMBER(__GNUC__, __GNUC_MINOR__, 0)
#endif
#else
#define NEED_GNUC(major, minor, patchlevel) 	(0)
#endif

#if defined(__clang__) &&	\
    defined(__clang_major__) &&	\
    defined(__clang_minor__) && \
    defined(__clang_patchlevel__)
#define NEED_CLANG(major, minor, patchlevel)			\
	STRESS_VERSION_NUMBER(major, minor, patchlevel) <=	\
	STRESS_VERSION_NUMBER(__clang_major__, __clang_minor__, __clang_patchlevel__)
#else
#define NEED_CLANG(major, minor, patchlevel)	(0)
#endif

#if defined(__ICC) &&			\
    defined(__INTEL_COMPILER) && 	\
    defined(__INTEL_COMPILER_UPDATE)
#define NEED_ICC(major, minor, patchlevel)		\
	STRESS_VERSION_NUMBER(major, minor, patchlevel) <=	\
	STRESS_VERSION_NUMBER(__INTEL_COMPILER, __INTEL_COMPILER_UPDATE, 0)
#else
#define NEED_ICC(major, minor, patchlevel)	(0)
#endif

#if defined(__clang__) &&     \
   (defined(__INTEL_CLANG_COMPILER) || defined(__INTEL_LLVM_COMPILER))
#define NEED_ICX(major, minor, patchlevel)		\
	STRESS_VERSION_NUMBER(major, minor, patchlevel) <= __INTEL_CLANG_COMPILER
#else
#define NEED_ICX(major, minor, patchlevel)	(0)
#endif

#endif
