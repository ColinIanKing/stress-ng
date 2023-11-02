// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2022, Red Hat Inc, Dorinda Bassey <dbassey@redhat.com>
 *
 */
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <gbm.h>

/* The following functions from libgbm are used by stress-ng */

static void *gbm_funcs[] = {
	(void *)gbm_create_device,
	(void *)gbm_surface_create,
};

/* This program does nothing as it is intended to be a compile-only check*/
int main(void)
{
	return 0;
}
