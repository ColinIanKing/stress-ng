/*
 * Copyright (C) 2022-2025 Colin Ian King
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
