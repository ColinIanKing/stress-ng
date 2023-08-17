// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */
#include <xxhash.h>

int main(void)
{
	const char buffer[] = "test123";

	XXH64_hash_t hash = XXH64(buffer, strlen(buffer), 0x12345678);

	return (int)hash;
}
