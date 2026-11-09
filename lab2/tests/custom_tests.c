// SPDX-License-Identifier: BSD-3-Clause

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include "osmem.h"

#define TEST_PASS(name) printf("  [PASS] %s\n", name)

static void test_edge_cases(void)
{
	printf("Running Edge Cases Tests...\n");

	/* 1. Zero size allocations */
	assert(os_malloc(0) == NULL);
	assert(os_calloc(0, 100) == NULL);
	assert(os_calloc(100, 0) == NULL);
	assert(os_calloc(0, 0) == NULL);
	assert(os_realloc(NULL, 0) == NULL);

	/* 2. Freeing NULL should be safe */
	os_free(NULL);

	/* 3. Realloc with NULL ptr acts like malloc */
	void *p1 = os_realloc(NULL, 128);
	assert(p1 != NULL);
	assert(((uintptr_t)p1 % 8) == 0);

	/* 4. Realloc with size 0 acts like free */
	void *p1_freed = os_realloc(p1, 0);
	assert(p1_freed == NULL);

	/* 5. Realloc on a free block must return NULL */
	void *p2 = os_malloc(256);
	assert(p2 != NULL);
	os_free(p2);
	assert(os_realloc(p2, 512) == NULL);

	/* 6. Overflow / Extreme size requests */
	assert(os_malloc((size_t)-1) == NULL);
	assert(os_malloc((size_t)-1 - 10) == NULL);
	assert(os_calloc((size_t)-1 / 2, 4) == NULL);
	assert(os_calloc(SIZE_MAX, SIZE_MAX) == NULL);

	void *p3 = os_malloc(64);
	assert(p3 != NULL);
	assert(os_realloc(p3, (size_t)-1) == NULL);
	os_free(p3);

	TEST_PASS("Edge cases & safety checks");
}

static void test_alignment(void)
{
	printf("Running Memory Alignment Tests...\n");

	size_t test_sizes[] = {
		1, 2, 3, 4, 7, 8, 9, 15, 16, 17, 31, 32, 33, 63, 64,
		127, 128, 255, 256, 511, 512, 1023, 1024,
		4095, 4096, 4097, 8192, 65536, 131071, 131072, 131073, 200000
	};
	size_t num_sizes = sizeof(test_sizes) / sizeof(test_sizes[0]);

	for (size_t i = 0; i < num_sizes; i++) {
		void *m = os_malloc(test_sizes[i]);
		assert(m != NULL);
		assert(((uintptr_t)m % 8) == 0);

		void *c = os_calloc(1, test_sizes[i]);
		assert(c != NULL);
		assert(((uintptr_t)c % 8) == 0);

		void *r = os_realloc(m, test_sizes[i] + 7);
		assert(r != NULL);
		assert(((uintptr_t)r % 8) == 0);

		os_free(c);
		os_free(r);
	}

	TEST_PASS("8-byte alignment on all sizes (heap and mmap)");
}

static void test_calloc_zeroing(void)
{
	printf("Running Calloc Zero-Initialization Tests...\n");

	/* Taint memory on heap first */
	void *t = os_malloc(3000);
	assert(t != NULL);
	memset(t, 0xEE, 3000);
	os_free(t);

	/* Calloc on previously tainted heap space */
	size_t sz1 = 2000;
	unsigned char *c1 = (unsigned char *)os_calloc(1, sz1);
	assert(c1 != NULL);
	for (size_t i = 0; i < sz1; i++)
		assert(c1[i] == 0);
	os_free(c1);

	/* Calloc with mmap size (> 4096) */
	size_t nmemb = 1000;
	size_t elem_size = 8;
	unsigned char *c2 = (unsigned char *)os_calloc(nmemb, elem_size);
	assert(c2 != NULL);
	for (size_t i = 0; i < nmemb * elem_size; i++)
		assert(c2[i] == 0);
	os_free(c2);

	TEST_PASS("Calloc zeroes out heap and mmapped memory");
}

static void test_realloc_transitions(void)
{
	printf("Running Realloc Transitions & Data Preservation Tests...\n");

	/* 1. Heap to Heap (expanding) */
	char *buf = os_malloc(100);
	assert(buf != NULL);
	for (int i = 0; i < 100; i++)
		buf[i] = (char)(i & 0xFF);

	buf = os_realloc(buf, 500);
	assert(buf != NULL);
	for (int i = 0; i < 100; i++)
		assert(buf[i] == (char)(i & 0xFF));

	/* 2. Heap to Heap (shrinking) */
	buf = os_realloc(buf, 50);
	assert(buf != NULL);
	for (int i = 0; i < 50; i++)
		assert(buf[i] == (char)(i & 0xFF));

	/* 3. Heap to MMAP (> 128KB) */
	buf = os_realloc(buf, 200 * 1024);
	assert(buf != NULL);
	for (int i = 0; i < 50; i++)
		assert(buf[i] == (char)(i & 0xFF));

	/* Fill mmap region with fresh pattern */
	for (int i = 0; i < 10000; i++)
		buf[i] = (char)((i * 7) & 0xFF);

	/* 4. MMAP to MMAP (larger mmap) */
	buf = os_realloc(buf, 400 * 1024);
	assert(buf != NULL);
	for (int i = 0; i < 10000; i++)
		assert(buf[i] == (char)((i * 7) & 0xFF));

	/* 5. MMAP to Heap (< 128KB) */
	buf = os_realloc(buf, 1024);
	assert(buf != NULL);
	for (int i = 0; i < 1024; i++)
		assert(buf[i] == (char)((i * 7) & 0xFF));

	os_free(buf);

	TEST_PASS("Realloc transitions (heap<->heap, heap<->mmap, mmap<->mmap)");
}

static void test_coalescing_and_best_fit(void)
{
	printf("Running Coalescing and Best-Fit Tests...\n");

	/* Allocate contiguous blocks */
	void *b1 = os_malloc(100);
	void *b2 = os_malloc(200);
	void *b3 = os_malloc(300);
	void *b4 = os_malloc(400);
	void *b5 = os_malloc(500);

	/* Free alternating blocks */
	os_free(b2);
	os_free(b4);

	/* Allocate size that fits best in b2 (200 bytes) */
	void *nb1 = os_malloc(180);
	/* Best fit should reuse b2 */
	assert(nb1 == b2);

	/* Now free b1, so b1 (100) and remainder of b2 coalesce */
	os_free(b1);
	os_free(nb1);

	/* Allocate a combined size */
	void *nb2 = os_malloc(280);
	assert(nb2 == b1);

	/* Cleanup */
	os_free(nb2);
	os_free(b3);
	os_free(b5);

	TEST_PASS("Coalesce and Best-Fit block reuse");
}

static void test_stress_randomized(void)
{
	printf("Running Stress & Random Operations Test (1000 iterations)...\n");

	#define NUM_SLOTS 64
	void *slots[NUM_SLOTS] = {0};
	size_t slot_sizes[NUM_SLOTS] = {0};
	uint8_t slot_checksum[NUM_SLOTS] = {0};

	srand(42);

	for (int iter = 0; iter < 1000; iter++) {
		int slot = rand() % NUM_SLOTS;
		int action = rand() % 4; /* 0: malloc, 1: calloc, 2: realloc, 3: free */

		if (action == 3 || (slots[slot] != NULL && action == 0)) {
			/* Free */
			if (slots[slot] != NULL) {
				/* Verify checksum before freeing */
				uint8_t sum = 0;
				uint8_t *p = (uint8_t *)slots[slot];
				for (size_t i = 0; i < slot_sizes[slot]; i++)
					sum ^= p[i];
				assert(sum == slot_checksum[slot]);

				os_free(slots[slot]);
				slots[slot] = NULL;
				slot_sizes[slot] = 0;
				slot_checksum[slot] = 0;
			}
		} else if (action == 0 || slots[slot] == NULL) {
			/* Malloc */
			size_t sz = 1 + (rand() % 4096);
			if (rand() % 20 == 0)
				sz = 130000 + (rand() % 65536); /* occasional mmap */

			if (slots[slot] != NULL)
				os_free(slots[slot]);

			slots[slot] = os_malloc(sz);
			assert(slots[slot] != NULL);
			assert(((uintptr_t)slots[slot] % 8) == 0);
			slot_sizes[slot] = sz;

			uint8_t *p = (uint8_t *)slots[slot];
			uint8_t sum = 0;
			for (size_t i = 0; i < sz; i++) {
				p[i] = (uint8_t)((iter + i) & 0xFF);
				sum ^= p[i];
			}
			slot_checksum[slot] = sum;
		} else if (action == 1) {
			/* Calloc */
			size_t nmemb = 1 + (rand() % 64);
			size_t esz = 1 + (rand() % 64);
			size_t total = nmemb * esz;

			if (slots[slot] != NULL)
				os_free(slots[slot]);

			slots[slot] = os_calloc(nmemb, esz);
			assert(slots[slot] != NULL);
			assert(((uintptr_t)slots[slot] % 8) == 0);
			slot_sizes[slot] = total;

			uint8_t *p = (uint8_t *)slots[slot];
			for (size_t i = 0; i < total; i++)
				assert(p[i] == 0);

			uint8_t sum = 0;
			for (size_t i = 0; i < total; i++) {
				p[i] = (uint8_t)((iter * 3 + i) & 0xFF);
				sum ^= p[i];
			}
			slot_checksum[slot] = sum;
		} else {
			/* Realloc */
			size_t new_sz = 1 + (rand() % 8192);
			if (rand() % 20 == 0)
				new_sz = 130000 + (rand() % 65536);

			/* Verify old prefix before realloc */
			size_t old_sz = slot_sizes[slot];
			uint8_t *old_p = (uint8_t *)slots[slot];
			uint8_t old_sum = 0;
			for (size_t i = 0; i < old_sz; i++)
				old_sum ^= old_p[i];
			assert(old_sum == slot_checksum[slot]);

			void *new_p = os_realloc(slots[slot], new_sz);
			assert(new_p != NULL);
			assert(((uintptr_t)new_p % 8) == 0);

			slots[slot] = new_p;
			slot_sizes[slot] = new_sz;

			/* Recalculate checksum */
			uint8_t *p = (uint8_t *)slots[slot];
			uint8_t sum = 0;
			for (size_t i = 0; i < new_sz; i++) {
				if (i >= old_sz)
					p[i] = (uint8_t)((iter + i) & 0xFF);
				sum ^= p[i];
			}
			slot_checksum[slot] = sum;
		}
	}

	/* Final cleanup & verification */
	for (int i = 0; i < NUM_SLOTS; i++) {
		if (slots[i] != NULL) {
			uint8_t sum = 0;
			uint8_t *p = (uint8_t *)slots[i];
			for (size_t j = 0; j < slot_sizes[i]; j++)
				sum ^= p[j];
			assert(sum == slot_checksum[i]);
			os_free(slots[i]);
		}
	}

	TEST_PASS("1000 randomized operations stress test");
}

int main(void)
{
	printf("========================================\n");
	printf("Starting Custom Comprehensive Test Suite\n");
	printf("========================================\n\n");

	test_edge_cases();
	test_alignment();
	test_calloc_zeroing();
	test_realloc_transitions();
	test_coalescing_and_best_fit();
	test_stress_randomized();

	printf("\n========================================\n");
	printf("ALL CUSTOM TESTS PASSED PERFECTLY (6/6)!\n");
	printf("========================================\n");
	return 0;
}
