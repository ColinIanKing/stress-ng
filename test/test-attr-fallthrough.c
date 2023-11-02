// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */
#define CASE_FALLTHROUGH __attribute__((fallthrough)) /* Fallthrough */

int main(int argc, char **argv)
{
	switch (argc) {
	case 0:
		CASE_FALLTHROUGH;
	case 1:
		CASE_FALLTHROUGH;
	default:
		return 0;
	}
	return 1;
}
