/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
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
#include "core-attribute.h"
#include "core-builtin.h"
#include "core-capabilities.h"
#include "core-killpid.h"
#include "core-madvise.h"
#include "core-out-of-memory.h"

#define STRESS_EFI_UNKNOWN	(0)
#define STRESS_EFI_VARS		(1)
#define STRESS_EFI_EFIVARS	(2)

#include <sys/ioctl.h>

#if defined(HAVE_LINUX_FS_H)
#include <linux/fs.h>
#endif

static const stress_help_t help[] = {
	{ NULL,	"efivar N",	"start N workers that read EFI variables" },
	{ NULL,	"efivar-ops N",	"stop after N EFI variable bogo read operations" },
	{ NULL,	NULL,		NULL }
};

#if defined(__linux__) &&	\
    !defined(STRESS_ARCH_ALPHA)

typedef struct {
	uint8_t		varname[512 * sizeof(uint16_t)];
	uint8_t		guid[16];
	uint8_t		datalen[8];
	uint8_t		data[1024];
	uint8_t		status[8];
	uint8_t		attributes[4];
} stress_efi_var_t ALIGNED(8);

static const char sysfs_efi_vars[] = "/sys/firmware/efi/vars";
static const char sysfs_efi_efivars[] = "/sys/firmware/efi/efivars";
static struct dirent **efi_dentries;
static bool *efi_ignore;
static int dir_count;
static int efi_mode = STRESS_EFI_UNKNOWN;

/*
 *  efi_var_ignore()
 *	check for filenames that are not efi vars
 */
static inline bool CONST efi_var_ignore(char *d_name)
{
	static const char * const ignore[] = {
		".",
		"..",
		"del_var",
		"new_var",
		"MokListRT",
	};

	size_t i;

	for (i = 0; i < SIZEOF_ARRAY(ignore); i++)
		if (strcmp(d_name, ignore[i]) == 0)
			return true;

	return false;
}

/*
 *  guid_to_str()
 *	turn efi GUID to a string
 */
static inline void guid_to_str(const uint8_t *guid, char *guid_str, const size_t guid_len)
{
	if (!guid_str)
		return;

	if (guid_len > 36) {
		(void)snprintf(guid_str, guid_len,
			"%02x%02x%02x%02x-%02x%02x-%02x%02x-"
			"%02x%02x-%02x%02x%02x%02x%02x%02x",
			guid[3], guid[2], guid[1], guid[0], guid[5], guid[4], guid[7], guid[6],
		guid[8], guid[9], guid[10], guid[11], guid[12], guid[13], guid[14], guid[15]);
	} else {
		*guid_str = '\0';
	}
}

/*
 *  efi_get_varname()
 *	fetch the UEFI variable name in terms of a 8 bit C string
 */
static inline void efi_get_varname(char *dst, const size_t len, const stress_efi_var_t *var)
{
	register size_t i = len;

	const uint16_t *src = (const uint16_t *)var->varname;

	while ((*src) && (i > 1)) {
		*dst++ = (char)(*(src++) & 0xff);
		i--;
	}
	*dst = '\0';
}

/*
 *  efi_lseek_read()
 *	perform a lseek and a 1 char read on fd, silently ignore errors
 */
static void efi_lseek_read(const int fd, const off_t offset, const int whence)
{
	off_t offret;

	offret = lseek(fd, offset, whence);
	if (offret != (off_t)-1) {
		char data[1];

		VOID_RET(ssize_t, read(fd, data, sizeof(data)));
	}
}

static void stress_efi_sysfs_fd(
	stress_args_t *args,
	const int fd,
	const ssize_t n)
{
	off_t offset;
	/*
	 *  And exercise the interface for some extra kernel
	 *  test coverage
	 */
	offset = (n > 0) ? stress_mwc32modn((uint32_t)n) : 0;
	efi_lseek_read(fd, offset, SEEK_SET);

	offset = (n > 0) ? stress_mwc32modn((uint32_t)n) : 0;
	efi_lseek_read(fd, offset, SEEK_END);

	efi_lseek_read(fd, 0, SEEK_SET);
	efi_lseek_read(fd, offset, SEEK_CUR);

	/*
	 *  exercise mmap
	 */
	{
		const size_t len = (n > 0) ? (size_t)n : args->page_size;
		void *ptr;

		ptr = mmap(NULL, len, PROT_READ | PROT_WRITE,
			MAP_PRIVATE | MAP_ANONYMOUS, fd, 0);
		if (ptr != MAP_FAILED) {
			stress_madvise_randomize(ptr, len);
			stress_madvise_mergeable(ptr, len);
			(void)munmap(ptr, len);
		}
	}

#if defined(FIGETBSZ)
	{
		int isz;

		VOID_RET(int, ioctl(fd, FIGETBSZ, &isz));
	}
#endif
#if defined(FIONREAD)
	{
		int isz;

		VOID_RET(int, ioctl(fd, FIONREAD, &isz));
	}
#endif
}

/*
 *  efi_get_data()
 *	read data from a raw efi sysfs entry
 */
static int efi_get_data(
	stress_args_t *args,
	const pid_t pid,
	const char *varname,
	const char *field,
	void *buf,
	const size_t buf_len,
	double *duration,
	double *count)
{
	int fd, rc = 0;
	ssize_t n;
	char filename[PATH_MAX];
	struct stat statbuf;
	double t;
	const bool metrics = (duration != NULL) && (count != NULL);

	(void)snprintf(filename, sizeof(filename),
		"%s/%s/%s", sysfs_efi_vars, varname, field);
	if ((fd = open(filename, O_RDONLY)) < 0)
		return 0;	/* silently fail for open-retry later on */

	if (shim_fstat(fd, &statbuf) < 0) {
		pr_fail("%s: failed to stat %s, errno=%d (%s)\n",
			args->name, filename, errno, strerror(errno));
		rc = -1;
		goto err_vars;
	}

	(void)stress_read_fdinfo(pid, fd);
	(void)shim_memset(buf, 0, buf_len);

	if (metrics)
		t = stress_time_now();
	n = read(fd, buf, buf_len);
	if ((n < 0) && (errno != EIO) && (errno != EAGAIN) && (errno != EINTR)) {
		pr_fail("%s: failed to read %s, errno=%d (%s)\n",
			args->name, filename, errno, strerror(errno));
		rc = -1;
		goto err_vars;
	}
	if (metrics) {
		(*duration) += stress_time_now() - t;
		(*count) += 1.0;
	}
	stress_efi_sysfs_fd(args, fd, n);

err_vars:
	(void)close(fd);

	return rc;
}

static int efi_read_variable(
	stress_args_t *args,
	char *data,
	const size_t data_len,
	const pid_t pid,
	const char *efi_path,
	const char *varname,
	double *duration,
	double *count)
{
	char filename[PATH_MAX];
	struct stat statbuf;
	double t;
	ssize_t n;
	int fd, ret, rc = 0;
#if defined(FS_IOC_GETFLAGS) &&	\
    defined(FS_IOC_SETFLAGS)
	int flags;
#endif

	(void)stress_mk_filename(filename, sizeof(filename), efi_path, varname);
	if ((fd = open(filename, O_RDONLY)) < 0)
		return 0;	/* silently fail for open-retry later on */

	ret = shim_fstat(fd, &statbuf);
	if (ret < 0) {
		pr_fail("%s: failed to stat %s, errno=%d (%s)\n",
			args->name, filename, errno, strerror(errno));
		rc = -1;
		goto err_efi_vars;
	}

	t = stress_time_now();
	n = read(fd, data, data_len);
	if ((n < 0) && (errno != EIO) && (errno != EAGAIN) && (errno != EINTR)) {
		pr_fail("%s: failed to read %s, errno=%d (%s)\n",
			args->name, filename, errno, strerror(errno));
		rc = -1;
		goto err_efi_vars;
	}
	(*duration) += stress_time_now() - t;
	(*count) += 1.0;

	(void)stress_read_fdinfo(pid, fd);
	stress_efi_sysfs_fd(args, fd, n);

#if defined(FS_IOC_GETFLAGS) &&	\
    defined(FS_IOC_SETFLAGS)
	ret = ioctl(fd, FS_IOC_GETFLAGS, &flags);
	if (ret < 0) {
		pr_fail("%s: ioctl FS_IOC_GETFLAGS on %s failed, errno=%d (%s)\n",
			args->name, filename, errno, strerror(errno));
		rc = -1;
		goto err_efi_vars;
	}

	VOID_RET(int, ioctl(fd, FS_IOC_SETFLAGS, &flags));
#endif

err_efi_vars:
	(void)close(fd);

	return rc;
}

/*
 *  get_variable_sysfs_efi_vars
 *	fetch a UEFI variable given its name via /sys/firmware/efi/vars
 */
static int get_variable_sysfs_efi_vars(
	stress_args_t *args,
	const pid_t pid,
	char *data,
	const size_t data_len,
	const char *varname,
	double *duration,
	double *count)
{
	size_t i;
	stress_efi_var_t var;
	const uint32_t *attributes = (uint32_t *)var.attributes;

	static const char * const efi_sysfs_names[] = {
		"attributes",
		"data",
		"guid",
		"size"
	};

	(void)efi_get_data(args, pid, varname, "raw_var", &var, sizeof(var),
			   duration, count);

	/* Exercise reading the efi sysfs files */
	for (i = 0; i < SIZEOF_ARRAY(efi_sysfs_names); i++) {
		(void)efi_get_data(args, pid, varname, efi_sysfs_names[i],
				   data, data_len, NULL, NULL);
	}

	if (efi_read_variable(args, data, data_len, pid,
			      sysfs_efi_vars, varname,
			      duration, count) < 0)
		return -1;

	if (*attributes) {
		char get_varname[513];
		char guid_str[37];

		efi_get_varname(get_varname, sizeof(get_varname), &var);
		guid_to_str(var.guid, guid_str, sizeof(guid_str));

		(void)guid_str;
	} else {
		efi_ignore[i] = true;
	}
	return 0;
}

/*
 *  get_variable_sysfs_efi_efivars
 *	fetch a UEFI variable given its name via /sys/firmware/efi/efivars
 */
static int get_variable_sysfs_efi_efivars(
	stress_args_t *args,
	const pid_t pid,
	char *data,
	const size_t data_len,
	const char *varname,
	double *duration,
	double *count)
{
	return efi_read_variable(args, data, data_len, pid,
				 sysfs_efi_efivars, varname,
				 duration, count);
}

/*
 *  efi_vars_get()
 *	read EFI variables
 */
static int efi_vars_get(
	stress_args_t *args,
	const pid_t pid,
	double *duration,
	double *count)
{
	static char data[4096];
	int i, rc = 0;

	for (i = 0; LIKELY(stress_continue(args) && (i < dir_count)); i++) {
		char *d_name = efi_dentries[i]->d_name;
		int ret;

		if (efi_ignore[i])
			continue;

		if (!d_name)
			continue;

		if (efi_var_ignore(d_name)) {
			efi_ignore[i] = true;
			continue;
		}

		switch (efi_mode) {
		case STRESS_EFI_VARS:
			ret = get_variable_sysfs_efi_vars(args, pid, data, sizeof(data), d_name, duration, count);
			break;
		case STRESS_EFI_EFIVARS:
			ret = get_variable_sysfs_efi_efivars(args, pid, data, sizeof(data), d_name, duration, count);
			break;
		default:
			efi_ignore[i] = true;
			continue;
		}
		if (ret < 0) {
			efi_ignore[i] = true;
			rc = -1;
			continue;
		}

		stress_bogo_inc(args);
	}

	return rc;
}

/*
 *  stress_efivar_supported()
 *      check if we can run this as root
 */
static int stress_efivar_supported(const char *name)
{
	if (access(sysfs_efi_efivars, R_OK) >= 0) {
		efi_mode = STRESS_EFI_EFIVARS;
		return 0;
	}
	if (access(sysfs_efi_vars, R_OK) >= 0) {
		efi_mode = STRESS_EFI_VARS;
		return 0;
	}

	if (!stress_check_capability(SHIM_CAP_SYS_ADMIN)) {
		pr_inf_skip("%s stressor will be skipped, "
			"need to be running with CAP_SYS_ADMIN "
			"rights for this stressor\n", name);
		return -1;
	}

	pr_inf_skip("%s stressor will be skipped, "
		"need to have access to EFI vars in %s\n",
		name, sysfs_efi_vars);
	return -1;
}

#define STRESS_EFI_UNKNOWN	(0)
#define STRESS_EFI_VARS		(1)
#define STRESS_EFI_EFIVARS	(2)
/*
 *  stress_efivar()
 *	stress that exercises the efi variables
 */
static int stress_efivar(stress_args_t *args)
{
	pid_t pid;
	size_t sz;
	double duration = 0.0, count = 0.0;
	efi_mode = STRESS_EFI_UNKNOWN;
	int rc = EXIT_SUCCESS;

	efi_dentries = NULL;

	dir_count = scandir(sysfs_efi_efivars, &efi_dentries, NULL, alphasort);
	if (efi_dentries && (dir_count > 0)) {
		efi_mode = STRESS_EFI_EFIVARS;
	} else {
		dir_count = scandir(sysfs_efi_vars, &efi_dentries, NULL, alphasort);
		if (efi_dentries && (dir_count > 0)) {
			efi_mode = STRESS_EFI_VARS;
		} else {
			pr_inf("%s: cannot read EFI vars in %s or %s\n", args->name, sysfs_efi_efivars, sysfs_efi_vars);
			return EXIT_NO_RESOURCE;
		}
	}

	sz = (((size_t)dir_count * sizeof(*efi_ignore)) + args->page_size) & (args->page_size - 1);
	efi_ignore = (bool *)mmap(NULL, sz, PROT_READ | PROT_WRITE,
			MAP_ANONYMOUS | MAP_SHARED, -1, 0);
	if (efi_ignore == MAP_FAILED) {
		pr_inf_skip("%s: cannot mmap %zu bytes of shared memory%s, "
			"errno=%d (%s), skipping stressor\n",
			args->name, sz, stress_get_memfree_str(),
			errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}
	stress_set_vma_anon_name(efi_ignore, sz, "efi-ignore-state");

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);
again:
	pid = fork();
	if (pid < 0) {
		if (stress_redo_fork(args, errno))
			goto again;
		if (UNLIKELY(!stress_continue(args)))
			goto finish;
		pr_err("%s: fork failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
	} else if (pid > 0) {
		int status;
		pid_t ret;

		/* Parent, wait for child */
		ret = shim_waitpid(pid, &status, 0);
		if (ret < 0) {
			if (errno != EINTR)
				pr_dbg("%s: waitpid() on PID %" PRIdMAX " failed, errno=%d (%s)\n",
					args->name, (intmax_t)pid, errno, strerror(errno));
			stress_force_killed_bogo(args);
			(void)stress_kill_pid_wait(pid, NULL);
		} else if (WIFSIGNALED(status)) {
			pr_dbg("%s: child died: %s (instance %d)\n",
				args->name, stress_strsignal(WTERMSIG(status)),
				args->instance);
			return EXIT_FAILURE;
		} else if (WIFEXITED(status)) {
			if (WEXITSTATUS(status) != EXIT_SUCCESS)
				rc = WEXITSTATUS(status);
		}
	} else {
		const pid_t mypid = getpid();
		double rate;

		stress_set_proc_state(args->name, STRESS_STATE_RUN);
		stress_parent_died_alarm();
		stress_set_oom_adjustment(args, true);
		(void)sched_settings_apply(true);

		do {
			if (efi_vars_get(args, mypid, &duration, &count) < 0) {
				rc = EXIT_FAILURE;
				break;
			}
		} while (stress_continue(args));

		rate = (duration > 0.0) ? count / duration : 0.0;
		stress_metrics_set(args, 0, "efi raw data reads per sec",
			rate, STRESS_METRIC_HARMONIC_MEAN);

		_exit(rc);
	}

finish:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	(void)munmap((void *)efi_ignore, sz);
	stress_dirent_list_free(efi_dentries, dir_count);

	return rc;
}

const stressor_info_t stress_efivar_info = {
	.stressor = stress_efivar,
	.supported = stress_efivar_supported,
	.classifier = CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
static int stress_efivar_supported(const char *name)
{
	pr_inf_skip("%s stressor will be skipped, "
		"it is not implemented on this platform\n", name);

	return -1;
}
const stressor_info_t stress_efivar_info = {
	.stressor = stress_unimplemented,
	.supported = stress_efivar_supported,
	.classifier = CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "only supported on Linux with EFI variable filesystem"
};
#endif
