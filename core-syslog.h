/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */
#ifndef CORE_SYSLOG_H
#define CORE_SYSLOG_H

/* Wrappers around syslog API */

#if defined(HAVE_SYSLOG_H)
#define shim_syslog(priority, format, ...)	\
		syslog(priority, format, __VA_ARGS__)
#define shim_openlog(ident, option, facility) \
		openlog(ident, option, facility)
#define shim_closelog()		closelog()
#else
#define shim_syslog(priority, format, ...)
#define shim_openlog(ident, option, facility)
#define shim_closelog()
#endif

#endif
