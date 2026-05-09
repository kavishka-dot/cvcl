/**
 * @file cvcl_platform.h
 * @brief Platform abstraction layer -- freestanding / bare-metal support
 *
 * CVCL can run without the C standard library by defining CVCL_NO_STDLIB
 * and providing a cvcl_platform_t at startup via cvcl_platform_init().
 *
 * Without CVCL_NO_STDLIB (default): all functions resolve to their stdlib
 * equivalents and no initialization is needed.
 *
 * With CVCL_NO_STDLIB: every stdlib call goes through function pointers
 * set by cvcl_platform_init(). The host provides implementations suitable
 * for the target (FreeRTOS heap, static arena, CMSIS functions, etc.).
 *
 * Minimum required for basic operation:
 *   malloc, free, memcpy, memset
 *
 * Additional functions enable specific modules:
 *   sqrtf  -- Sobel magnitude visualization, Harris (can be approximated)
 *   expf   -- Gaussian blur kernel, bilateral filter
 *   fabsf  -- various filters
 *   qsort  -- Harris keypoint sorting (provide NULL to skip sorting)
 *
 * Modules that require float math and can be disabled:
 *   bilateral.c  -- expf (sigma weighting)
 *   blur.c       -- expf (Gaussian kernel build, one-time per call)
 *   harris.c     -- fabsf, qsort
 *   pyramid.c    -- expf (via blur)
 *
 * Modules that are fully freestanding (no float math, no stdlib except
 * memcpy/memset):
 *   image.c, alloc.c, error.c, pixel_hist.c, integral.c
 *   connected_components.c (needs malloc for union-find temp arrays)
 *   morph.c, median.c, convolve.c
 *   draw.c
 *   io_ppm.c, io_png_native.c (needs malloc for IDAT buffer)
 *   resize.c (nearest + bilinear; bicubic needs no float math)
 */

#ifndef CVCL_PLATFORM_H
#define CVCL_PLATFORM_H

#include "cvcl_types.h"
#include "cvcl_error.h"

/* -------------------------------------------------------------------------
 * Platform function pointer table
 * ---------------------------------------------------------------------- */

typedef struct {
    /* Memory -- required */
    void *(*malloc )(cvcl_size n);
    void  (*free   )(void *ptr);
    void *(*realloc)(void *ptr, cvcl_size n);

    /* Memory ops -- required */
    void *(*memcpy )(void *dst, const void *src, cvcl_size n);
    void *(*memset )(void *dst, int val, cvcl_size n);
    void *(*memmove)(void *dst, const void *src, cvcl_size n);

    /* Float math -- NULL disables ops that need them */
    float (*sqrtf)(float x);
    float (*expf )(float x);
    float (*fabsf)(float x);
    float (*floorf)(float x);

    /* Sorting -- NULL disables keypoint sorting in Harris */
    void  (*qsort )(void *base, cvcl_size n, cvcl_size sz,
                    int (*cmp)(const void*, const void*));
} cvcl_platform_t;

/* -------------------------------------------------------------------------
 * Init / query
 * ---------------------------------------------------------------------- */

/**
 * @brief Install a platform implementation.
 *
 * Must be called before any other CVCL function when CVCL_NO_STDLIB is set.
 * Safe to call multiple times (last call wins).
 * Thread-safe only if the caller ensures sequential init.
 *
 * @param p  Platform table. All required fields (malloc, free, memcpy,
 *           memset) must be non-NULL.
 * @return   CVCL_OK, or CVCL_ERR_ARG if a required field is NULL.
 */
cvcl_result_t cvcl_platform_init(const cvcl_platform_t *p);

/** @brief Return the active platform table (read-only). */
const cvcl_platform_t *cvcl_platform_get(void);

/* -------------------------------------------------------------------------
 * Internal dispatch macros
 *
 * These replace every stdlib call inside CVCL source files.
 * With CVCL_NO_STDLIB they go through the platform table.
 * Without it they resolve directly to stdlib symbols (zero overhead).
 * ---------------------------------------------------------------------- */

#ifdef CVCL_NO_STDLIB

#  define CVCL_MALLOC(n)          (cvcl_platform_get()->malloc(n))
#  define CVCL_FREE(p)            (cvcl_platform_get()->free(p))
#  define CVCL_REALLOC(p,n)       (cvcl_platform_get()->realloc ? cvcl_platform_get()->realloc((p),(n)) : NULL)
#  define CVCL_MEMCPY(d,s,n)      (cvcl_platform_get()->memcpy((d),(s),(n)))
#  define CVCL_MEMSET(d,v,n)      (cvcl_platform_get()->memset((d),(v),(n)))
#  define CVCL_MEMMOVE(d,s,n)     (cvcl_platform_get()->memmove((d),(s),(n)))

/* Float ops -- fall back to no-op / identity if NULL */
CVCL_INLINE float cvcl__sqrtf(float x) {
    const cvcl_platform_t *p = cvcl_platform_get();
    return p->sqrtf ? p->sqrtf(x) : x;  /* identity fallback */
}
CVCL_INLINE float cvcl__expf(float x) {
    const cvcl_platform_t *p = cvcl_platform_get();
    return p->expf ? p->expf(x) : 1.f;  /* constant fallback */
}
CVCL_INLINE float cvcl__fabsf(float x) {
    const cvcl_platform_t *p = cvcl_platform_get();
    return p->fabsf ? p->fabsf(x) : (x < 0.f ? -x : x);
}
CVCL_INLINE float cvcl__floorf(float x) {
    const cvcl_platform_t *p = cvcl_platform_get();
    return p->floorf ? p->floorf(x) : (float)(int)x;
}
CVCL_INLINE void cvcl__qsort(void *base, cvcl_size n, cvcl_size sz,
                                      int (*cmp)(const void*,const void*)) {
    const cvcl_platform_t *p = cvcl_platform_get();
    if (p->qsort) p->qsort(base, n, sz, cmp);
}

#  define CVCL_SQRTF(x)    cvcl__sqrtf(x)
#  define CVCL_EXPF(x)     cvcl__expf(x)
#  define CVCL_FABSF(x)    cvcl__fabsf(x)
#  define CVCL_FLOORF(x)   cvcl__floorf(x)
#  define CVCL_QSORT(b,n,s,c) cvcl__qsort((b),(n),(s),(c))

/* calloc emulation -- with overflow check */
CVCL_INLINE void *cvcl__calloc(cvcl_size n, cvcl_size sz) {
    /* Guard against multiplication overflow */
    if (sz && n > (cvcl_size)(-1) / sz) return NULL;
    cvcl_size total = n * sz;
    void *p = CVCL_MALLOC(total);
    if (p) CVCL_MEMSET(p, 0, total);
    return p;
}
#  define CVCL_CALLOC(n,sz)  cvcl__calloc((n),(sz))

#else  /* CVCL_NO_STDLIB not set -- use stdlib directly */

#  include <stdlib.h>
#  include <string.h>
#  include <math.h>

#  define CVCL_MALLOC(n)          malloc(n)
#  define CVCL_FREE(p)            free(p)
#  define CVCL_REALLOC(p,n)       realloc((p),(n))
#  define CVCL_CALLOC(n,sz)       calloc((n),(sz))
#  define CVCL_MEMCPY(d,s,n)      memcpy((d),(s),(n))
#  define CVCL_MEMSET(d,v,n)      memset((d),(v),(n))
#  define CVCL_MEMMOVE(d,s,n)     memmove((d),(s),(n))
#  define CVCL_SQRTF(x)           sqrtf(x)
#  define CVCL_EXPF(x)            expf(x)
#  define CVCL_FABSF(x)           fabsf(x)
#  define CVCL_FLOORF(x)          floorf(x)
#  define CVCL_QSORT(b,n,s,c)     qsort((b),(n),(s),(c))

#endif /* CVCL_NO_STDLIB */

#endif /* CVCL_PLATFORM_H */
