// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */
#include <dlfcn.h>
#include <gnu/lib-names.h>

int main(void)
{
	void *handle;

	handle = dlopen(LIBM_SO, RTLD_LAZY);
	(void)dlerror();
	if (handle)
		(void)dlclose(handle);

	handle = dlopen(LIBM_SO, RTLD_NOW);
	(void)dlerror();
	if (handle)
		(void)dlclose(handle);

	return 0;
}
