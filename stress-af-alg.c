/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2025 Colin Ian King.
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
#include "stress-ng.h"
#include "core-attribute.h"
#include "core-builtin.h"

#include <sys/socket.h>

#if defined(HAVE_LINUX_IF_ALG_H)
#include <linux/if_alg.h>
#endif

#if defined(HAVE_LINUX_SOCKET_H)
#include <linux/socket.h>
#endif

#define ALLOC_SLOP	(64)

static const stress_help_t help[] = {
	{ NULL,	"af-alg N",	"start N workers that stress AF_ALG socket domain" },
	{ NULL,	"af-alg-dump",	"dump internal list from /proc/crypto to stdout" },
	{ NULL,	"af-alg-ops N",	"stop after N af-alg bogo operations" },
	{ NULL, NULL,		NULL }
};

static const stress_opt_t opts[] = {
	{ OPT_af_alg_dump, "af-alg-dump", TYPE_ID_BOOL, 0, 1, NULL },
	END_OPT,
};

#if defined(HAVE_LINUX_IF_ALG_H) &&	\
    defined(HAVE_LINUX_SOCKET_H) &&	\
    defined(AF_ALG)

static volatile bool do_jmp = true;
static sigjmp_buf jmpbuf;

#if !defined(SOL_ALG)
#define SOL_ALG				(279)
#endif
#define DATA_LEN 			(1024)
#define MAX_AF_ALG_RETRIES		(25)
#define MAX_AF_ALG_RETRIES_BIND		(3)

/* See https://lwn.net/Articles/410833/ */

typedef enum {
	CRYPTO_AHASH,
	CRYPTO_SHASH,
	CRYPTO_CIPHER,
	CRYPTO_AKCIPHER,
	CRYPTO_SKCIPHER,
	CRYPTO_RNG,
	CRYPTO_AEAD,
	CRYPTO_UNKNOWN,
} stress_crypto_type_t;

typedef enum {
	SOURCE_DEFCONFIG,
	SOURCE_PROC_CRYPTO,
} stress_crypto_source_t;

typedef struct {
	const stress_crypto_type_t type;
	const char		*type_string;
	const char		*name;
} stress_crypto_type_info_t;

static const stress_crypto_type_info_t crypto_type_info[] = {
	{ CRYPTO_AHASH,		"CRYPTO_AHASH",		"ahash" },
	{ CRYPTO_SHASH,		"CRYPTO_SHASH",		"shash" },
	{ CRYPTO_CIPHER,	"CRYPTO_CIPHER",	"cipher" },
	{ CRYPTO_AKCIPHER,	"CRYPTO_AKCIPHER",	"akcipher" },
	{ CRYPTO_SKCIPHER,	"CRYPTO_SKCIPHER",	"skcipher" },
	{ CRYPTO_RNG,		"CRYPTO_RNG",		"rng" },
	{ CRYPTO_AEAD,		"CRYPTO_AEAD",		"aead" },
	{ CRYPTO_UNKNOWN,	"CRYPTO_UNKNOWN",	"unknown" },
};

typedef struct stress_crypto_info {
	stress_crypto_type_t	crypto_type;
	stress_crypto_source_t	source;
	char 	*type;
	char 	*name;
	int8_t	block_size;
	int8_t	max_key_size;
	int8_t	max_auth_size;
	int8_t	iv_size;
	int8_t	digest_size;
	uint8_t	internal:1;		/* true if accessible to userspace */
	uint8_t	ignore:1;
	uint8_t	selftest:1;		/* true if passed */
	stress_metrics_t metrics;	/* performance metrics */
	struct stress_crypto_info *next;
} stress_crypto_info_t;

static stress_crypto_info_t *crypto_info_list;

/*
 * Provide some predefined/default configs
 * to the list generated from /proc/crypto
 * so some not-loaded cryptographic module
 * (thus not yet present into /proc/crypto)
 * is loaded on-demand with bind() syscall.
 */
static stress_crypto_info_t crypto_info_defconfigs[] = {
#include "stress-af-alg-defconfigs.h"
};

static void stress_af_alg_add_crypto_defconfigs(void);

static void MLOCKED_TEXT stress_af_alg_alarm_handler(int signum)
{
	static int count = 0;

	/* Indicate we need to stop */
	stress_handle_stop_stressing(signum);

	/*
	 * If we've not stopped after 5 seconds then an af-alg
	 * got stuck, so force  jmp to terminate path
	 */
	if (UNLIKELY(do_jmp && count++ > 5)) {
		do_jmp = false;
		siglongjmp(jmpbuf, 1);
		stress_no_return();
	}
}

/*
 *   name_to_type()
 *	map text type name to symbolic enum value
 */
static stress_crypto_type_t name_to_type(const char *buffer)
{
	const char *end, *ptr = strchr(buffer, ':');
	size_t i;

	if (!ptr)
		return CRYPTO_UNKNOWN;

	end = ptr + strlen(buffer);
	ptr += 2;
	if (ptr >= end)
		return CRYPTO_UNKNOWN;
	for (i = 0; i < SIZEOF_ARRAY(crypto_type_info); i++) {
		const size_t n = strlen(crypto_type_info[i].name);

		if (!strncmp(crypto_type_info[i].name, ptr, n))
			return crypto_type_info[i].type;
	}
	return CRYPTO_UNKNOWN;
}

/*
 *   type_to_type_string()
 *	map type to stringified type
 */
static const char * PURE type_to_type_string(const stress_crypto_type_t type)
{
	size_t i;

	for (i = 0; i < SIZEOF_ARRAY(crypto_type_info); i++) {
		if (crypto_type_info[i].type == type)
			return crypto_type_info[i].type_string;
	}
	return "unknown";
}

/*
 *  stress_af_alg_ignore()
 *	some crypto engines may return EINVAL, so flag these up in
 *	debug and ignore them for the next iteration
 */
static void stress_af_alg_ignore(
	stress_args_t *args,
	stress_crypto_info_t *info,
	const char *systemcall)
{
	if ((stress_instance_zero(args)) && (!info->ignore)) {
		pr_dbg_skip("%s: %s using %s (%s), failed with EINVAL, skipping this crypto engine\n",
			args->name, systemcall, info->name, info->type);
	}
	info->ignore = true;
}

static int stress_af_alg_hash(
	stress_args_t *args,
	const int sockfd,
	stress_crypto_info_t *info)
{
	int fd, rc;
	size_t j;
	const size_t digest_size = (size_t)info->digest_size;
	struct sockaddr_alg sa;
	int retries = MAX_AF_ALG_RETRIES_BIND;
	char input[DATA_LEN + ALLOC_SLOP] ALIGN64;
	char *digest;

	if (UNLIKELY(digest_size < 1))
		return EXIT_NO_RESOURCE;

	digest = (char *)malloc(digest_size + ALLOC_SLOP);
	if (UNLIKELY(!digest))
		return EXIT_NO_RESOURCE;

	(void)shim_memset(&sa, 0, sizeof(sa));
	sa.salg_family = AF_ALG;
	(void)shim_strscpy((char *)sa.salg_type, "hash", sizeof(sa.salg_type));
	(void)shim_strscpy((char *)sa.salg_name, info->name, sizeof(sa.salg_name) - 1);

retry_bind:
	if (bind(sockfd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
		switch (errno) {
		case ENOENT:
			/* Perhaps the hash does not exist with this kernel */
			info->ignore = true;
			rc = EXIT_SUCCESS;
			goto err;
		case ELIBBAD:
			if (info->selftest) {
				pr_fail("%s: bind failed but %s (%s) self test passed, errno=%d (%s)\n",
					args->name, info->name, info->type, errno, strerror(errno));
				rc = EXIT_FAILURE;
			} else {
				/*
				 *  self test was not marked as passed, this
				 *  could be because algo was not allowed, e.g.
				 *  FIPS enabled, so silently ignore bind failure
				 */
				rc = EXIT_SUCCESS;
			}
			goto err;
		case EBUSY:
		case EINTR:
			rc = EXIT_SUCCESS;
			goto err;
		case ETIMEDOUT:
			if (retries-- > 0)
				goto retry_bind;
			rc = EXIT_NO_RESOURCE;
			goto err;
		}
		pr_fail("%s: %s (%s): bind failed, errno=%d (%s)\n",
			args->name, info->name, info->type, errno, strerror(errno));
		rc = EXIT_FAILURE;
		goto err;
	}

	fd = accept(sockfd, NULL, 0);
	if (UNLIKELY(fd < 0)) {
		if (errno == EINTR) {
			rc = EXIT_SUCCESS;
			goto err;
		}
		pr_fail("%s: %s (%s): accept failed, errno=%d (%s)\n",
			args->name, info->name, info->type, errno, strerror(errno));
		rc = EXIT_FAILURE;
		goto err;
	}

	stress_rndbuf(input, DATA_LEN);

	for (j = 32; j < DATA_LEN; j += 32) {
		double t, delta;
		ssize_t ret;

		if (UNLIKELY(!stress_continue(args)))
			break;

		t = stress_time_now();
		ret = send(fd, input, j, 0);
		if (UNLIKELY(ret != (ssize_t)j)) {
			if ((ret < 0) && (errno != 0)) {
				if (errno == EINTR)
					break;
				if ((errno == ENOKEY) || (errno == ENOENT))
					continue;
				if (errno == EINVAL) {
					stress_af_alg_ignore(args, info, "send()");
					break;
				}
				pr_fail("%s: %s (%s): send failed, errno=%d (%s)\n",
						args->name, info->name, info->type,
						errno, strerror(errno));
				rc = EXIT_FAILURE;
				goto err_close;
			}
			/* Silently ignore incorrectly sized data */
			continue;
		}
		ret = recv(fd, digest, digest_size, MSG_WAITALL);
		if (UNLIKELY(ret != (ssize_t)digest_size)) {
			if (ret < 0) {
				if (errno == EINTR)
					break;
				if (errno == EOPNOTSUPP)
					goto err_abort;
				pr_fail("%s: %s (%s): recv failed, errno=%d (%s)\n",
					args->name, info->name, info->type,
					errno, strerror(errno));
				rc = EXIT_FAILURE;
				goto err_close;
			}
			/* Silently ignore incorrectly sized data */
			continue;
		}
		delta = stress_time_now() - t;
		if (delta > 0.0) {
			info->metrics.duration += delta;
			info->metrics.count += 1.0;
		}
		stress_bogo_inc(args);
		if (args->bogo.max_ops && (stress_bogo_get(args) >= args->bogo.max_ops)) {
			rc = EXIT_SUCCESS;
			goto err_close;
		}
	}
err_abort:
	rc = EXIT_SUCCESS;
err_close:
	(void)close(fd);
err:
	free(digest);
	return rc;
}

static int stress_af_alg_cipher(
	stress_args_t *args,
	const int sockfd,
	stress_crypto_info_t *info)
{
	int fd, rc;
	ssize_t j;
	struct sockaddr_alg sa;
	const ssize_t iv_size = info->iv_size;
	const size_t cbuf_size = CMSG_SPACE(sizeof(__u32)) +
				  CMSG_SPACE(4) + CMSG_SPACE(iv_size);
	const char *salg_type = (info->crypto_type != CRYPTO_AEAD) ? "skcipher" : "aead";
	int retries = MAX_AF_ALG_RETRIES_BIND;
	char input[DATA_LEN + ALLOC_SLOP] ALIGN64;
	char output[DATA_LEN + ALLOC_SLOP] ALIGN64;
	char *cbuf;

	cbuf = (char *)malloc(cbuf_size);
	if (UNLIKELY(!cbuf))
		return EXIT_NO_RESOURCE;

	(void)shim_memset(&sa, 0, sizeof(sa));
	sa.salg_family = AF_ALG;
	(void)shim_strscpy((char *)sa.salg_type, salg_type, sizeof(sa.salg_type));
	(void)shim_strscpy((char *)sa.salg_name, info->name, sizeof(sa.salg_name) - 1);

retry_bind:
	if (bind(sockfd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
		switch (errno) {
		case 0:
		case ENOKEY:
		case ENOENT:
			/* Perhaps the hash does not exist with this kernel */
			info->ignore = true;
			rc = EXIT_SUCCESS;
			goto err;
		case ELIBBAD:
			/* Perhaps the cipher does not exist with this kernel */
			if (info->selftest) {
				pr_fail("%s: %s (%s): bind failed but self test passed, errno=%d (%s)\n",
					args->name, info->name, info->type, errno, strerror(errno));
				rc = EXIT_FAILURE;
			} else {
				/*
				 *  self test was not marked as passed, this
				 *  could be because algo was not allowed, e.g.
				 *  FIPS enabled, so silently ignore bind failure
				 */
				rc = EXIT_SUCCESS;
			}
			goto err;
		case EBUSY:
		case EINTR:
			rc = EXIT_SUCCESS;
			goto err;
		case ETIMEDOUT:
			if (retries-- > 0)
				goto retry_bind;
			rc = EXIT_NO_RESOURCE;
			goto err;
		case EINVAL:
			/* Ignore bind EINVAL failures, these should not abort the stressor */
			stress_af_alg_ignore(args, info, "bind()");
			rc = EXIT_SUCCESS;
			goto err;
		default:
			break;
		}
		pr_fail("%s: %s (%s): bind failed, errno=%d (%s)\n",
			args->name, info->name, info->type, errno, strerror(errno));
		rc = EXIT_FAILURE;
		goto err;
	}

	if (LIKELY(info->crypto_type != CRYPTO_AEAD)) {
#if defined(ALG_SET_KEY)
		char *key;

		key = (char *)malloc((size_t)(info->max_key_size + ALLOC_SLOP));
		if (UNLIKELY(!key)) {
			rc = EXIT_NO_RESOURCE;
			goto err;
		}

		stress_rndbuf(key, (size_t)info->max_key_size);
		if (UNLIKELY(setsockopt(sockfd, SOL_ALG, ALG_SET_KEY, key, (socklen_t)info->max_key_size) < 0)) {
			free(key);
			if (errno == ENOPROTOOPT) {
				rc = EXIT_SUCCESS;
				goto err;
			}
			pr_fail("%s: %s (%s): setsockopt ALG_SET_KEY failed, errno=%d (%s)\n",
				args->name, info->name, info->type, errno, strerror(errno));
			rc = EXIT_FAILURE;
			goto err;
		}
		free(key);
#else
		/* Not supported, skip */
		rc = EXIT_SUCCESS;
		goto err;
#endif
	} else {
#if defined(ALG_SET_AEAD_ASSOCLEN)
		char *assocdata;

		assocdata = (char *)malloc((size_t)(info->max_auth_size + ALLOC_SLOP));
		if (UNLIKELY(!assocdata)) {
			rc = EXIT_NO_RESOURCE;
			goto err;
		}

		stress_rndbuf(assocdata, (size_t)info->max_auth_size);
		if (UNLIKELY(setsockopt(sockfd, SOL_ALG, ALG_SET_AEAD_ASSOCLEN, assocdata, (socklen_t)info->max_auth_size) < 0)) {
			free(assocdata);
			if (errno == ENOPROTOOPT) {
				rc = EXIT_SUCCESS;
				goto err;
			}
			pr_fail("%s: %s (%s): setsockopt ALG_SET_AEAD_ASSOCLEN failed, errno=%d (%s)\n",
				args->name, info->name, info->type, errno, strerror(errno));
			rc = EXIT_FAILURE;
			goto err;
		}
		free(assocdata);
#else
		/* Not supported, skip */
		rc = EXIT_SUCCESS;
		goto err;
#endif
	}

	fd = accept(sockfd, NULL, 0);
	if (fd < 0) {
		pr_fail("%s: %s (%s): accept failed, errno=%d (%s)\n",
			args->name, info->name, info->type, errno, strerror(errno));
		rc = EXIT_FAILURE;
		goto err;
	}

	for (j = 32; j < (ssize_t)DATA_LEN; j += 32) {
		__u32 *u32ptr;
		struct msghdr msg;
		struct cmsghdr *cmsg;
		struct af_alg_iv *iv;	/* Initialisation Vector */
		struct iovec iov;
		double t;
		ssize_t ret;

		if (UNLIKELY(!stress_continue(args)))
			break;
		(void)shim_memset(&msg, 0, sizeof(msg));
		(void)shim_memset(cbuf, 0, cbuf_size);

		msg.msg_control = cbuf;
		msg.msg_controllen = cbuf_size;

		/* Chosen operation - ENCRYPT */
		cmsg = CMSG_FIRSTHDR(&msg);
		/* Keep static analysis happy */
		if (UNLIKELY(!cmsg)) {
			pr_fail("%s: %s (%s): unexpected null cmsg found\n",
				args->name, info->name, info->type);
			rc = EXIT_FAILURE;
			goto err_close;
		}
		cmsg->cmsg_level = SOL_ALG;
		cmsg->cmsg_type = ALG_SET_OP;
		cmsg->cmsg_len = CMSG_LEN(4);
		u32ptr = (__u32 *)(uintptr_t)CMSG_DATA(cmsg);
		*u32ptr = ALG_OP_ENCRYPT;

		/* Set up random Initialization Vector */
		cmsg = CMSG_NXTHDR(&msg, cmsg);
		if (!cmsg)
			break;
		cmsg->cmsg_level = SOL_ALG;
		cmsg->cmsg_type = ALG_SET_IV;
		cmsg->cmsg_len = CMSG_LEN(4) + CMSG_LEN(iv_size);
		iv = (void *)CMSG_DATA(cmsg);
		iv->ivlen = (uint32_t)iv_size;

		stress_rndbuf((char *)iv->iv, (size_t)iv_size);

		/* Generate random message to encrypt */
		stress_rndbuf(input, DATA_LEN);
		iov.iov_base = input;
		iov.iov_len = DATA_LEN;

		msg.msg_iov = &iov;
		msg.msg_iovlen = 1;

		if (UNLIKELY(sendmsg(fd, &msg, 0) < 0)) {
			if (errno == 0)
				break;
			if (errno == EINTR)
				break;
			if (errno == ENOMEM)
				break;
			if (errno == EINVAL) {
				stress_af_alg_ignore(args, info, "sendmsg()");
				break;
			}
			pr_fail("%s: %s (%s): sendmsg failed, errno=%d (%s)\n",
				args->name, info->name, info->type,
				errno, strerror(errno));
			rc = EXIT_FAILURE;
			goto err_close;
		}
		ret = recv(fd, output, DATA_LEN, 0);
		if (UNLIKELY(ret != DATA_LEN)) {
			if (ret < 0) {
				if (errno == EINTR)
					break;
				if (errno == EOPNOTSUPP)
					goto err_abort;
				pr_fail("%s: %s (%s): read failed, errno=%d (%s)\n",
					args->name, info->name, info->type,
					errno, strerror(errno));
				rc = EXIT_FAILURE;
				goto err_close;
			}
			pr_fail("%s: %s (%s): read failed, unexpected return length\n",
				args->name, info->name, info->type);
			rc = EXIT_FAILURE;
			goto err_close;
		}

		/* Chosen operation - DECRYPT */
		cmsg = CMSG_FIRSTHDR(&msg);
		if (!cmsg)
			break;
		cmsg->cmsg_level = SOL_ALG;
		cmsg->cmsg_type = ALG_SET_OP;
		cmsg->cmsg_len = CMSG_LEN(4);
		u32ptr = (__u32 *)(uintptr_t)CMSG_DATA(cmsg);
		*u32ptr = ALG_OP_DECRYPT;

		/* Set up random Initialization Vector */
		cmsg = CMSG_NXTHDR(&msg, cmsg);
		if (!cmsg)
			break;

		cmsg->cmsg_level = SOL_ALG;
		cmsg->cmsg_type = ALG_SET_IV;
		cmsg->cmsg_len = CMSG_LEN(4) + CMSG_LEN(iv_size);
		iv = (void *)CMSG_DATA(cmsg);
		iv->ivlen = (uint32_t)iv_size;

		iov.iov_base = output;
		iov.iov_len = DATA_LEN;

		msg.msg_iov = &iov;
		msg.msg_iovlen = 1;

		t = stress_time_now();
		if (UNLIKELY(sendmsg(fd, &msg, 0) < 0)) {
			if (errno == 0)
				break;
			if (errno == ENOMEM)
				break;
			if (errno == EINTR)
				break;
			if (errno == EINVAL) {
				stress_af_alg_ignore(args, info, "sendmsg()");
				break;
			}
			pr_fail("%s: %s (%s): sendmsg failed, errno=%d (%s)\n",
				args->name, info->name, info->type,
				errno, strerror(errno));
			rc = EXIT_FAILURE;
			goto err_close;
		}
		if (UNLIKELY(read(fd, output, DATA_LEN) != DATA_LEN)) {
			pr_fail("%s: %s (%s): read failed, errno=%d (%s)\n",
				args->name, info->name, info->type,
				errno, strerror(errno));
			rc = EXIT_FAILURE;
			goto err_close;
		} else {
			if (shim_memcmp(input, output, DATA_LEN)) {
				pr_fail("%s: %s (%s): decrypted data "
					"different from original data "
					"(possible kernel bug)\n",
					args->name, info->name, info->type);
			} else {
				const double delta = stress_time_now() - t;

				if (delta > 0.0) {
					info->metrics.duration += delta;
					info->metrics.count += 1.0;
				}
				stress_bogo_inc(args);
			}
		}
	}

err_abort:
	rc = EXIT_SUCCESS;
err_close:
	(void)close(fd);
err:
	free(cbuf);
	return rc;
}

static int stress_af_alg_rng(
	stress_args_t *args,
	const int sockfd,
	stress_crypto_info_t *info)
{
	int fd, rc;
	ssize_t j;
	struct sockaddr_alg sa;
	int retries = MAX_AF_ALG_RETRIES_BIND;
	const ssize_t output_size = 16;
	char output[output_size + ALLOC_SLOP] ALIGN64;

	(void)shim_memset(&sa, 0, sizeof(sa));
	sa.salg_family = AF_ALG;
	(void)shim_strscpy((char *)sa.salg_type, "rng", sizeof(sa.salg_type));
	(void)shim_strscpy((char *)sa.salg_name, info->name, sizeof(sa.salg_name) - 1);

retry_bind:
	if (bind(sockfd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
		switch (errno) {
		case 0:
		case ENOKEY:
		case ENOENT:
			/* Perhaps the hash does not exist with this kernel */
			info->ignore = true;
			rc = EXIT_SUCCESS;
			goto err;
		case ELIBBAD:
			/* Perhaps the cipher does not exist with this kernel */
			if (info->selftest) {
				pr_fail("%s: %s (%s): bind failed but self test passed, errno=%d (%s)\n",
					args->name, info->name, info->type, errno, strerror(errno));
				rc = EXIT_FAILURE;
			} else {
				/*
				 *  self test was not marked as passed, this
				 *  could be because algo was not allowed, e.g.
				 *  FIPS enabled, so silently ignore bind failure
				 */
				rc = EXIT_SUCCESS;
			}
			goto err;
		case EBUSY:
		case EINTR:
			rc = EXIT_SUCCESS;
			goto err;
		case ETIMEDOUT:
			if (retries-- > 0)
				goto retry_bind;
			rc = EXIT_NO_RESOURCE;
			goto err;
		case EINVAL:
			/* Ignore bind EINVAL failures, these should not abort the stressor */
			stress_af_alg_ignore(args, info, "bind()");
			rc = EXIT_SUCCESS;
			goto err;
		default:
			break;
		}
		pr_fail("%s: %s (%s): bind failed, errno=%d (%s)\n",
			args->name, info->name, info->type, errno, strerror(errno));
		rc = EXIT_FAILURE;
		goto err;
	}

	fd = accept(sockfd, NULL, 0);
	if (UNLIKELY(fd < 0)) {
		pr_fail("%s: %s (%s): accept failed, errno=%d (%s)\n",
			args->name, info->name, info->type,
			errno, strerror(errno));
		rc = EXIT_FAILURE;
		goto err;
	}

	for (j = 0; j < 16; j++) {
		double delta, t;

		if (UNLIKELY(!stress_continue(args)))
			break;

		t = stress_time_now();
		if (UNLIKELY(read(fd, output, output_size) != output_size)) {
			if (errno != EINVAL) {
				pr_fail("%s: %s (%s): read failed, errno=%d (%s)\n",
					args->name, info->name, info->type,
					errno, strerror(errno));
				rc = EXIT_FAILURE;
				goto err_close;
			}
		}
		delta = stress_time_now() - t;
		if (LIKELY(delta > 0.0)) {
			info->metrics.duration += delta;
			info->metrics.count += 1.0;
		}
		stress_bogo_inc(args);
		if (args->bogo.max_ops && (stress_bogo_get(args) >= args->bogo.max_ops)) {
			rc = EXIT_SUCCESS;
			goto err_close;
		}
	}
	rc = EXIT_SUCCESS;

err_close:
	(void)close(fd);
err:
	return rc;
}

static void stress_af_alg_count_crypto(size_t *count, size_t *internal)
{
	stress_crypto_info_t *ci;

	*count = 0;
	*internal = 0;

	/* Scan for duplications */
	for (ci = crypto_info_list; ci; ci = ci->next) {
		if (ci->internal)
			(*internal)++;
		(*count)++;
	}
}

/*
 *  stress_af_alg_cmp_crypto()
 *	qsort comparison on type then name
 */
static int CONST stress_af_alg_cmp_crypto(const void *p1, const void *p2)
{
	int n;
	const stress_crypto_info_t * const *ci1 = (const stress_crypto_info_t * const *)p1;
	const stress_crypto_info_t * const *ci2 = (const stress_crypto_info_t * const *)p2;

	n = strcmp((*ci1)->type, (*ci2)->type);
	if (n < 0)
		return -1;
	if (n > 0)
		return 1;

	n = strcmp((*ci1)->name, (*ci2)->name);
	if (n < 0)
		return -1;
	if (n > 0)
		return 1;
	return 0;
}

/*
 *  stress_af_alg_sort_crypto()
 *	sort crypto_info_list keyed on type and name
 */
static void stress_af_alg_sort_crypto(void)
{
	stress_crypto_info_t **array, *ci;

	size_t i, n, internal;

	stress_af_alg_count_crypto(&n, &internal);
	if (n == 0)
		return;

	/* Attempt to sort, if we can't silently don't sort */
	array = (stress_crypto_info_t **)calloc(n, sizeof(*array));
	if (!array)
		return;

	for (i = 0, ci = crypto_info_list; ci; ci = ci->next, i++) {
		array[i] = ci;
	}

	qsort(array, n, sizeof(*array), stress_af_alg_cmp_crypto);

	for (i = 0; i < n - 1; i++) {
		array[i]->next = array[i + 1];
	}
	array[i]->next = NULL;
	crypto_info_list = array[0];

	free(array);
}

/*
 *  stress_af_alg_dump_crypto_list()
 *	dump crypto algorithm list to stdout
 */
static void stress_af_alg_dump_crypto_list(void)
{
	stress_crypto_info_t *ci;

	for (ci = crypto_info_list; ci; ci = ci->next) {
		if (ci->internal)
			continue;
		(void)fprintf(stdout, "{ .crypto_type = %s, .type = \"%s\", .name = \"%s\"",
			type_to_type_string(ci->crypto_type), ci->type, ci->name);
		if (ci->block_size)
			(void)fprintf(stdout, ",\t.block_size = %d",
				ci->block_size);
		if (ci->max_key_size)
			(void)fprintf(stdout, ",\t.max_key_size = %d",
				ci->max_key_size);
		if (ci->max_auth_size)
			(void)fprintf(stdout, ",\t.max_auth_size = %d",
				ci->max_auth_size);
		if (ci->iv_size)
			(void)fprintf(stdout, ",\t.iv_size = %d",
				ci->iv_size);
		if (ci->digest_size)
			(void)fprintf(stdout, ",\t.digest_size = %d",
				ci->digest_size);
		(void)fprintf(stdout, " },\n");
	}
	(void)fflush(stdout);
}

/*
 *  stress_af_alg()
 *	stress socket AF_ALG domain
 */
static int stress_af_alg(stress_args_t *args)
{
	int sockfd = -1;
	NOCLOBBER int rc = EXIT_FAILURE;
	const bool verify = !!(g_opt_flags & OPT_FLAGS_VERIFY);
	int retries = MAX_AF_ALG_RETRIES;
	size_t proc_count, count, internal, idx;
	bool af_alg_dump = false;
	stress_crypto_info_t *info;

	stress_af_alg_count_crypto(&proc_count, &internal);

	(void)stress_get_setting("af-alg-dump", &af_alg_dump);

	if (af_alg_dump && stress_instance_zero(args)) {
		pr_inf("%s: dumping cryptographic algorithms found in /proc/crypto to stdout\n",
			args->name);
		stress_af_alg_sort_crypto();
		stress_af_alg_dump_crypto_list();
	}

	stress_af_alg_add_crypto_defconfigs();
	stress_af_alg_sort_crypto();
	stress_af_alg_count_crypto(&count, &internal);

	if (stress_instance_zero(args)) {
		pr_block_begin();
		pr_inf("%s: %zd cryptographic algorithms found in /proc/crypto\n",
			args->name, proc_count);
		pr_inf("%s: %zd cryptographic algorithms in total (with defconfigs)\n",
			args->name, count);
		if (internal)
			pr_inf("%s: %zd cryptographic algorithms are internal and may be unused\n",
				args->name, internal);
		pr_block_end();
	}

	for (;;) {
		sockfd = socket(AF_ALG, SOCK_SEQPACKET, 0);
		if (sockfd >= 0)
			break;

		retries--;
		if ((!stress_continue_flag()) ||
                    (retries < 0) ||
                    (errno != EAFNOSUPPORT)) {
			if (errno == EAFNOSUPPORT) {
				/*
				 *  If we got got here, the protocol is not supported
				 *  so mark it as not implemented and skip the test
				 */
				return EXIT_NOT_IMPLEMENTED;
			}
			pr_fail("%s: socket failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			return rc;
		}
		/*
		 * We may need to retry on EAFNOSUPPORT
		 * as udev may have to load in some
		 * cipher modules which can be racy or
		 * take some time
		 */
		(void)shim_usleep(200000);
	}

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	if (sigsetjmp(jmpbuf, 1) != 0)
		goto deinit;

	if (stress_sighandler(args->name, SIGALRM, stress_af_alg_alarm_handler, NULL) < 0) {
		rc = EXIT_NO_RESOURCE;
		goto deinit;
	}

	do {
		for (info = crypto_info_list; LIKELY(info && stress_continue(args)); info = info->next) {
			if (info->internal || info->ignore)
				continue;

			switch (info->crypto_type) {
			case CRYPTO_AHASH:
			case CRYPTO_SHASH:
				rc = stress_af_alg_hash(args, sockfd, info);
				if (UNLIKELY(verify && (rc == EXIT_FAILURE)))
					goto deinit;
				break;
#if defined(ALG_SET_AEAD_ASSOCLEN)
			case CRYPTO_AEAD:
#endif
			case CRYPTO_CIPHER:
			case CRYPTO_AKCIPHER:
			case CRYPTO_SKCIPHER:
				rc = stress_af_alg_cipher(args, sockfd, info);
				if (UNLIKELY(verify && (rc == EXIT_FAILURE)))
					goto deinit;
				break;
			case CRYPTO_RNG:
				rc = stress_af_alg_rng(args, sockfd, info);
				if (UNLIKELY(verify && (rc == EXIT_FAILURE)))
					goto deinit;
				break;
			case CRYPTO_UNKNOWN:
			default:
				break;
			}
		}
	} while (stress_continue(args));

	rc = EXIT_SUCCESS;
deinit:
	do_jmp = false;
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	for (idx = 0, info = crypto_info_list; info; info = info->next) {
		if (info->metrics.duration > 0.0) {
			const double rate = info->metrics.count / info->metrics.duration;
			char str[64];

			(void)snprintf(str, sizeof(str), "%s (%s) ops/sec", info->name, info->type),

			stress_metrics_set(args, idx, str, rate, STRESS_METRIC_HARMONIC_MEAN);
			idx++;
		}
	}

	(void)close(sockfd);

	return rc;
}

/*
 *  dup_field()
 * 	duplicate a text string field
 */
static char *dup_field(const char *buffer)
{
	const char *ptr = strchr(buffer, ':');
	char *eol = strchr(buffer, '\n');

	if (!ptr)
		return NULL;
	if (eol)
		*eol = '\0';

	return shim_strdup(ptr + 2);
}

/*
 *  int_field()
 *	parse an integer from a numeric field
 */
static int CONST int_field(const char *buffer)
{
	const char *ptr = strchr(buffer, ':');

	if (!ptr)
		return -1;
	return atoi(ptr + 2);
}

/*
 *  bool_field()
 *	parse a boolean from a string field
 *	error/default is false.
 */
static bool CONST bool_field(const char *buffer)
{
	const char *ptr = strchr(buffer, ':');

	if (!ptr)
		return false;
	if (!strncmp("yes", ptr + 2, 3))
		return true;
	if (!strncmp("no", ptr + 2, 2))
		return false;
	return false;
}

/*
 *  stress_af_alg_add_crypto()
 *	add crypto algorithm to list if it is unique
 */
static bool stress_af_alg_add_crypto(const stress_crypto_info_t *info)
{
	stress_crypto_info_t *ci;

	/* Don't add info with empty text fields */
	if ((info->name == NULL) || (info->type == NULL))
		return false;

	/*
	 * Deprecated in Linux 5.9
	 * see commit 9ace6771831017ce75a2bdf03c284b686dd39dba
         */
	if (strcmp(info->name, "ecb(arc4)") == 0)
		return false;
	/*
	 * Don't support non-mainline tk transformations that some
	 * kernels use, see
	 * https://lore.kernel.org/lkml/1594591536-531-1-git-send-email-iuliana.prodan@nxp.com/t/#Z2e.:..:1594591536-531-3-git-send-email-iuliana.prodan::40nxp.com:1drivers:crypto:caam:caamalg.c
	 */
	if (strcmp(info->name, "tk(cbc(aes))") == 0)
		return false;
	if (strcmp(info->name, "tk(ecb(aes))") == 0)
		return false;

	/* Discard invalid data */
	if ((info->digest_size < 0) ||
	    (info->block_size < 0) ||
	    (info->iv_size < 0) ||
	    (info->max_key_size < 0) ||
	    (info->max_auth_size < 0))
		return false;

	/* Scan for duplications */
	for (ci = crypto_info_list; ci; ci = ci->next) {
		if ((strcmp(ci->name, info->name) == 0) &&
		    (strcmp(ci->type, info->type) == 0) &&
		    (ci->block_size == info->block_size) &&
		    (ci->max_key_size == info->max_key_size) &&
		    (ci->max_auth_size == info->max_auth_size) &&
		    (ci->iv_size == info->iv_size) &&
		    (ci->digest_size == info->digest_size) &&
		    (ci->internal == info->internal))
			return false;
	}
	/*
	 *  Add new item, if we can't allocate, silently
	 *  ignore failure and don't add.
	 */
	ci = (stress_crypto_info_t *)malloc(sizeof(*ci));
	if (!ci)
		return false;
	*ci = *info;
	stress_zero_metrics(&ci->metrics, 1);
	ci->next = crypto_info_list;
	crypto_info_list = ci;

	return true;
}

/*
 *  stress_af_alg_add_crypto_defconfigs()
 *	add crypto algorithm predefined/default configs to list
 */
static void stress_af_alg_add_crypto_defconfigs(void)
{
	size_t i;

	for (i = 0; i < SIZEOF_ARRAY(crypto_info_defconfigs); i++) {
		crypto_info_defconfigs[i].source = SOURCE_DEFCONFIG;
		stress_af_alg_add_crypto(&crypto_info_defconfigs[i]);
	}
}

/*
 *  stress_af_alg_info_free()
 *	free and clear stress_crypto_info_t data
 */
static void stress_af_alg_info_free(stress_crypto_info_t *info)
{
	if (info->name)
		free(info->name);
	if (info->type)
		free(info->type);
}

/*
 *  stress_af_alg_init()
 *	populate crypto info list from data from /proc/crypto
 */
static void stress_af_alg_init(const uint32_t instances)
{
	FILE *fp;
	char buffer[1024];
	stress_crypto_info_t info;

	(void)instances;

	crypto_info_list = NULL;
	fp = fopen("/proc/crypto", "r");
	if (!fp)
		return;

	(void)shim_memset(buffer, 0, sizeof(buffer));
	(void)shim_memset(&info, 0, sizeof(info));

	while (fgets(buffer, sizeof(buffer) - 1, fp)) {
		if (!strncmp(buffer, "name", 4)) {
			if (info.name)
				free(info.name);
			info.name = dup_field(buffer);
		}
		else if (!strncmp(buffer, "type", 4)) {
			info.crypto_type = name_to_type(buffer);
			if (info.type)
				free(info.type);
			info.type = dup_field(buffer);
		}
		else if (!strncmp(buffer, "blocksize", 9))
			info.block_size = int_field(buffer);
		else if (!strncmp(buffer, "max keysize", 11))
			info.max_key_size = int_field(buffer);
		else if (!strncmp(buffer, "maxauthsize", 11))
			info.max_auth_size = int_field(buffer);
		else if (!strncmp(buffer, "ivsize", 6))
			info.iv_size = int_field(buffer);
		else if (!strncmp(buffer, "digestsize", 10))
			info.digest_size = int_field(buffer);
		else if (!strncmp(buffer, "internal", 8))
			info.internal = bool_field(buffer);
		else if (!strncmp(buffer, "selftest", 8))
			info.selftest = bool_field(buffer);
		else if (buffer[0] == '\n') {
			if (info.crypto_type != CRYPTO_UNKNOWN) {
				info.source = SOURCE_PROC_CRYPTO;
				if (!stress_af_alg_add_crypto(&info)) {
					free(info.name);
					free(info.type);
				}
			} else {
				stress_af_alg_info_free(&info);
			}
			(void)shim_memset(&info, 0, sizeof(info));
		}
	}
	stress_af_alg_info_free(&info);

	(void)fclose(fp);
}

/*
 *  stress_af_alg_deinit()
 *	free crypto info list
 */
static void stress_af_alg_deinit(void)
{
	stress_crypto_info_t *ci = crypto_info_list;

	while (ci) {
		stress_crypto_info_t *next = ci->next;

		free(ci->name);
		free(ci->type);
		free(ci);
		ci = next;
	}
	crypto_info_list = NULL;
}

const stressor_info_t stress_af_alg_info = {
	.stressor = stress_af_alg,
	.init = stress_af_alg_init,
	.deinit = stress_af_alg_deinit,
	.classifier = CLASS_CPU | CLASS_OS,
	.opts = opts,
	.verify = VERIFY_OPTIONAL,
	.help = help
};

#else
const stressor_info_t stress_af_alg_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_CPU | CLASS_OS,
	.opts = opts,
	.verify = VERIFY_OPTIONAL,
	.help = help,
	.unimplemented_reason = "built without linux/if_alg.h"
};
#endif
