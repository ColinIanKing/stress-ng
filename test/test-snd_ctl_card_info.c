// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
 *
 */
#include <sound/asound.h>

int main(void)
{
	struct snd_ctl_card_info i;

	(void)i;

	return sizeof(i);
}
