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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <jpeglib.h>

int main(void)
{
	const int x_max = 64, y_max = 64, quality = 95;
	unsigned char rgb[x_max * y_max * 3], *ptr;

	(void)memset(rgb, 0, sizeof(rgb));

	struct jpeg_compress_struct cinfo;
	struct jpeg_error_mgr jerr;
	JSAMPROW row_pointer[y_max];
	int y;

	cinfo.err = jpeg_std_error(&jerr);
	jpeg_create_compress(&cinfo);
	jpeg_stdio_dest(&cinfo, stdout);

	cinfo.image_width = x_max;
	cinfo.image_height = y_max;
	cinfo.input_components = 3;
	cinfo.in_color_space = JCS_RGB;
	jpeg_set_defaults(&cinfo);
	jpeg_set_quality(&cinfo, (int)quality, TRUE);
	jpeg_start_compress(&cinfo, TRUE);

	for (ptr = rgb, y = 0; y < y_max; y++, ptr += 3 * x_max)
		row_pointer[y] = ptr;

	(void)jpeg_write_scanlines(&cinfo, row_pointer, y_max);
	jpeg_finish_compress(&cinfo);
	jpeg_destroy_compress(&cinfo);

	return 0;
}
