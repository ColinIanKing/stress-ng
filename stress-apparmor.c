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
#include "core-builtin.h"
#include "core-capabilities.h"
#include "core-killpid.h"
#include "core-lock.h"
#include "core-mmap.h"

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

typedef int (*stress_apparmor_func)(stress_args_t *args);

typedef struct {
	void *counter_lock;
	void *failure_lock;
	uint32_t failure_count;
} stress_apparmor_shared_info_t;

static stress_apparmor_shared_info_t *stress_apparmor_shared_info;
static volatile bool apparmor_run = true;
static char *apparmor_path = NULL;
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

static bool stress_apparmor_stress_continue_inc(stress_args_t *args, bool inc)
{
	/* fast check */
	if (!apparmor_run)
		return false;
	if (!stress_apparmor_shared_info)
		return false;

	return stress_bogo_inc_lock(args, stress_apparmor_shared_info->counter_lock, inc);
}

static void stress_apparmor_failure_inc(void)
{
	if (!stress_apparmor_shared_info)
		return;
	if (stress_lock_acquire(stress_apparmor_shared_info->failure_lock) < 0)
		return;
	stress_apparmor_shared_info->failure_count++;
	(void)stress_lock_release(stress_apparmor_shared_info->failure_lock);
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

/*
 *  stress_apparmor_read()
 *	read a proc file
 */
static void stress_apparmor_read(
	stress_args_t *args,
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
		ssize_t ret;
		const ssize_t sz = 1 + stress_mwc32modn((uint32_t)sizeof(buffer));
redo:
		if (!stress_apparmor_stress_continue_inc(args, false))
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
	stress_args_t *args,
	const char *path,
	const bool recurse,
	const int depth)
{
	DIR *dp;
	const struct dirent *d;

	if (!stress_apparmor_stress_continue_inc(args, false))
		return;

	/* Don't want to go too deep */
	if (depth > 8)
		return;

	dp = opendir(path);
	if (dp == NULL)
		return;

	while ((d = readdir(dp)) != NULL) {
		char name[PATH_MAX];

		if (!stress_apparmor_stress_continue_inc(args, false))
			break;
		if (stress_is_dot_filename(d->d_name))
			continue;
		switch (shim_dirent_type(path, d)) {
		case SHIM_DT_DIR:
			if (recurse) {
				(void)stress_mk_filename(name, sizeof(name), path, d->d_name);
				stress_apparmor_dir(args, name, recurse, depth + 1);
			}
			break;
		case SHIM_DT_REG:
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
static void apparmor_spawn(
	stress_args_t *args,
	stress_apparmor_func func,
	stress_pid_t **s_pids_head,
	stress_pid_t *s_pid)
{
again:
	s_pid->pid = fork();
	if (s_pid->pid < 0) {
		if (stress_redo_fork(args, errno))
			goto again;
		return;
	} else if (s_pid->pid == 0) {
		int ret = EXIT_SUCCESS;

		stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
		s_pid->pid = getpid();
		stress_sync_start_wait_s_pid(s_pid);
		stress_set_proc_state(args->name, STRESS_STATE_RUN);

		if (!stress_apparmor_stress_continue_inc(args, false))
			goto abort;

		if (stress_sighandler(args->name, SIGALRM,
				      stress_apparmor_alrm_handler, NULL) < 0) {
			_exit(EXIT_FAILURE);
		}

		(void)sched_settings_apply(true);
		stress_parent_died_alarm();
		if (!stress_apparmor_stress_continue_inc(args, false))
			goto abort;
		ret = func(args);
abort:
		free(apparmor_path);
		(void)shim_kill(args->pid, SIGUSR1);
		_exit(ret);
	} else {
		stress_sync_start_s_pid_list_add(s_pids_head, s_pid);
	}
}

/*
 *  apparmor_stress_profiles()
 *	hammer profile reading
 */
static int apparmor_stress_profiles(stress_args_t *args)
{
	char path[PATH_MAX];

	(void)stress_mk_filename(path, sizeof(path), apparmor_path, "profiles");

	do {
		stress_apparmor_read(args, path);
	} while (stress_apparmor_stress_continue_inc(args, true));

	return EXIT_SUCCESS;
}

/*
 *  apparmor_stress_features()
 *	hammer features reading
 */
static int apparmor_stress_features(stress_args_t *args)
{
	char path[PATH_MAX];

	(void)stress_mk_filename(path, sizeof(path), apparmor_path, "features");

	do {
		stress_apparmor_dir(args, path, true, 0);
	} while (stress_apparmor_stress_continue_inc(args, true));

	return EXIT_SUCCESS;
}

/*
 *  apparmor_stress_kernel_interface()
 *	load/replace/unload stressing
 */
static int apparmor_stress_kernel_interface(stress_args_t *args)
{
	int rc = EXIT_SUCCESS;
	aa_kernel_interface *kern_if;

	/*
	 *  Try and create a lot of contention and load
	 */
	do {
		int ret;

		ret = aa_kernel_interface_new(&kern_if, NULL, NULL);
		if (ret < 0) {
			pr_fail("%s: aa_kernel_interface_new() failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			stress_apparmor_failure_inc();
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
				stress_apparmor_failure_inc();
				rc = EXIT_FAILURE;
				break;
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
				stress_apparmor_failure_inc();
				rc = EXIT_FAILURE;
				break;
			}
		}
		aa_kernel_interface_unref(kern_if);

	} while (stress_apparmor_stress_continue_inc(args, true));

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
	const uint32_t n = stress_mwc32modn(17);

	for (i = 0; i < n; i++) {
		const uint32_t rnd = stress_mwc32modn((uint32_t)len);

		copy[rnd] ^= (1U << ((rnd >> 16) & 7));
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

	p = (p >= len) ? 0 : p;
	copy[p] ^= (1U << (p & 7));
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

	p = (p >= len) ? 0 : p;
	copy[p] &= ~(1U << (p & 7));
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

	p = (p >= len) ? 0 : p;
	copy[p] |= (1U << (p & 7));
	p++;
}

/*
 *  apparmor_corrupt_flip_byte_random()
 *	randomly flip entire bytes
 */
static inline void apparmor_corrupt_flip_byte_random(
	char *copy, const size_t len)
{
	copy[stress_mwc32modn((uint32_t)len)] ^= 0xff;
}

/*
 *  apparmor_corrupt_clr_bits_random()
 *	randomly clear bits
 */
static inline void apparmor_corrupt_clr_bits_random(
	char *copy, const size_t len)
{
	uint32_t i;
	const uint32_t n = stress_mwc32modn(17);

	for (i = 0; i < n; i++) {
		const uint32_t rnd = stress_mwc32modn((uint32_t)len);

		copy[rnd] &= ~(1U << ((rnd >> 16) & 7));
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
	const uint32_t n = stress_mwc32modn(17);

	for (i = 0; i < n; i++) {
		const uint32_t rnd = stress_mwc32modn((uint32_t)len);

		copy[rnd] |= (1U << ((rnd >> 16) & 7));
	}
}

/*
 *  apparmor_corrupt_clr_byte_random()
 *	randomly clear an entire byte
 */
static inline void apparmor_corrupt_clr_byte_random(
	char *copy, const size_t len)
{
	copy[stress_mwc32modn((uint32_t)len)] = 0;
}

/*
 *  apparmor_corrupt_set_byte_random()
 *	randomly set an entire byte
 */
static inline void apparmor_corrupt_set_byte_random(
	char *copy, const size_t len)
{
	copy[stress_mwc32modn((uint32_t)len)] = (char)0xff;
}

/*
 *  apparmor_corrupt_flip_bits_random_burst
 *	randomly flip a burst of contiguous bits
 */
static inline void apparmor_corrupt_flip_bits_random_burst(
	char *copy, const size_t len)
{
	uint32_t i;
	size_t p = (size_t)stress_mwc32modn((uint32_t)len * sizeof(*copy));

	for (i = 0; i < 32; i++) {
		p = (p >= len) ? 0 : p;
		copy[p] ^= (1U << (p & 7));
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
	const uint32_t rnd = stress_mwc32modn((uint32_t)len);

	copy[rnd] ^= 1U << stress_mwc8modn(8);
}

/*
 *  apparmor_stress_corruption()
 *	corrupt data and see if we can oops the loader
 *	parser.
 */
static int apparmor_stress_corruption(stress_args_t *args)
{
	int rc = EXIT_SUCCESS, i = (int)args->instance, ret = -1;
	int j = 0;
	aa_kernel_interface *kern_if;

	/*
	 *  Lets feed AppArmor with some bit corrupted data...
	 */
	do {
		if (ret < 0 || j > 1024) {
			(void)shim_memcpy(data_copy, g_apparmor_data, g_apparmor_data_len);
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
			stress_apparmor_failure_inc();
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
			(void)shim_memcpy(data_copy, data_prev, g_apparmor_data_len);
		} else {
			(void)shim_memcpy(data_prev, data_copy, g_apparmor_data_len);
		}
		aa_kernel_interface_unref(kern_if);
		i++;
	} while (stress_apparmor_stress_continue_inc(args, true));

	return rc;
}

static const stress_apparmor_func apparmor_funcs[] = {
	apparmor_stress_profiles,
	apparmor_stress_features,
	apparmor_stress_kernel_interface,
	apparmor_stress_corruption,
};

#define MAX_APPARMOR_FUNCS	(SIZEOF_ARRAY(apparmor_funcs))

/*
 *  stress_apparmor()
 *	stress AppArmor
 */
static int stress_apparmor(stress_args_t *args)
{
	stress_pid_t *s_pids, *s_pids_head = NULL;
	size_t i;
	int rc = EXIT_NO_RESOURCE;

	if (stress_sighandler(args->name, SIGUSR1, stress_sighandler_nop, NULL) < 0) {
		return EXIT_FAILURE;
	}

	s_pids = stress_sync_s_pids_mmap(MAX_APPARMOR_FUNCS);
	if (s_pids == MAP_FAILED) {
		pr_inf_skip("%s: failed to mmap %zu PIDs%s, skipping stressor\n",
			args->name, MAX_APPARMOR_FUNCS, stress_get_memfree_str());
                return EXIT_NO_RESOURCE;
	}

	stress_apparmor_shared_info = (stress_apparmor_shared_info_t *)
		stress_mmap_populate(NULL, sizeof(*stress_apparmor_shared_info),
			PROT_READ | PROT_WRITE,
			MAP_ANONYMOUS | MAP_SHARED, -1, 0);
	if (stress_apparmor_shared_info == MAP_FAILED) {
		pr_inf_skip("%s: failed to allocated shared memory%s, skipping stressor\n",
			args->name, stress_get_memfree_str());
		goto err_free_s_pids;
	}
	stress_set_vma_anon_name(stress_apparmor_shared_info, sizeof(*stress_apparmor_shared_info), "lock-counter");
	data_copy = (char *)malloc(g_apparmor_data_len);
	if (!data_copy) {
		pr_inf_skip("%s: failed to allocate apparmor data copy buffer%s, skipping stressor\n",
			args->name, stress_get_memfree_str());
		goto err_free_shared_info;
	}
	data_prev = (char *)malloc(g_apparmor_data_len);
	if (!data_prev) {
		pr_inf_skip("%s: failed to allocate apparmor data prev buffer%s, skipping stressor\n",
			args->name, stress_get_memfree_str());
		goto err_free_data_copy;
	}
	stress_apparmor_shared_info->counter_lock = stress_lock_create("counter");
	if (!stress_apparmor_shared_info->counter_lock) {
		pr_inf_skip("%s: failed to create counter lock. skipping stressor\n", args->name);
		goto err_free_data_prev;
	}
	stress_apparmor_shared_info->failure_lock = stress_lock_create("failure");
	if (!stress_apparmor_shared_info->counter_lock) {
		pr_inf_skip("%s: failed to create failure counter lock. skipping stressor\n", args->name);
		goto err_free_counter_lock;
	}

	for (i = 0; i < MAX_APPARMOR_FUNCS; i++) {
		stress_sync_start_init(&s_pids[i]);
		apparmor_spawn(args, apparmor_funcs[i], &s_pids_head, &s_pids[i]);
	}

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_sync_start_cont_list(s_pids_head);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	while (stress_apparmor_stress_continue_inc(args, false)) {
#if defined(HAVE_SELECT)
		(void)select(0, NULL, NULL, NULL, NULL);
#else
		(void)shim_pause();
#endif
	}

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	/* Wakeup, time to die */
	stress_kill_and_wait_many(args, s_pids, MAX_APPARMOR_FUNCS, SIGALRM, true);

	free(apparmor_path);
	apparmor_path = NULL;

	rc = (stress_apparmor_shared_info->failure_count > 0) ? EXIT_FAILURE : EXIT_SUCCESS;

	(void)stress_lock_destroy(stress_apparmor_shared_info->failure_lock);
err_free_counter_lock:
	(void)stress_lock_destroy(stress_apparmor_shared_info->counter_lock);
err_free_data_prev:
	free(data_prev);
err_free_data_copy:
	free(data_copy);
err_free_shared_info:
	(void)munmap((void *)stress_apparmor_shared_info, sizeof(*stress_apparmor_shared_info));
err_free_s_pids:
	(void)stress_sync_s_pids_munmap(s_pids, MAX_APPARMOR_FUNCS);

	return rc;
}

const stressor_info_t stress_apparmor_info = {
	.stressor = stress_apparmor,
	.supported = stress_apparmor_supported,
	.classifier = CLASS_OS | CLASS_SECURITY,
	.verify = VERIFY_ALWAYS,
	.help = help
};

#else

static int stress_apparmor_supported(const char *name)
{
	pr_inf_skip("%s: stressor will be skipped, "
		"AppArmor is not available\n", name);
	return -1;
}

const stressor_info_t stress_apparmor_info = {
	.stressor = stress_unimplemented,
	.supported = stress_apparmor_supported,
	.classifier = CLASS_OS | CLASS_SECURITY,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without sys/apparmor.h"
};
#endif
