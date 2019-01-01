/*
 * Copyright (C) 2013-2019 Canonical, Ltd.
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

#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>

#if defined(__gnu_hurd__)
#error msync is defined but not implemented and will always fail
#endif

int main(void)
{
	char buffer[8192];
	static const char *filename = "/tmp/test-msync.tmp";
	int fd, ret, err = 1;
	void *ptr;
	ssize_t rc;
	const size_t sz = sizeof buffer;

	(void)memset(buffer, 0, sizeof(buffer));
	fd = open(filename, O_RDWR | O_CREAT, 0666);
	if (fd < 0)
		return 1;
	(void)unlink(filename);

	rc = write(fd, buffer, sz);
	if (rc != (ssize_t)sz) {
		goto err;
	}
	ptr = mmap(NULL, sz, PROT_READ | PROT_WRITE,
		MAP_PRIVATE, fd, 0);
	if (ptr == MAP_FAILED)
		goto err;

	ret = msync(ptr, sz, MS_ASYNC);
	(void)ret;
	ret = msync(ptr, sz, MS_SYNC);
	(void)ret;
	ret = msync(ptr, sz, MS_INVALIDATE);
	(void)ret;

	(void)munmap(ptr, sz);
	err = 0;
err:
	(void)close(fd);

	return err;
}
