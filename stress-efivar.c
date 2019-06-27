/*
 * Copyright (C) 2013-2019 Canonical, Ltd.
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

static const help_t help[] = {
	{ NULL,	"efivar N",	"start N workers that read EFI variables" },
	{ NULL,	"efivar-ops N",	"stop after N EFI variable bogo read operations" },
	{ NULL,	NULL,		NULL }
};

#if defined(__linux__)

typedef struct {
        uint16_t        varname[512];
        uint8_t         guid[16];
        uint64_t        datalen;
        uint8_t         data[1024];
        uint64_t        status;
        uint32_t        attributes;
} __attribute__((packed)) efi_var;

static const char vars[] = "/sys/firmware/efi/vars";
static const char efi_vars[] = "/sys/firmware/efi/efivars";
static struct dirent **efi_dentries;
static bool *efi_ignore;
static int dir_count;

static const char * const efi_sysfs_names[] = {
	"attributes",
	"data",
	"guid",
	"size"
};

/*
 *  efi_var_ignore()
 *	check for filenames that are not efi vars
 */
static inline bool efi_var_ignore(char *d_name)
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
		(void)snprintf(guid_str, guid_len,
			"%02x%02x%02x%02x-%02x%02x-%02x%02x-"
			"%02x%02x-%02x%02x%02x%02x%02x%02x",
			guid[3], guid[2], guid[1], guid[0], guid[5], guid[4], guid[7], guid[6],
		guid[8], guid[9], guid[10], guid[11], guid[12], guid[13], guid[14], guid[15]);
	else
		*guid_str = '\0';
}

/*
 *  efi_get_varname()
 *	fetch the UEFI variable name in terms of a 8 bit C string
 */
static inline void efi_get_varname(char *dst, const size_t len, const efi_var *var)
{
	register size_t i = len;

	/*
	 * gcc-9 -Waddress-of-packed-member workaround, urgh, we know
	 * this is always going to be aligned correctlty, but gcc-9 whines
	 * so this hack works around it for now.
	 */
	const uint8_t *src8 = (const uint8_t *)var->varname;
	const uint16_t *src = (const uint16_t *)src8;

	while ((*src) && (i > 1)) {
		*dst++ = *(src++) & 0xff;
		i--;
	}
	*dst = '\0';
}

/*
 *  efi_get_data()
 *	read data from a raw efi sysfs entry
 */
static int efi_get_data(
	const args_t *args,
	const char *varname,
	const char *field,
	void *buf,
	size_t buf_len)
{
	int fd, rc = -1;
	ssize_t n;
	char filename[PATH_MAX];
	struct stat statbuf;

	(void)snprintf(filename, sizeof filename,
		"%s/%s/%s", vars, varname, field);
	if ((fd = open(filename, O_RDONLY)) < 0)
		return rc;

	if (fstat(fd, &statbuf) < 0) {
		pr_err("%s: failed to stat %s, errno=%d (%s)\n",
			args->name, filename, errno, strerror(errno));
		goto err_vars;
	}

	(void)memset(buf, 0, buf_len);

	n = read(fd, buf, buf_len);
	if ((n < 0) && (errno != EIO) && (errno != EAGAIN) && (errno != EINTR)) {
		pr_err("%s: failed to read %s, errno=%d (%s)\n",
			args->name, filename, errno, strerror(errno));
		goto err_vars;
	}

	rc = 0;
err_vars:
	(void)close(fd);

	return rc;
}

/*
 *  efi_get_variable()
 *	fetch a UEFI variable given its name.
 */
static int efi_get_variable(const args_t *args, const char *varname, efi_var *var)
{
	int fd, ret, rc = 0, flags;
	size_t i;
	ssize_t n;
	char filename[PATH_MAX];
	static char data[4096];
	struct stat statbuf;

	if ((!varname) || (!var))
		return -1;

	if (efi_get_data(args, varname, "raw_var", var, sizeof(efi_var)) < 0)
		rc = -1;

	/* Exercise reading the efi sysfs files */
	for (i = 0; i < SIZEOF_ARRAY(efi_sysfs_names); i++) {
		(void)efi_get_data(args, varname, efi_sysfs_names[i], data, sizeof(data));
	}

	(void)snprintf(filename, sizeof filename, "%s/%s", efi_vars, varname);
	if ((fd = open(filename, O_RDONLY)) < 0)
		return -1;

	ret = fstat(fd, &statbuf);
	if (ret < 0) {
		pr_err("%s: failed to stat %s, errno=%d (%s)\n",
			args->name, filename, errno, strerror(errno));
		rc = -1;
		goto err_efi_vars;
	}

	n = read(fd, data, sizeof(data));
	if ((n < 0) && (errno != EIO) && (errno != EAGAIN) && (errno != EINTR)) {
		pr_err("%s: failed to read %s, errno=%d (%s)\n",
			args->name, filename, errno, strerror(errno));
		rc = -1;
		goto err_efi_vars;
	}


	ret = ioctl(fd, FS_IOC_GETFLAGS, &flags);
	if (ret < 0) {
		pr_err("%s: ioctl FS_IOC_GETFLAGS on %s failed, errno=%d (%s)\n",
			args->name, filename, errno, strerror(errno));
		rc = -1;
		goto err_efi_vars;
	}

	ret = ioctl(fd, FS_IOC_SETFLAGS, &flags);
	if (ret < 0) {
		pr_err("%s: ioctl FS_IOC_SETFLAGS on %s failed, errno=%d (%s)\n",
			args->name, filename, errno, strerror(errno));
		rc = -1;
	}

err_efi_vars:
	(void)close(fd);

	return rc;
}

/*
 *  efi_vars_get()
 *	read EFI variables
 */
static int efi_vars_get(const args_t *args)
{
	int i;

	for (i = 0; i < dir_count; i++) {
		efi_var var;
		char *d_name = efi_dentries[i]->d_name;
		int ret;

		if (efi_ignore[i])
			continue;

		if (efi_var_ignore(d_name)) {
			efi_ignore[i] = true;
			continue;
		}

		ret = efi_get_variable(args, d_name, &var);
		if (ret < 0) {
			efi_ignore[i] = true;
			continue;
		}

		if (var.attributes) {
			char varname[513];
			char guid_str[37];

			efi_get_varname(varname, sizeof(varname), &var);
			guid_to_str(var.guid, guid_str, sizeof(guid_str));

			(void)guid_str;
		} else {
			efi_ignore[i] = true;
		}
		inc_counter(args);
	}

	return 0;
}

/*
 *  stress_efivar_supported()
 *      check if we can run this as root
 */
static int stress_efivar_supported(void)
{
	DIR *dir;

	if (!stress_check_capability(SHIM_CAP_SYS_ADMIN)) {
		pr_inf("efivar stressor will be skipped, "
			"need to be running with CAP_SYS_ADMIN "
			"rights for this stressor\n");
		return -1;
	}

	dir = opendir(efi_vars);
	if (!dir) {
		pr_inf("efivar stressor will be skipped, "
			"need to have access to EFI vars in %s\n",
			vars);
		return -1;
	}
	(void)closedir(dir);

	return 0;
}

/*
 *  stress_efivar()
 *	stress that does lots of not a lot
 */
static int stress_efivar(const args_t *args)
{
	pid_t pid;
	int i;
	size_t sz;

	efi_dentries = NULL;
	dir_count = scandir(vars, &efi_dentries, NULL, alphasort);
	if (!efi_dentries || (dir_count <= 0)) {
		pr_inf("%s: cannot read EFI vars in %s\n", args->name, vars);
		return EXIT_SUCCESS;
	}

	sz = ((dir_count * sizeof(bool)) + args->page_size) & (args->page_size - 1);
	efi_ignore = mmap(NULL, sz, PROT_READ | PROT_WRITE,
			MAP_ANONYMOUS | MAP_SHARED, -1, 0);
	if (efi_ignore == MAP_FAILED) {
		pr_err("%s: cannot mmap shared memory: %d (%s))\n",
			args->name, errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}

again:
	if (!g_keep_stressing_flag)
		return EXIT_SUCCESS;
	pid = fork();
	if (pid < 0) {
		if ((errno == EAGAIN) || (errno == ENOMEM))
			goto again;
		pr_err("%s: fork failed: errno=%d (%s)\n",
			args->name, errno, strerror(errno));
	} else if (pid > 0) {
		int status, ret;

		(void)setpgid(pid, g_pgrp);
		/* Parent, wait for child */
		ret = shim_waitpid(pid, &status, 0);
		if (ret < 0) {
			if (errno != EINTR)
				pr_dbg("%s: waitpid(): errno=%d (%s)\n",
					args->name, errno, strerror(errno));
			(void)kill(pid, SIGTERM);
			(void)kill(pid, SIGKILL);
			(void)shim_waitpid(pid, &status, 0);
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

	(void)munmap(efi_ignore, sz);
	for (i = 0; i < dir_count; i++)
		free(efi_dentries[i]);
	free(efi_dentries);

	return EXIT_SUCCESS;
}

stressor_info_t stress_efivar_info = {
	.stressor = stress_efivar,
	.supported = stress_efivar_supported,
	.class = CLASS_OS,
	.help = help
};
#else
static int stress_efivar_supported(void)
{
	pr_inf("efivar stressor will be skipped, "
		"it is not implemented on this platform\n");

	return -1;
}
stressor_info_t stress_efivar_info = {
	.stressor = stress_not_implemented,
	.supported = stress_efivar_supported,
	.class = CLASS_OS,
	.help = help
};
#endif
