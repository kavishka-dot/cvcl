/**
 * @file 08_custom_allocator.c
 * @brief Tutorial 08: Pluggable allocator -- arena, pool, zero stdlib heap
 *
 * Concepts covered:
 *   - cvcl_allocator_t interface
 *   - Arena allocator (bump pointer, O(1) alloc, no individual frees)
 *   - Counting allocator (track how much memory CVCL uses)
 *   - Why this matters for embedded / RTOS targets
 *   - Mixing allocators (different images from different pools)
 *
 * Run:
 *   ./08_custom_allocator
 */

#include <cvcl/cvcl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* -------------------------------------------------------------------------
 * Arena allocator -- bump pointer, reset all at once
 *
 * On an RTOS or bare-metal target you might use a statically declared
 * buffer here instead of malloc. This completely avoids the heap.
 * ---------------------------------------------------------------------- */
typedef struct {
    cvcl_u8   *base;
    cvcl_size  pos;
    cvcl_size  cap;
    cvcl_size  peak;
} arena_t;

static void *arena_alloc(cvcl_size n, void *ctx) {
    arena_t *a = (arena_t *)ctx;
    /* Align to 16 bytes */
    cvcl_size aligned = (n + 15) & ~(cvcl_size)15;
    if (a->pos + aligned > a->cap) return NULL;
    void *p = a->base + a->pos;
    a->pos  += aligned;
    if (a->pos > a->peak) a->peak = a->pos;
    return p;
}

static void arena_free(void *ptr, void *ctx) {
    /* Arena doesn't free individual allocations -- reset frees everything */
    (void)ptr; (void)ctx;
}

static void arena_reset(arena_t *a) { a->pos = 0; }

/* -------------------------------------------------------------------------
 * Counting allocator -- wraps malloc, tracks bytes in flight
 * ---------------------------------------------------------------------- */
typedef struct {
    cvcl_size bytes_allocated;
    cvcl_size alloc_count;
    cvcl_size free_count;
} counter_t;

static void *counting_alloc(cvcl_size n, void *ctx) {
    counter_t *c = (counter_t *)ctx;
    /* Store size just before the returned pointer */
    cvcl_u8 *raw = (cvcl_u8 *)malloc(n + sizeof(cvcl_size));
    if (!raw) return NULL;
    memcpy(raw, &n, sizeof(cvcl_size));
    c->bytes_allocated += n;
    c->alloc_count++;
    return raw + sizeof(cvcl_size);
}

static void counting_free(void *ptr, void *ctx) {
    if (!ptr) return;
    counter_t *c = (counter_t *)ctx;
    cvcl_u8 *raw = (cvcl_u8 *)ptr - sizeof(cvcl_size);
    cvcl_size n;
    memcpy(&n, raw, sizeof(cvcl_size));
    c->bytes_allocated -= n;
    c->free_count++;
    free(raw);
}

/* -------------------------------------------------------------------------
 * Main
 * ---------------------------------------------------------------------- */
int main(void) {
    printf("=== Tutorial 08: Custom Allocator ===\n\n");

    /* ------------------------------------------------------------------
     * Part A: Default allocator (malloc/free)
     *
     * Pass NULL wherever an allocator is expected to use the default.
     * The default allocator wraps malloc with 16-byte alignment.
     * ------------------------------------------------------------------ */
    printf("Part A: Default allocator (NULL = malloc)\n");
    {
        cvcl_image_t img;
        cvcl_image_create(&img, 64, 64, 3, CVCL_DEPTH_U8, NULL);
        printf("  Created 64x64 RGB -- buffer at %p\n", (void *)img.data);
        cvcl_image_free(&img, NULL);  /* must match: NULL allocator */
        printf("  Freed.\n\n");
    }

    /* ------------------------------------------------------------------
     * Part B: Counting allocator -- see exactly what CVCL allocates
     * ------------------------------------------------------------------ */
    printf("Part B: Counting allocator\n");
    {
        counter_t counter = {0, 0, 0};
        cvcl_allocator_t counting = { counting_alloc, counting_free, &counter };

        cvcl_image_t src, dst;
        cvcl_image_create(&src, 256, 256, 3, CVCL_DEPTH_U8, &counting);
        cvcl_image_create(&dst, 256, 256, 3, CVCL_DEPTH_U8, &counting);

        printf("  After create x2: %zu allocs, %zu bytes in flight\n",
               counter.alloc_count, counter.bytes_allocated);

        /* Gaussian blur internally allocates a temporary image */
        cvcl_blur_gaussian(&dst, &src, 9, 2.f, CVCL_BORDER_REPLICATE);

        /* Note: blur uses the DEFAULT allocator for its temp buffer
         * (NULL passed internally). Only images you create use your allocator. */
        printf("  After gaussian:  %zu allocs, %zu bytes in flight\n",
               counter.alloc_count, counter.bytes_allocated);

        cvcl_image_free(&src, &counting);
        cvcl_image_free(&dst, &counting);

        printf("  After free x2:   %zu allocs, %zu frees, %zu bytes in flight\n",
               counter.alloc_count, counter.free_count, counter.bytes_allocated);
        printf("  Leak: %s\n\n",
               counter.bytes_allocated == 0 ? "none (clean)" : "DETECTED");
    }

    /* ------------------------------------------------------------------
     * Part C: Arena allocator -- no individual frees, reset all at once
     *
     * Perfect for processing pipelines where you create many images,
     * process them, then discard all results together.
     *
     * On embedded targets, replace the malloc/free with a static array:
     *   static uint8_t arena_buf[1 << 20];
     * ------------------------------------------------------------------ */
    printf("Part C: Arena allocator (bump pointer)\n");
    {
        cvcl_size arena_size = 4 * 1024 * 1024;  /* 4 MB */
        cvcl_u8  *arena_buf  = (cvcl_u8 *)malloc(arena_size);

        arena_t arena = { arena_buf, 0, arena_size, 0 };
        cvcl_allocator_t ar = { arena_alloc, arena_free, &arena };

        /* Create multiple images from the arena */
        cvcl_image_t gray, edges;
        cvcl_image_create(&gray,  512, 512, 1, CVCL_DEPTH_U8, &ar);
        cvcl_image_create(&edges, 512, 512, 1, CVCL_DEPTH_U8, &ar);

        printf("  Arena pos after 2x 512x512 gray: %zu / %zu bytes\n",
               arena.pos, arena_size);

        /* Fill gray with a gradient */
        for (cvcl_i32 y = 0; y < gray.height; y++) {
            cvcl_u8 *row = cvcl_image_row(&gray, y);
            for (cvcl_i32 x = 0; x < gray.width; x++)
                row[x] = (cvcl_u8)((x + y) / 4);
        }

        cvcl_canny(&edges, &gray, 20.f, 60.f, &ar);

        printf("  Arena pos after Canny:           %zu / %zu bytes\n",
               arena.pos, arena_size);
        printf("  Peak usage:                      %zu bytes (%.1f MB)\n",
               arena.peak, (double)arena.peak / (1024*1024));

        /* "Free" everything at once -- O(1) */
        arena_reset(&arena);
        printf("  Arena reset -- all memory reclaimed in O(1).\n");
        printf("  No individual free calls needed.\n\n");

        free(arena_buf);
    }

    /* ------------------------------------------------------------------
     * Part D: Mixing allocators
     *
     * Different images can use different allocators.
     * You MUST use the same allocator for create and free.
     * ------------------------------------------------------------------ */
    printf("Part D: Mixing allocators\n");
    {
        counter_t counter = {0, 0, 0};
        cvcl_allocator_t counting = { counting_alloc, counting_free, &counter };

        cvcl_image_t from_heap;   /* default malloc */
        cvcl_image_t from_pool;   /* counting allocator */

        cvcl_image_create(&from_heap, 64, 64, 1, CVCL_DEPTH_U8, NULL);
        cvcl_image_create(&from_pool, 64, 64, 1, CVCL_DEPTH_U8, &counting);

        printf("  heap image:    %p  (malloc)\n",   (void *)from_heap.data);
        printf("  counted image: %p  (counting)\n", (void *)from_pool.data);
        printf("  counter bytes: %zu\n", counter.bytes_allocated);

        /* CORRECT: free with matching allocator */
        cvcl_image_free(&from_heap, NULL);
        cvcl_image_free(&from_pool, &counting);
        printf("  Both freed correctly.\n\n");
    }

    printf("Key takeaways:\n");
    printf("  1. Pass NULL for the default malloc/free allocator.\n");
    printf("  2. Always free with the SAME allocator used to create.\n");
    printf("  3. Arena allocators are ideal for batch pipelines.\n");
    printf("  4. On bare-metal, use a static buffer -- zero heap dependency.\n");
    return 0;
}
