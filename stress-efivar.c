/*
 * Copyright (C) 2013-2018 Canonical, Ltd.
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
 * This code is a complete clean re-write of the stress tool by
 * Colin Ian King <colin.king@canonical.com> and attempts to be
 * backwardly compatible with the stress tool by Amos Waterland
 * <apw@rossby.metr.ou.edu> but has more stress tests and more
 * functionality.
 *
 */
#include "stress-ng.h"

#if defined(__linux__)

typedef struct {
        uint16_t        varname[512];
        uint8_t         guid[16];
        uint64_t        datalen;
        uint8_t         data[1024];
        uint64_t        status;
        uint32_t        attributes;
} __attribute__((packed)) efi_var;

static const char efi_vars[] = "/sys/firmware/efi/vars";

/*
 *  efivar_ignore()
 *	check for filenames that are not efi vars
 */
static inline bool efivar_ignore(char *d_name)
{
	if (strcmp(d_name, "del_var") == 0)
		return true;
	if (strcmp(d_name, "new_var") == 0)
		return true;
	if (strcmp(d_name, ".") == 0)
		return true;
	if (strcmp(d_name, "..") == 0)
		return true;
	if (strstr(d_name, "MokListRT"))
		return true;
        return false;
}

/*
 *  guid_to_str()
 *	turn efi GUID to a string
 */
static inline void guid_to_str(const uint8_t *guid, char *guid_str, size_t guid_len)
{
	if (!guid_str)
		return;

	if (guid_len > 36)
		snprintf(guid_str, guid_len,
			"%02x%02x%02x%02x-%02x%02x-%02x%02x-"
			"%02x%02x-%02x%02x%02x%02x%02x%02x",
			guid[3], guid[2], guid[1], guid[0], guid[5], guid[4], guid[7], guid[6],
		guid[8], guid[9], guid[10], guid[11], guid[12], guid[13], guid[14], guid[15]);
	else
		*guid_str = '\0';
}

/*
 *  efi_str16_to_str()
 *	convert 16 bit string to 8 bit C string.
 */
static inline void efi_str16_to_str(char *dst, const size_t len, const uint16_t *src)
{
	size_t i = len;

	while ((*src) && (i > 1)) {
		*dst++ = *(src++) & 0xff;
		i--;
	}
	*dst = '\0';
}

/*
 *  efi_get_varname()
 *	fetch the UEFI variable name in terms of a 8 bit C string
 */
static inline void efi_get_varname(char *varname, const size_t len, const efi_var *var)
{
	efi_str16_to_str(varname, len, var->varname);
}

/*
 *  efi_get_variable()
 *	fetch a UEFI variable given its name.
 */
static int efi_get_variable(const args_t *args, const char *varname, efi_var *var)
{
	int  fd, n, ret = 0;
	char filename[PATH_MAX];

	if ((!varname) || (!var))
		return -1;

	(void)snprintf(filename, sizeof filename,
		"%s/%s/raw_var", efi_vars, varname);

	if ((fd = open(filename, O_RDONLY)) < 0)
		return -1;

	(void)memset(var, 0, sizeof(efi_var));

	if ((n = read(fd, var, sizeof(efi_var))) != sizeof(efi_var)) {
		if (errno != EIO) {
			pr_err("%s: failed to read %s, errno=%d (%s)\n",
				args->name, varname, errno, strerror(errno));
		}
		ret = -1;
	}

	(void)close(fd);

	return ret;
}

/*
 *  efi_vars_get()
 *	read EFI variables
 */
static int efi_vars_get(const args_t *args)
{
	DIR *dir;
	struct dirent *d;

	dir = opendir(efi_vars);
	if (!dir) {
		pr_fail("%s: cannot open %s, errno=%d (%s)\n",
			args->name, efi_vars, errno, strerror(errno));
		return -1;
	}

	while ((d = readdir(dir)) != NULL) {
		efi_var var;
		char varname[513];
		char guid_str[37];
		char *d_name = d->d_name;
		int ret;

		if (efivar_ignore(d_name))
			continue;

		ret = efi_get_variable(args, d_name, &var);
		if (ret < 0)
			continue;

		if (var.attributes) {
			efi_get_varname(varname, sizeof(varname), &var);
			guid_to_str(var.guid, guid_str, sizeof(guid_str));

			(void)guid_str;
		}
		inc_counter(args);
	}
	(void)closedir(dir);

	return 0;
}

/*
 *  stress_efivar_supported()
 *      check if we can run this as root
 */
int stress_efivar_supported(void)
{
	DIR *dir;

	if (geteuid() != 0) {
		pr_inf("efivar stressor will be skipped, "
			"need to be running as root for this stressor\n");
		return -1;
	}

	dir = opendir(efi_vars);
	if (!dir) {
		pr_inf("efivar stressor will be skipped, "
			"need to have access to EFI vars in %s\n",
			efi_vars);
		return -1;
	}
	(void)closedir(dir);

	return 0;
}

/*
 *  stress_efivar()
 *	stress that does lots of not a lot
 */
int stress_efivar(const args_t *args)
{
	pid_t pid;

again:
	if (!g_keep_stressing_flag)
		return EXIT_SUCCESS;
	pid = fork();
	if (pid < 0) {
		if (errno == EAGAIN)
			goto again;
		pr_err("%s: fork failed: errno=%d (%s)\n",
			args->name, errno, strerror(errno));
	} else if (pid > 0) {
		int status, ret;

		(void)setpgid(pid, g_pgrp);
		/* Parent, wait for child */
		ret = waitpid(pid, &status, 0);
		if (ret < 0) {
			if (errno != EINTR)
				pr_dbg("%s: waitpid(): errno=%d (%s)\n",
					args->name, errno, strerror(errno));
			(void)kill(pid, SIGTERM);
			(void)kill(pid, SIGKILL);
			(void)waitpid(pid, &status, 0);
		} else if (WIFSIGNALED(status)) {
			pr_dbg("%s: child died: %s (instance %d)\n",
				args->name, stress_strsignal(WTERMSIG(status)),
				args->instance);
			return EXIT_FAILURE;
		}
	} else if (pid == 0) {
		(void)setpgid(0, g_pgrp);
		stress_parent_died_alarm();
		set_oom_adjustment(args->name, true);

		do {
			efi_vars_get(args);
		} while (keep_stressing());
		_exit(0);
	}

	return EXIT_SUCCESS;
}
#else
int stress_efivar_supported(void)
{
	pr_inf("efivar stressor will be skipped, "
		"it is not implemented on this platform\n");

	return -1;
}

int stress_efivar(const args_t *args)
{
	return stress_not_implemented(args);
}
#endif
