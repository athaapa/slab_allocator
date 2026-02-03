#define _POSIX_C_SOURCE 199309L
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "slab.h"

static uint32_t seed = 0xDEADBEEF;
static inline uint32_t fast_rand() {
    uint32_t x = seed;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return seed = x;
}

#define NUM_ORDERS 1000000
#define CHURN_BATCH 100
#define CHURN_ITER 10000
#define SWISS_WARMUP 10000
#define SWISS_ITERATIONS 1000000
#define SWISS_NUM_ACTIVE 16384
#define SWISS_MASK (SWISS_NUM_ACTIVE - 1)

int main() {
    struct timespec start, end;
    long long malloc_time, slab_time, malloc_dealloc_time, slab_dealloc_time, malloc_churn_time, slab_churn_time, swiss_time, swiss_malloc_time;
    srand(time(NULL));
    seed = time(NULL);

    // Benchmark malloc
    clock_gettime(CLOCK_MONOTONIC, &start);
    LimitOrder **malloc_orders = malloc(NUM_ORDERS * sizeof(LimitOrder*));
    for (int i = 0; i < NUM_ORDERS; i++) {
        malloc_orders[i] = malloc(sizeof(LimitOrder));
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    malloc_time = (end.tv_sec - start.tv_sec) * 1000000000LL + (end.tv_nsec - start.tv_nsec);

    // Dealloc malloc
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int i = 0; i < NUM_ORDERS; i++) {
        free(malloc_orders[i]);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    free(malloc_orders);
    malloc_dealloc_time = (end.tv_sec - start.tv_sec) * 1000000000LL + (end.tv_nsec - start.tv_nsec);

    // Initialize slab allocator
    size_t mem_size = 64 * NUM_ORDERS;  // 64 bytes per LimitOrder
    void *mem_block = malloc(mem_size);
    slab_init(mem_block, mem_size);

    // Benchmark slab_alloc
    clock_gettime(CLOCK_MONOTONIC, &start);
    LimitOrder **slab_orders = malloc(NUM_ORDERS * sizeof(LimitOrder*));
    for (int i = 0; i < NUM_ORDERS; i++) {
        slab_orders[i] = slab_alloc();
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    slab_time = (end.tv_sec - start.tv_sec) * 1000000000LL + (end.tv_nsec - start.tv_nsec);

    // Dealloc slab
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int i = NUM_ORDERS - 1; i >= 0; i--) {
        slab_free(slab_orders[i]);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    slab_dealloc_time = (end.tv_sec - start.tv_sec) * 1000000000LL + (end.tv_nsec - start.tv_nsec);

    free(slab_orders);
    free(mem_block);

    // Churn benchmark malloc
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int iter = 0; iter < CHURN_ITER; iter++) {
        LimitOrder *batch[CHURN_BATCH];
        for (int i = 0; i < CHURN_BATCH; i++) {
            batch[i] = malloc(sizeof(LimitOrder));
        }
        for (int i = 0; i < CHURN_BATCH; i++) {
            free(batch[i]);
        }
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    malloc_churn_time = (end.tv_sec - start.tv_sec) * 1000000000LL + (end.tv_nsec - start.tv_nsec);

    // Churn benchmark slab
    size_t churn_mem_size = 64 * CHURN_BATCH;
    void *churn_mem_block = malloc(churn_mem_size);
    slab_init(churn_mem_block, churn_mem_size);
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int iter = 0; iter < CHURN_ITER; iter++) {
        LimitOrder *batch[CHURN_BATCH];
        for (int i = 0; i < CHURN_BATCH; i++) {
            batch[i] = slab_alloc();
        }
        for (int i = CHURN_BATCH - 1; i >= 0; i--) {
            slab_free(batch[i]);
        }
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    slab_churn_time = (end.tv_sec - start.tv_sec) * 1000000000LL + (end.tv_nsec - start.tv_nsec);
    free(churn_mem_block);

    // Malloc Swiss Cheese Test
    LimitOrder *active_orders[SWISS_NUM_ACTIVE];

    // 1. Warm Up: Fill 50% of the book
    memset(active_orders, 0, sizeof(active_orders));
    for (int i = 0; i < SWISS_NUM_ACTIVE / 2; i++) {
        active_orders[i] = malloc(sizeof(LimitOrder));
    }

    // 2. The Loop: 1,000,000 iterations
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int i = 0; i < SWISS_ITERATIONS; i++) {
        // EXACT SAME LOGIC AS SLAB LOOP
        int slot = fast_rand() & SWISS_MASK;

        if (active_orders[slot]) {
            // Slot is full -> Free it
            free(active_orders[slot]);
            active_orders[slot] = NULL;
        } else {
            // Slot is empty -> Fill it
            active_orders[slot] = malloc(sizeof(LimitOrder));
        }
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    swiss_malloc_time = (end.tv_sec - start.tv_sec) * 1000000000LL + (end.tv_nsec - start.tv_nsec);

    // 3. Cleanup
    for (int i = 0; i < SWISS_NUM_ACTIVE; i++) {
        if (active_orders[i]) free(active_orders[i]);
    }

    // Slab Swiss Cheese Test
    size_t swiss_mem_size = 64 * SWISS_NUM_ACTIVE;
    void *swiss_mem_block = malloc(swiss_mem_size);
    slab_init(swiss_mem_block, swiss_mem_size);

    memset(active_orders, 0, sizeof(active_orders));

    // Warm up: Allocate 10,000 orders
    for (int i = 0; i < SWISS_WARMUP; i++) {
        active_orders[i] = slab_alloc();
    }

    // The Loop: 1,000,000 iterations
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int i = 0; i < SWISS_ITERATIONS; i++) {
        // 1. Bitwise Mask (1 cycle) instead of Modulo (20 cycles)
        int slot = fast_rand() & SWISS_MASK;

        // 2. O(1) Toggle - No Retries
        if (active_orders[slot]) {
            // Slot is full -> Free it
            slab_free(active_orders[slot]);
            active_orders[slot] = NULL;
        } else {
            // Slot is empty -> Fill it
            active_orders[slot] = slab_alloc();
        }
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    swiss_time = (end.tv_sec - start.tv_sec) * 1000000000LL + (end.tv_nsec - start.tv_nsec);

    // Clean up remaining allocations
    for (int i = 0; i < SWISS_NUM_ACTIVE; i++) {
        if (active_orders[i]) {
            slab_free(active_orders[i]);
        }
    }
    free(swiss_mem_block);

    // Calculate Per-Op Latencies for cleaner printing
    double malloc_alloc_op  = (double)malloc_time / NUM_ORDERS;
    double slab_alloc_op    = (double)slab_time / NUM_ORDERS;

    double malloc_free_op   = (double)malloc_dealloc_time / NUM_ORDERS;
    double slab_free_op     = (double)slab_dealloc_time / NUM_ORDERS;

    double malloc_churn_op  = (double)malloc_churn_time / (CHURN_ITER * CHURN_BATCH * 2);
    double slab_churn_op    = (double)slab_churn_time / (CHURN_ITER * CHURN_BATCH * 2);
    double swiss_malloc_op  = (double)swiss_malloc_time / SWISS_ITERATIONS;
    double swiss_op         = (double)swiss_time / SWISS_ITERATIONS;

    // Print The Executive Summary
    printf("\n");
    printf("================================================================================\n");
    printf("FINAL BENCHMARK REPORT: 64-BYTE SLAB ALLOCATOR\n");
    printf("Platform: x86-64 (attu) | Resolution: Nanoseconds\n");
    printf("================================================================================\n");
    printf("%-24s | %14s | %14s | %14s\n", "Operation", "Malloc (ns/op)", "Slab (ns/op)", "Speedup");
    printf("-------------------------|----------------|----------------|----------------\n");

    printf("%-24s | %14.2f | %14.2f | %13.2fx\n",
           "Allocation (Linear)", malloc_alloc_op, slab_alloc_op, malloc_alloc_op / slab_alloc_op);

    printf("%-24s | %14.2f | %14.2f | %13.2fx\n",
           "Deallocation (Linear)", malloc_free_op, slab_free_op, malloc_free_op / slab_free_op);

    printf("%-24s | %14.2f | %14.2f | %13.2fx\n",
           "Hot Churn (100 batch)", malloc_churn_op, slab_churn_op, malloc_churn_op / slab_churn_op);

    printf("%-24s | %14.2f | %14.2f | %13.2fx\n",
           "Swiss Cheese Churn", swiss_malloc_op, swiss_op, swiss_malloc_op / swiss_op);

    printf("================================================================================\n");

    return 0;
}
