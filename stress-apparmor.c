/*
 * Copyright (C) 2013-2017 Canonical, Ltd.
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

#if defined(__linux__) && defined(HAVE_APPARMOR)

#include <sys/select.h>
#include <sys/apparmor.h>

#define APPARMOR_BUF_SZ	(4096)

typedef int (*apparmor_func)(const char *name,
			     const uint64_t max_ops, uint64_t *counter);

static volatile bool apparmor_run = true;
static char *apparmor_path = NULL;

extern char g_apparmor_data[];
extern const size_t g_apparmor_data_len;

/*
 *  stress_apparmor_supported()
 *      check if AppArmor is supported
 */
int stress_apparmor_supported(void)
{
	int fd;
	char path[PATH_MAX];

	/* Initial sanity checks for AppArmor */
	if (!aa_is_enabled()) {
		pr_inf("apparmor stressor will be skipped, "
			"AppArmor is not enabled\n");
		return -1;
	}
	if (aa_find_mountpoint(&apparmor_path) < 0) {
		pr_inf("apparmor stressor will be skipped, "
			"cannot get AppArmor path, errno=%d (%s)\n",
			errno, strerror(errno));
		return -1;
	}
	/* ..and see if profiles are accessible */
	(void)snprintf(path, sizeof(path), "%s/%s", apparmor_path, "profiles");
	if ((fd = open(path, O_RDONLY)) < 0) {
		switch (errno) {
		case EACCES:
			pr_inf("apparmor stressor will be skipped, "
				"stress-ng needs to be run with root "
				"privilege to access AppArmor /sys files.\n");
			break;
		case ENOENT:
			pr_inf("apparmor stressor will be skipped, "
				"AppArmor /sys files do not exist\n");
			break;
		default:
			pr_inf("apparmor stressor will be skipped, "
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


/*
 *  stress_apparmor_handler()
 *      signal handler
 */
static void MLOCKED stress_apparmor_handler(int dummy)
{
	(void)dummy;

	apparmor_run = false;
}

/*
 *  stress_apparmor_read()
 *	read a proc file
 */
static void stress_apparmor_read(const char *path)
{
	int fd;
	ssize_t i = 0;
	char buffer[APPARMOR_BUF_SZ];

	if ((fd = open(path, O_RDONLY | O_NONBLOCK)) < 0)
		return;
	/*
	 *  Multiple randomly sized reads
	 */
	while (apparmor_run && (i < (4096 * APPARMOR_BUF_SZ))) {
		ssize_t ret, sz = 1 + (mwc32() % sizeof(buffer));
redo:
		if (!g_keep_stressing_flag)
			break;
		ret = read(fd, buffer, sz);
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
	const char *path,
	const bool recurse,
	const int depth)
{
	DIR *dp;
	struct dirent *d;

	if (!g_keep_stressing_flag)
		return;

	/* Don't want to go too deep */
	if (depth > 8)
		return;

	dp = opendir(path);
	if (dp == NULL)
		return;

	while ((d = readdir(dp)) != NULL) {
		char name[PATH_MAX];

		if (!g_keep_stressing_flag)
			break;
		if (!strcmp(d->d_name, ".") ||
		    !strcmp(d->d_name, ".."))
			continue;
		switch (d->d_type) {
		case DT_DIR:
			if (recurse) {
				(void)snprintf(name, sizeof(name),
					"%s/%s", path, d->d_name);
				stress_apparmor_dir(name, recurse, depth + 1);
			}
			break;
		case DT_REG:
			(void)snprintf(name, sizeof(name),
				"%s/%s", path, d->d_name);
			stress_apparmor_read(name);
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
	const char *name,
	const uint64_t max_ops,
	uint64_t *counter,
	apparmor_func func)
{
	pid_t pid;

again:
	pid = fork();
	if (pid < 0) {
		if (g_keep_stressing_flag && (errno == EAGAIN))
			goto again;
		return -1;
	}
	if (pid == 0) {
		int ret;

		if (stress_sighandler(name, SIGALRM,
				stress_apparmor_handler, NULL) < 0)
			exit(EXIT_FAILURE);
		(void)setpgid(0, g_pgrp);
		stress_parent_died_alarm();

		ret = func(name, max_ops, counter);
		free(apparmor_path);
		exit(ret);
	}
	(void)setpgid(pid, g_pgrp);
	return pid;
}


/*
 *  apparmor_stress_profiles()
 *	hammer profile reading
 */
static int apparmor_stress_profiles(
	const char *name,
	const uint64_t max_ops,
	uint64_t *counter)
{
	char path[PATH_MAX];

	(void)name;
	(void)snprintf(path, sizeof(path), "%s/%s", apparmor_path, "profiles");

	do {
		stress_apparmor_read(path);
		(*counter)++;
	} while (apparmor_run && (!max_ops || *counter < max_ops));

	return EXIT_SUCCESS;
}

/*
 *  apparmor_stress_features()
 *	hammer features reading
 */
static int apparmor_stress_features(
	const char *name,
	const uint64_t max_ops,
	uint64_t *counter)
{
	char path[PATH_MAX];

	(void)name;
	(void)snprintf(path, sizeof(path), "%s/%s", apparmor_path, "features");

	do {
		stress_apparmor_dir(path, true, 0);
		(*counter)++;
	} while (apparmor_run && (!max_ops || *counter < max_ops));

	return EXIT_SUCCESS;
}

/*
 *  apparmor_stress_kernel_interface()
 *	load/replace/unload stressing
 */
static int apparmor_stress_kernel_interface(
	const char *name,
	const uint64_t max_ops,
	uint64_t *counter)
{
	int rc = EXIT_SUCCESS;
	aa_kernel_interface *kern_if;

	/*
	 *  Try and create a lot of contention and load
	 */
	do {
		int ret = aa_kernel_interface_new(&kern_if, NULL, NULL);
		if (ret < 0) {
			pr_fail("%s: aa_kernel_interface_new() failed, "
				"errno=%d (%s)\n", name,
				errno, strerror(errno));
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
				pr_fail("%s: aa_kernel_interface_load_policy() failed, "
					"errno=%d (%s)\n", name, errno, strerror(errno));
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
		if (ret < 0) {
			aa_kernel_interface_unref(kern_if);

			pr_fail("%s: aa_kernel_interface_replace_policy() failed, "
				"errno=%d (%s)\n", name, errno,
				strerror(errno));
			rc = EXIT_FAILURE;
		}

		/*
		 *  Removal may fail if another stressor has already removed the
		 *  policy, so we may get ENOENT
		 */
		ret = aa_kernel_interface_remove_policy(kern_if,
			"/usr/bin/pulseaudio-eg");
		if (ret < 0) {
			if (errno != ENOENT) {
				pr_fail("%s: aa_kernel_interface_remove_policy() failed, "
					"errno=%d (%s)\n", name, errno,
					strerror(errno));
				rc = EXIT_FAILURE;
			}
		}
		aa_kernel_interface_unref(kern_if);

		(*counter)++;
	} while (g_keep_stressing_flag && apparmor_run && (!max_ops || *counter < max_ops));

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
	const uint32_t n = mwc32() % 17;

	for (i = 0; i < n; i++) {
		uint32_t rnd = mwc32();

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

	if (p > (len * sizeof(char)))
		p = 0;

	copy[p / sizeof(char)] ^= (1 << (p & 7));
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

	if (p > (len * sizeof(char)))
		p = 0;

	copy[p / sizeof(char)] &= ~(1 << (p & 7));
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

	if (p > (len * sizeof(char)))
		p = 0;

	copy[p / sizeof(char)] |= (1 << (p & 7));
	p++;
}


/*
 *  apparmor_corrupt_flip_byte_random()
 *	ramndomly flip entire bytes
 */
static inline void apparmor_corrupt_flip_byte_random(
	char *copy, const size_t len)
{
	copy[mwc32() % len] ^= 0xff;
}

/*
 *  apparmor_corrupt_clr_bits_random()
 *	randomly clear bits
 */
static inline void apparmor_corrupt_clr_bits_random(
	char *copy, const size_t len)
{
	uint32_t i;
	const uint32_t n = mwc32() % 17;

	for (i = 0; i < n; i++) {
		uint32_t rnd = mwc32();

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
	const uint32_t n = mwc32() % 17;

	for (i = 0; i < n; i++) {
		uint32_t rnd = mwc32();

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
	copy[mwc32() % len] = 0;
}

/*
 *  apparmor_corrupt_set_byte_random()
 *	randomly set an entire byte
 */
static inline void apparmor_corrupt_set_byte_random(
	char *copy, const size_t len)
{
	copy[mwc32() % len] = 0xff;
}

/*
 *  apparmor_corrupt_flip_bits_random_burst
 *	randomly flip a burst of contiguous bits
 */
static inline void apparmor_corrupt_flip_bits_random_burst(
	char *copy, const size_t len)
{
	uint32_t i;
	size_t p = (size_t)mwc32() % (len * sizeof(char));

	for (i = 0; i < 32; i++) {
		if (p > (len * sizeof(char)))
			p = 0;

		copy[p / sizeof(char)] ^= (1 << (p & 7));
		p++;
	}
}


/*
 *  apparmor_stress_corruption()
 *	corrupt data and see if we can oops the loader
 *	parser.
 */
static int apparmor_stress_corruption(
	const char *name,
	const uint64_t max_ops,
	uint64_t *counter)
{
	char copy[g_apparmor_data_len];

	int rc = EXIT_SUCCESS;
	aa_kernel_interface *kern_if;


	/*
	 *  Lets feed AppArmor with some bit corrupted data...
	 */
	do {
		int ret;

		memcpy(copy, g_apparmor_data, g_apparmor_data_len);
		/*
		 *  Apply various corruption methods
		 */
		switch ((*counter) % 10) {
		case 0:
			apparmor_corrupt_flip_seq(copy, g_apparmor_data_len);
			break;
		case 1:
			apparmor_corrupt_clr_seq(copy, g_apparmor_data_len);
			break;
		case 2:
			apparmor_corrupt_set_seq(copy, g_apparmor_data_len);
			break;
		case 3:
			apparmor_corrupt_flip_bits_random(copy,
				g_apparmor_data_len);
			break;
		case 4:
			apparmor_corrupt_flip_byte_random(copy,
				g_apparmor_data_len);
			break;
		case 5:
			apparmor_corrupt_clr_bits_random(copy,
				g_apparmor_data_len);
			break;
		case 6:
			apparmor_corrupt_set_bits_random(copy,
				g_apparmor_data_len);
			break;
		case 7:
			apparmor_corrupt_clr_byte_random(copy,
				g_apparmor_data_len);
			break;
		case 8:
			apparmor_corrupt_set_byte_random(copy,
				g_apparmor_data_len);
			break;
		case 9:
			apparmor_corrupt_flip_bits_random_burst(copy,
				g_apparmor_data_len);
			break;
		default:
			/* Should not happen */
			break;
		}

		ret = aa_kernel_interface_new(&kern_if, NULL, NULL);
		if (ret < 0) {
			pr_fail("%s: aa_kernel_interface_new() failed, "
				"errno=%d (%s)\n", name, errno,
				strerror(errno));
			return EXIT_FAILURE;
		}
		/*
		 *  Expect EPROTO failures
		 */
		ret = aa_kernel_interface_replace_policy(kern_if,
			copy, g_apparmor_data_len);
		if (ret < 0) {
			if ((errno != EPROTO) &&
			    (errno != EPROTONOSUPPORT)) {
				pr_fail("%s: aa_kernel_interface_replace_policy() failed, "
					"errno=%d (%s)\n", name, errno,
					strerror(errno));
				rc = EXIT_FAILURE;
			}
		}
		aa_kernel_interface_unref(kern_if);
		(*counter)++;
	} while (g_keep_stressing_flag && apparmor_run &&
		 (!max_ops || *counter < max_ops));

	return rc;
}


static const apparmor_func apparmor_funcs[] = {
	apparmor_stress_profiles,
	apparmor_stress_features,
	apparmor_stress_kernel_interface,
	apparmor_stress_corruption,
};


/*
 *  stress_apparmor()
 *	stress AppArmor
 */
int stress_apparmor(const args_t *args)
{
	const size_t n = SIZEOF_ARRAY(apparmor_funcs);
	const size_t counters_sz = n * sizeof(uint64_t);
	pid_t pids[n];
	size_t i;
	uint64_t *counters, tmp_counter = 0, ops;

	counters = mmap(NULL, counters_sz, PROT_READ | PROT_WRITE,
		MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (counters == MAP_FAILED) {
		pr_err("%s: mmap failed: errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		exit(EXIT_FAILURE);
	}
	memset(counters, 0, counters_sz);

	if ((args->max_ops > 0) && (args->max_ops < n))
		ops = n;
	else
		ops = args->max_ops / n;

	for (i = 0; i < n; i++) {
		pids[i] = apparmor_spawn(args->name, ops,
			&counters[i], apparmor_funcs[i]);
	}
	do {
		(void)select(0, NULL, NULL, NULL, NULL);
		for (i = 0; i < n; i++)
			tmp_counter += counters[i];
	} while (keep_stressing());

	for (i = 0; i < n; i++) {
		int status;

		if (pids[i] >= 0) {
			(void)kill(pids[i], SIGALRM);
			(void)waitpid(pids[i], &status, 0);
			*(args->counter) += counters[i];
		}
	}
	munmap(counters, counters_sz);

	free(apparmor_path);
	apparmor_path = NULL;

	return EXIT_SUCCESS;
}
#else
int stress_apparmor_supported(void)
{
	pr_inf("apparmor stressor will be skipped, "
		"AppArmor is not available\n");
	return -1;
}

int stress_apparmor(const args_t *args)
{
	return stress_not_implemented(args);

}
#endif
