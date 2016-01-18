/*
 * Copyright (C) 2013-2016 Canonical, Ltd.
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
 * This code is a complete clean re-write of the stress tool by
 * Colin Ian King <colin.king@canonical.com> and attempts to be
 * backwardly compatible with the stress tool by Amos Waterland
 * <apw@rossby.metr.ou.edu> but has more stress tests and more
 * functionality.
 *
 */
#define _GNU_SOURCE

#include "stress-ng.h"

#if defined(STRESS_AF_ALG)

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <linux/if_alg.h>
#include <linux/socket.h>

#define SHA1_DIGEST_SIZE        (20)
#define SHA224_DIGEST_SIZE      (28)
#define SHA256_DIGEST_SIZE      (32)
#define SHA384_DIGEST_SIZE      (48)
#define SHA512_DIGEST_SIZE      (64)
#define MD4_DIGEST_SIZE		(16)
#define MD5_DIGEST_SIZE		(16)
#define RMD128_DIGEST_SIZE	(16)
#define RMD160_DIGEST_SIZE	(20)
#define RMD256_DIGEST_SIZE	(32)
#define RMD320_DIGEST_SIZE	(40)
#define WP256_DIGEST_SIZE	(32)
#define WP384_DIGEST_SIZE	(48)
#define WP512_DIGEST_SIZE	(64)
#define TGR128_DIGEST_SIZE	(16)
#define TGR160_DIGEST_SIZE	(20)
#define TGR192_DIGEST_SIZE	(24)

/* See https://lwn.net/Articles/410833/ */

typedef struct {
	const char *type;
	const char *name;
	const ssize_t digest_size;
	bool  bind_fail;
} alg_info_t;

static alg_info_t algo_info[] = {
	{ "hash",	"sha1",		SHA1_DIGEST_SIZE,	false },
	{ "hash",	"sha224",	SHA224_DIGEST_SIZE,	false },
	{ "hash",	"sha256",	SHA256_DIGEST_SIZE,	false },
	{ "hash",	"sha384",	SHA384_DIGEST_SIZE,	false },
	{ "hash",	"sha512",	SHA512_DIGEST_SIZE,	false },
	{ "hash",	"md4",		MD4_DIGEST_SIZE,	false },
	{ "hash",	"md5",		MD5_DIGEST_SIZE,	false },
	{ "hash",	"rmd128",	RMD128_DIGEST_SIZE,	false },
	{ "hash",	"rmd160",	RMD160_DIGEST_SIZE,	false },
	{ "hash",	"rmd256",	RMD256_DIGEST_SIZE,	false },
	{ "hash",	"rmd320",	RMD320_DIGEST_SIZE,	false },
	{ "hash",	"wp256",	WP256_DIGEST_SIZE,	false },
	{ "hash",	"wp384",	WP384_DIGEST_SIZE,	false },
	{ "hash",	"wp512",	WP512_DIGEST_SIZE,	false },
	{ "hash",	"tgr128",	TGR128_DIGEST_SIZE,	false },
	{ "hash",	"tgr160",	TGR160_DIGEST_SIZE,	false },
	{ "hash",	"tgr192",	TGR192_DIGEST_SIZE,	false },
	{ NULL,		NULL,		0,			false },
};

/*
 *  stress_af_alg()
 *	stress socket AF_ALG domain
 */
int stress_af_alg(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	int sockfd, rc = EXIT_FAILURE;

	(void)instance;
	(void)name;

	sockfd = socket(AF_ALG, SOCK_SEQPACKET, 0);
	if (sockfd < 0) {
		pr_fail_err(name, "socket");
		return rc;
	}

	do {
		int i;
		bool bind_ok = false;

		for (i = 0; algo_info[i].type; i++) {
			int fd;
			ssize_t j;
			const ssize_t digest_size = algo_info[i].digest_size;
			char input[1024], digest[digest_size];
			struct sockaddr_alg sa;

			if (algo_info[i].bind_fail)
				continue;

			memset(&sa, 0, sizeof(sa));
			sa.salg_family = AF_ALG;
			memcpy(sa.salg_type, algo_info[i].type, sizeof(sa.salg_type));
			memcpy(sa.salg_name, algo_info[i].name, sizeof(sa.salg_name));

			if (bind(sockfd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
				/* Perhaps the hash does not exist with this kernel */
				if (errno == ENOENT) {
					algo_info[i].bind_fail = true;
					continue;
				}
				pr_fail_err(name, "bind");
				goto tidy;
			}
			bind_ok = true;
			fd = accept(sockfd, NULL, 0);
			if (fd < 0) {
				pr_fail_err(name, "accept");
				(void)close(fd);
				goto tidy;
			}

			stress_strnrnd(input, sizeof(input));

			for (j = 32; j < (ssize_t)sizeof(input); j++) {
				if (send(fd, input, j, 0) != j) {
					pr_fail_err(name, "send");
					(void)close(fd);
					goto tidy;
				}
				if (recv(fd, digest, digest_size, MSG_WAITALL) != digest_size) {
					pr_fail_err(name, "recv");
					(void)close(fd);
					goto tidy;
				}
				(*counter)++;
				if (max_ops && (*counter >= max_ops)) {
					(void)close(fd);
					goto done;
				}
			}
			(void)close(fd);
		}
		if (!bind_ok) {
			errno = ENOENT;
			pr_fail_err(name, "bind to all hash types");
			break;
		}
	} while (opt_do_run && (!max_ops || *counter < max_ops));

done:
	rc = EXIT_SUCCESS;
tidy:
	(void)close(sockfd);

	return rc;
}
#endif
