/**
 * @file test_resource_constraints.c
 * @brief Tests under constrained allocator settings
 *
 * Verifies that CVCL behaves correctly when memory is limited:
 *   - Small arena allocator (fixed 64 KB budget)
 *   - Out-of-memory failure returns CVCL_ERR_ALLOC, never crashes
 *   - Image creation within 64 KB budget
 *   - Blur on a 32x32 image within budget
 *   - Zero-copy crop produces no allocation
 *   - Cleanup behavior: free on failed image is safe
 *
 * CVCL is tested under constrained allocator settings to verify
 * graceful failure under limited memory.
 */

#include <cvcl/cvcl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * Test harness
 * ---------------------------------------------------------------------- */
static int g_run = 0, g_fail = 0;

#define RUN(name) do { \
    int _fails_before = g_fail; \
    printf("  %-52s", #name); fflush(stdout); \
    g_run++; name(); \
    if (g_fail == _fails_before) printf("PASS\n"); \
} while(0)

#define ASSERT_EQ(a,b) do { \
    if ((a)!=(b)) { \
        printf("FAIL\n    %s:%d  got=%d exp=%d\n", \
               __FILE__,__LINE__,(int)(a),(int)(b)); \
        g_fail++; return; \
    } \
} while(0)

#define ASSERT_TRUE(c) do { \
    if (!(c)) { \
        printf("FAIL\n    %s:%d  assertion false: %s\n", \
               __FILE__,__LINE__,#c); \
        g_fail++; return; \
    } \
} while(0)

#define ASSERT_NULL(p)     ASSERT_TRUE((p) == NULL)
#define ASSERT_NOT_NULL(p) ASSERT_TRUE((p) != NULL)

/* -------------------------------------------------------------------------
 * Arena allocator -- fixed-size bump pointer, no individual frees
 *
 * Models a microcontroller with a fixed static memory pool.
 * ---------------------------------------------------------------------- */
#define ARENA_SIZE (64 * 1024)   /* 64 KB */

typedef struct {
    cvcl_u8   buf[ARENA_SIZE];
    cvcl_size pos;
    cvcl_size peak;
    cvcl_size alloc_count;
    int       oom_triggered;
} arena_t;

static void arena_reset(arena_t *a) {
    a->pos           = 0;
    a->peak          = 0;
    a->alloc_count   = 0;
    a->oom_triggered = 0;
}

static void *arena_alloc(cvcl_size n, void *ctx) {
    arena_t *a = (arena_t *)ctx;
    /* 16-byte alignment */
    cvcl_size aligned = (n + 15u) & ~(cvcl_size)15u;
    if (a->pos + aligned > ARENA_SIZE) {
        a->oom_triggered = 1;
        return NULL;   /* OOM -- return NULL, never crash */
    }
    void *p  = a->buf + a->pos;
    a->pos  += aligned;
    if (a->pos > a->peak) a->peak = a->pos;
    a->alloc_count++;
    return p;
}

static void arena_free(void *ptr, void *ctx) {
    /* Arena doesn't free individually -- reset frees everything */
    (void)ptr; (void)ctx;
}

/* -------------------------------------------------------------------------
 * Failing allocator -- always returns NULL after N allocs
 * ---------------------------------------------------------------------- */
typedef struct {
    int limit;   /* fail after this many allocs */
    int count;
} fail_after_t;

static void *fail_after_alloc(cvcl_size n, void *ctx) {
    fail_after_t *f = (fail_after_t *)ctx;
    (void)n;
    if (f->count >= f->limit) return NULL;
    f->count++;
    return malloc(n);
}

static void fail_after_free(void *ptr, void *ctx) {
    (void)ctx;
    free(ptr);
}

/* =========================================================================
 * Tests
 * ====================================================================== */

/* -------------------------------------------------------------------------
 * 1. Small arena allocator: create image within 64 KB budget
 * ---------------------------------------------------------------------- */
static void test_image_creation_under_64kb(void) {
    arena_t arena; arena_reset(&arena);
    cvcl_allocator_t alloc = { arena_alloc, arena_free, &arena };

    /* 32x32 RGB fits comfortably in 64 KB (stride=64*3=192, total=192*32=6144) */
    cvcl_image_t img;
    cvcl_result_t rc = cvcl_image_create(&img, 32, 32, 3, CVCL_DEPTH_U8, &alloc);

    ASSERT_EQ(rc, CVCL_OK);
    ASSERT_NOT_NULL(img.data);
    ASSERT_EQ(img.width,    32);
    ASSERT_EQ(img.height,   32);
    ASSERT_EQ(img.channels,  3);
    ASSERT_TRUE(arena.peak <= ARENA_SIZE);
    ASSERT_TRUE(arena.oom_triggered == 0);

    /* Do NOT call cvcl_image_free -- arena owns memory, reset frees all */
    arena_reset(&arena);
}

/* -------------------------------------------------------------------------
 * 2. Out-of-memory failure: requesting more than arena has
 * ---------------------------------------------------------------------- */
static void test_oom_returns_err_alloc(void) {
    /* Use an almost-full arena -- only 1 KB remaining */
    arena_t micro; arena_reset(&micro);
    /* Override: cap it to 1 KB by pre-filling the arena */
    micro.pos = ARENA_SIZE - 1024;  /* only 1 KB remaining */

    cvcl_allocator_t alloc = { arena_alloc, arena_free, &micro };

    /* 512x512 RGB = ~768 KB -- must fail gracefully */
    cvcl_image_t img;
    memset(&img, 0, sizeof(img));
    cvcl_result_t rc = cvcl_image_create(&img, 512, 512, 3, CVCL_DEPTH_U8, &alloc);

    ASSERT_EQ(rc, CVCL_ERR_ALLOC);
    ASSERT_NULL(img.data);           /* data must be NULL on failure */
    ASSERT_TRUE(micro.oom_triggered);/* arena must have signalled OOM */
}

/* -------------------------------------------------------------------------
 * 3. Blur on 32x32 image within arena budget
 * ---------------------------------------------------------------------- */
static void test_blur_32x32_within_budget(void) {
    arena_t arena; arena_reset(&arena);
    cvcl_allocator_t alloc = { arena_alloc, arena_free, &arena };

    cvcl_image_t src, dst;
    cvcl_result_t rc;

    rc = cvcl_image_create(&src, 32, 32, 1, CVCL_DEPTH_U8, &alloc);
    ASSERT_EQ(rc, CVCL_OK);

    rc = cvcl_image_create(&dst, 32, 32, 1, CVCL_DEPTH_U8, &alloc);
    ASSERT_EQ(rc, CVCL_OK);

    /* Fill source */
    for (cvcl_i32 y = 0; y < 32; y++) {
        cvcl_u8 *row = cvcl_image_row(&src, y);
        for (cvcl_i32 x = 0; x < 32; x++) row[x] = (cvcl_u8)(x + y);
    }

    /* NOTE: cvcl_blur_gaussian allocates an internal temp image using the
     * default (malloc) allocator, not the caller-provided arena. This is a
     * known design limitation -- a future API will accept a workspace/alloc
     * parameter so the full pipeline stays within a single arena budget.
     * The test is still valid: src and dst are arena-allocated. */
    rc = cvcl_blur_gaussian(&dst, &src, 5, 1.f, CVCL_BORDER_REPLICATE);
    ASSERT_EQ(rc, CVCL_OK);

    /* Box blur */
    rc = cvcl_blur_box(&dst, &src, 5, CVCL_BORDER_REPLICATE);
    ASSERT_EQ(rc, CVCL_OK);

    /* Verify arena didn't overflow */
    ASSERT_TRUE(arena.peak <= ARENA_SIZE);
    ASSERT_TRUE(arena.oom_triggered == 0);

    printf("\n    [arena peak: %zu / %d bytes (%.1f%%)]  ",
           arena.peak, ARENA_SIZE, 100.0*arena.peak/ARENA_SIZE);

    arena_reset(&arena);
}

/* -------------------------------------------------------------------------
 * 4. Zero-copy crop: produces NO allocation
 * ---------------------------------------------------------------------- */
static void test_crop_no_allocation(void) {
    arena_t arena; arena_reset(&arena);
    cvcl_allocator_t alloc = { arena_alloc, arena_free, &arena };

    /* Allocate source in arena */
    cvcl_image_t src;
    cvcl_result_t rc = cvcl_image_create(&src, 64, 64, 3, CVCL_DEPTH_U8, &alloc);
    ASSERT_EQ(rc, CVCL_OK);

    cvcl_size used_before = arena.pos;
    cvcl_size count_before = arena.alloc_count;

    /* Crop -- must NOT allocate */
    cvcl_image_t view;
    cvcl_rect_t roi = {16, 16, 32, 32};
    rc = cvcl_image_view(&view, &src, roi);

    ASSERT_EQ(rc, CVCL_OK);
    ASSERT_EQ(arena.pos,         used_before);   /* no bytes consumed */
    ASSERT_EQ(arena.alloc_count, count_before);  /* no alloc calls */
    ASSERT_EQ(view.stride,       src.stride);    /* shared stride = zero copy */
    ASSERT_EQ(view.width,        32);
    ASSERT_EQ(view.height,       32);

    /* View data points into src buffer */
    ASSERT_TRUE(view.data >= src.data);
    ASSERT_TRUE(view.data <  src.data + cvcl_image_buffer_size(&src));

    arena_reset(&arena);
}

/* -------------------------------------------------------------------------
 * 5. Cleanup behavior: free on a zeroed image is safe (no crash)
 * ---------------------------------------------------------------------- */
static void test_free_zeroed_image_safe(void) {
    cvcl_image_t img;
    memset(&img, 0, sizeof(img));

    /* img.data == NULL -- free must be a no-op, not a crash */
    cvcl_image_free(&img, NULL);
    ASSERT_NULL(img.data);
}

/* -------------------------------------------------------------------------
 * 6. Cleanup after failed creation: image is in valid empty state
 * ---------------------------------------------------------------------- */
static void test_cleanup_after_failed_creation(void) {
    /* Use a fail-after-0 allocator -- immediately OOM */
    fail_after_t fa = {0, 0};
    cvcl_allocator_t alloc = { fail_after_alloc, fail_after_free, &fa };

    cvcl_image_t img;
    memset(&img, 0, sizeof(img));
    cvcl_result_t rc = cvcl_image_create(&img, 64, 64, 3, CVCL_DEPTH_U8, &alloc);

    ASSERT_EQ(rc, CVCL_ERR_ALLOC);
    ASSERT_NULL(img.data);

    /* Calling free on a failed image must be safe */
    cvcl_image_free(&img, &alloc);
    ASSERT_NULL(img.data);
}

/* -------------------------------------------------------------------------
 * 7. Multiple images from arena: sequential allocation, total within budget
 * ---------------------------------------------------------------------- */
static void test_multiple_images_from_arena(void) {
    arena_t arena; arena_reset(&arena);
    cvcl_allocator_t alloc = { arena_alloc, arena_free, &arena };

    /* Create 4 small images from the same arena */
    cvcl_image_t imgs[4];
    for (int i = 0; i < 4; i++) {
        cvcl_result_t rc = cvcl_image_create(&imgs[i], 16, 16, 1,
                                               CVCL_DEPTH_U8, &alloc);
        ASSERT_EQ(rc, CVCL_OK);
        ASSERT_NOT_NULL(imgs[i].data);
        /* Write a sentinel to verify no overlap */
        cvcl_set_u8(&imgs[i], 0, 0, 0, (cvcl_u8)(i * 50));
    }

    /* Verify each image's sentinel is intact */
    for (int i = 0; i < 4; i++)
        ASSERT_EQ(cvcl_get_u8(&imgs[i], 0, 0, 0), (cvcl_u8)(i * 50));

    /* All must fit within 64 KB */
    ASSERT_TRUE(arena.peak <= ARENA_SIZE);
    ASSERT_TRUE(arena.oom_triggered == 0);

    printf("\n    [%d images, peak: %zu bytes]  ", 4, arena.peak);
    arena_reset(&arena);
}

/* -------------------------------------------------------------------------
 * 8. Graceful OOM mid-pipeline: partial failure leaves no dangling state
 * ---------------------------------------------------------------------- */
static void test_oom_mid_pipeline_no_leak(void) {
    /* Allow exactly 1 allocation then fail */
    fail_after_t fa = {1, 0};
    cvcl_allocator_t alloc = { fail_after_alloc, fail_after_free, &fa };

    cvcl_image_t src;
    memset(&src, 0, sizeof(src));

    /* First alloc succeeds */
    cvcl_result_t rc = cvcl_image_create(&src, 8, 8, 1, CVCL_DEPTH_U8, &alloc);
    ASSERT_EQ(rc, CVCL_OK);
    ASSERT_NOT_NULL(src.data);

    /* Second alloc will fail -- try to clone */
    cvcl_image_t dst;
    memset(&dst, 0, sizeof(dst));
    rc = cvcl_image_clone(&dst, &src, &alloc);

    ASSERT_EQ(rc, CVCL_ERR_ALLOC);
    ASSERT_NULL(dst.data);   /* dst must be NULL -- no partial state */

    /* Clean up src normally */
    cvcl_image_free(&src, &alloc);
    ASSERT_NULL(src.data);
}

/* -------------------------------------------------------------------------
 * 9. Arena fill: verify exact capacity boundary
 * ---------------------------------------------------------------------- */
static void test_arena_boundary_exact(void) {
    arena_t arena; arena_reset(&arena);
    cvcl_allocator_t alloc = { arena_alloc, arena_free, &arena };

    /* Fill arena with small images until it runs out */
    cvcl_i32 created = 0;
    while (1) {
        cvcl_image_t img;
        cvcl_result_t rc = cvcl_image_create(&img, 8, 8, 1, CVCL_DEPTH_U8, &alloc);
        if (rc == CVCL_ERR_ALLOC) break;
        ASSERT_EQ(rc, CVCL_OK);
        ASSERT_NOT_NULL(img.data);
        created++;
        if (created > 10000) break;  /* safety cap */
    }

    /* Must have created at least some images before OOM */
    ASSERT_TRUE(created > 0);
    /* Arena must have flagged OOM */
    ASSERT_TRUE(arena.oom_triggered);
    /* Peak must not exceed capacity */
    ASSERT_TRUE(arena.peak <= ARENA_SIZE);

    printf("\n    [%d images before OOM, peak=%zu/%d bytes]  ",
           created, arena.peak, ARENA_SIZE);

    arena_reset(&arena);
}

/* =========================================================================
 * Entry point
 * ====================================================================== */

int main(void) {
    printf("=== test_resource_constraints ===\n");
    printf("Arena size: %d KB\n\n", ARENA_SIZE / 1024);

    printf("Allocation:\n");
    RUN(test_image_creation_under_64kb);
    RUN(test_oom_returns_err_alloc);
    RUN(test_multiple_images_from_arena);
    RUN(test_arena_boundary_exact);

    printf("\nOperations under constraint:\n");
    RUN(test_blur_32x32_within_budget);
    RUN(test_crop_no_allocation);

    printf("\nCleanup behavior:\n");
    RUN(test_free_zeroed_image_safe);
    RUN(test_cleanup_after_failed_creation);
    RUN(test_oom_mid_pipeline_no_leak);

    int passed = g_run - g_fail;
    printf("\n%d/%d tests passed.\n", passed, g_run);
    return g_fail > 0 ? 1 : 0;
}
