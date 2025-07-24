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
#include "core-capabilities.h"

#include <sys/ioctl.h>

#if defined(HAVE_SYS_SELECT_H)
#include <sys/select.h>
#endif
#if defined(HAVE_LINUX_RANDOM_H)
#include <linux/random.h>
#else
UNEXPECTED
#endif

static const stress_help_t help[] = {
	{ "u N","urandom N",	 "start N workers reading /dev/urandom" },
	{ NULL, "urandom-ops N", "stop after N urandom bogo read operations" },
	{ NULL, NULL,		 NULL }
};

#if defined(RNDCLEARPOOL) ||	\
    defined(RNDZAPENTCNT) ||	\
    defined(RNDADDTOENTCNT) ||	\
    defined(RNDRESEEDCRNG) || 	\
    defined(__linux__)
static void check_eperm(stress_args_t *args, const ssize_t ret, const int err)
{
	if (UNLIKELY((g_opt_flags & OPT_FLAGS_VERIFY) &&
		     ((ret == 0) || ((err != EPERM) &&
		      (err != ENOSYS) &&
		      (err != EINVAL) &&
		      (err != ENOTTY))))) {
		pr_fail("%s: expected errno to be EPERM, got errno %d (%s) instead\n",
			args->name, err, strerror(err));
	}
}
#endif

/*
 *  stress_urandom
 *	stress reading of /dev/urandom and /dev/random
 */
static int stress_urandom(stress_args_t *args)
{
	int fd_urnd, fd_rnd, fd_rnd_blk, rc = EXIT_FAILURE;
#if defined(__linux__)
	int fd_rnd_wr;
#endif
	bool sys_admin = stress_check_capability(SHIM_CAP_SYS_ADMIN);
	double duration = 0.0, bytes = 0.0, rate;
	const size_t page_size = args->page_size;

	fd_urnd = open("/dev/urandom", O_RDONLY);
	if (fd_urnd < 0) {
		if (errno != ENOENT) {
			pr_fail("%s: open /dev/urandom failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			return EXIT_FAILURE;
		}
	}
	/*
	 *  non-blockable /dev/random
	 */
	fd_rnd = open("/dev/random", O_RDONLY | O_NONBLOCK);
	if (fd_rnd < 0) {
		if (errno != ENOENT) {
			pr_fail("%s: open /dev/random failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			(void)close(fd_urnd);
			return EXIT_FAILURE;
		}
	}
	/*
	 *  blockable /dev/random
	 */
	fd_rnd_blk = open("/dev/random", O_RDONLY);
	if (fd_rnd_blk < 0) {
		if (errno != ENOENT) {
			pr_fail("%s: open /dev/random failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			(void)close(fd_rnd);
			(void)close(fd_urnd);
			return EXIT_FAILURE;
		}
	}

#if defined(__linux__)
	/* Maybe we can write, don't report failure if we can't */
	fd_rnd_wr = open("/dev/random", O_WRONLY | O_NONBLOCK);
#endif

	if ((fd_urnd < 0) && (fd_rnd < 0)) {
		if (stress_instance_zero(args))
			pr_inf_skip("%s: random device(s) do not exist, skipping stressor\n",
				args->name);
#if defined(__linux__)
		if (fd_rnd_wr >= 0)
			(void)close(fd_rnd_wr);
#endif
		if (fd_rnd_blk >= 0)
			(void)close(fd_rnd_blk);
		return EXIT_NOT_IMPLEMENTED;
	}

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		uint8_t buffer[8192];
		ssize_t ret;
#if defined(HAVE_SELECT)
		struct timeval timeout;
		fd_set rdfds;
#endif

		if (fd_urnd >= 0) {
			double t;

			t = stress_time_now();
			ret = read(fd_urnd, buffer, sizeof(buffer));
			if (LIKELY(ret >= 0)) {
				duration += stress_time_now() - t;
				bytes += (double)ret;
			} else {
				if ((errno != EAGAIN) && (errno != EINTR)) {
					pr_fail("%s: read of /dev/urandom failed, errno=%d (%s)\n",
						args->name, errno, strerror(errno));
					goto err;
				}
			}
		}

		/*
		 * Fetch entropy pool count, not considered fatal if
		 * this fails, just skip this part of the stressor
		 */
		if (fd_rnd >= 0) {
			double t;
#if defined(RNDGETENTCNT)
			unsigned long int val = 0;

			if (UNLIKELY(ioctl(fd_rnd, RNDGETENTCNT, &val) < 0))
				goto next;
			/* Try to avoid emptying entropy pool */
			if (val < 128)
				goto next;
#else
			UNEXPECTED
#endif
			t = stress_time_now();
			ret = read(fd_rnd, buffer, 1);
			if (LIKELY(ret >= 0)) {
				duration += stress_time_now() - t;
				bytes += (double)ret;
			} else {
				if ((errno != EAGAIN) && (errno != EINTR)) {
					pr_fail("%s: read of /dev/urandom failed, errno=%d (%s)\n",
						args->name, errno, strerror(errno));
					goto err;
				}
			}

#if defined(RNDGETENTCNT)
next:
#endif
			VOID_RET(off_t, lseek(fd_rnd, (off_t)0, SEEK_SET));

			if (!sys_admin) {
				/*
				 *  Exercise the following ioctl's
				 *  that require CAP_SYS_ADMIN capability
				 *  and hence these should return -EPERM.
				 *  We don't want to exericse this with
				 *  the capability since we don't want to
				 *  damage the entropy pool.
				 */
#if defined(RNDCLEARPOOL)
				ret = ioctl(fd_rnd, RNDCLEARPOOL, NULL);
				check_eperm(args, ret, errno);
#else
				UNEXPECTED
#endif
#if defined(RNDZAPENTCNT)
				ret = ioctl(fd_rnd, RNDZAPENTCNT, NULL);
				check_eperm(args, ret, errno);
#else
				UNEXPECTED
#endif
#if defined(RNDADDTOENTCNT)
				{
					int count = stress_mwc8();

					ret = ioctl(fd_rnd, RNDADDTOENTCNT, &count);
					check_eperm(args, ret, errno);

					count = -1;
					ret = ioctl(fd_rnd, RNDADDTOENTCNT, &count);
					check_eperm(args, ret, errno);
				}
#else
				UNEXPECTED
#endif
#if defined(RNDRESEEDCRNG)
				ret = ioctl(fd_rnd, RNDRESEEDCRNG, stress_mwc32());
				check_eperm(args, ret, errno);
#else
				UNEXPECTED
#endif
				/*
				 *  Exercise invalid ioctl command
				 */
				VOID_RET(int, ioctl(fd_rnd, 0xffff, NULL));
#if defined(__linux__)
				if (fd_rnd_wr >= 0) {
					buffer[0] = stress_mwc8();
					ret = write(fd_rnd_wr, buffer, 1);
					check_eperm(args, ret, errno);
				}
#endif
			}
		}

		/*
		 *  Exerise mmap'ing to /dev/urandom
		 */
		if (fd_urnd >= 0) {
			void *ptr;

			ptr = mmap(NULL, page_size, PROT_READ,
				MAP_PRIVATE, fd_urnd, 0);
			if (ptr != MAP_FAILED)
				(void)munmap(ptr, page_size);
		}

#if defined(HAVE_SELECT)
		/*
		 *  Peek if data is available on blockable /dev/random and
		 *  try to read it.
		 */
		if (fd_rnd_blk >= 0) {
			timeout.tv_sec = 0;
			timeout.tv_usec = 0;
			FD_ZERO(&rdfds);
			FD_SET(fd_rnd_blk, &rdfds);

			ret = select(fd_rnd_blk + 1, &rdfds, NULL, NULL, &timeout);
			if (LIKELY(ret > 0)) {
				if (FD_ISSET(fd_rnd_blk, &rdfds)) {
#if defined(__linux__)
					char *ptr;
#endif
					ret = -1;
#if defined(__linux__)

					/*
					 *  Older kernels will EFAULT on reads of data off the end of
					 *  a page, where as newer kernels 5.18-rc2+ will return a
					 *  single byte in the same way as reading /dev/zero does,
					 *  as fixed in Linux kernel commit:
					 *  "random: allow partial reads if later user copies fail"
					 *
					 *  mmap 2 pages, make second page read-only then write off
					 *  the end of the first page.
					 */
					ptr = (char *)mmap(NULL, page_size * 2, PROT_WRITE,
						MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
					if (ptr != MAP_FAILED) {
						/* unmap last page.. */
						if (mprotect((void *)(ptr + page_size), page_size, PROT_READ) == 0) {
							double t;

							/* Exercise 2 byte read on last 1 byte of page */
							t = stress_time_now();
							ret = read(fd_rnd, ptr + args->page_size - 1, 2);
							if (ret >= 0) {
								duration += stress_time_now() - t;
								bytes += (double)ret;
							}
						}
						(void)munmap((void *)ptr, page_size * 2);
					}
#endif
					if (ret < 0) {
						double t;

						t = stress_time_now();
						ret = read(fd_rnd, buffer, 1);
						if (ret >= 0) {
							duration += stress_time_now() - t;
							bytes += (double)ret;
						} else {
							if ((errno != EAGAIN) && (errno != EINTR)) {
								pr_fail("%s: read of /dev/random failed, errno=%d (%s)\n",
									args->name, errno, strerror(errno));
								goto err;
							}
						}
					}
				}
			}
		}
#endif

		stress_bogo_inc(args);
	} while (stress_continue(args));

	stress_metrics_set(args, 0, "million random bits read",
		bytes * 8.0 / 1000000.0, STRESS_METRIC_GEOMETRIC_MEAN);
	rate = (duration > 0.0) ? bytes * 8.0 / duration : 0.0;
	stress_metrics_set(args, 1, "million random bits per sec",
		rate / 1000000.0, STRESS_METRIC_HARMONIC_MEAN);

	rc = EXIT_SUCCESS;
err:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	if (fd_urnd >= 0)
		(void)close(fd_urnd);
	if (fd_rnd >= 0)
		(void)close(fd_rnd);
	if (fd_rnd_blk >= 0)
		(void)close(fd_rnd_blk);
#if defined(__linux__)
	if (fd_rnd_wr >= 0)
		(void)close(fd_rnd_wr);
#endif

	return rc;
}
const stressor_info_t stress_urandom_info = {
	.stressor = stress_urandom,
	.classifier = CLASS_DEV | CLASS_OS,
	.verify = VERIFY_OPTIONAL,
	.help = help
};
