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
	{ NULL,	"apparmor",	  "start N workers exercising AppArmor interfaces" },
	{ NULL,	"apparmor-ops N", "stop after N bogo AppArmor worker bogo operations" },
	{ NULL,	NULL,		  NULL }
};

#if defined(HAVE_APPARMOR) &&		\
    defined(HAVE_SYS_APPARMOR_H) && 	\
    defined(HAVE_SYS_SELECT_H)

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
static int stress_apparmor_supported(void)
{
	int fd;
	char path[PATH_MAX];

	if (!stress_check_capability(SHIM_CAP_MAC_ADMIN)) {
		pr_inf("apparmor stressor will be skipped, "
			"need to be running with CAP_SYS_ADMIN "
			"rights for this stressor\n");
		return -1;
	}

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
				"stress-ng needs CAP_MAC_ADMIN "
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
	while (g_keep_stressing_flag &&
	       apparmor_run &&
	       (i < (4096 * APPARMOR_BUF_SZ))) {
		ssize_t ret, sz = 1 + (mwc32() % sizeof(buffer));
redo:
		if (!g_keep_stressing_flag || !apparmor_run)
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

	if (!g_keep_stressing_flag || !apparmor_run)
		return;

	/* Don't want to go too deep */
	if (depth > 8)
		return;

	dp = opendir(path);
	if (dp == NULL)
		return;

	while ((d = readdir(dp)) != NULL) {
		char name[PATH_MAX];

		if (!g_keep_stressing_flag || !apparmor_run)
			break;
		if (stress_is_dot_filename(d->d_name))
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
	const args_t *args,
	const uint64_t max_ops,
	uint64_t *counter,
	apparmor_func func)
{
	pid_t pid;

again:
	pid = fork();
	if (pid < 0) {
		if (g_keep_stressing_flag &&
		    ((errno == EAGAIN) || (errno == ENOMEM)))
			goto again;
		return -1;
	}
	if (pid == 0) {
		int ret = EXIT_SUCCESS;

		if (!g_keep_stressing_flag)
			goto abort;

		if (stress_sighandler(args->name, SIGALRM,
				      stress_apparmor_alrm_handler, NULL) < 0) {
			_exit(EXIT_FAILURE);
		}

		(void)setpgid(0, g_pgrp);
		stress_parent_died_alarm();
		if (!g_keep_stressing_flag || !apparmor_run)
			goto abort;
		ret = func(args->name, max_ops, counter);
abort:
		free(apparmor_path);
		kill(args->pid, SIGUSR1);
		_exit(ret);
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
	} while (g_keep_stressing_flag &&
		 apparmor_run &&
		 (!max_ops || *counter < max_ops));

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
	} while (g_keep_stressing_flag &&
		 apparmor_run &&
		 (!max_ops || *counter < max_ops));

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

			pr_inf("%s: aa_kernel_interface_replace_policy() failed, "
				"errno=%d (%s)\n", name, errno,
				strerror(errno));
		}

		/*
		 *  Removal may fail if another stressor has already removed the
		 *  policy, so we may get ENOENT
		 */
		ret = aa_kernel_interface_remove_policy(kern_if,
			"/usr/bin/pulseaudio-eg");
		if (ret < 0) {
			if (errno != ENOENT) {
				aa_kernel_interface_unref(kern_if);

				pr_fail("%s: aa_kernel_interface_remove_policy() failed, "
					"errno=%d (%s)\n", name, errno,
					strerror(errno));
				rc = EXIT_FAILURE;
				break;
			}
		}
		aa_kernel_interface_unref(kern_if);

		(*counter)++;
	} while (g_keep_stressing_flag &&
		 apparmor_run &&
                 (!max_ops || *counter < max_ops));

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

		(void)memcpy(copy, g_apparmor_data, g_apparmor_data_len);
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
				pr_inf("%s: aa_kernel_interface_replace_policy() failed, "
					"errno=%d (%s)\n", name, errno,
					strerror(errno));
			}
		}
		aa_kernel_interface_unref(kern_if);
		(*counter)++;
	} while (g_keep_stressing_flag &&
		 apparmor_run &&
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
static int stress_apparmor(const args_t *args)
{
	const size_t n = SIZEOF_ARRAY(apparmor_funcs);
	pid_t pids[n];
	size_t i;
	uint64_t *counters, max_ops, ops_per_child, ops;
	const size_t counters_sz = n * sizeof(*counters);

	if (stress_sighandler(args->name, SIGUSR1, stress_apparmor_usr1_handler, NULL) < 0)
		return EXIT_FAILURE;

	counters = mmap(NULL, counters_sz, PROT_READ | PROT_WRITE,
		MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (counters == MAP_FAILED) {
		pr_err("%s: mmap failed: errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_FAILURE;
	}
	(void)memset(counters, 0, counters_sz);

	if (args->max_ops > 0) {
		if ((args->max_ops < n)) {
			max_ops = n;
			ops_per_child = 1;
		} else {
			max_ops = args->max_ops;
			ops_per_child = (args->max_ops / n);
		}
		/*
		 * ops is the number of ops left over when dividing
		 * max_ops amongst the child processes
		 */
		ops = max_ops - (ops_per_child * n);
	} else {
		max_ops = 0;
		ops_per_child = 0;
		ops = 0;
	}

	for (i = 0; i < n; i++) {
		pids[i] = apparmor_spawn(args, 
			ops_per_child + ((i == 0) ? ops : 0),
			&counters[i], apparmor_funcs[i]);
	}
	while (keep_stressing()) {
		uint64_t tmp_counter = 0;

		(void)select(0, NULL, NULL, NULL, NULL);

		for (i = 0; i < n; i++)
			tmp_counter += counters[i];

		if (max_ops && tmp_counter >= max_ops)
			break;
	}

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
			add_counter(args, counters[i]);
		}
	}
	(void)munmap(counters, counters_sz);

	free(apparmor_path);
	apparmor_path = NULL;

	return EXIT_SUCCESS;
}

stressor_info_t stress_apparmor_info = {
	.stressor = stress_apparmor,
	.supported = stress_apparmor_supported,
	.class = CLASS_OS | CLASS_SECURITY,
	.help = help
};

#else

static int stress_apparmor_supported(void)
{
	pr_inf("apparmor stressor will be skipped, "
		"AppArmor is not available\n");
	return -1;
}

stressor_info_t stress_apparmor_info = {
	.stressor = stress_not_implemented,
	.supported = stress_apparmor_supported,
	.class = CLASS_OS | CLASS_SECURITY,
	.help = help
};
#endif
