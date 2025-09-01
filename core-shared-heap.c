/*
 * Copyright (C) 2023-2025 Colin Ian King.
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
#include "core-lock.h"
#include "core-madvise.h"
#include "core-mmap.h"
#include "core-stressors.h"
#include "core-shared-heap.h"

/*
 *   The max heap size needs to be larger if we can't determine if strings
 *   being dup'd are literal strings. Literal string dups just use the
 *   literal string, non-literals (e.g. stack based) need to be dup'd from
 *   a shared memory heap.
 */
#if defined(HAVE_BUILTIN_CONSTANT_P)
#define STRESS_MAX_SHARED_HEAP_SIZE		(64 * KB)
#else
#define STRESS_MAX_SHARED_HEAP_SIZE		(256 * KB)
#endif

/* Used just to determine number of stressors via STRESS_MAX */
enum {
        STRESSORS(STRESSOR_ENUM)
        STRESS_MAX
};

typedef struct stress_shared_heap_str {
	struct stress_shared_heap_str	*next;
	char str[];
} stress_shared_heap_str_t;

/*
 *  stress_shared_heap_init()
 *	initialized shared heap
 */
void *stress_shared_heap_init(void)
{
	const size_t page_size = stress_get_page_size();

	/* Allocate enough heap for all stressor descriptions with 100% metrics allocated */
	size_t size = (STRESS_MISC_METRICS_MAX * (32 + sizeof(void *)) * STRESS_MAX);

	size = STRESS_MINIMUM(size, STRESS_MAX_SHARED_HEAP_SIZE);
	g_shared->shared_heap.out_of_memory = false;
	g_shared->shared_heap.heap_size = (size + page_size - 1) & ~(page_size - 1);
	g_shared->shared_heap.str_list_head = NULL;
	g_shared->shared_heap.heap = stress_mmap_anon_shared(size, PROT_READ | PROT_WRITE);
	if (UNLIKELY(g_shared->shared_heap.heap == MAP_FAILED)) {
		g_shared->shared_heap.lock = NULL;
		return NULL;
	}
	stress_set_vma_anon_name(g_shared->shared_heap.heap, size, "shared-heap");
	(void)stress_madvise_mergeable(g_shared->shared_heap.heap, size);
	g_shared->shared_heap.lock = stress_lock_create("shared-heap");
	if (UNLIKELY(!g_shared->shared_heap.lock)) {
		(void)stress_munmap_anon_shared((void *)g_shared->shared_heap.heap, g_shared->shared_heap.heap_size);
		g_shared->shared_heap.heap = NULL;
		return NULL;
	}
	return g_shared->shared_heap.lock;
}

/*
 *  stress_shared_heap_free()
 *	free shared heap
 */
void stress_shared_heap_free(void)
{
	if (g_shared->shared_heap.out_of_memory) {
		pr_inf("shared heap: out of memory duplicating some strings, increase STRESS_MAX_SHARED_HEAP_SIZE to fix this\n");
	}
#if defined(STRESS_SHARED_HEAD_DEBUG)
	if (g_shared->shared_heap.offset > 0) {
		pr_dbg("shared heap: used %zd of %zd bytes of heap\n", g_shared->shared_heap.offset, g_shared->shared_heap.heap_size);
	}
#endif
	if (g_shared->shared_heap.heap) {
		(void)stress_munmap_anon_shared((void *)g_shared->shared_heap.heap, g_shared->shared_heap.heap_size);
		g_shared->shared_heap.heap = NULL;
	}
	if (g_shared->shared_heap.lock) {
		(void)stress_lock_destroy(g_shared->shared_heap.lock);
		g_shared->shared_heap.lock = NULL;
	}
	g_shared->shared_heap.out_of_memory = false;
}

/*
 *  stress_shared_heap_malloc()
 *	Primitive non-free'ing heap allocator. Just return next allocated chunk from
 *	the shared memory heap. We don't use need a per-object free'ing, so no need
 *	to keep track of holes or do hole coalescing. Keep it simple for now.
 */
void *stress_shared_heap_malloc(const size_t size)
{
	ssize_t heap_free;
	void *ptr;

	if (UNLIKELY(stress_lock_acquire(g_shared->shared_heap.lock) < 0))
		return NULL;

	heap_free = g_shared->shared_heap.heap_size - g_shared->shared_heap.offset;
	if (heap_free < (ssize_t)size) {
		g_shared->shared_heap.out_of_memory = true;
		(void)stress_lock_release(g_shared->shared_heap.lock);
		return NULL;
	}
	ptr = (void *)((uintptr_t)g_shared->shared_heap.heap + g_shared->shared_heap.offset);
	g_shared->shared_heap.offset += (size + sizeof(void *) - 1) & ~(sizeof(void *) - 1);
	(void)stress_lock_release(g_shared->shared_heap.lock);

	return ptr;
}

/*
 *  stress_shared_heap_dup_const()
 *	Do a strdup of string using the shared heap. String must never be
 *	modified as this dup operation reused existing identical strings
 *	allocated on the shared heap. This is designed for storing metric
 *	descriptions that get allocated per stressor and we want to reduce
 *	duplicated allocations where possible.
 */
char *stress_shared_heap_dup_const(const char *str)
{
	size_t len, str_len;
	stress_shared_heap_str_t *heap_str;

	if (UNLIKELY(stress_lock_acquire(g_shared->shared_heap.lock) < 0))
		return NULL;

	for (heap_str = (stress_shared_heap_str_t *)g_shared->shared_heap.str_list_head; heap_str; heap_str = heap_str->next) {
		if (strcmp(str, heap_str->str) == 0) {
			(void)stress_lock_release(g_shared->shared_heap.lock);
			return heap_str->str;
		}
	}
	(void)stress_lock_release(g_shared->shared_heap.lock);
	str_len = strlen(str) + 1;
	len = str_len + sizeof(void *);
	heap_str = (stress_shared_heap_str_t *)stress_shared_heap_malloc(len);
	if (UNLIKELY(!heap_str))
		return NULL;

	(void)shim_strscpy(heap_str->str, str, str_len);
	heap_str->next = NULL;

	/*
	 *  We failed to acquire so we can't add to list, return dup'd string
	 *  and skip adding it to the list, at least the dup worked!
	 */
	if (UNLIKELY(stress_lock_acquire(g_shared->shared_heap.lock) < 0))
		return heap_str->str;

	/*
	 *  Save a copy so it can be reused
	 */
	heap_str->next = (stress_shared_heap_str_t *)g_shared->shared_heap.str_list_head;
	g_shared->shared_heap.str_list_head = (void *)heap_str;

	(void)stress_lock_release(g_shared->shared_heap.lock);
	return heap_str->str;
}

