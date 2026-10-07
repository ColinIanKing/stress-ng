/*
 * Copyright (C) 2026      Colin Ian King.
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
#include "core-killpid.h"

#include <sys/select.h>
#include <sys/ioctl.h>
#if defined(HAVE_SOUND_ASOUND_H)
#include <sound/asound.h>
#endif

static const stress_help_t help[] = {
	{ NULL,	"snd-pcm N",		"start N workers exercising ioctls on /dev/snd pcm drivers" },
	{ NULL,	"snd-pcm-ops N",	"stop after N pcm bogo itctl operations" },
	{ NULL,	NULL,			NULL }
};

#if defined(__linux__) &&	\
    defined(HAVE_SOUND_ASOUND_H)

typedef struct stress_snd_pcm_dev_info {
	char *path;
	uint64_t ioctl_calls;
	double duration;
} stress_snd_pcm_dev_info_t;

/*
 *  stress_snd_pcm()
 */
static int stress_snd_pcm(stress_args_t *args)
{
	int rc = EXIT_SUCCESS;
	size_t i;
	size_t n_snd_devs = 0;
	static const char dev_snd_path[] = "/dev/snd";
	double t_start;
	DIR *dp;
	struct dirent *de;
	stress_snd_pcm_dev_info_t *snd_devs;

	dp = opendir(dev_snd_path);
	if (!dp) {
		pr_inf_skip("%s: failed to open '%s', skipping stressor\n",
			args->name, dev_snd_path);
		return EXIT_NO_RESOURCE;
	}

	while ((de = readdir(dp)) != NULL) {
		const size_t len = strlen(de->d_name);

		if ((len > 0) && (de->d_name[len - 1] == 'p'))
			n_snd_devs++;
	}

	if (n_snd_devs == 0) {
		pr_inf_skip("%s: failed to find any pcm devices in '%s', skipping stressor\n",
			args->name, dev_snd_path);
		(void)closedir(dp);
		return EXIT_NO_RESOURCE;
	}

	snd_devs = calloc(n_snd_devs, sizeof(*snd_devs));
	if (!snd_devs) {
		pr_inf_skip("%s: failed to allocate %zu pcm device structs, skipping stressor\n",
			args->name, n_snd_devs);
		(void)closedir(dp);
		return EXIT_NO_RESOURCE;
	}
	rewinddir(dp);
	for (i = 0; (i < n_snd_devs) && (de = readdir(dp)) != NULL;) {
		const size_t len = strlen(de->d_name);
		char path[PATH_MAX];

		if ((len > 0) && ((de->d_name[len - 1] == 'p') || de->d_name[len - 1] == 'c')) {
			(void)snprintf(path, sizeof(path), "%s/%s", dev_snd_path, de->d_name);
			snd_devs[i].path = strdup(path);
			if (snd_devs[i].path) {
				snd_devs[i].ioctl_calls = 0U;
				snd_devs[i].duration = 0.0;
				i++;
			}
		}
	}
	n_snd_devs = i;
	(void)closedir(dp);

	stress_proc_state_set(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_proc_state_set(args->name, STRESS_STATE_RUN);

	do {
		for (i = 0; i < n_snd_devs; i++) {
			int fd;
			int j;

			fd = open(snd_devs[i].path, O_RDWR | O_NONBLOCK);
			if (fd < 0)
				continue;

			t_start = stress_time_now();
#if defined(SNDRV_PCM_IOCTL_PVERSION)
			{
				int pcm_version = 0;

				if (ioctl(fd, SNDRV_PCM_IOCTL_PVERSION, &pcm_version) < 0) {
					pr_fail("%s: ioctl SNDRV_PCM_IOCTL_PVERSION on '%s' failed, errno=%d (%s)\n",
						args->name, snd_devs[i].path, errno, strerror(errno));
				} else {
					snd_devs[i].ioctl_calls++;
				}
			}
#endif

#if defined(SNDRV_PCM_IOCTL_STATUS)
			{
				struct snd_pcm_status pcm_status;

				(void)shim_memset(&pcm_status, 0, sizeof(pcm_status));
				if (ioctl(fd, SNDRV_PCM_IOCTL_STATUS, &pcm_status) < 0) {
					pr_fail("%s: ioctl SNDRV_PCM_IOCTL_STATUS on '%s' failed, errno=%d (%s)\n",
						args->name, snd_devs[i].path, errno, strerror(errno));
				} else {
					snd_devs[i].ioctl_calls++;
				}
			}
#endif

#if defined(SNDRV_PCM_IOCTL_STATUS_EXT)
			{
				struct snd_pcm_status pcm_status;

				(void)shim_memset(&pcm_status, 0, sizeof(pcm_status));
				if (ioctl(fd, SNDRV_PCM_IOCTL_STATUS_EXT, &pcm_status) < 0) {
					pr_fail("%s: ioctl SNDRV_PCM_IOCTL_STATUS_EXT on '%s' failed, errno=%d (%s)\n",
						args->name, snd_devs[i].path, errno, strerror(errno));
				} else {
					snd_devs[i].ioctl_calls++;
				}
			}
#endif

#if defined(SNDRV_PCM_IOCTL_CHANNEL_INFO) && 0
			{
				struct snd_pcm_channel_info pcm_channel_info;

				(void)shim_memset(&pcm_channel_info, 0, sizeof(pcm_channel_info));
				if (ioctl(fd, SNDRV_PCM_IOCTL_CHANNEL_INFO, &pcm_channel_info) < 0) {
					pr_fail("%s: ioctl SNDRV_PCM_IOCTL_CHANNEL_INFO on '%s' failed, errno=%d (%s)\n",
						args->name, snd_devs[i].path, errno, strerror(errno));
				} else {
					snd_devs[i].ioctl_calls++;
				}
			}
#endif

#if defined(SNDRV_PCM_IOCTL_HW_REFINE)
			{
				struct snd_pcm_hw_params hw_params;

				(void)shim_memset(&hw_params, 0, sizeof(hw_params));
				for (j = SNDRV_PCM_HW_PARAM_FIRST_MASK; j <= SNDRV_PCM_HW_PARAM_LAST_MASK; j++) {
					struct snd_mask *sm = &hw_params.masks[j - SNDRV_PCM_HW_PARAM_FIRST_MASK];

					(void)shim_memset(&sm->bits, 0xff, sizeof(sm->bits));
				}
				for (j = SNDRV_PCM_HW_PARAM_FIRST_INTERVAL; j <= SNDRV_PCM_HW_PARAM_LAST_INTERVAL; j++) {
					struct snd_interval *si = &hw_params.intervals[j - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL];
					si->min = 0;
					si->max = ~0;
				}
				hw_params.rmask = ~0U;
				hw_params.cmask = 0;
				hw_params.info = ~0U;

				if (ioctl(fd, SNDRV_PCM_IOCTL_HW_REFINE, &hw_params) == 0)
					snd_devs[i].ioctl_calls++;

			}
#endif

#if defined(SNDRV_PCM_IOCTL_INFO)
			{
				struct snd_pcm_info info;

				(void)shim_memset(&info, 0, sizeof(info));
				if (ioctl(fd, SNDRV_PCM_IOCTL_INFO, &info) < 0) {
					pr_fail("%s: ioctl SNDRV_PCM_IOCTL_CHANNEL_INFO on '%s' failed, errno=%d (%s)\n",
						args->name, snd_devs[i].path, errno, strerror(errno));
				} else {
					snd_devs[i].ioctl_calls++;
				}
			}
#endif
			snd_devs[i].duration += stress_time_now() - t_start;
			(void)close(fd);
			stress_bogo_inc(args);
		}
	} while (stress_continue(args));

	for (i = 0; i < n_snd_devs; i++) {
		char buf[64];

		const double duration = snd_devs[i].duration;
		const double rate = (duration > 0.0) ?
					(double)snd_devs[i].ioctl_calls / duration : 0;

		(void)snprintf(buf, sizeof(buf), "ioctls on %s per sec",
				snd_devs[i].path);
		stress_metrics_set(args, buf, rate, STRESS_METRIC_GEOMETRIC_MEAN);
	}

	stress_proc_state_set(args->name, STRESS_STATE_DEINIT);

	for (i = 0; i < n_snd_devs; i++)
		free(snd_devs[i].path);
	free(snd_devs);

	return rc;
}

static const stress_exercises_t exercises[] = {
	STRESS_EX_FEATURE("sound"),

	STRESS_EX_SYSCALL("ioctl"),
	STRESS_EX_SYSCALL("select"),
	STRESS_EX_SYSCALL("read"),

	STRESS_EX_END,
};

const stressor_info_t stress_snd_pcm_info = {
	.stressor = stress_snd_pcm,
	.classifier = CLASS_SOUND,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.exercises = exercises,
};
#else
const stressor_info_t stress_snd_pcm_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_SOUND,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "Linux only stressor and/or sound/asound.h not supported",
};
#endif
