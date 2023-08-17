// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */
#include <sys/types.h>
#include <sha2.h>
#include <string.h>

int main(void)
{
	SHA2_CTX ctx;
	uint8_t results[SHA256_DIGEST_LENGTH];
	char *buf = "test";

	SHA256Init(&ctx);
	SHA256Update(&ctx, (uint8_t *)buf, strlen(buf));
	SHA256Final(results, &ctx);

	return 0;
}
