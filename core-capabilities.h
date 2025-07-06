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
#ifndef CORE_CAPABILITIES_H
#define CORE_CAPABILITIES_H

#include "stress-ng.h"

#define SHIM_CAP_IS_ROOT		(-1)

/* POSIX-draft defined capabilities */
#if defined(CAP_CHOWN)
#define SHIM_CAP_CHOWN			CAP_CHOWN
#else
#define SHIM_CAP_CHOWN			SHIM_CAP_IS_ROOT
#endif

#if defined(CAP_DAC_OVERRIDE)
#define SHIM_CAP_DAC_OVERRIDE		CAP_DAC_OVERRIDE
#else
#define SHIM_CAP_DAC_OVERRIDE		SHIM_CAP_IS_ROOT
#endif

#if defined(CAP_DAC_READ_SEARCH)
#define SHIM_CAP_DAC_READ_SEARCH	CAP_DAC_READ_SEARCH
#else
#define SHIM_CAP_DAC_READ_SEARCH	SHIM_CAP_IS_ROOT
#endif

#if defined(CAP_FOWNER)
#define SHIM_CAP_FOWNER			CAP_FOWNER
#else
#define SHIM_CAP_FOWNER			SHIM_CAP_IS_ROOT
#endif

#if defined(CAP_FSETID)
#define SHIM_CAP_FSETID			CAP_FSETID
#else
#define SHIM_CAP_FSETID			SHIM_CAP_IS_ROOT
#endif

#if defined(CAP_KILL)
#define SHIM_CAP_KILL			CAP_KILL
#else
#define SHIM_CAP_KILL			SHIM_CAP_IS_ROOT
#endif

#if defined(CAP_SETGID)
#define SHIM_CAP_SETGID			CAP_SETGID
#else
#define SHIM_CAP_SETGID			SHIM_CAP_IS_ROOT
#endif

#if defined(CAP_SETUID)
#define SHIM_CAP_SETUID			CAP_SETUID
#else
#define SHIM_CAP_SETUID			SHIM_CAP_IS_ROOT
#endif

/* Linux specific capabilities */
#if defined(CAP_SETPCAP)
#define SHIM_CAP_SETPCAP		CAP_SETPCAP
#else
#define SHIM_CAP_SETPCAP		SHIM_CAP_IS_ROOT
#endif

#if defined(CAP_LINUX_IMMUTABLE)
#define SHIM_CAP_LINUX_IMMUTABLE	CAP_LINUX_IMMUTABLE
#else
#define SHIM_CAP_LINUX_IMMUTABLE	SHIM_CAP_IS_ROOT
#endif

#if defined(CAP_NET_BIND_SERVICE)
#define SHIM_CAP_NET_BIND_SERVICE	CAP_NET_BIND_SERVICE
#else
#define SHIM_CAP_NET_BIND_SERVICE	SHIM_CAP_IS_ROOT
#endif

#if defined(CAP_NET_BROADCAST)
#define SHIM_CAP_NET_BROADCAST		CAP_NET_BROADCAST
#else
#define SHIM_CAP_NET_BROADCAST		SHIM_CAP_IS_ROOT
#endif

#if defined(CAP_NET_ADMIN)
#define SHIM_CAP_NET_ADMIN		CAP_NET_ADMIN
#else
#define SHIM_CAP_NET_ADMIN		SHIM_CAP_IS_ROOT
#endif

#if defined(CAP_NET_RAW)
#define SHIM_CAP_NET_RAW		CAP_NET_RAW
#else
#define SHIM_CAP_NET_RAW		SHIM_CAP_IS_ROOT
#endif

#if defined(CAP_IPC_LOCK)
#define SHIM_CAP_IPC_LOCK		CAP_IPC_LOCK
#else
#define SHIM_CAP_IPC_LOCK		SHIM_CAP_IS_ROOT
#endif

#if defined(CAP_IPC_OWNER)
#define SHIM_CAP_IPC_OWNER		CAP_IPC_OWNER
#else
#define SHIM_CAP_IPC_OWNER		SHIM_CAP_IS_ROOT
#endif

#if defined(CAP_SYS_MODULE)
#define SHIM_CAP_SYS_MODULE		CAP_SYS_MODULE
#else
#define SHIM_CAP_SYS_MODULE		SHIM_CAP_IS_ROOT
#endif

#if defined(CAP_SYS_RAWIO)
#define SHIM_CAP_SYS_RAWIO		CAP_SYS_RAWIO
#else
#define SHIM_CAP_SYS_RAWIO		SHIM_CAP_IS_ROOT
#endif

#if defined(CAP_SYS_CHROOT)
#define SHIM_CAP_SYS_CHROOT		CAP_SYS_CHROOT
#else
#define SHIM_CAP_SYS_CHROOT		SHIM_CAP_IS_ROOT
#endif

#if defined(CAP_SYS_PTRACE)
#define SHIM_CAP_SYS_PTRACE		CAP_SYS_PTRACE
#else
#define SHIM_CAP_SYS_PTRACE		SHIM_CAP_IS_ROOT
#endif

#if defined(CAP_SYS_PACCT)
#define SHIM_CAP_SYS_PACCT		CAP_SYS_PACCT
#else
#define SHIM_CAP_SYS_PACCT		SHIM_CAP_IS_ROOT
#endif

#if defined(CAP_SYS_ADMIN)
#define SHIM_CAP_SYS_ADMIN		CAP_SYS_ADMIN
#else
#define SHIM_CAP_SYS_ADMIN		SHIM_CAP_IS_ROOT
#endif

#if defined(CAP_SYS_BOOT)
#define SHIM_CAP_SYS_BOOT		CAP_SYS_BOOT
#else
#define SHIM_CAP_SYS_BOOT		SHIM_CAP_IS_ROOT
#endif

#if defined(CAP_SYS_NICE)
#define SHIM_CAP_SYS_NICE		CAP_SYS_NICE
#else
#define SHIM_CAP_SYS_NICE		SHIM_CAP_IS_ROOT
#endif

#if defined(CAP_SYS_RESOURCE)
#define SHIM_CAP_SYS_RESOURCE		CAP_SYS_RESOURCE
#else
#define SHIM_CAP_SYS_RESOURCE		SHIM_CAP_IS_ROOT
#endif

#if defined(CAP_SYS_TIME)
#define SHIM_CAP_SYS_TIME		CAP_SYS_TIME
#else
#define SHIM_CAP_SYS_TIME		SHIM_CAP_IS_ROOT
#endif

#if defined(CAP_SYS_TTY_CONFIG)
#define SHIM_CAP_SYS_TTY_CONFIG		CAP_SYS_TTY_CONFIG
#else
#define SHIM_CAP_SYS_TTY_CONFIG		SHIM_CAP_IS_ROOT
#endif

#if defined(CAP_MKNOD)
#define SHIM_CAP_MKNOD			CAP_MKNOD
#else
#define SHIM_CAP_MKNOD			SHIM_CAP_IS_ROOT
#endif

#if defined(CAP_LEASE)
#define SHIM_CAP_LEASE			CAP_LEASE
#else
#define SHIM_CAP_LEASE			SHIM_CAP_IS_ROOT
#endif

#if defined(CAP_AUDIT_WRITE)
#define SHIM_CAP_AUDIT_WRITE		CAP_AUDIT_WRITE
#else
#define SHIM_CAP_AUDIT_WRITE		SHIM_CAP_IS_ROOT
#endif

#if defined(CAP_AUDIT_CONTROL)
#define SHIM_CAP_AUDIT_CONTROL		CAP_AUDIT_CONTROL
#else
#define SHIM_CAP_AUDIT_CONTROL		SHIM_CAP_IS_ROOT
#endif

#if defined(CAP_SETFCAP)
#define SHIM_CAP_SETFCAP		CAP_SETFCAP
#else
#define SHIM_CAP_SETFCAP		SHIM_CAP_IS_ROOT
#endif

#if defined(CAP_MAC_OVERRIDE)
#define SHIM_CAP_MAC_OVERRIDE		CAP_MAC_OVERRIDE
#else
#define SHIM_CAP_MAC_OVERRIDE		SHIM_CAP_IS_ROOT
#endif

#if defined(CAP_MAC_ADMIN)
#define SHIM_CAP_MAC_ADMIN		CAP_MAC_ADMIN
#else
#define SHIM_CAP_MAC_ADMIN		SHIM_CAP_IS_ROOT
#endif

#if defined(CAP_SYSLOG)
#define SHIM_CAP_SYSLOG			CAP_SYSLOG
#else
#define SHIM_CAP_SYSLOG			SHIM_CAP_IS_ROOT
#endif

#if defined(CAP_WAKE_ALARM)
#define SHIM_CAP_WAKE_ALARM		CAP_WAKE_ALARM
#else
#define SHIM_CAP_WAKE_ALARM		SHIM_CAP_IS_ROOT
#endif

#if defined(CAP_BLOCK_SUSPEND)
#define SHIM_CAP_BLOCK_SUSPEND		CAP_BLOCK_SUSPEND
#else
#define SHIM_CAP_BLOCK_SUSPEND		SHIM_CAP_IS_ROOT
#endif

#if defined(CAP_AUDIT_READ)
#define SHIM_CAP_AUDIT_READ		CAP_AUDIT_READ
#else
#define SHIM_CAP_AUDIT_READ		SHIM_CAP_IS_ROOT
#endif

#if defined(CAP_PERFMON)
#define SHIM_CAP_PERFMON		CAP_PERFMON
#else
#define SHIM_CAP_PERFMON		SHIM_CAP_IS_ROOT
#endif

#if defined(CAP_BPF)
#define SHIM_CAP_BPF			CAP_BPF
#else
#define SHIM_CAP_BPF			SHIM_CAP_IS_ROOT
#endif

void stress_getset_capability(void);
WARN_UNUSED bool stress_check_capability(const int capability);
WARN_UNUSED int stress_drop_capabilities(const char *name);

#endif
