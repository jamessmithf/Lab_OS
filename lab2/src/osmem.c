// SPDX-License-Identifier: BSD-3-Clause

#include "osmem.h"
#include "block_meta.h"
#include <sys/mman.h>
#include <unistd.h>
#include <string.h>

#define ALIGNMENT 8
#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~(ALIGNMENT - 1))
#define METADATA_SIZE ALIGN(sizeof(struct block_meta))
#define MMAP_THRESHOLD (128 * 1024)

static struct block_meta *heap_start;
static int heap_preallocated;

static struct block_meta *get_last_block(void)
{
	struct block_meta *curr = heap_start;

	while (curr != NULL && curr->next != NULL)
		curr = curr->next;
	return curr;
}

static void coalesce_blocks(void)
{
	struct block_meta *curr = heap_start;

	while (curr != NULL) {
		if (curr->status == STATUS_FREE) {
			while (curr->next != NULL && curr->next->status == STATUS_FREE) {
				curr->size += METADATA_SIZE + curr->next->size;
				curr->next = curr->next->next;
				if (curr->next != NULL)
					curr->next->prev = curr;
			}
		}
		curr = curr->next;
	}
}

static struct block_meta *find_best_block(size_t aligned_size)
{
	struct block_meta *best = NULL;

	for (struct block_meta *curr = heap_start; curr != NULL; curr = curr->next) {
		if (curr->status == STATUS_FREE && curr->size >= aligned_size) {
			if (best == NULL || curr->size < best->size)
				best = curr;
		}
	}
	return best;
}

static void split_block(struct block_meta *block, size_t aligned_size)
{
	if (block->size >= aligned_size + METADATA_SIZE + 8) {
		struct block_meta *new_block = (struct block_meta *)((char *)(block + 1) + aligned_size);

		new_block->size = block->size - aligned_size - METADATA_SIZE;
		new_block->status = STATUS_FREE;
		new_block->prev = block;
		new_block->next = block->next;
		if (block->next != NULL)
			block->next->prev = new_block;
		block->next = new_block;
		block->size = aligned_size;
	}
}

static void *alloc_heap(size_t aligned_size)
{
	if (!heap_preallocated) {
		heap_preallocated = 1;
		void *ptr = sbrk(MMAP_THRESHOLD);

		DIE(ptr == (void *)-1, "sbrk");
		heap_start = (struct block_meta *)ptr;
		heap_start->size = MMAP_THRESHOLD - METADATA_SIZE;
		heap_start->status = STATUS_FREE;
		heap_start->prev = NULL;
		heap_start->next = NULL;
	}

	coalesce_blocks();

	struct block_meta *best = find_best_block(aligned_size);

	if (best != NULL) {
		split_block(best, aligned_size);
		best->status = STATUS_ALLOC;
		return (void *)(best + 1);
	}

	struct block_meta *last = get_last_block();

	if (last != NULL && last->status == STATUS_FREE) {
		size_t diff = aligned_size - last->size;
		void *ptr = sbrk(diff);

		DIE(ptr == (void *)-1, "sbrk");
		last->size = aligned_size;
		last->status = STATUS_ALLOC;
		return (void *)(last + 1);
	}

	void *ptr = sbrk(METADATA_SIZE + aligned_size);

	DIE(ptr == (void *)-1, "sbrk");
	struct block_meta *new_block = (struct block_meta *)ptr;

	new_block->size = aligned_size;
	new_block->status = STATUS_ALLOC;
	new_block->prev = last;
	new_block->next = NULL;
	if (last != NULL)
		last->next = new_block;
	else
		heap_start = new_block;

	return (void *)(new_block + 1);
}

void *os_malloc(size_t size)
{
	if (size == 0)
		return NULL;

	size_t aligned_size = ALIGN(size);
	size_t total_size = aligned_size + METADATA_SIZE;

	if (total_size >= MMAP_THRESHOLD) {
		void *ptr = mmap(NULL, total_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);

		DIE(ptr == MAP_FAILED, "mmap");
		struct block_meta *block = (struct block_meta *)ptr;

		block->size = aligned_size;
		block->status = STATUS_MAPPED;
		block->prev = NULL;
		block->next = NULL;
		return (void *)(block + 1);
	}

	return alloc_heap(aligned_size);
}

void os_free(void *ptr)
{
	if (ptr == NULL)
		return;

	struct block_meta *block = (struct block_meta *)ptr - 1;

	if (block->status == STATUS_MAPPED) {
		int ret = munmap(block, block->size + METADATA_SIZE);

		DIE(ret == -1, "munmap");
	} else if (block->status == STATUS_ALLOC) {
		block->status = STATUS_FREE;
	}
}

void *os_calloc(size_t nmemb, size_t size)
{
	if (nmemb == 0 || size == 0)
		return NULL;

	size_t total_bytes = nmemb * size;

	if (total_bytes / nmemb != size)
		return NULL;

	size_t aligned_size = ALIGN(total_bytes);
	size_t total_size = aligned_size + METADATA_SIZE;
	size_t page_size = (size_t)getpagesize();

	if (total_size >= page_size) {
		void *ptr = mmap(NULL, total_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);

		DIE(ptr == MAP_FAILED, "mmap");
		struct block_meta *block = (struct block_meta *)ptr;

		block->size = aligned_size;
		block->status = STATUS_MAPPED;
		block->prev = NULL;
		block->next = NULL;
		memset((void *)(block + 1), 0, aligned_size);
		return (void *)(block + 1);
	}

	void *payload = alloc_heap(aligned_size);

	memset(payload, 0, aligned_size);
	return payload;
}

void *os_realloc(void *ptr, size_t size)
{
	if (ptr == NULL)
		return os_malloc(size);

	if (size == 0) {
		os_free(ptr);
		return NULL;
	}

	struct block_meta *block = (struct block_meta *)ptr - 1;

	if (block->status == STATUS_FREE)
		return NULL;

	size_t aligned_size = ALIGN(size);

	if (block->status == STATUS_MAPPED) {
		void *new_ptr = os_malloc(size);

		if (new_ptr == NULL)
			return NULL;
		size_t copy_size = (block->size < size) ? block->size : size;

		memcpy(new_ptr, ptr, copy_size);
		os_free(ptr);
		return new_ptr;
	}

	/* Block is on the heap */
	if (aligned_size + METADATA_SIZE >= MMAP_THRESHOLD) {
		void *new_ptr = os_malloc(size);

		if (new_ptr == NULL)
			return NULL;
		size_t copy_size = (block->size < size) ? block->size : size;

		memcpy(new_ptr, ptr, copy_size);
		os_free(ptr);
		return new_ptr;
	}

	if (aligned_size == block->size)
		return ptr;

	if (aligned_size < block->size) {
		split_block(block, aligned_size);
		return ptr;
	}

	/* Try to expand block by coalescing following free blocks */
	while (block->next != NULL && block->next->status == STATUS_FREE) {
		block->size += METADATA_SIZE + block->next->size;
		block->next = block->next->next;
		if (block->next != NULL)
			block->next->prev = block;
		if (block->size >= aligned_size)
			break;
	}

	if (block->size >= aligned_size) {
		split_block(block, aligned_size);
		return ptr;
	}

	/* If block is the last one on heap, expand it with sbrk */
	if (block->next == NULL) {
		size_t diff = aligned_size - block->size;
		void *p = sbrk(diff);

		DIE(p == (void *)-1, "sbrk");
		block->size = aligned_size;
		return ptr;
	}

	/* Cannot expand in place, allocate new and copy */
	void *new_ptr = os_malloc(size);

	if (new_ptr == NULL)
		return NULL;
	size_t copy_size = (block->size < size) ? block->size : size;

	memcpy(new_ptr, ptr, copy_size);
	os_free(ptr);
	return new_ptr;
}
