// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
 *
 */
#include <linux/videodev2.h>

int main(void)
{
	struct v4l2_audio a;

	(void)a;

	return sizeof(a);
}
