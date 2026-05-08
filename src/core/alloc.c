/**
 * @file alloc.c
 * @brief Default allocator implementation (malloc/free wrapper)
 */

#include <cvcl/cvcl_alloc.h>
#include <stdlib.h>

/* -------------------------------------------------------------------------
 * Default allocator implementation
 * ---------------------------------------------------------------------- */

static void *default_alloc(cvcl_size size, void *ctx) {
    (void)ctx;
    /* malloc does not guarantee 16-byte alignment on all platforms.
     * Use aligned_alloc / _aligned_malloc where available. */
#if defined(_MSC_VER)
    return _aligned_malloc(size, 16);
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
    /* C11 aligned_alloc requires size to be a multiple of alignment */
    cvcl_size aligned_size = CVCL_ALIGN_UP(size, 16);
    return aligned_alloc(16, aligned_size);
#else
    /* Fallback: over-allocate and manually align */
    void **guard;
    cvcl_u8 *raw = (cvcl_u8 *)malloc(size + 16 + sizeof(void *));
    if (!raw) return NULL;
    cvcl_u8 *aligned = (cvcl_u8 *)(((uintptr_t)(raw + sizeof(void *)) + 15) & ~(uintptr_t)15);
    guard = (void **)(aligned - sizeof(void *));
    *guard = raw;
    return aligned;
#endif
}

static void default_free(void *ptr, void *ctx) {
    (void)ctx;
    if (!ptr) return;
#if defined(_MSC_VER)
    _aligned_free(ptr);
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
    free(ptr);
#else
    /* Recover original pointer stored just before aligned block */
    void **guard = (void **)((cvcl_u8 *)ptr - sizeof(void *));
    free(*guard);
#endif
}

static const cvcl_allocator_t g_default_allocator = {
    default_alloc,
    default_free,
    NULL
};

const cvcl_allocator_t *cvcl_default_allocator(void) {
    return &g_default_allocator;
}
