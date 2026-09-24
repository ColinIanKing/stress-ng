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
	{ NULL,	"snd-timer N",		"start N workers exercise the /dev/snd tiner" },
	{ NULL,	"snd-timer-ops N",	"stop after N timer events" },
	{ NULL,	NULL,			NULL }
};

#if defined(__linux__) &&	\
    defined(HAVE_SOUND_ASOUND_H)

static const int snd_timer_classes[] = {
	SNDRV_TIMER_CLASS_PCM,
	SNDRV_TIMER_CLASS_CARD,
	SNDRV_TIMER_CLASS_GLOBAL,
};

/*
 *  stress_snd_timer()
 */
static int stress_snd_timer(stress_args_t *args)
{
	int fd;
	int rc = EXIT_SUCCESS;
	int flag = 1;
	size_t i;
	static const char dev[] = "/dev/snd/timer";
	struct snd_timer_params tp;
	struct snd_timer_select ts;
	double t_prev = -1.0;
	double t_start;
	double duration;
	double rate;

	fd = open(dev, O_RDONLY | O_SYNC);
	if (fd < 0) {
		pr_inf_skip("%s: failed to open '%s', errno=%d (%s), skipping stressor\n",
			args->name, dev, errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}

	stress_proc_state_set(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_proc_state_set(args->name, STRESS_STATE_RUN);

	flag = 1;
	if (ioctl(fd, SNDRV_TIMER_IOCTL_TREAD, &flag) < 0) {
		pr_fail("%s: ioctl SNDRV_TIMER_IOCTL_TREAD failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		rc = EXIT_FAILURE;
		goto close_fd;
	}

	for (i = 0; i < SIZEOF_ARRAY(snd_timer_classes); i++) {
		(void)shim_memset(&ts, 0, sizeof(ts));
		ts.id.dev_class = snd_timer_classes[i];
		ts.id.dev_sclass = 0;
		ts.id.card = 0;
		ts.id.device = 0;
		ts.id.subdevice = 0;

		if (ioctl(fd, SNDRV_TIMER_IOCTL_SELECT, &ts) == 0)
			break;
	}
	if (i == SIZEOF_ARRAY(snd_timer_classes)) {
		pr_inf_skip("%s: ioctl SNDRV_TIMER_SELECT failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		rc = EXIT_NO_RESOURCE;
		goto close_fd;
	}

	(void)shim_memset(&tp, 0, sizeof(tp));
	tp.flags = SNDRV_TIMER_PSFLG_AUTO;
	tp.ticks = 1;
	tp.filter = (1 << SNDRV_TIMER_EVENT_RESOLUTION) |
		    (1 << SNDRV_TIMER_EVENT_TICK) |
		    (1 << SNDRV_TIMER_EVENT_START) |
		    (1 << SNDRV_TIMER_EVENT_STOP);
	tp.queue_size = 64;
	if (ioctl(fd, SNDRV_TIMER_IOCTL_PARAMS, &tp) < 0) {
		pr_fail("%s: ioctl SNDRV_TIMER_IOCTL_PARAMS failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		rc = EXIT_FAILURE;
		goto close_fd;
	}

	if (ioctl(fd, SNDRV_TIMER_IOCTL_START) < 0) {
		pr_fail("%s: ioctl SNDRV_TIMER_IOCTL_START failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		rc = EXIT_FAILURE;
		goto close_fd;
	}

	t_start = stress_time_now();
	do {
		struct timeval tv;
		int ret;
		fd_set rfds;

		FD_ZERO(&rfds);
		FD_SET(fd, &rfds);

		tv.tv_sec = 0;
		tv.tv_usec = 100000;

		ret = select(fd + 1, &rfds, NULL, NULL, &tv);
		if (ret < 0) {
			if (errno == EINTR)
				continue;
			pr_fail("%s: select failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			rc = EXIT_FAILURE;
			break;
		} else if ((ret > 0) && FD_ISSET(fd, &rfds)) {
			struct snd_timer_tread data;
			ssize_t rret;

			rret = read(fd, &data, sizeof(data));
			if (rret < (ssize_t)sizeof(data)) {
				pr_fail("%s: read failed, got %zd bytes, expecting %zu\n",
					args->name, rret, sizeof(data));
				rc = EXIT_FAILURE;
				break;
			} else {
				double t = (double)data.tstamp.tv_sec +
					   ((double)data.tstamp.tv_nsec * 1.0E-9);
				if (t < t_prev) {
					pr_fail("%s: timestamp %f less than previous timestamp %f\n",
						args->name, t_prev, t);
				}
				(void)ioctl(fd, SNDRV_TIMER_IOCTL_START);
				t_prev = t;
				stress_bogo_inc(args);
			}
		}
	} while (stress_continue(args));
	duration = stress_time_now() - t_start;

	if (ioctl(fd, SNDRV_TIMER_IOCTL_STOP) < 0) {
		pr_fail("%s: ioctl SNDRV_TIMER_IOCTL_STOP failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		rc = EXIT_FAILURE;
		goto close_fd;
	}

	rate = duration > 0.0 ? (double)stress_bogo_get(args) / duration : 0.0;
	stress_metrics_set(args, "snd timer events per sec", rate, STRESS_METRIC_GEOMETRIC_MEAN);

close_fd:
	stress_proc_state_set(args->name, STRESS_STATE_DEINIT);

	(void)close(fd);

	return rc;
}

static const stress_exercises_t exercises[] = {
	STRESS_EX_FEATURE("sound"),

	STRESS_EX_SYSCALL("ioctl"),
	STRESS_EX_SYSCALL("select"),
	STRESS_EX_SYSCALL("read"),

	STRESS_EX_END,
};

const stressor_info_t stress_snd_timer_info = {
	.stressor = stress_snd_timer,
	.classifier = CLASS_SOUND,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.exercises = exercises,
};
#else
const stressor_info_t stress_snd_timer_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_SOUND,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "Linux only stressor and/or sound/asound.h not supported",
};
#endif
