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
#include "core-builtin.h"

#if defined(HAVE_SYS_XATTR_H)
#include <sys/xattr.h>
#undef HAVE_ATTR_XATTR_H
#elif defined(HAVE_ATTR_XATTR_H)
#include <attr/xattr.h>
#endif
/*  Sanity check */
#if defined(HAVE_SYS_XATTR_H) &&        \
    defined(HAVE_ATTR_XATTR_H)
#error cannot have both HAVE_SYS_XATTR_H and HAVE_ATTR_XATTR_H
#endif

static const stress_help_t help[] = {
	{ NULL,	"xattr N",	"start N workers stressing file extended attributes" },
	{ NULL,	"xattr-ops N",	"stop after N bogo xattr operations" },
	{ NULL,	NULL,		NULL }
};

#if (defined(HAVE_SYS_XATTR_H) ||	\
     defined(HAVE_ATTR_XATTR_H)) &&	\
    defined(HAVE_FGETXATTR) &&		\
    defined(HAVE_FLISTXATTR) &&		\
    defined(HAVE_FREMOVEXATTR) &&	\
    defined(HAVE_FSETXATTR) &&		\
    defined(HAVE_GETXATTR) &&		\
    defined(HAVE_LISTXATTR) &&		\
    defined(HAVE_SETXATTR)

#define MAX_XATTRS		(4096)	/* must be a multiple of sizeof(uint32_t) */

/*
 *  stress_xattr
 *	stress the xattr operations
 */
static int stress_xattr(stress_args_t *args)
{
	int ret, fd, rc = EXIT_FAILURE;
	const int bad_fd = stress_get_bad_fd();
	char filename[PATH_MAX];
#if defined(O_TMPFILE)
	char dirname[PATH_MAX];
#endif
	char bad_filename[PATH_MAX + 4];
	char *hugevalue = NULL;
	const char *fs_type;
#if defined(XATTR_SIZE_MAX)
	const size_t hugevalue_sz = XATTR_SIZE_MAX + 16;
	char *large_tmp;
#else
	const size_t hugevalue_sz = 256 * KB;
#endif
	uint32_t rnd32;

#if defined(XATTR_SIZE_MAX)
	large_tmp = (char *)calloc(XATTR_SIZE_MAX + 2, sizeof(*large_tmp));
	if (!large_tmp) {
		pr_inf_skip("%s: failed to allocate large %zu byte xattr buffer%s, skipping stressor\n",
			args->name, (size_t)(XATTR_SIZE_MAX + 2) * sizeof(*large_tmp),
			stress_get_memfree_str());
		return EXIT_NO_RESOURCE;
	}
#endif

	ret = stress_temp_dir_mk_args(args);
	if (ret < 0) {
		rc = stress_exit_status(-ret);
		goto out_free;
	}

	rnd32 = stress_mwc32();
	(void)stress_temp_filename_args(args, filename, sizeof(filename), rnd32);
	if ((fd = open(filename, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR)) < 0) {
		rc = stress_exit_status(errno);
		pr_fail("%s: open %s failed, errno=%d (%s)\n",
			args->name, filename, errno, strerror(errno));
		goto out;
	}
	fs_type = stress_get_fs_type(filename);
#if defined(O_TMPFILE)
	(void)stress_temp_dir_args(args, dirname, sizeof(dirname));
#endif
	(void)snprintf(bad_filename, sizeof(bad_filename), "%s_bad", filename);

	hugevalue = (char *)calloc(1, hugevalue_sz);
	if (hugevalue)
		(void)shim_memset(hugevalue, 'X', hugevalue_sz - 1);

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		int i, j;
		char attrname[32];
		char value[32];
		char tmp[sizeof(value)];
		char small_tmp[1];
		ssize_t sret;
		char *buffer;
		char bad_attrname[32];
		uint32_t set_xattr_ok[MAX_XATTRS / sizeof(uint32_t)];

		(void)shim_memset(set_xattr_ok, 0, sizeof(set_xattr_ok));

		for (i = 0; i < MAX_XATTRS; i++) {
			STRESS_CLRBIT(set_xattr_ok, i);
			(void)snprintf(attrname, sizeof(attrname), "user.var_%d", i);
			(void)snprintf(value, sizeof(value), "orig-value-%d", i);

			(void)shim_fremovexattr(fd, attrname);
			ret = shim_fsetxattr(fd, attrname, value, strlen(value), XATTR_CREATE);
			if (UNLIKELY(ret < 0)) {
				if ((errno == ENOTSUP) || (errno == ENOSYS)) {
					if (stress_instance_zero(args))
						pr_inf_skip("%s stressor will be "
							"skipped, filesystem does not "
							"support xattr%s\n", args->name, fs_type);
					rc = EXIT_NO_RESOURCE;
					goto out_close;
				}
				if ((errno != ENOSPC) && (errno != EDQUOT) && (errno != E2BIG)) {
					pr_fail("%s: fsetxattr failed, errno=%d (%s)%s\n",
						args->name, errno, strerror(errno), fs_type);
					goto out_close;
				}
			} else {
				/* set xattr OK, lets remember that for later */
				STRESS_SETBIT(set_xattr_ok, i);
			}
			if (UNLIKELY(!stress_continue(args)))
				goto out_finished;
		}

		/* Exercise empty zero length value */
		ret = shim_fsetxattr(fd, "user.var_empty", "", 0, XATTR_CREATE);
		if (ret == 0)
			VOID_RET(int, shim_fremovexattr(fd, "user.var_empty"));

		(void)snprintf(attrname, sizeof(attrname), "user.var_%d", MAX_XATTRS);
		(void)snprintf(value, sizeof(value), "orig-value-%d", MAX_XATTRS);

		/*
		 *  Exercise bad/invalid fd
		 */
		VOID_RET(int, shim_fsetxattr(bad_fd, attrname, value, strlen(value), XATTR_CREATE));

		/*
		 *  Exercise invalid flags
		 */
		ret = shim_fsetxattr(fd, attrname, value, strlen(value), ~0);
		if (UNLIKELY(ret >= 0)) {
			pr_fail("%s: fsetxattr unexpectedly succeeded on invalid flags, "
				"errno=%d (%s)%s\n", args->name,
				errno, strerror(errno), fs_type);
			goto out_close;
		}

#if defined(HAVE_LSETXATTR)
		ret = shim_lsetxattr(filename, attrname, value, strlen(value), ~0);
		if (UNLIKELY(ret >= 0)) {
			pr_fail("%s: lsetxattr unexpectedly succeeded on invalid flags, "
				"errno=%d (%s)%s\n", args->name,
				errno, strerror(errno), fs_type);
			goto out_close;
		}
#endif
		ret = shim_setxattr(filename, attrname, value, strlen(value), ~0);
		if (UNLIKELY(ret >= 0)) {
			pr_fail("%s: setxattr unexpectedly succeeded on invalid flags, "
				"errno=%d (%s)%s\n", args->name,
				errno, strerror(errno), fs_type);
			goto out_close;
		}
		/* Exercise invalid filename, ENOENT */
		VOID_RET(int, shim_setxattr("", attrname, value, strlen(value), 0));

		/* Exercise invalid attrname, ERANGE */
		VOID_RET(int, shim_setxattr(filename, "", value, strlen(value), 0));

		/* Exercise huge value length, E2BIG */
		if (hugevalue) {
			VOID_RET(int, shim_setxattr(filename, "hugevalue", hugevalue, hugevalue_sz, 0));
		}

		/*
		 * Check fsetxattr syscall cannot succeed in replacing
		 * attribute name and value pair which doesn't exist
		 */
		ret = shim_fsetxattr(fd, attrname, value, strlen(value),
			XATTR_REPLACE);
		if (UNLIKELY(ret >= 0)) {
			pr_fail("%s: fsetxattr succeeded unexpectedly, "
				"replaced attribute which "
				"doesn't exist, errno=%d (%s)%s\n",
				args->name, errno, strerror(errno), fs_type);
			goto out_close;
		}

#if defined(HAVE_LSETXATTR)
		ret = shim_lsetxattr(filename, attrname, value, strlen(value),
			XATTR_REPLACE);
		if (UNLIKELY(ret >= 0)) {
			pr_fail("%s: lsetxattr succeeded unexpectedly, "
				"replaced attribute which "
				"doesn't exist, errno=%d (%s)%s\n",
				args->name, errno, strerror(errno), fs_type);
			goto out_close;
		}
#endif
		ret = shim_setxattr(filename, attrname, value, strlen(value),
			XATTR_REPLACE);
		if (UNLIKELY(ret >= 0)) {
			pr_fail("%s: setxattr succeeded unexpectedly, "
				"replaced attribute which "
				"doesn't exist, errno=%d (%s)%s\n",
				args->name, errno, strerror(errno), fs_type);
			goto out_close;
		}

#if defined(XATTR_SIZE_MAX)
		/* Exercise invalid size argument fsetxattr syscall */
		(void)shim_memset(large_tmp, 'f', XATTR_SIZE_MAX + 1);
		ret = shim_fsetxattr(fd, attrname, large_tmp, XATTR_SIZE_MAX + 1,
			XATTR_CREATE);
		if (UNLIKELY(ret >= 0)) {
			pr_fail("%s: fsetxattr succeeded unexpectedly, "
				"created attribute with size greater "
				"than permitted size, errno=%d (%s)%s\n",
				args->name, errno, strerror(errno), fs_type);
			goto out_close;
		}
#endif

#if defined(HAVE_LSETXATTR) && \
    defined(XATTR_SIZE_MAX)
		(void)shim_memset(large_tmp, 'l', XATTR_SIZE_MAX + 1);
		ret = shim_lsetxattr(filename, attrname, large_tmp,
			XATTR_SIZE_MAX + 1, XATTR_CREATE);
		if (UNLIKELY(ret >= 0)) {
			pr_fail("%s: lsetxattr succeeded unexpectedly, "
				"created attribute with size greater "
				"than permitted size, errno=%d (%s)%s\n",
				args->name, errno, strerror(errno), fs_type);
			goto out_close;
		}
#endif

#if defined(XATTR_SIZE_MAX)
		(void)shim_memset(large_tmp, 's', XATTR_SIZE_MAX + 1);
		ret = shim_setxattr(filename, attrname, large_tmp,
			XATTR_SIZE_MAX + 1, XATTR_CREATE);
		if (UNLIKELY(ret >= 0)) {
			pr_fail("%s: setxattr succeeded unexpectedly, "
				"created attribute with size greater "
				"than permitted size, errno=%d (%s)%s\n",
				args->name, errno, strerror(errno), fs_type);
			goto out_close;
		}
#endif

		/*
		 * Check fsetxattr syscall cannot succeed in creating
		 * attribute name and value pair which already exist
		 */
		(void)snprintf(attrname, sizeof(attrname), "user.var_%d", 0);
		(void)snprintf(value, sizeof(value), "orig-value-%d", 0);
		ret = shim_fsetxattr(fd, attrname, value, strlen(value),
			XATTR_CREATE);
		if (UNLIKELY(ret >= 0)) {
			pr_fail("%s: fsetxattr succeeded unexpectedly, "
				"created attribute which "
				"already exists, errno=%d (%s)%s\n",
				args->name, errno, strerror(errno), fs_type);
			goto out_close;
		} else {
			if (errno == ENOSPC)
				STRESS_CLRBIT(set_xattr_ok, i);
		}

#if defined(HAVE_LSETXATTR)
		ret = shim_lsetxattr(filename, attrname, value, strlen(value),
			XATTR_CREATE);
		if (UNLIKELY(ret >= 0)) {
			pr_fail("%s: lsetxattr succeeded unexpectedly, "
				"created attribute which "
				"already exists, errno=%d (%s)%s\n",
				args->name, errno, strerror(errno), fs_type);
			goto out_close;
		} else {
			if (errno == ENOSPC)
				STRESS_CLRBIT(set_xattr_ok, i);
		}
#endif
		ret = shim_setxattr(filename, attrname, value, strlen(value),
			XATTR_CREATE);
		if (UNLIKELY(ret >= 0)) {
			pr_fail("%s: setxattr succeeded unexpectedly, "
				"created attribute which "
				"already exists, errno=%d (%s)%s\n",
				args->name, errno, strerror(errno), fs_type);
			goto out_close;
		} else {
			if (errno == ENOSPC)
				STRESS_CLRBIT(set_xattr_ok, i);
		}

		for (j = 0; j < i; j++) {
			if (STRESS_GETBIT(set_xattr_ok, j) == 0)
				continue;

			(void)snprintf(attrname, sizeof(attrname), "user.var_%d", j);
			(void)snprintf(value, sizeof(value), "value-%d", j);

			ret = shim_fsetxattr(fd, attrname, value, strlen(value),
				XATTR_REPLACE);
			if (ret < 0) {
				STRESS_CLRBIT(set_xattr_ok, i);
				if (UNLIKELY((errno != ENOSPC) && (errno != EDQUOT) && (errno != E2BIG))) {
					pr_fail("%s: fsetxattr failed, errno=%d (%s)%s\n",
						args->name, errno, strerror(errno), fs_type);
					goto out_close;
				}
			}

			/* ..and do it again using setxattr */
			ret = shim_setxattr(filename, attrname, value, strlen(value),
				XATTR_REPLACE);
			if (ret < 0) {
				STRESS_CLRBIT(set_xattr_ok, i);
				if (UNLIKELY((errno != ENOSPC) && (errno != EDQUOT) && (errno != E2BIG))) {
					pr_fail("%s: setxattr failed, errno=%d (%s)%s\n",
						args->name, errno, strerror(errno), fs_type);
					goto out_close;
				}
			}
#if defined(HAVE_SETXATTRAT) &&	\
    defined(AT_FDCWD)
			{
				shim_xattr_args arg;

				arg.value = (uint64_t)value;
				arg.size = strlen(value);
				arg.flags = 0;

				ret = shim_setxattrat(AT_FDCWD, filename, 0, attrname, &arg, sizeof(arg));
				if (ret < 0) {
					STRESS_CLRBIT(set_xattr_ok, i);
					if (UNLIKELY((errno != ENOSYS) && (errno != ENOSPC) && (errno != EDQUOT) && (errno != E2BIG))) {
						pr_fail("%s: setxattrat failed, errno=%d (%s)%s\n",
							args->name, errno, strerror(errno), fs_type);
						goto out_close;
					}
				}
			}
#endif

#if defined(HAVE_LSETXATTR)
			/* Although not a link, it's good to exercise this call */
			ret = shim_lsetxattr(filename, attrname, value, strlen(value),
				XATTR_REPLACE);
			if (ret < 0) {
				STRESS_CLRBIT(set_xattr_ok, i);
				if (UNLIKELY((errno != ENOSPC) && (errno != EDQUOT) && (errno != E2BIG))) {
					pr_fail("%s: lsetxattr failed, errno=%d (%s)%s\n",
						args->name, errno, strerror(errno), fs_type);
					goto out_close;
				}
			}
#endif
			if (UNLIKELY(!stress_continue(args)))
				goto out_finished;
		}
		for (j = 0; j < i; j++) {
			if (STRESS_GETBIT(set_xattr_ok, i) == 0)
				continue;
			(void)snprintf(attrname, sizeof(attrname), "user.var_%d", j);
			(void)snprintf(value, sizeof(value), "value-%d", j);

			(void)shim_memset(tmp, 0, sizeof(tmp));
			sret = shim_fgetxattr(fd, attrname, tmp, sizeof(tmp));
			if (UNLIKELY(sret < 0)) {
				pr_fail("%s: fgetxattr failed, errno=%d (%s)%s\n",
					args->name, errno, strerror(errno), fs_type);
				goto out_close;
			}
			if (UNLIKELY((STRESS_GETBIT(set_xattr_ok, j) != 0) && strncmp(value, tmp, (size_t)sret))) {
				pr_fail("%s: fgetxattr values different %.*s vs %.*s\n",
					args->name, ret, value, ret, tmp);
				goto out_close;
			}

			/* Exercise getxattr syscall having small value buffer */
			VOID_RET(ssize_t, shim_getxattr(filename, attrname, small_tmp, sizeof(small_tmp)));
			VOID_RET(ssize_t, shim_getxattr(filename, "", small_tmp, 0));
			VOID_RET(ssize_t, shim_getxattr(filename, "", small_tmp, sizeof(small_tmp)));
			sret = shim_getxattr(filename, attrname, tmp, sizeof(tmp));
			if (UNLIKELY(sret < 0)) {
				pr_fail("%s: getxattr failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				goto out_close;
			}
			if (UNLIKELY((STRESS_GETBIT(set_xattr_ok, j) != 0) && strncmp(value, tmp, (size_t)sret))) {
				pr_fail("%s: getxattr values different %.*s vs %.*s\n",
					args->name, ret, value, ret, tmp);
				goto out_close;
			}

#if defined(HAVE_LGETXATTR)
			sret = shim_lgetxattr(filename, attrname, tmp, sizeof(tmp));
			if (UNLIKELY(sret < 0)) {
				pr_fail("%s: lgetxattr failed, errno=%d (%s)%s\n",
					args->name, errno, strerror(errno), fs_type);
				goto out_close;
			}
			if (UNLIKELY((STRESS_GETBIT(set_xattr_ok, j) != 0) && strncmp(value, tmp, (size_t)sret))) {
				pr_fail("%s: lgetxattr values different %.*s vs %.*s\n",
					args->name, ret, value, ret, tmp);
				goto out_close;
			}

			/* Invalid attribute name */
			(void)shim_memset(&bad_attrname, 0, sizeof(bad_attrname));
			VOID_RET(ssize_t, shim_lgetxattr(filename, bad_attrname, tmp, sizeof(tmp)));
#endif
			if (UNLIKELY(!stress_continue(args)))
				goto out_finished;
		}

		/*
		 *  Exercise bad/invalid fd
		 */
		VOID_RET(ssize_t, shim_fgetxattr(bad_fd, "user.var_bad", tmp, sizeof(tmp)));

		/* Invalid attribute name */
		(void)shim_memset(&bad_attrname, 0, sizeof(bad_attrname));
		VOID_RET(ssize_t, shim_fgetxattr(fd, bad_attrname, tmp, sizeof(tmp)));

		/* Exercise fgetxattr syscall having small value buffer */
		VOID_RET(ssize_t, shim_fgetxattr(fd, attrname, small_tmp, sizeof(small_tmp)));

		/* Determine how large a buffer we required... */
		sret = shim_flistxattr(fd, NULL, 0);
		if (UNLIKELY(sret < 0)) {
			pr_fail("%s: flistxattr failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			goto out_close;
		}
		buffer = (char *)malloc((size_t)sret);
		if (LIKELY(buffer != NULL)) {
			/* ...and fetch */
			sret = shim_listxattr(filename, buffer, (size_t)sret);

			if (UNLIKELY(sret < 0)) {
				pr_fail("%s: listxattr failed, errno=%d (%s)%s\n",
					args->name, errno, strerror(errno), fs_type);
				free(buffer);
				goto out_close;
			}

#if defined(HAVE_LISTXATTRAT) &&	\
    defined(AT_FDCWD)
			sret = shim_listxattrat(AT_FDCWD, filename, 0, buffer, (size_t)sret);
			if (UNLIKELY(sret < 0) && (errno != ENOSYS)) {
				pr_fail("%s: listxattr failed, errno=%d (%s)%s\n",
					args->name, errno, strerror(errno), fs_type);
				free(buffer);
				goto out_close;
			}
#endif
			free(buffer);
		}

		/*
		 *  Exercise bad/invalid fd
		 */
		VOID_RET(ssize_t, shim_flistxattr(bad_fd, NULL, 0));

		/*
		 *  Exercise invalid path, ENOENT
		 */
		VOID_RET(ssize_t, shim_listxattr(bad_filename, NULL, 0));
#if defined(HAVE_LISTXATTRAT) &&	\
    defined(AT_FDCWD)
		VOID_RET(ssize_t, shim_listxattrat(AT_FDCWD, bad_filename, 0, NULL, 0));
#endif
#if defined(HAVE_LLISTXATTR)
		VOID_RET(ssize_t, shim_llistxattr(bad_filename, NULL, 0));
#endif

		for (j = 0; j < i; j++) {
			char *errmsg;

			(void)snprintf(attrname, sizeof(attrname), "user.var_%d", j);

			switch (j % 4) {
			case 0:
				ret = shim_fremovexattr(fd, attrname);
				errmsg = "fremovexattr";
				break;
#if defined(HAVE_LREMOVEXATTR)
			case 1:
				ret = shim_lremovexattr(filename, attrname);
				errmsg = "lremovexattr";
				break;
#endif
#if defined(HAVE_REMOVEXATTRAT) &&	\
    defined(AT_FDCWD)
			case 2:
				ret = shim_removexattrat(AT_FDCWD, filename, 0, attrname);
				errmsg = "removexattrat";
				break;
#endif
			default:
				ret = shim_removexattr(filename, attrname);
				errmsg = "removexattr";
				break;
			}
			if (UNLIKELY(ret < 0)) {
#if defined(ENODATA)
				if ((errno != ENODATA) && (errno != ENOSPC) && (errno != ENOSYS)) {
#else
				/* NetBSD does not have ENODATA */
				if ((errno != ENOSPC) && (errno != ENOSYS)) {
#endif
					pr_fail("%s: %s failed, errno=%d (%s)\n",
						args->name, errmsg, errno, strerror(errno));
					goto out_close;
				}
			}
			if (UNLIKELY(!stress_continue(args)))
				goto out_finished;
		}
		/*
		 *  Exercise invalid filename, ENOENT
		 */
		VOID_RET(int, shim_removexattr("", "user.var_1234"));
		VOID_RET(int, shim_lremovexattr("", "user.var_1234"));

#if defined(XATTR_SIZE_MAX)
		/*
		 *  Exercise long attribute, ERANGE
		 */
		(void)shim_memset(large_tmp, 'X', XATTR_SIZE_MAX + 1);
		VOID_RET(int, shim_removexattr(filename, large_tmp));
		VOID_RET(int, shim_lremovexattr(filename, large_tmp));
		VOID_RET(int, shim_fremovexattr(fd, large_tmp));
#endif

		/*
		 *  Exercise bad/invalid fd
		 */
		VOID_RET(int, shim_fremovexattr(bad_fd, "user.var_bad"));

#if defined(HAVE_LLISTXATTR)
		sret = shim_llistxattr(filename, NULL, 0);
		if (UNLIKELY(sret < 0)) {
			pr_fail("%s: llistxattr failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			goto out_close;
		}
#endif

#if defined(O_TMPFILE)
		{
			/*
			 *  Try to reproduce issue fixed in Linux commit
		         *  dd7db149bcd9 ("ubifs: ubifs_jnl_change_xattr:
			 *  Remove assertion 'nlink > 0' for host inode")
			 */
			int tmp_fd;

			tmp_fd = open(dirname, O_TMPFILE | O_RDWR, S_IRUSR | S_IWUSR);
			if (LIKELY(tmp_fd != -1)) {
				fsetxattr(tmp_fd, "user.var_1", "somevalue", 9, XATTR_CREATE);
				fsetxattr(tmp_fd, "user.var_1", "somevalue", 9, XATTR_REPLACE);
				fsetxattr(tmp_fd, "user.var_1", "anothervalue", 12, XATTR_REPLACE);
				(void)close(tmp_fd);
			}
		}
#endif

		stress_bogo_inc(args);
	} while (stress_continue(args));

out_finished:
	rc = EXIT_SUCCESS;
out_close:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	(void)close(fd);
out:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	free(hugevalue);
	(void)shim_unlink(filename);
	(void)stress_temp_dir_rm_args(args);
out_free:
#if defined(XATTR_SIZE_MAX)
	free(large_tmp);
#endif
	return rc;
}

const stressor_info_t stress_xattr_info = {
	.stressor = stress_xattr,
	.classifier = CLASS_FILESYSTEM | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
const stressor_info_t stress_xattr_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_FILESYSTEM | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without sys/xattr.h or attr/xattr.h and xattr family of system calls"
};
#endif
