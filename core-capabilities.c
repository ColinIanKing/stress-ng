/*
 * Copyright (C) 2014-2021 Canonical, Ltd.
 * Copyright (C) 2022-2025 Colin Ian King.
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
#include "stress-ng.h"
#include "core-builtin.h"
#include "core-capabilities.h"

#if defined(HAVE_SYS_CAPABILITY_H)
#include <sys/capability.h>
#endif

#if defined(HAVE_SYS_PRCTL_H)
#include <sys/prctl.h>
#endif

/*
 *  stress_check_root()
 *	returns true if root
 */
static inline bool stress_check_root(void)
{
	if (geteuid() == 0)
		return true;

#if defined(__CYGWIN__) &&	\
    defined(HAVE_GETGROUPS)
	{
		/*
		 * Cygwin would only return uid 0 if the Windows user is mapped
		 * to this uid by a custom /etc/passwd file.  Regardless of uid,
		 * a process has administrator privileges if the local
		 * administrator group (S-1-5-32-544) is present in the process
		 * token.  By default, Cygwin maps this group to gid 544 but it
		 * may be mapped to gid 0 by a custom /etc/group file.
		 */
		gid_t *gids;
		long int gids_max;
		int ngids;

#if defined(_SC_NGROUPS_MAX)
		gids_max = sysconf(_SC_NGROUPS_MAX);
		if ((gids_max < 0) || (gids_max > 65536))
			gids_max = 65536;
#else
		gids_max = 65536;
#endif
		gids = (gid_t *)calloc((size_t)gids_max, sizeof(*gids));
		if (!gids)
			return false;

		ngids = getgroups((int)gids_max, gids);
		if (ngids > 0) {
			int i;

			for (i = 0; i < ngids; i++) {
				if ((gids[i] == 0) || (gids[i] == 544)) {
					free(gids);
					return true;
				}
			}
		}
		free(gids);
	}
#endif
	return false;
}

#if defined(HAVE_SYS_CAPABILITY_H)
void stress_getset_capability(void)
{
	struct __user_cap_header_struct uch;
	struct __user_cap_data_struct ucd[_LINUX_CAPABILITY_U32S_3];

	uch.version = _LINUX_CAPABILITY_VERSION_3;
	uch.pid = getpid();

	if (capget(&uch, ucd) < 0)
		return;
	(void)capset(&uch, ucd);
}
#else
void stress_getset_capability(void)
{
}
#endif

#if defined(HAVE_SYS_CAPABILITY_H)
/*
 *  stress_check_capability()
 *	returns true if process has the given capability,
 *	if capability is SHIM_CAP_IS_ROOT then just check if process is
 *	root.
 */
bool stress_check_capability(const int capability)
{
	int ret;
	struct __user_cap_header_struct uch;
	struct __user_cap_data_struct ucd[_LINUX_CAPABILITY_U32S_3];
	uint32_t mask;
	size_t idx;

	if (capability == SHIM_CAP_IS_ROOT)
		return stress_check_root();

	(void)shim_memset(&uch, 0, sizeof uch);
	(void)shim_memset(ucd, 0, sizeof ucd);

	uch.version = _LINUX_CAPABILITY_VERSION_3;
	uch.pid = getpid();

	ret = capget(&uch, ucd);
	if (ret < 0)
		return stress_check_root();

	idx = (size_t)CAP_TO_INDEX(capability);
	mask = CAP_TO_MASK(capability);

	return (ucd[idx].permitted & mask) ? true : false;
}
#else
bool stress_check_capability(const int capability)
{
	(void)capability;

	return stress_check_root();
}
#endif

/*
 *  stress_drop_capabilities()
 *	drop all capabilities and disable any new privileges
 */
#if defined(HAVE_SYS_CAPABILITY_H)
int stress_drop_capabilities(const char *name)
{
	int ret;
	uint32_t i;
	struct __user_cap_header_struct uch;
	struct __user_cap_data_struct ucd[_LINUX_CAPABILITY_U32S_3];

	(void)shim_memset(&uch, 0, sizeof uch);
	(void)shim_memset(ucd, 0, sizeof ucd);

	uch.version = _LINUX_CAPABILITY_VERSION_3;
	uch.pid = getpid();

	ret = capget(&uch, ucd);
	if (UNLIKELY(ret < 0)) {
		pr_fail("%s: capget on PID %" PRIdMAX " failed, errno=%d (%s)\n",
			name, (intmax_t)uch.pid, errno, strerror(errno));
		return -1;
	}

	/*
	 *  We could just memset ucd to zero, but
	 *  lets explicitly set all the capability
	 *  bits to zero to show the intent
	 */
	for (i = 0; i <= CAP_LAST_CAP; i++) {
		const uint32_t idx = CAP_TO_INDEX(i);
		const uint32_t mask = CAP_TO_MASK(i);

		ucd[idx].inheritable &= ~mask;
		ucd[idx].permitted &= ~mask;
		ucd[idx].effective &= ~mask;
	}

	ret = capset(&uch, ucd);
	if (UNLIKELY(ret < 0)) {
		pr_fail("%s: capset on PID %" PRIdMAX " failed, errno=%d (%s)\n",
			name, (intmax_t)uch.pid, errno, strerror(errno));
		return -1;
	}
#if defined(HAVE_PRCTL) &&		\
    defined(HAVE_SYS_PRCTL_H) &&	\
    defined(PR_SET_NO_NEW_PRIVS)
	ret = prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
	if (UNLIKELY(ret < 0)) {
		/* Older kernels that don't support this prctl throw EINVAL */
		if (errno != EINVAL) {
			pr_inf("%s: prctl PR_SET_NO_NEW_PRIVS on PID %" PRIdMAX " failed: "
				"errno=%d (%s)\n",
				name, (intmax_t)uch.pid, errno, strerror(errno));
			return -1;
		}
	}
#endif
	return 0;
}
#else
int stress_drop_capabilities(const char *name)
{
	(void)name;

	return 0;
}
#endif
