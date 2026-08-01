/*
 * Copyright (C) 2026 Colin Ian King
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

/*
 *  Probe for a linker that supports --wrap and a libgcc that provides
 *  __cpu_indicator_init, the initialiser the resolvers emitted for
 *  target_clones call before reading __cpu_model.
 */
#if defined(__x86_64__) ||	\
    defined(__x86_64) ||	\
    defined(__amd64__) ||	\
    defined(__amd64)
#else
#error arch not supported
#endif

struct cpu_model_t {
	unsigned int __cpu_vendor;
	unsigned int __cpu_type;
	unsigned int __cpu_subtype;
	unsigned int __cpu_features[1];
};

extern struct cpu_model_t __cpu_model;
extern void __real___cpu_indicator_init(void);

void __wrap___cpu_indicator_init(void);

void __wrap___cpu_indicator_init(void)
{
	__real___cpu_indicator_init();
}

int main(void)
{
	__wrap___cpu_indicator_init();

	return (int)__cpu_model.__cpu_subtype;
}
