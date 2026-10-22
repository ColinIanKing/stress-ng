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
#include "core-madvise.h"
#include "core-signal.h"
#include "core-sort.h"

#include <sys/ioctl.h>

#if defined(HAVE_LINUX_VIDEODEV2_H)
#include <linux/videodev2.h>
#endif

#define MAX_INDEX	(64)

static const stress_help_t help[] = {
	{ NULL, "v4l2 N",		"start N workers" },
	{ NULL,	"v4l2-ops N",		"stop after N bogo operations" },
	{ NULL,	NULL,			NULL }
};

#if defined(HAVE_LINUX_VIDEODEV2_H)

typedef struct stress_v4l2_dev {
	char *path;
	char driver[32];
} stress_v4l2_dev_t;

typedef int (*stress_v4l2_func_t)(stress_args_t *args, const int fd, stress_v4l2_dev_t *dev);

#if (defined(VIDIOC_ENUMAUDIO) &&	\
     defined(HAVE_V4L2_AUDIO)) ||	\
    (defined(VIDIOC_ENUMAUDOUT) &&	\
     defined(HAVE_V4L2_AUDIOOUT)) ||	\
    (defined(VIDIOC_ENUM_FMT) && 	\
     defined(HAVE_V4L2_FMTDESC) &&	\
     defined(HAVE_V4L2_BUF_TYPE)) ||	\
    (defined(VIDIOC_QUERYMENU) &&	\
     defined(HAVE_V4L2_QUERYMENU)) ||	\
    (defined(VIDIOC_G_MODULATOR) &&	\
     defined(HAVE_V4L2_MODULATOR))
static const uint32_t indices[] = {
	0x00000000UL,
	0x00000001UL,
	0x7ffffffeUL,
	0x7fffffffUL,
	0x80000000UL,
	0x80000001UL,
	0xfffffffeUL,
	0xffffffffUL,
};
#endif

#if defined(HAVE_V4L2_BUF_TYPE) &&	\
    (defined(VIDIOC_ENUM_FMT) &&	\
     defined(HAVE_V4L2_FMTDESC))
static const enum v4l2_buf_type buf_types[] = {
	V4L2_BUF_TYPE_VIDEO_CAPTURE,
	V4L2_BUF_TYPE_VIDEO_OUTPUT,
	V4L2_BUF_TYPE_VIDEO_OVERLAY,
	V4L2_BUF_TYPE_VBI_CAPTURE,
	V4L2_BUF_TYPE_VBI_OUTPUT,
	V4L2_BUF_TYPE_SLICED_VBI_CAPTURE,
	V4L2_BUF_TYPE_SLICED_VBI_OUTPUT,
	V4L2_BUF_TYPE_VIDEO_OUTPUT_OVERLAY,
	V4L2_BUF_TYPE_PRIVATE,
};
#endif

#if defined(HAVE_V4L2_BUF_TYPE) &&		\
    ((defined(VIDIOC_G_SLICED_VBI_CAP) &&	\
      defined(HAVE_V4L2_SLICED_VBI_CAP)) ||	\
     (defined(VIDIOC_G_PARM) &&			\
      defined(HAVE_V4L2_STREAMPARM)))
static const enum v4l2_buf_type buf_check_types[] = {
	0,
	V4L2_BUF_TYPE_VIDEO_CAPTURE,
	V4L2_BUF_TYPE_VIDEO_OUTPUT,
	V4L2_BUF_TYPE_VIDEO_OVERLAY,
	V4L2_BUF_TYPE_VBI_CAPTURE,
	V4L2_BUF_TYPE_VBI_OUTPUT,
	V4L2_BUF_TYPE_VIDEO_OUTPUT_OVERLAY,
	V4L2_BUF_TYPE_PRIVATE,
	0x7fffffffUL,
	0x80000000UL,
	0x80000001UL,
	0xfffffffeUL,
	0xffffffffUL,
};
#endif

#if defined(HAVE_V4L2_BUF_TYPE) &&	\
    ((defined(VIDIOC_G_AUDIO) &&	\
      defined(HAVE_V4L2_AUDIO)) ||	\
     (defined(VIDIOC_CROPCAP) &&	\
      defined(HAVE_V4L2_CROPCAP)))
static const enum v4l2_buf_type buf_crop_types[] = {
	V4L2_BUF_TYPE_VIDEO_CAPTURE,
	V4L2_BUF_TYPE_VIDEO_OUTPUT,
	V4L2_BUF_TYPE_VIDEO_OVERLAY,
};
#endif

/*
 *  stress_v4l2_devs_get()
 *	get all /dev/video* devices
 */
static size_t stress_v4l2_devs_get(
	stress_args_t *args,
	stress_v4l2_dev_t **devs)
{
	DIR *dp;
	struct dirent *de;
	size_t i;
	size_t n_devs;
	stress_v4l2_dev_t *new_devs;

	if (!devs) {
		pr_inf_skip("%s: bad devs pointer, skipping stressor\n", args->name);
		return 0;
	}

	n_devs = 0;
	*devs = NULL;

	dp = opendir("/dev");
	if (!dp)
		goto ret_n_devs;

	for (i = 0; (de = readdir(dp)) != NULL; ) {
		if (strncmp(de->d_name, "video", 5))
			continue;
		i++;
	}

	if (i == 0)
		goto dp_close;

	new_devs = calloc(i, sizeof(*new_devs));
	if (!new_devs) {
		pr_inf_skip("%s: failed to allocate %zu video device structures, skipping stressor\n",
			args->name, i);
		closedir(dp);
		return 0;
	}

	rewinddir(dp);
	for (n_devs = 0; (de = readdir(dp)) != NULL; ) {
		char path[PATH_MAX];

		if (strncmp(de->d_name, "video", 5))
			continue;

		(void)snprintf(path, sizeof(path), "/dev/%s", de->d_name);
		new_devs[n_devs].path = strdup(path);
		if (!new_devs[n_devs].path)
			break;
		n_devs++;
	}

	*devs = new_devs;
dp_close:
	(void)closedir(dp);

ret_n_devs:
	if (n_devs == 0)
		pr_inf_skip("%s: failed to find any /dev/video devices, skipping stressor\n", args->name);

	return n_devs;
}

/*
 *  stress_v4l2_devs_free()
 *  	free device info
 */
static void stress_v4l2_devs_free(
	stress_v4l2_dev_t *devs,
	const size_t n_devs)
{
	if (devs) {
		size_t i;

		for (i = 0; i < n_devs; i++) {
			if (devs[i].path)
				free(devs[i].path);
		}
		free(devs);
	}
}

#if defined(_IO)
static int stress_v4l2_dev_bad_ioctl_cmd(
	stress_args_t *args,
	const stress_v4l2_dev_t *dev,
	const int fd,
	const unsigned long cmd,
	const char *cmd_str)
{
	int ret;

	errno = 0;
	ret = ioctl(fd, cmd, NULL);
	if (ret != -1) {
		pr_fail("%s: ioctl %s on %s returned %d, expecting -1\n",
			args->name, dev->path, cmd_str, ret);
		return -1;
	}
	if ((errno != EINVAL) && (errno != ENOTTY))  {
		pr_fail("%s: ioctl %s on %s got unexpected errno %d (%s)\n",
			args->name, dev->path, cmd_str, errno, strerror(errno));
		return -1;
	}

	return 0;
}

#define STRESS_V4L2_DEV_BAD_IOCTL_CMD(fd, cmd)	\
	stress_v4l2_dev_bad_ioctl_cmd(args, dev, fd, cmd, STRESS_XSTRINGIFY(cmd))

/*
 *  stress_v4l2_dev_bad_ioctl()
 *	exercise bad ioctl commands
 */
static int stress_v4l2_dev_bad_ioctl(
	stress_args_t *args,
	const int fd,
	stress_v4l2_dev_t *dev)
{
	int ret = 0;

        if (STRESS_V4L2_DEV_BAD_IOCTL_CMD(fd, _IO(0, 0)) < 0)
		ret = -1;
        if (STRESS_V4L2_DEV_BAD_IOCTL_CMD(fd, _IO(0, 0xff)) < 0)
		ret = -1;
        if (STRESS_V4L2_DEV_BAD_IOCTL_CMD(fd, _IO(0xff, 0xff)) < 0)
		ret = -1;
        if (STRESS_V4L2_DEV_BAD_IOCTL_CMD(fd, _IO(0xff, stress_mwc8())) < 0)
		ret = -1;
        if (STRESS_V4L2_DEV_BAD_IOCTL_CMD(fd, _IO('v', 0xff)) < 0)
		ret = -1;
        if (STRESS_V4L2_DEV_BAD_IOCTL_CMD(fd, _IO('V', 0xff)) < 0)
		ret = -1;

	return ret;
}
#endif

#if defined(VIDIOC_ENUMAUDIO) &&	\
    defined(HAVE_V4L2_AUDIO)
static int stress_v4l2_dev_vidioc_enum_audio_ioctl(
	stress_args_t *args,
	const int fd,
	const stress_v4l2_dev_t *dev,
	const uint32_t idx)
{
	struct v4l2_audio audio;
	int ret;
	int rc = 0;

	(void)shim_memset(&audio, 0, sizeof(audio));
	audio.index = idx;
	errno = 0;
	ret = ioctl(fd, VIDIOC_ENUMAUDIO, &audio);
	if ((ret < 0) && (errno != ENOTTY) && (errno != EINVAL) && (errno != ENODATA)) {
		pr_fail("%s: ioctl VIDIOC_ENUMAUDIO on %s failed, errno=%d (%s)\n",
			args->name, dev->path, errno, strerror(errno));
		rc = -1;
	}
	return rc;
}

/*
 *  stress_v4l2_dev_vidioc_enum_audio
 *	exercise VIDIOC_ENUMAUDIO
 */
static int stress_v4l2_dev_vidioc_enum_audio(
	stress_args_t *args,
	const int fd,
	stress_v4l2_dev_t *dev)
{
	uint32_t idx;
	size_t i;
	int rc = 0;

	for (idx = 0; idx < MAX_INDEX; idx++) {
		if (stress_v4l2_dev_vidioc_enum_audio_ioctl(args, fd, dev, idx) < 0) {
			rc = -1;
			break;
		}
	}
	for (i = 0; i < SIZEOF_ARRAY(indices); i++) {
		if (stress_v4l2_dev_vidioc_enum_audio_ioctl(args, fd, dev, indices[i]) < 0) {
			rc = -1;
			break;
		}
	}
	return rc;
}
#endif

#if defined(VIDIOC_ENUMAUDOUT) &&	\
    defined(HAVE_V4L2_AUDIOOUT)
static int stress_v4l2_dev_vidioc_enum_audout_ioctl(
	stress_args_t *args,
	const int fd,
	const stress_v4l2_dev_t *dev,
	const uint32_t idx)
{
	struct v4l2_audioout audioout;
	int ret;
	int rc = 0;

	(void)shim_memset(&audioout, 0, sizeof(audioout));
	audioout.index = idx;
	errno = 0;
	ret = ioctl(fd, VIDIOC_ENUMAUDOUT, &audioout);
	if ((ret < 0) && (errno != ENOTTY) && (errno != EINVAL) && (errno != ENODATA)) {
		pr_fail("%s: ioctl VIDIOC_ENUMAUDOUT on %s failed, errno=%d (%s)\n",
			args->name, dev->path, errno, strerror(errno));
		rc = -1;
	}
	return rc;
}

/*
 *  stress_v4l2_dev_vidioc_enum_audio
 *	exercise VIDIOC_ENUMAUDOUT
 */
static int stress_v4l2_dev_vidioc_enum_audout(
	stress_args_t *args,
	const int fd,
	stress_v4l2_dev_t *dev)
{
	uint32_t idx;
	size_t i;
	int rc = 0;

	for (idx = 0; idx < MAX_INDEX; idx++) {
		if (stress_v4l2_dev_vidioc_enum_audout_ioctl(args, fd, dev, idx) < 0) {
			rc = -1;
			break;
		}
	}
	for (i = 0; i < SIZEOF_ARRAY(indices); i++) {
		if (stress_v4l2_dev_vidioc_enum_audout_ioctl(args, fd, dev, indices[i]) < 0) {
			rc = -1;
			break;
		}
	}
	return rc;
}
#endif

#if defined(VIDIOC_ENUM_FMT) && 	\
    defined(HAVE_V4L2_FMTDESC) &&	\
    defined(HAVE_V4L2_BUF_TYPE)
static int stress_v4l2_dev_vidioc_enum_fmt_type_ioctl(
	stress_args_t *args,
	const int fd,
	const stress_v4l2_dev_t *dev,
	const enum v4l2_buf_type type,
	const uint32_t idx)
{
	struct v4l2_fmtdesc fmtdesc;
	int ret;
	int rc = 0;

	(void)shim_memset(&fmtdesc, 0, sizeof(fmtdesc));
	fmtdesc.index = idx;
	fmtdesc.type = type;
	ret = ioctl(fd, VIDIOC_ENUM_FMT, &fmtdesc);
	if ((ret < 0) && (errno != ENOTTY) && (errno != EINVAL) && (errno != ENODATA)) {
		pr_fail("%s: ioctl VIDIOC_ENUM_FMT on %s failed, errno=%d (%s)\n",
			args->name, dev->path, errno, strerror(errno));
		rc = -1;
	}
	return rc;
}

/*
 *  stress_v4l2_dev_vidioc_enum_fmt_type()
 *	exercise VIDIOC_ENUM_FMT
 */
static int stress_v4l2_dev_vidioc_enum_fmt_type(
	stress_args_t *args,
	const int fd,
	const stress_v4l2_dev_t *dev,
	const enum v4l2_buf_type type)
{
	uint32_t idx;
	size_t i;
	int rc = 0;

	for (idx = 0; idx < MAX_INDEX; idx++) {
		if (stress_v4l2_dev_vidioc_enum_fmt_type_ioctl(args, fd, dev, type, idx) < 0) {
			rc = -1;
			break;
		}
	}
	for (i = 0; i < SIZEOF_ARRAY(indices); i++) {
		if (stress_v4l2_dev_vidioc_enum_fmt_type_ioctl(args, fd, dev, type, indices[i]) < 0) {
			rc = -1;
			break;
		}
	}
	return rc;
}

static int stress_v4l2_dev_vidioc_enum_fmt(
	stress_args_t *args,
	const int fd,
	stress_v4l2_dev_t *dev)
{
	int rc = 0;
	size_t i;

	for (i = 0; i < SIZEOF_ARRAY(buf_types); i++) {
		if (stress_v4l2_dev_vidioc_enum_fmt_type(args, fd, dev, buf_types[i]) < 0)
			rc = -1;
	}
	return rc;
}
#endif

#if defined(VIDIOC_ENUMOUTPUT) &&	\
    defined(HAVE_V4L2_OUTPUT)
/*
 *  stress_v4l2_dev_vidioc_enum_output
 *	exercise VIDIOC_ENUMOUTPUT
 */
static int stress_v4l2_dev_vidioc_enum_output(
	stress_args_t *args,
	const int fd,
	stress_v4l2_dev_t *dev)
{
	uint32_t i;
	int rc = 0;

	for (i = 0; i < MAX_INDEX; i++) {
		struct v4l2_output output;
		int ret;

		(void)shim_memset(&output, 0, sizeof(output));
		output.index = i;
		errno = 0;
		ret = ioctl(fd, VIDIOC_ENUMOUTPUT, &output);
		if (ret < 0) {
			if ((errno != ENOTTY) && (errno != EINVAL)) {
				pr_fail("%s: ioctl VIDIOC_ENUMOUTPUT on %s failed, errno=%d (%s)\n",
					args->name, dev->path, errno, strerror(errno));
				rc = -1;
			}
			break;
		}
	}
	return rc;
}
#endif

#if defined(VIDIOC_ENUMINPUT) &&	\
    defined(HAVE_V4L2_INTPUT)
/*
 *  stress_v4l2_dev_vidioc_enum_input
 *	exercise VIDIOC_ENUMINPUT
 */
static int stress_v4l2_dev_vidioc_enum_input(
	stress_args_t *args,
	const int fd,
	stress_v4l2_dev_t *dev)
{
	uint32_t i;
	int rc = 0;

	for (i = 0; i < MAX_INDEX; i++) {
		struct v4l2_input input;
		int ret;

		(void)shim_memset(&input, 0, sizeof(input));
		input.index = i;
		errno = 0;
		ret = ioctl(fd, VIDIOC_ENUMINPUT, &input);
		if (ret < 0) {
			if ((errno != ENOTTY) && (errno != EINVAL) && (errno != ENODATA)) {
				pr_fail("%s: ioctl VIDIOC_ENUMINTPUT on %s failed, errno=%d (%s)\n",
					args->name, dev->path, errno, strerror(errno));
				rc = -1;
			}
			break;
		}
	}
	return rc;
}
#endif

#if defined(VIDIOC_QUERYCTRL) &&	\
    defined(HAVE_V4L2_QUERYCTRL)
/*
 *  stress_v4l2_dev_queryctrl
 *	exercise VIDIOC_QUERYCTRL
 */
static int stress_v4l2_dev_queryctrl(
	stress_args_t *args,
	const int fd,
	stress_v4l2_dev_t *dev)
{
	uint32_t i;
	int rc = 0;
	int ret;

	for (i = V4L2_CID_BASE; i < V4L2_CID_LASTP1; i++) {
		struct v4l2_queryctrl queryctrl;

		(void)shim_memset(&queryctrl, 0, sizeof(queryctrl));
		queryctrl.id = i;
		errno = 0;
		ret = ioctl(fd, VIDIOC_QUERYCTRL, &queryctrl);
		if ((ret < 0) && ((errno != ENOTTY) && (errno != EINVAL) && (errno != ENODATA))) {
			pr_fail("%s: ioctl VIDIOC_QUERYCTRL on %s failed, errno=%d (%s)\n",
				args->name, dev->path, errno, strerror(errno));
			rc = -1;
		}
	}

	for (i = V4L2_CID_PRIVATE_BASE; i <= V4L2_CID_PRIVATE_BASE; i++) {
		struct v4l2_queryctrl queryctrl;

                (void)shim_memset(&queryctrl, 0, sizeof(queryctrl));
                queryctrl.id = i;
		errno = 0;
                ret = ioctl(fd, VIDIOC_QUERYCTRL, &queryctrl);
		if (ret != 0)
			break;
	}

	errno = 0;
	ret = ioctl(fd, VIDIOC_QUERYCTRL, NULL);
	if ((ret != -1) || (errno != EFAULT)) {
		pr_fail("%s: VIDIOC_QUERYCTRL on %s with NULL v4l2_queryctrl did not "
			"return -1 with EFAULT, got %d errno=%d (%s) instead\n",
			args->name, dev->path, ret, errno, strerror(errno));
		rc = -1;
	}
	return rc;
}
#endif

#if defined(VIDIOC_QUERYMENU) &&	\
    defined(HAVE_V4L2_QUERYMENU)
static int stress_v4l2_dev_querymenu_ioctl(
	stress_args_t *args,
	const int fd,
	const stress_v4l2_dev_t *dev,
	const uint32_t id,
	const uint32_t index)
{
	struct v4l2_querymenu querymenu;
	int ret;
	int rc = 0;

	(void)shim_memset(&querymenu, 0, sizeof(querymenu));
	querymenu.id = id;
	querymenu.index = index;

	errno = 0;
	ret = ioctl(fd, VIDIOC_QUERYMENU, &querymenu);
	if ((ret < 0) && (errno != ENOTTY) && (errno != EINVAL) && (errno != ENODATA)) {
		pr_fail("%s: ioctl VIDIOC_QUERYMENU on %s failed, errno=%d (%s)\n",
			args->name, dev->path, errno, strerror(errno));
		rc = -1;
	}
	return rc;
}

static int stress_v4l2_dev_querymenu(
	stress_args_t *args,
	const int fd,
	stress_v4l2_dev_t *dev)
{
	uint32_t i;
	int rc = 0;

	for (i = V4L2_CID_BASE; i < V4L2_CID_LASTP1; i++) {
		uint32_t j;
		size_t k;

		for (j = 0; j < MAX_INDEX; j++) {
			if (stress_v4l2_dev_querymenu_ioctl(args, fd, dev, i, j) < 0) {
				rc = -1;
				break;
			}
		}
		for (k = 0; k < SIZEOF_ARRAY(indices); k++) {
			if (stress_v4l2_dev_querymenu_ioctl(args, fd, dev, i, indices[k]) < 0) {
				rc = -1;
				break;
			}
		}
	}
	return rc;
}
#endif

#if defined(VIDIOC_QUERYSTD) &&	\
    defined(HAVE_V4L2_STD_ID)
static int stress_v4l2_dev_querystd(
	stress_args_t *args,
	const int fd,
	stress_v4l2_dev_t *dev)
{
	v4l2_std_id id;
	int ret;

	(void)shim_memset(&id, 0, sizeof(id));
	errno = 0;
	ret = ioctl(fd, VIDIOC_QUERYSTD, &id);
	if (ret < 0) {
		if ((errno != ENOTTY) && (errno != ENODATA))
			pr_fail("%s: ioctl VIDIOC_QUERYSTD on %s failed, errno=%d (%s)\n",
				args->name, dev->path, errno, strerror(errno));
		return -1;
	}
	errno = 0;
	ret = ioctl(fd, VIDIOC_QUERYSTD, NULL);
	if ((ret != -1) || (errno != EFAULT)) {
		pr_fail("%s: VIDIOC_QUERYCTRL on %s with NULL v4l2_sdt_id did not "
			"return -1 with EFAULT, got %d errno=%d (%s) instead\n",
			args->name, dev->path, ret, errno, strerror(errno));
		return -1;
	}
	return 0;
}
#endif

#if defined(VIDIOC_G_AUDIO) &&	\
    defined(HAVE_V4L2_AUDIO)
/*
 *  stress_v4l2_dev_g_audio()
 *  	exercise VIDIOC_G_AUDIO
 */
static int stress_v4l2_dev_g_audio(
	stress_args_t *args,
	const int fd,
	stress_v4l2_dev_t *dev)
{
	struct v4l2_audio audio_get;
	int ret;
	int rc = 0;

        (void)shim_memset(&audio_get, 0, sizeof(audio_get));
	errno = 0;
        ret = ioctl(fd, VIDIOC_G_AUDIO, &audio_get);
	if ((ret < 0) && (errno != ENOTTY) && (errno != EINVAL) && (errno != ENODATA)) {
		pr_fail("%s: ioctl VIDIOC_G_AUDIO on %s failed, errno=%d (%s)\n",
			args->name, dev->path, errno, strerror(errno));
		rc = -1;
	}
	return rc;
}
#endif

#if defined(VIDIOC_G_CROP) &&	\
    defined(HAVE_V4L2_CROP) &&	\
    defined(HAVE_V4L2_BUF_TYPE)
/*
 *  stress_v4l2_dev_g_crop()
 *  	exercise VIDIOC_G_CROP
 */
static int stress_v4l2_dev_g_crop(
	stress_args_t *args,
	const int fd,
	stress_v4l2_dev_t *dev)
{
	int rc = 0;
	size_t i;

	for (i = 0; i < SIZEOF_ARRAY(buf_crop_types); i++) {
		int ret;
		struct v4l2_crop crop;

		(void)shim_memset(&crop, 0, sizeof(crop));
		crop.type = buf_crop_types[i];
		ret = ioctl(fd, VIDIOC_G_CROP, &crop);
		if ((ret < 0) && (errno != ENOTTY) && (errno != EINVAL) && (errno != ENODATA)) {
			pr_fail("%s: ioctl VIDIOC_G_CROP type %d on %s failed, errno=%d (%s)\n",
				args->name, buf_crop_types[i], dev->path, errno, strerror(errno));
			rc = -1;
		}
	}
	return rc;
}
#endif

#if defined(VIDIOC_CROPCAP) &&		\
    defined(HAVE_V4L2_CROPCAP) &&	\
    defined(HAVE_V4L2_BUF_TYPE)
/*
 *  stress_v4l2_dev_cropcap()
 *  	exercise VIDIOC_CROPCAP
 */
static int stress_v4l2_dev_cropcap(
	stress_args_t *args,
	const int fd,
	stress_v4l2_dev_t *dev)
{
	int rc = 0;
	size_t i;

	for (i = 0; i < SIZEOF_ARRAY(buf_crop_types); i++) {
		int ret;
		struct v4l2_cropcap cropcap;

		(void)shim_memset(&cropcap, 0, sizeof(cropcap));
		cropcap.type = buf_crop_types[i];
		ret = ioctl(fd, VIDIOC_CROPCAP, &cropcap);
		if ((ret < 0) && (errno != ENOTTY) && (errno != EINVAL) && (errno != ENODATA)) {
			pr_fail("%s: ioctl VIDIOC_CROPAP type %d on %s failed, errno=%d (%s)\n",
				args->name, buf_crop_types[i], dev->path, errno, strerror(errno));
			rc = -1;
		}
	}
	return rc;
}
#endif

#if defined(VIDIOC_G_CTRL) &&	\
    defined(HAVE_V4L2_CONTROL)
/*
 *  stress_v4l2_dev_g_ctrl
 *	exercise VIDIOC_G_CTRL
 */
static int stress_v4l2_dev_g_ctrl(
	stress_args_t *args,
	const int fd,
	stress_v4l2_dev_t *dev)
{
	uint32_t i;
	int rc = 0;
	int ret;

	for (i = V4L2_CID_BASE; i < V4L2_CID_LASTP1; i++) {
		struct v4l2_control control;

		(void)shim_memset(&control, 0, sizeof(control));
		control.id = i;
		errno = 0;
		ret = ioctl(fd, VIDIOC_G_CTRL, &control);
		if ((ret < 0) && ((errno != ENOTTY) && (errno != EINVAL) && (errno != ENODATA))) {
			pr_fail("%s: ioctl VIDIOC_G_CTRL on %s failed, errno=%d (%s)\n",
				args->name, dev->path, errno, strerror(errno));
			rc = -1;
		}
	}

	for (i = V4L2_CID_PRIVATE_BASE; i <= V4L2_CID_PRIVATE_BASE; i++) {
		struct v4l2_control control;

		(void)shim_memset(&control, 0, sizeof(control));
                control.id = i;
		errno = 0;
                ret = ioctl(fd, VIDIOC_G_CTRL, &control);
		if (ret != 0)
			break;
	}

	errno = 0;
	ret = ioctl(fd, VIDIOC_G_CTRL, NULL);
	if ((ret != -1) || (errno != EFAULT)) {
		pr_fail("%s: VIDIOC_G_CTRL on %s with NULL v4l2_control did not "
			"return -1 with EFAULT, got %d errno=%d (%s) instead\n",
			args->name, dev->path, ret, errno, strerror(errno));
		rc = -1;
	}
	return rc;
}
#endif

#if defined(VIDIOC_G_ENC_INDEX) &&	\
    defined(HAVE_V4L2_ENC_IDX)
/*
 *  stress_v4l2_dev_g_enc_index
 *	exercise VIDIOC_G_ENC_INDEX
 */
static int stress_v4l2_dev_g_enc_index(
	stress_args_t *args,
	const int fd,
	stress_v4l2_dev_t *dev)
{
	struct v4l2_enc_idx enc_index;
	int ret;
	int rc = 0;

	(void)shim_memset(&enc_index, 0, sizeof(enc_index));
	errno = 0;
	ret = ioctl(fd, VIDIOC_G_ENC_INDEX, &enc_index);
	if ((ret < 0) && ((errno != ENOTTY) && (errno != EINVAL) && (errno != ENODATA))) {
		pr_fail("%s: ioctl VIDIOC_G_ENC_INDEX on %s failed, errno=%d (%s)\n",
			args->name, dev->path, errno, strerror(errno));
		rc = -1;
	}

	/*
	errno = 0;
	ret = ioctl(fd, VIDIOC_G_ENC_INDEX, NULL);
	if ((ret != -1) || (errno != EFAULT)) {
		pr_fail("%s: VIDIOC_G_ENC_INDEX on %s with NULL v4l2_enc_idx did not "
			"return -1 with EFAULT, got %d errno=%d (%s) instead\n",
			args->name, dev->path, ret, errno, strerror(errno));
		rc = -1;
	}
	*/
	return rc;
}
#endif

#if defined(VIDIOC_G_SLICED_VBI_CAP) &&		\
    defined(HAVE_V4L2_SLICED_VBI_CAP) &&	\
    defined(HAVE_V4L2_BUF_TYPE)
static int stress_v4l2_dev_g_sliced_vbi_cap_ioctl(
	stress_args_t *args,
	const int fd,
	const stress_v4l2_dev_t *dev,
	const enum v4l2_buf_type type)
{
	struct v4l2_sliced_vbi_cap sliced_vbi_cap;
	int ret;
	int rc = 0;

	(void)shim_memset(&sliced_vbi_cap, 0, sizeof(sliced_vbi_cap));
	sliced_vbi_cap.type = type;
	errno = 0;
	ret = ioctl(fd, VIDIOC_G_SLICED_VBI_CAP, &sliced_vbi_cap);
	if ((ret < 0) && ((errno != ENOTTY) && (errno != EINVAL) && (errno != ENODATA))) {
		pr_fail("%s: ioctl VIDIOC_G_SLICED_VBI_CAP with type %d on %s failed, errno=%d (%s)\n",
			args->name, type, dev->path, errno, strerror(errno));
		rc = -1;
	}
	return rc;
}

/*
 *  stress_v4l2_dev_g_sliced_vbi_cap
 *	exercise VIDIOC_G_SLICED_VBI_CAP
 */
static int stress_v4l2_dev_g_sliced_vbi_cap(
	stress_args_t *args,
	const int fd,
	stress_v4l2_dev_t *dev)
{
	int ret;
	int rc = 0;
	size_t i;

	for (i = 0; i < SIZEOF_ARRAY(buf_check_types); i++) {
		ret = stress_v4l2_dev_g_sliced_vbi_cap_ioctl(args, fd, dev, buf_check_types[i]);
		if (ret < 0)
			rc = -1;
	}

	errno = 0;
	ret = ioctl(fd, VIDIOC_G_SLICED_VBI_CAP, NULL);
	if ((ret != -1) || (errno != EFAULT)) {
		pr_fail("%s: VIDIOC_G_SLICED_VBI_CAP on %s with NULL v4l2_sliced_vbi_cap did not "
			"return -1 with EFAULT, got %d errno=%d (%s) instead\n",
			args->name, dev->path, ret, errno, strerror(errno));
		rc = -1;
	}
	return rc;
}
#endif

#if defined(VIDIOC_G_INPUT) &&	\
    defined(HAVE_V4L2_INTPUT)
/*
 *  stress_v4l2_dev_g_input
 *	exercise VIDIOC_G_INPUT
 */
static int stress_v4l2_dev_g_input(
	stress_args_t *args,
	const int fd,
	stress_v4l2_dev_t *dev)
{
	struct v4l2_input input;
	int ret;
	int rc = 0;

	(void)shim_memset(&input, 0, sizeof(input));
	errno = 0;
	ret = ioctl(fd, VIDIOC_G_INPUT, &input);
	if ((ret < 0) && ((errno != ENOTTY) && (errno != EINVAL) && (errno != ENODATA))) {
		pr_fail("%s: ioctl VIDIOC_G_INPUT on %s failed, errno=%d (%s)\n",
			args->name, dev->path, errno, strerror(errno));
		rc = -1;
	}
	return rc;
}
#endif

#if defined(VIDIOC_G_OUTPUT) &&	\
    defined(HAVE_V4L2_OUTPUT)
/*
 *  stress_v4l2_dev_g_output
 *	exercise VIDIOC_G_OUTPUT
 */
static int stress_v4l2_dev_g_output(
	stress_args_t *args,
	const int fd,
	stress_v4l2_dev_t *dev)
{
	struct v4l2_output output;
	int ret;
	int rc = 0;

	(void)shim_memset(&output, 0, sizeof(output));
	errno = 0;
	ret = ioctl(fd, VIDIOC_G_OUTPUT, &output);
	if ((ret < 0) && ((errno != ENOTTY) && (errno != EINVAL) && (errno != ENODATA))) {
		pr_fail("%s: ioctl VIDIOC_G_OUTPUT on %s failed, errno=%d (%s)\n",
			args->name, dev->path, errno, strerror(errno));
		rc = -1;
	}
	return rc;
}
#endif

#if defined(VIDIOC_G_JPEGCOMP) &&	\
    defined(HAVE_V4L2_JPEGCOMPRESSION)
/*
 *  stress_v4l2_dev_g_jpegcomp
 *	exercise VIDIOC_G_JPEGCOMP
 */
static int stress_v4l2_dev_g_jpegcomp(
	stress_args_t *args,
	const int fd,
	stress_v4l2_dev_t *dev)
{
	struct v4l2_jpegcompression jpegcomp;
	int ret;
	int rc = 0;

	(void)shim_memset(&jpegcomp, 0, sizeof(jpegcomp));
	errno = 0;
	ret = ioctl(fd, VIDIOC_G_JPEGCOMP, &jpegcomp);
	if ((ret < 0) && ((errno != ENOTTY) && (errno != EINVAL) && (errno != ENODATA))) {
		pr_fail("%s: ioctl VIDIOC_G_JPEGCOMP on %s failed, errno=%d (%s)\n",
			args->name, dev->path, errno, strerror(errno));
		rc = -1;
	}

	/*
	errno = 0;
	ret = ioctl(fd, VIDIOC_G_JPEGCOMP, NULL);
	if ((ret != -1) || (errno != EFAULT)) {
		pr_fail("%s: VIDIOC_G_JPEGCOMP on %s with NULL v4l2_enc_idx did not "
			"return -1 with EFAULT, got %d errno=%d (%s) instead\n",
			args->name, dev->path, ret, errno, strerror(errno));
		rc = -1;
	}
	*/
	return rc;
}
#endif

#if defined(VIDIOC_LOG_STATUS)
/*
 *  stress_v4l2_dev_log_status
 *	exercise VIDIOC_LOG_STATUS
 */
static int stress_v4l2_dev_log_status(
	stress_args_t *args,
	const int fd,
	stress_v4l2_dev_t *dev)
{
	int ret;
	int rc = 0;

	errno = 0;
	ret = ioctl(fd, VIDIOC_LOG_STATUS);
	if ((ret < 0) && ((errno != ENOTTY) && (errno != EINVAL) && (errno != ENODATA))) {
		pr_fail("%s: ioctl VIDIOC_LOG_STATUS on %s failed, errno=%d (%s)\n",
			args->name, dev->path, errno, strerror(errno));
		rc = -1;
	}
	return rc;
}
#endif

#if defined(VIDIOC_G_MODULATOR) &&	\
    defined(HAVE_V4L2_MODULATOR)
static int stress_v4l2_dev_g_modulator_ioctl(
	stress_args_t *args,
	const int fd,
	const stress_v4l2_dev_t *dev,
	const uint32_t idx)
{
	struct v4l2_modulator modulator;
	int ret;
	int rc = 0;

	(void)shim_memset(&modulator, 0, sizeof(modulator));
	modulator.index = idx;
	errno = 0;
	ret = ioctl(fd, VIDIOC_G_MODULATOR, &modulator);
	if (ret < 0) {
		if ((errno != ENOTTY) && (errno != EINVAL) && (errno != ENODATA)) {
			pr_fail("%s: ioctl VIDIOC_G_MODULATOR with index %" PRIu32 " on %s failed, errno=%d (%s)\n",
				args->name, idx, dev->path, errno, strerror(errno));
		}
		rc = -1;
	}
	return rc;
}

/*
 *  stress_v4l2_dev_g_modulator
 *	exercise VIDIOC_G_MODULATOR
 */
static int stress_v4l2_dev_g_modulator(
	stress_args_t *args,
	const int fd,
	stress_v4l2_dev_t *dev)
{
	int ret;
	int rc = 0;
	uint32_t idx = 0;
	size_t j;

	do {
		ret = stress_v4l2_dev_g_modulator_ioctl(args, fd, dev, idx);
		if (ret < 0)
			rc = -1;
		idx++;
	} while (rc == 0);

	for (j = 0; j < SIZEOF_ARRAY(indices); j++) {
		ret = stress_v4l2_dev_g_modulator_ioctl(args, fd, dev, indices[j]);
		if (ret < 0)
			rc = -1;
	}
	return rc;
}
#endif

#if defined(VIDIOC_G_PARM) &&		\
    defined(HAVE_V4L2_STREAMPARM) &&	\
    defined(HAVE_V4L2_BUF_TYPE)
static int stress_v4l2_dev_g_parm_ioctl(
	stress_args_t *args,
	const int fd,
	const stress_v4l2_dev_t *dev,
	const enum v4l2_buf_type type)
{
	struct v4l2_streamparm streamparm;
	int ret;
	int rc = 0;

	(void)shim_memset(&streamparm, 0, sizeof(streamparm));
	streamparm.type = type;
	errno = 0;
	ret = ioctl(fd, VIDIOC_G_PARM, &streamparm);
	if ((ret < 0) && ((errno != ENOTTY) && (errno != EINVAL) && (errno != ENODATA))) {
		pr_fail("%s: ioctl VIDIOC_G_PARM with type %d on %s failed, errno=%d (%s)\n",
			args->name, type, dev->path, errno, strerror(errno));
		rc = -1;
	}
	return rc;
}

/*
 *  stress_v4l2_dev_g_parm
 *	exercise VIDIOC_G_PARM
 */
static int stress_v4l2_dev_g_parm(
	stress_args_t *args,
	const int fd,
	stress_v4l2_dev_t *dev)
{
	int ret;
	int rc = 0;
	size_t i;

	for (i = 0; i < SIZEOF_ARRAY(buf_check_types); i++) {
		ret = stress_v4l2_dev_g_parm_ioctl(args, fd, dev, buf_check_types[i]);
		if (ret < 0)
			rc = -1;
	}

	errno = 0;
	ret = ioctl(fd, VIDIOC_G_PARM, NULL);
	if ((ret != -1) || (errno != EFAULT)) {
		pr_fail("%s: VIDIOC_G_PARM on %s with NULL v4l2_parm did not "
			"return -1 with EFAULT, got %d errno=%d (%s) instead\n",
			args->name, dev->path, ret, errno, strerror(errno));
		rc = -1;
	}
	return rc;
}
#endif

#if defined(VIDIOC_G_PRIORITY) &&	\
    defined(HAVE_V4L2_PRIORITY)
/*
 *  stress_v4l2_dev_g_priority
 *	exercise VIDIOC_G_PRIORITY
 */
static int stress_v4l2_dev_g_priority(
	stress_args_t *args,
	const int fd,
	stress_v4l2_dev_t *dev)
{
	enum v4l2_priority priority;
	int ret;
	int rc = 0;

	errno = 0;
	(void)shim_memset(&priority, 0, sizeof(priority));
	ret = ioctl(fd, VIDIOC_G_PRIORITY, &priority);
	if ((ret < 0) && ((errno != ENOTTY) && (errno != EINVAL) && (errno != ENODATA))) {
		pr_fail("%s: ioctl VIDIOC_G_PRIORITY on %s failed, errno=%d (%s)\n",
			args->name, dev->path, errno, strerror(errno));
		rc = -1;
	}
	return rc;
}
#endif

#if defined(VIDIOC_QUERYCAP) &&	\
    defined(HAVE_V4L2_CAPABILITY)
/*
 *  stress_v4l2_dev_querycap
 *	exercise VIDIOC_QUERYCAP
 */
static int stress_v4l2_dev_querycap(
	stress_args_t *args,
	const int fd,
	stress_v4l2_dev_t *dev)
{
	struct v4l2_capability capability;
	int ret;
	int rc = 0;

	errno = 0;
	(void)shim_memset(&capability, 0, sizeof(capability));
	ret = ioctl(fd, VIDIOC_QUERYCAP, &capability);
	if ((ret < 0) && ((errno != ENOTTY) && (errno != EINVAL))) {
		pr_fail("%s: ioctl VIDIOC_QUERYCAP on %s failed, errno=%d (%s)\n",
			args->name, dev->path, errno, strerror(errno));
		rc = -1;
	}
	if (ret == 0) {
		if (dev->driver[0] == '\0')
			(void)shim_strscpy(dev->driver, (char *)capability.driver, sizeof(dev->driver));
	}
	return rc;
}
#endif

static const stress_v4l2_func_t stress_v4l2_funcs[] = {
#if defined(_IO)
	stress_v4l2_dev_bad_ioctl,
#endif
#if defined(VIDIOC_ENUMAUDIO) &&	\
    defined(HAVE_V4L2_AUDIO)
	stress_v4l2_dev_vidioc_enum_audio,
#endif
#if defined(VIDIOC_ENUMAUDOUT) &&	\
    defined(HAVE_V4L2_AUDIOOUT)
	stress_v4l2_dev_vidioc_enum_audout,
#endif
#if defined(VIDIOC_ENUM_FMT) &&		\
    defined(HAVE_V4L2_FMTDESC) &&	\
    defined(HAVE_V4L2_BUF_TYPE)
	stress_v4l2_dev_vidioc_enum_fmt,
#endif
#if defined(VIDIOC_ENUMOUTPUT) &&	\
    defined(HAVE_V4L2_OUTPUT)
	stress_v4l2_dev_vidioc_enum_output,
#endif
#if defined(VIDIOC_ENUMINPUT) &&	\
    defined(HAVE_V4L2_INTPUT)
	stress_v4l2_dev_vidioc_enum_input,
#endif
#if defined(VIDIOC_QUERYCTRL) &&	\
    defined(HAVE_V4L2_QUERYCTRL)
	stress_v4l2_dev_queryctrl,
#endif
#if defined(VIDIOC_QUERYMENU) &&	\
    defined(HAVE_V4L2_QUERYMENU)
	stress_v4l2_dev_querymenu,
#endif
#if defined(VIDIOC_QUERYSTD) &&	\
    defined(HAVE_V4L2_STD_ID)
	stress_v4l2_dev_querystd,
#endif
#if defined(VIDIOC_G_AUDIO) &&	\
    defined(HAVE_V4L2_AUDIO)
	stress_v4l2_dev_g_audio,
#endif
#if defined(VIDIOC_G_CROP) &&	\
    defined(HAVE_V4L2_CROP) &&	\
    defined(HAVE_V4L2_BUF_TYPE)
	stress_v4l2_dev_g_crop,
#endif
#if defined(VIDIOC_CROPCAP) &&		\
    defined(HAVE_V4L2_CROPCAP) &&	\
    defined(HAVE_V4L2_BUF_TYPE)
	stress_v4l2_dev_cropcap,
#endif
#if defined(VIDIOC_G_CTRL) &&	\
    defined(HAVE_V4L2_CONTROL)
	stress_v4l2_dev_g_ctrl,
#endif
#if defined(VIDIOC_G_ENC_INDEX) &&	\
    defined(HAVE_V4L2_ENC_IDX)
	stress_v4l2_dev_g_enc_index,
#endif
#if defined(VIDIOC_G_SLICED_VBI_CAP) &&		\
    defined(HAVE_V4L2_SLICED_VBI_CAP) &&	\
    defined(HAVE_V4L2_BUF_TYPE)
	stress_v4l2_dev_g_sliced_vbi_cap,
#endif
#if defined(VIDIOC_G_INPUT) &&	\
    defined(HAVE_V4L2_INTPUT)
	stress_v4l2_dev_g_input,
#endif
#if defined(VIDIOC_G_OUTPUT) &&	\
    defined(HAVE_V4L2_OUTPUT)
	stress_v4l2_dev_g_output,
#endif
#if defined(VIDIOC_G_JPEGCOMP) && \
    defined(HAVE_V4L2_JPEGCOMPRESSION)
	stress_v4l2_dev_g_jpegcomp,
#endif
#if defined(VIDIOC_LOG_STATUS)
	stress_v4l2_dev_log_status,
#endif
#if defined(VIDIOC_G_MODULATOR) &&	\
    defined(HAVE_V4L2_MODULATOR)
	stress_v4l2_dev_g_modulator,
#endif
#if defined(VIDIOC_G_PARM) &&		\
    defined(HAVE_V4L2_STREAMPARM) &&	\
    defined(HAVE_V4L2_BUF_TYPE)
	stress_v4l2_dev_g_parm,
#endif
#if defined(VIDIOC_G_PRIORITY) &&	\
    defined(HAVE_V4L2_PRIORITY)
	stress_v4l2_dev_g_priority,
#endif
#if defined(VIDIOC_QUERYCAP) &&	\
    defined(HAVE_V4L2_CAPABILITY)
	stress_v4l2_dev_querycap,
#endif
};

static int stress_v4l2_dev(stress_args_t *args, stress_v4l2_dev_t *dev)
{
	size_t j;
	int fd;

	fd = open(dev->path, O_RDWR);
	if (fd < 0)
		return -1;
	for (j = 0; (j < SIZEOF_ARRAY(stress_v4l2_funcs)) && stress_continue(args); j++) {
		stress_bogo_inc(args);
		stress_v4l2_funcs[j](args, fd, dev);
	}
	(void)close(fd);

	return 0;
}

/*
 *  stress_v4l2_dev_cmp()
 *  	qsort compare for sorting by device path
 */
static int stress_v4l2_dev_cmp(const void *p1, const void *p2)
{
	const stress_v4l2_dev_t *dev1 = (const stress_v4l2_dev_t *)p1;
	const stress_v4l2_dev_t *dev2 = (const stress_v4l2_dev_t *)p2;

	return strcmp(dev1->path, dev2->path);
}

/*
 *  stress_v4l2()
 *	stress v4l2 interfaces (Video For Linux v2)
 */
static int OPTIMIZE3 stress_v4l2(stress_args_t *args)
{
	int rc = EXIT_SUCCESS;
	size_t i;
	size_t n_devs;
	stress_v4l2_dev_t *devs = NULL;

	n_devs = stress_v4l2_devs_get(args, &devs);
	if (n_devs == 0) {
		rc = EXIT_NO_RESOURCE;
		goto devs_free;
	}

	stress_proc_state_set(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_proc_state_set(args->name, STRESS_STATE_RUN);

	do {
		for (i = 0; (i < n_devs) && stress_continue(args); i++) {
			stress_v4l2_dev(args, &devs[i]);
		}
	} while (stress_continue(args));

	stress_proc_state_set(args->name, STRESS_STATE_DEINIT);

	if (stress_instance_zero(args)) {
		/* show device and driver info */
		qsort_bm(devs, n_devs, sizeof(stress_v4l2_dev_t), stress_v4l2_dev_cmp);
		pr_inf("%s: %-18.18s %s\n", args->name, "device", "driver");
		for (i = 0; i < n_devs; i++)
			pr_inf("%s: %-18.18s %s\n", args->name, devs[i].path, devs[i].driver);
	}

devs_free:
	stress_v4l2_devs_free(devs, n_devs);

	return rc;
}

static const stress_exercises_t exercises[] = {
	STRESS_EX_FEATURE("v4l2"),

	STRESS_EX_SYSCALL("close"),
	STRESS_EX_SYSCALL("ioctl"),
	STRESS_EX_SYSCALL("open"),

	STRESS_EX_END,
};

const stressor_info_t stress_v4l2_info = {
	.stressor = stress_v4l2,
	.classifier = CLASS_OS,
	.verify = VERIFY_OPTIONAL,
	.help = help,
	.exercises = exercises,
};

#else

const stressor_info_t stress_v4l2_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_OS,
	.verify = VERIFY_OPTIONAL,
	.help = help,
	.unimplemented_reason = "built without linux/videodev2.h"
};

#endif
