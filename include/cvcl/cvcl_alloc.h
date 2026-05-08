/**
 * @file cvcl_alloc.h
 * @brief Pluggable allocator interface
 *
 * All heap interaction in CVCL goes through a cvcl_allocator_t.
 * The default allocator wraps malloc/free. Replace it to integrate
 * with RTOS pools, arena allocators, or custom slab allocators.
 *
 * Example -- scratch arena allocator:
 * @code
 *   static uint8_t arena[1 << 20];
 *   static size_t  arena_pos = 0;
 *
 *   void *arena_alloc(size_t n, void *ctx) {
 *       (void)ctx;
 *       if (arena_pos + n > sizeof(arena)) return NULL;
 *       void *p = &arena[arena_pos];
 *       arena_pos = CVCL_ALIGN_UP(arena_pos + n, 16);
 *       return p;
 *   }
 *   void arena_free(void *p, void *ctx) { (void)p; (void)ctx; }
 *
 *   cvcl_allocator_t my_alloc = { arena_alloc, arena_free, NULL };
 * @endcode
 */

#ifndef CVCL_ALLOC_H
#define CVCL_ALLOC_H

#include "cvcl_types.h"

/* -------------------------------------------------------------------------
 * Allocator function signatures
 * ---------------------------------------------------------------------- */

/**
 * @brief Allocation function.
 * @param size  Bytes requested. Always > 0.
 * @param ctx   User-provided context pointer.
 * @return      Pointer to allocated memory, or NULL on failure.
 *              Returned memory MUST be at least 16-byte aligned.
 */
typedef void *(*cvcl_alloc_fn)(cvcl_size size, void *ctx);

/**
 * @brief Free function.
 * @param ptr   Pointer previously returned by cvcl_alloc_fn. May be NULL.
 * @param ctx   User-provided context pointer.
 */
typedef void  (*cvcl_free_fn)(void *ptr, void *ctx);

/* -------------------------------------------------------------------------
 * Allocator descriptor
 * ---------------------------------------------------------------------- */
typedef struct {
    cvcl_alloc_fn alloc;  /**< Required. Must not be NULL.                */
    cvcl_free_fn  free;   /**< Required. Must not be NULL.                */
    void         *ctx;    /**< Passed as-is to alloc/free. May be NULL.   */
} cvcl_allocator_t;

/* -------------------------------------------------------------------------
 * Default allocator (malloc / free)
 * ---------------------------------------------------------------------- */

/** Returns a pointer to the global default allocator (malloc/free). */
const cvcl_allocator_t *cvcl_default_allocator(void);

/* -------------------------------------------------------------------------
 * Convenience wrappers
 * ---------------------------------------------------------------------- */

CVCL_INLINE void *cvcl_alloc(const cvcl_allocator_t *a, cvcl_size size) {
    CVCL_ASSERT(a && a->alloc);
    return a->alloc(size, a->ctx);
}

CVCL_INLINE void cvcl_free(const cvcl_allocator_t *a, void *ptr) {
    CVCL_ASSERT(a && a->free);
    a->free(ptr, a->ctx);
}

#endif /* CVCL_ALLOC_H */
