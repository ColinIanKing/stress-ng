/*
 * Copyright (C) 2014-2020 Canonical, Ltd.
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

static inline uint32_t OPTIMIZE3 stress_hash_sdbm(const char *str)
{
        register uint32_t hash = 0;
        register int c;

        while ((c = *str++))
                hash = c + (hash << 6) + (hash << 16) - hash;
        return hash;
}


stress_hash_table_t *stress_hash_create(const size_t n)
{
	stress_hash_table_t *hash_table;

	if (n == 0)
		return NULL;

	hash_table = malloc(sizeof(*hash_table));
	if (!hash_table)
		return NULL;

	hash_table->table = calloc(n, sizeof(stress_hash_t *));
	if (!hash_table->table) {
		free(hash_table);
		return NULL;
	}
	hash_table->n = n;

	return hash_table;
}

stress_hash_t *stress_hash_get(stress_hash_table_t *hash_table, const char *str)
{
	stress_hash_t *hash;
	uint32_t h;

	if (!hash_table)
		return NULL;
	if (!str)
		return NULL;

	h = stress_hash_sdbm(str) % hash_table->n;
	hash = hash_table->table[h];
	while (hash) {
		if (!strcmp(str, hash->str))
			return hash;
		hash = hash->next;
	}
	return NULL;
}

stress_hash_t *stress_hash_add(stress_hash_table_t *hash_table, const char *str)
{
	stress_hash_t *hash, *new_hash;
	uint32_t h;

	if (!hash_table)
		return NULL;
	if (!str)
		return NULL;

	new_hash = malloc(sizeof(*new_hash));
	if (!new_hash)
		return NULL;
	new_hash->str = strdup(str);
	if (!new_hash->str) {
		free(new_hash);
		return NULL;
	}

	h = stress_hash_sdbm(str) % hash_table->n;
	hash = hash_table->table[h];
	while (hash) {
		if (!strcmp(str, hash->str))
			return hash;
		hash = hash->next;
	}
	new_hash->next = hash_table->table[h];
	hash_table->table[h] = new_hash;
	return new_hash;
}

void stress_hash_delete(stress_hash_table_t *hash_table)
{
	size_t i;

	if (!hash_table)
		return;

	for (i = 0; i < hash_table->n; i++) {
		stress_hash_t *hash = hash_table->table[i];

		while (hash) {
			stress_hash_t *next = hash->next;

			free(hash->str);
			free(hash);
			hash = next;
		}
	}
	free(hash_table->table);
	free(hash_table);
}
