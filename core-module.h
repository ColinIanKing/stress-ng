/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2023      Colin Ian King.
 *
 */
#ifndef CORE_MODULE_H
#define CORE_MODULE_H

extern int stress_module_load(const char *name, const char *alias,
	const char *options, bool *already_loaded);
extern int stress_module_unload(const char *name, const char *alias,
	const bool already_loaded);

#endif
