/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
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
#include "stress-ng.h"

static const stress_help_t help[] = {
	{ NULL,	"loop N",	"start N workers exercising loopback devices" },
	{ NULL,	"loop-ops N",	"stop after N bogo loopback operations" },
	{ NULL,	NULL,		NULL }
};

#if defined(HAVE_LINUX_LOOP_H) && \
    defined(LOOP_CTL_GET_FREE) && \
    defined(LOOP_SET_FD) && \
    defined(LOOP_CLR_FD) && \
    defined(LOOP_CTL_REMOVE)

#if !defined(LOOP_CONFIGURE)
#define LOOP_CONFIGURE	(0x4C0A)
#endif

/*
 *  See include/uapi/linux/loop.h
 */
struct shim_loop_info64 {
        uint64_t	lo_device;
        uint64_t	lo_inode;
        uint64_t	lo_rdevice;
        uint64_t	lo_offset;
        uint64_t	lo_sizelimit;
        uint32_t	lo_number;
        uint32_t	lo_encrypt_type;
        uint32_t	lo_encrypt_key_size;
        uint32_t	lo_flags;
        uint8_t		lo_file_name[LO_NAME_SIZE];
        uint8_t		lo_crypt_name[LO_NAME_SIZE];
        uint8_t		lo_encrypt_key[LO_KEY_SIZE];
        uint64_t	lo_init[2];
};

struct shim_loop_config {
        uint32_t		fd;
        uint32_t		block_size;
        struct loop_info64      info;
        uint64_t 		__reserved[8];
};


static const char *loop_attr[] = {
	"backing_file",
	"offset",
	"sizelimit",
	"autoclear",
	"partscan",
	"dio"
};

/*
 *  stress_loot_supported()
 *      check if we can run this as root
 */
static int stress_loop_supported(const char *name)
{
	if (!stress_check_capability(SHIM_CAP_SYS_ADMIN)) {
		pr_inf("%s stressor will be skipped, "
			"need to be running with CAP_SYS_ADMIN "
			"rights for this stressor\n", name);
		return -1;
	}
	return 0;
}

/*
 *  stress_loop()
 *	stress loopback device
 */
static int stress_loop(const stress_args_t *args)
{
	int ret, backing_fd, rc = EXIT_FAILURE;
	char backing_file[PATH_MAX];
	size_t backing_size = 2 * MB;
	const int bad_fd = stress_get_bad_fd();

	ret = stress_temp_dir_mk_args(args);
	if (ret < 0)
		return exit_status(-ret);

	(void)stress_temp_filename_args(args,
		backing_file, sizeof(backing_file), stress_mwc32());

	if ((backing_fd = open(backing_file, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR)) < 0) {
		pr_fail("%s: open %s failed, errno=%d (%s)\n",
			args->name, backing_file, errno, strerror(errno));
		goto tidy;
	}
	(void)unlink(backing_file);
	if (ftruncate(backing_fd, backing_size) < 0) {
		pr_fail("%s: ftruncate failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		(void)close(backing_fd);
		goto tidy;
	}

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		int ctrl_dev, loop_dev;
		void *ptr;
		size_t i;
		long dev_num;
#if defined(LOOP_SET_DIRECT_IO)
		unsigned long dio;
#endif
		char dev_name[PATH_MAX];
#if defined(LOOP_GET_STATUS)
		struct loop_info info;
#endif
#if defined(LOOP_GET_STATUS64)
		struct loop_info64 info64;
#endif
#if defined(LOOP_SET_BLOCK_SIZE)
		unsigned long blk_size;
		static const uint16_t blk_sizes[] = {
			512, 1024, 2048, 4096,
		};
#endif

		/*
		 *  Open loop control device
		 */
		ctrl_dev = open("/dev/loop-control", O_RDWR);
		if (ctrl_dev < 0) {
			pr_fail("%s: cannot open /dev/loop-control: %d (%s)\n",
				args->name, errno, strerror(errno));
			break;
		}

		/*
		 *  Try for a random loop number first...
		 */
		dev_num = stress_mwc1() ? -1 :
				ioctl(ctrl_dev, LOOP_CTL_ADD, stress_mwc16() + 1024);
		if (dev_num < 0) {
			/*
			 *  then attempt to get a free loop device
			 */
			dev_num = ioctl(ctrl_dev, LOOP_CTL_GET_FREE);
			if (dev_num < 0)
				goto next;
		}

		/*
		 *  Open new loop device
		 */
		(void)snprintf(dev_name, sizeof(dev_name), "/dev/loop%ld", dev_num);
		loop_dev = open(dev_name, O_RDWR);
		if (loop_dev < 0)
			goto destroy_loop;

		/*
		 *  Associate loop device with bad backing storage fd
		 */
		ret = ioctl(loop_dev, LOOP_SET_FD, bad_fd);
		if (ret == 0)
			ioctl(loop_dev, LOOP_CLR_FD, bad_fd);

		/*
		 *  Associate loop device with backing storage
		 */
		ret = ioctl(loop_dev, LOOP_SET_FD, backing_fd);
		if (ret < 0)
			goto close_loop;

		for (i = 0; i < SIZEOF_ARRAY(loop_attr); i++) {
			char attr_path[PATH_MAX];
			char buf[4096];

			(void)snprintf(attr_path, sizeof(attr_path),
				"/sys/devices/virtual/block/loop%ld/loop/%s",
				dev_num, loop_attr[i]);
			ret = system_read(attr_path, buf, sizeof(buf));
			(void)ret;
		}

#if defined(LOOP_GET_STATUS)
		/*
		 *  Fetch loop device status information
		 */
		ret = ioctl(loop_dev, LOOP_GET_STATUS, &info);
		if (ret < 0)
			goto clr_loop;

		/*
		 *  Try to set some flags
		 */
		info.lo_flags |= (LO_FLAGS_AUTOCLEAR | LO_FLAGS_READ_ONLY);
#if defined(LOOP_SET_STATUS)
		ret = ioctl(loop_dev, LOOP_SET_STATUS, &info);
		(void)ret;
		switch (stress_mwc1()) {
		case 0:
			info.lo_encrypt_type = LO_CRYPT_NONE;
			info.lo_encrypt_key_size = 0;
			break;
		case 1:
			info.lo_encrypt_type = LO_CRYPT_XOR;
			stress_strnrnd((char *)info.lo_encrypt_key, LO_KEY_SIZE);
			info.lo_encrypt_key[LO_KEY_SIZE - 1] = '\0';
			info.lo_encrypt_key_size = LO_KEY_SIZE - 1;
			break;
		}
		ret = ioctl(loop_dev, LOOP_SET_STATUS, &info);
		(void)ret;
#endif
#endif

		ptr = mmap(NULL, backing_size, PROT_READ | PROT_WRITE,
			MAP_SHARED, loop_dev, 0);
		if (ptr != MAP_FAILED) {
			(void)stress_mincore_touch_pages_interruptible(ptr, backing_size);
#if defined(MS_ASYNC)
			(void)shim_msync(ptr, backing_size, MS_ASYNC);
#endif
			(void)munmap(ptr, backing_size);
			(void)shim_fsync(loop_dev);
		}

#if defined(LOOP_GET_STATUS64)
		/*
		 *  Fetch loop device status information
		 */
		ret = ioctl(loop_dev, LOOP_GET_STATUS64, &info64);
		if (ret < 0)
			goto clr_loop;

		/*
		 *  Try to set some flags
		 */
		info.lo_flags |= (LO_FLAGS_AUTOCLEAR | LO_FLAGS_READ_ONLY);
#if defined(LOOP_SET_STATUS64)
		ret = ioctl(loop_dev, LOOP_SET_STATUS64, &info64);
		(void)ret;
#endif
#endif

#if defined(LOOP_SET_CAPACITY)
		/*
		 *  Resize command (even though we have not changed size)
		 */
		ret = ftruncate(backing_fd, backing_size * 2);
		(void)ret;
		ret = ioctl(loop_dev, LOOP_SET_CAPACITY);
		(void)ret;
#endif

		ret = fsync(backing_fd);
		(void)ret;

#if defined(LOOP_SET_BLOCK_SIZE)
		/*
		 *  Set block size, ignore error return.  This will
		 *  produce kernel warnings but should not break the
		 *  kernel.
		 */
		blk_size = (unsigned long)blk_sizes[stress_mwc8() % SIZEOF_ARRAY(blk_sizes)];
		ret = ioctl(loop_dev, LOOP_SET_BLOCK_SIZE, blk_size);
		(void)ret;
#endif

#if defined(LOOP_SET_DIRECT_IO)
		dio = 1;
		ret = ioctl(loop_dev, LOOP_SET_DIRECT_IO, dio);
		(void)ret;

		dio = 0;
		ret = ioctl(loop_dev, LOOP_SET_DIRECT_IO, dio);
		(void)ret;
#endif

#if defined(LOOP_CHANGE_FD)
		/*
		 *  Attempt to change fd using a known illegal
		 *  fd to force a failure.
		 */
		ret = ioctl(loop_dev, LOOP_CHANGE_FD, bad_fd);
		(void)ret;
		/*
		 *  This should fail because backing store is
		 *  not read-only.
		 */
		ret = ioctl(loop_dev, LOOP_CHANGE_FD, backing_fd);
		(void)ret;
#endif

#if defined(LOOP_CONFIGURE)
		{
			struct shim_loop_config config;
			/*
			 *  Attempt to configure with illegal fd
			 */
			(void)memset(&config, 0, sizeof(config));
			config.fd = bad_fd;

			ret = ioctl(loop_dev, LOOP_CONFIGURE, &config);
			(void)ret;

			/*
			 *  Attempt to configure with NULL config
			 */
			ret = ioctl(loop_dev, LOOP_CONFIGURE, NULL);
			(void)ret;
		}
#endif

#if defined(LOOP_GET_STATUS)
clr_loop:
#endif
		/*
		 *  Disassociate backing store from loop device
		 */
		for (i = 0; i < 1000; i++) {
			ret = ioctl(loop_dev, LOOP_CLR_FD, backing_fd);
			if (ret < 0) {
				if (errno == EBUSY) {
					(void)shim_usleep(10);
				} else {
					pr_fail("%s: failed to disassociate %s from backing store, "
						"errno=%d (%s)\n",
						args->name, dev_name, errno, strerror(errno));
					goto close_loop;
				}
			} else {
				break;
			}
		}
close_loop:
		(void)close(loop_dev);

		/*
		 *  Remove the loop device, may need several retries
		 *  if we get EBUSY
		 */
destroy_loop:
		for (i = 0; i < 1000; i++) {
			ret = ioctl(ctrl_dev, LOOP_CTL_REMOVE, dev_num);
			if ((ret < 0) && (errno == EBUSY)) {
				(void)shim_usleep(10);
			} else {
				break;
			}
		}
next:
		(void)close(ctrl_dev);
#if defined(LOOP_SET_CAPACITY)
		ret = ftruncate(backing_fd, backing_size);
		(void)ret;
#endif

		inc_counter(args);
	} while (keep_stressing(args));

	rc = EXIT_SUCCESS;

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	(void)close(backing_fd);
tidy:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	(void)stress_temp_dir_rm_args(args);

	return rc;
}

stressor_info_t stress_loop_info = {
	.stressor = stress_loop,
	.supported = stress_loop_supported,
	.class = CLASS_OS | CLASS_DEV,
	.help = help
};
#else

static int stress_loop_supported(const char *name)
{
	pr_inf("%s: stressor will be skipped, loop is not available\n", name);
	return -1;
}

stressor_info_t stress_loop_info = {
	.stressor = stress_not_implemented,
	.supported = stress_loop_supported,
	.class = CLASS_OS | CLASS_DEV,
	.help = help
};
#endif
