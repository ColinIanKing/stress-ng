/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C)      2022 Colin Ian King.
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
#include "core-capabilities.h"

#if defined(HAVE_SYS_APPARMOR_H)
#include <sys/apparmor.h>
#endif

#if defined(HAVE_SYS_SELECT_H)
#include <sys/select.h>
#endif

static const stress_help_t help[] = {
	{ NULL,	"apparmor",	  "start N workers exercising AppArmor interfaces" },
	{ NULL,	"apparmor-ops N", "stop after N bogo AppArmor worker bogo operations" },
	{ NULL,	NULL,		  NULL }
};

#if defined(HAVE_APPARMOR) &&		\
    defined(HAVE_SYS_APPARMOR_H) && 	\
    defined(HAVE_SYS_SELECT_H)

#define APPARMOR_BUF_SZ	(4096)

typedef int (*stress_apparmor_func)(const stress_args_t *args);

static volatile bool apparmor_run = true;
static char *apparmor_path = NULL;
static void *counter_lock;
static char *data_copy, *data_prev;

extern char g_apparmor_data[];
extern const size_t g_apparmor_data_len;

/*
 *  stress_apparmor_supported()
 *      check if AppArmor is supported
 */
static int stress_apparmor_supported(const char *name)
{
	int fd;
	char path[PATH_MAX];

	if (!stress_check_capability(SHIM_CAP_MAC_ADMIN)) {
		pr_inf_skip("%s stressor will be skipped, "
			"need to be running with CAP_SYS_ADMIN "
			"rights for this stressor\n", name);
		return -1;
	}

	/* Initial sanity checks for AppArmor */
	if (!aa_is_enabled()) {
		pr_inf_skip("apparmor stressor will be skipped, "
			"AppArmor is not enabled\n");
		return -1;
	}
	if (aa_find_mountpoint(&apparmor_path) < 0) {
		pr_inf_skip("apparmor stressor will be skipped, "
			"cannot get AppArmor path, errno=%d (%s)\n",
			errno, strerror(errno));
		return -1;
	}
	/* ..and see if profiles are accessible */
	(void)stress_mk_filename(path, sizeof(path), apparmor_path, "profiles");
	if ((fd = open(path, O_RDONLY)) < 0) {
		switch (errno) {
		case EACCES:
			pr_inf_skip("apparmor stressor will be skipped, "
				"stress-ng needs CAP_MAC_ADMIN "
				"privilege to access AppArmor /sys files.\n");
			break;
		case ENOENT:
			pr_inf_skip("apparmor stressor will be skipped, "
				"AppArmor /sys files do not exist\n");
			break;
		default:
			pr_inf_skip("apparmor stressor will be skipped, "
				"cannot access AppArmor /sys files: "
				"errno=%d (%s)\n", errno, strerror(errno));
			break;
		}
		free(apparmor_path);
		apparmor_path = NULL;
		return -1;
	}
	(void)close(fd);

	return 0;
}

static bool stress_apparmor_keep_stressing_inc(const stress_args_t *args, bool inc)
{
	bool ret;

	/* fast check */
	if (!apparmor_run)
		return false;

	/* slower path */
	stress_lock_acquire(counter_lock);
	ret = keep_stressing(args);
	if (inc && ret)
		inc_counter(args);
	stress_lock_release(counter_lock);

	return ret;
}

/*
 *  stress_apparmor_handler()
 *      signal handler
 */
static void MLOCKED_TEXT stress_apparmor_alrm_handler(int signum)
{
	(void)signum;

	apparmor_run = false;
}

static void MLOCKED_TEXT stress_apparmor_usr1_handler(int signum)
{
	(void)signum;
}

/*
 *  stress_apparmor_read()
 *	read a proc file
 */
static void stress_apparmor_read(
	const stress_args_t *args,
	const char *path)
{
	int fd;
	ssize_t i = 0;
	char buffer[APPARMOR_BUF_SZ];

	if ((fd = open(path, O_RDONLY | O_NONBLOCK)) < 0)
		return;
	/*
	 *  Multiple randomly sized reads
	 */
	while (i < (4096 * APPARMOR_BUF_SZ)) {
		ssize_t ret, sz = 1 + (stress_mwc32() % sizeof(buffer));
redo:
		if (!stress_apparmor_keep_stressing_inc(args, false))
			break;
		ret = read(fd, buffer, (size_t)sz);
		if (ret < 0) {
			if ((errno == EAGAIN) || (errno == EINTR))
				goto redo;
			break;
		}
		if (ret < sz)
			break;
		i += sz;
	}
	(void)close(fd);
}

/*
 *  stress_apparmor_dir()
 *	read directory
 */
static void stress_apparmor_dir(
	const stress_args_t *args,
	const char *path,
	const bool recurse,
	const int depth)
{
	DIR *dp;
	struct dirent *d;

	if (!stress_apparmor_keep_stressing_inc(args, false))
		return;

	/* Don't want to go too deep */
	if (depth > 8)
		return;

	dp = opendir(path);
	if (dp == NULL)
		return;

	while ((d = readdir(dp)) != NULL) {
		char name[PATH_MAX];

		if (!stress_apparmor_keep_stressing_inc(args, false))
			break;
		if (stress_is_dot_filename(d->d_name))
			continue;
		switch (d->d_type) {
		case DT_DIR:
			if (recurse) {
				(void)stress_mk_filename(name, sizeof(name), path, d->d_name);
				stress_apparmor_dir(args, name, recurse, depth + 1);
			}
			break;
		case DT_REG:
			(void)stress_mk_filename(name, sizeof(name), path, d->d_name);
			stress_apparmor_read(args, name);
			break;
		default:
			break;
		}
	}
	(void)closedir(dp);
}

/*
 *  apparmor_spawn()
 *	spawn a process
 */
static pid_t apparmor_spawn(
	const stress_args_t *args,
	stress_apparmor_func func)
{
	pid_t pid;

again:
	pid = fork();
	if (pid < 0) {
		if (stress_redo_fork(errno))
			goto again;
		return -1;
	}
	if (pid == 0) {
		int ret = EXIT_SUCCESS;

		if (!stress_apparmor_keep_stressing_inc(args, false))
			goto abort;

		if (stress_sighandler(args->name, SIGALRM,
				      stress_apparmor_alrm_handler, NULL) < 0) {
			_exit(EXIT_FAILURE);
		}

		(void)setpgid(0, g_pgrp);
		(void)sched_settings_apply(true);
		stress_parent_died_alarm();
		if (!stress_apparmor_keep_stressing_inc(args, false))
			goto abort;
		ret = func(args);
abort:
		free(apparmor_path);
		(void)kill(args->pid, SIGUSR1);
		_exit(ret);
	}
	(void)setpgid(pid, g_pgrp);
	return pid;
}

/*
 *  apparmor_stress_profiles()
 *	hammer profile reading
 */
static int apparmor_stress_profiles(const stress_args_t *args)
{
	char path[PATH_MAX];

	(void)stress_mk_filename(path, sizeof(path), apparmor_path, "profiles");

	do {
		stress_apparmor_read(args, path);
	} while (stress_apparmor_keep_stressing_inc(args, true));

	return EXIT_SUCCESS;
}

/*
 *  apparmor_stress_features()
 *	hammer features reading
 */
static int apparmor_stress_features(const stress_args_t *args)
{
	char path[PATH_MAX];

	(void)stress_mk_filename(path, sizeof(path), apparmor_path, "features");

	do {
		stress_apparmor_dir(args, path, true, 0);
	} while (stress_apparmor_keep_stressing_inc(args, true));

	return EXIT_SUCCESS;
}

/*
 *  apparmor_stress_kernel_interface()
 *	load/replace/unload stressing
 */
static int apparmor_stress_kernel_interface(const stress_args_t *args)
{
	int rc = EXIT_SUCCESS;
	aa_kernel_interface *kern_if;

	/*
	 *  Try and create a lot of contention and load
	 */
	do {
		int ret = aa_kernel_interface_new(&kern_if, NULL, NULL);
		if (ret < 0) {
			pr_fail("%s: aa_kernel_interface_new() failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			rc = EXIT_FAILURE;
			break;
		}

		/*
		 *  Loading a policy may fail if another stressor has
		 *  already loaded the same policy, so we may get EEXIST
		 */
		ret = aa_kernel_interface_load_policy(kern_if,
			g_apparmor_data, g_apparmor_data_len);
		if (ret < 0) {
			if (errno != EEXIST) {
				pr_fail("%s: aa_kernel_interface_load_policy() failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				rc = EXIT_FAILURE;
			}
		}

		/*
		 *  Replacing should always be atomic and not fail when
		 *  competing against other stressors if I understand the
		 *  interface correctly.
		 */
		ret = aa_kernel_interface_replace_policy(kern_if,
			g_apparmor_data, g_apparmor_data_len);
		if (ret < 0)
			aa_kernel_interface_unref(kern_if);

		/*
		 *  Removal may fail if another stressor has already removed the
		 *  policy, so we may get ENOENT
		 */
		ret = aa_kernel_interface_remove_policy(kern_if,
			"/usr/bin/pulseaudio-eg");
		if (ret < 0) {
			if (errno != ENOENT) {
				aa_kernel_interface_unref(kern_if);

				pr_fail("%s: aa_kernel_interface_remove_policy() failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				rc = EXIT_FAILURE;
				break;
			}
		}
		aa_kernel_interface_unref(kern_if);

	} while (stress_apparmor_keep_stressing_inc(args, true));

	return rc;
}

/*
 *  apparmor_corrupt_flip_bits_random()
 *	bit flip random bits in data
 */
static inline void apparmor_corrupt_flip_bits_random(
	char *copy, const size_t len)
{
	uint32_t i;
	const uint32_t n = stress_mwc32() % 17;

	for (i = 0; i < n; i++) {
		uint32_t rnd = stress_mwc32();

		copy[rnd % len] ^= (1 << ((rnd >> 16) & 7));
	}
}

/*
 *  apparmor_corrupt_flip_seq()
 *	sequentially flip bits
 */
static inline void apparmor_corrupt_flip_seq(
	char *copy, const size_t len)
{
	static size_t p = 0;

	if (p > len)
		p = 0;

	copy[p] ^= (1 << (p & 7));
	p++;
}

/*
 *  apparmor_corrupt_clr_seq()
 *	sequentially clear bits
 */
static inline void apparmor_corrupt_clr_seq(
	char *copy, const size_t len)
{
	static size_t p = 0;

	if (p > len)
		p = 0;

	copy[p] &= ~(1 << (p & 7));
	p++;
}

/*
 *  apparmor_corrupt_set_seq()
 *	sequentially set bits
 */
static inline void apparmor_corrupt_set_seq(
	char *copy, const size_t len)
{
	static size_t p = 0;

	if (p > len)
		p = 0;

	copy[p] |= (1 << (p & 7));
	p++;
}

/*
 *  apparmor_corrupt_flip_byte_random()
 *	randomly flip entire bytes
 */
static inline void apparmor_corrupt_flip_byte_random(
	char *copy, const size_t len)
{
	copy[stress_mwc32() % len] ^= 0xff;
}

/*
 *  apparmor_corrupt_clr_bits_random()
 *	randomly clear bits
 */
static inline void apparmor_corrupt_clr_bits_random(
	char *copy, const size_t len)
{
	uint32_t i;
	const uint32_t n = stress_mwc32() % 17;

	for (i = 0; i < n; i++) {
		uint32_t rnd = stress_mwc32();

		copy[rnd % len] &= ~(1 << ((rnd >> 16) & 7));
	}
}

/*
 *  apparmor_corrupt_set_bits_random()
 *	randomly set bits
 */
static inline void apparmor_corrupt_set_bits_random(
	char *copy, const size_t len)
{
	uint32_t i;
	const uint32_t n = stress_mwc32() % 17;

	for (i = 0; i < n; i++) {
		uint32_t rnd = stress_mwc32();

		copy[rnd % len] |= (1 << ((rnd >> 16) & 7));
	}
}

/*
 *  apparmor_corrupt_clr_byte_random()
 *	randomly clear an entire byte
 */
static inline void apparmor_corrupt_clr_byte_random(
	char *copy, const size_t len)
{
	copy[stress_mwc32() % len] = 0;
}

/*
 *  apparmor_corrupt_set_byte_random()
 *	randomly set an entire byte
 */
static inline void apparmor_corrupt_set_byte_random(
	char *copy, const size_t len)
{
	copy[stress_mwc32() % len] = (char)0xff;
}

/*
 *  apparmor_corrupt_flip_bits_random_burst
 *	randomly flip a burst of contiguous bits
 */
static inline void apparmor_corrupt_flip_bits_random_burst(
	char *copy, const size_t len)
{
	uint32_t i;
	size_t p = (size_t)stress_mwc32() % (len * sizeof(*copy));

	for (i = 0; i < 32; i++) {
		if (p > len)
			p = 0;

		copy[p] ^= (1 << (p & 7));
		p++;
	}
}

/*
 *  apparmor_corrupt_flip_one_bit_random()
 *	randomly flip 1 bit
 */
static inline void apparmor_corrupt_flip_one_bit_random(
	char *copy, const size_t len)
{
	uint32_t rnd = stress_mwc32();

	copy[rnd % len] ^= (1 << ((stress_mwc8() & 7)));
}

/*
 *  apparmor_stress_corruption()
 *	corrupt data and see if we can oops the loader
 *	parser.
 */
static int apparmor_stress_corruption(const stress_args_t *args)
{
	int rc = EXIT_SUCCESS, i = (int)args->instance, ret = -1;
	int j = 0;
	aa_kernel_interface *kern_if;

	/*
	 *  Lets feed AppArmor with some bit corrupted data...
	 */
	do {
		if (ret < 0 || j > 1024) {
			(void)memcpy(data_copy, g_apparmor_data, g_apparmor_data_len);
			j = 0;
		}
		/*
		 *  Apply various corruption methods
		 */
		switch (i) {
		case 0:
			apparmor_corrupt_flip_seq(data_copy, g_apparmor_data_len);
			break;
		case 1:
			apparmor_corrupt_clr_seq(data_copy, g_apparmor_data_len);
			break;
		case 2:
			apparmor_corrupt_set_seq(data_copy, g_apparmor_data_len);
			break;
		case 3:
			apparmor_corrupt_flip_bits_random(data_copy,
				g_apparmor_data_len);
			break;
		case 4:
			apparmor_corrupt_flip_byte_random(data_copy,
				g_apparmor_data_len);
			break;
		case 5:
			apparmor_corrupt_clr_bits_random(data_copy,
				g_apparmor_data_len);
			break;
		case 6:
			apparmor_corrupt_set_bits_random(data_copy,
				g_apparmor_data_len);
			break;
		case 7:
			apparmor_corrupt_clr_byte_random(data_copy,
				g_apparmor_data_len);
			break;
		case 8:
			apparmor_corrupt_set_byte_random(data_copy,
				g_apparmor_data_len);
			break;
		case 9:
			apparmor_corrupt_flip_bits_random_burst(data_copy,
				g_apparmor_data_len);
			break;
		case 10:
			apparmor_corrupt_flip_one_bit_random(data_copy,
				g_apparmor_data_len);
			i = 0;
			break;
		default:
			/* Should not happen */
			i = 0;
			break;
		}

		ret = aa_kernel_interface_new(&kern_if, NULL, NULL);
		if (ret < 0) {
			pr_fail("%s: aa_kernel_interface_new() failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			return EXIT_FAILURE;
		}
		/*
		 *  Expect EPROTO failures
		 */
		ret = aa_kernel_interface_replace_policy(kern_if,
			data_copy, g_apparmor_data_len);
		if (ret < 0) {
			j--;
			if ((errno != EPROTO) &&
			    (errno != EPROTONOSUPPORT) &&
			     errno != ENOENT) {
				pr_inf("%s: aa_kernel_interface_replace_policy() failed, "
					"errno=%d (%s)\n", args->name, errno,
					strerror(errno));
			}
			(void)memcpy(data_copy, data_prev, g_apparmor_data_len);
		} else {
			(void)memcpy(data_prev, data_copy, g_apparmor_data_len);
		}
		aa_kernel_interface_unref(kern_if);
		i++;
	} while (stress_apparmor_keep_stressing_inc(args, true));

	return rc;
}

static const stress_apparmor_func apparmor_funcs[] = {
	apparmor_stress_profiles,
	apparmor_stress_features,
	apparmor_stress_kernel_interface,
	apparmor_stress_corruption,
};

/*
 *  stress_apparmor()
 *	stress AppArmor
 */
static int stress_apparmor(const stress_args_t *args)
{
	const size_t n = SIZEOF_ARRAY(apparmor_funcs);
	pid_t pids[n];
	size_t i;
	int rc = EXIT_NO_RESOURCE;

	if (stress_sighandler(args->name, SIGUSR1, stress_apparmor_usr1_handler, NULL) < 0)
		return EXIT_FAILURE;

	data_copy = malloc(g_apparmor_data_len);
	if (!data_copy) {
		pr_inf("%s: failed to allocate apparmor data copy buffer, skipping stressor\n", args->name);
		return rc;
	}
	data_prev = malloc(g_apparmor_data_len);
	if (!data_prev) {
		pr_inf("%s: failed to allocate apparmor data prev buffer, skipping stressor\n", args->name);
		goto err_free_data_copy;
	}

	counter_lock = stress_lock_create();
	if (!counter_lock) {
		pr_inf("%s: failed to create counter lock. skipping stressor\n", args->name);
		goto err_free_data_prev;
	}

	for (i = 0; i < n; i++) {
		pids[i] = apparmor_spawn(args, apparmor_funcs[i]);
	}

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	while (stress_apparmor_keep_stressing_inc(args, false)) {
#if defined(HAVE_SELECT)
		(void)select(0, NULL, NULL, NULL, NULL);
#else
		(void)pause();
#endif
	}

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	/* Wakeup, time to die */
	for (i = 0; i < n; i++) {
		if (pids[i] >= 0)
			(void)kill(pids[i], SIGALRM);
	}
	/* Now apply death grip */
	for (i = 0; i < n; i++) {
		int status;

		if (pids[i] >= 0) {
			(void)kill(pids[i], SIGKILL);
			(void)shim_waitpid(pids[i], &status, 0);
		}
	}

	free(apparmor_path);
	apparmor_path = NULL;
	(void)stress_lock_destroy(counter_lock);

	rc = EXIT_SUCCESS;

err_free_data_prev:
	free(data_prev);
err_free_data_copy:
	free(data_copy);

	return rc;
}

stressor_info_t stress_apparmor_info = {
	.stressor = stress_apparmor,
	.supported = stress_apparmor_supported,
	.class = CLASS_OS | CLASS_SECURITY,
	.help = help
};

#else

static int stress_apparmor_supported(const char *name)
{
	pr_inf_skip("%s: stressor will be skipped, "
		"AppArmor is not available\n", name);
	return -1;
}

stressor_info_t stress_apparmor_info = {
	.stressor = stress_not_implemented,
	.supported = stress_apparmor_supported,
	.class = CLASS_OS | CLASS_SECURITY,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#endif
