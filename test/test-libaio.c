// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */
#include <libaio.h>

/* The following functions from librt are used by stress-ng */

static void *aio_funcs[] = {
	io_setup,
	io_destroy,
	io_submit,
	io_getevents
};

int main(void)
{
	return 0;
}
