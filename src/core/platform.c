/**
 * @file platform.c
 * @brief Platform abstraction layer implementation
 *
 * Provides cvcl_platform_init() and cvcl_platform_get().
 * When CVCL_NO_STDLIB is not set, this file is essentially empty --
 * the macros in cvcl_platform.h resolve directly to stdlib.
 */

#include <cvcl/cvcl_platform.h>
#include <cvcl/cvcl_error.h>

#ifdef CVCL_NO_STDLIB

/* Global platform table -- set by cvcl_platform_init() */
static cvcl_platform_t g_platform;
static int             g_platform_init = 0;

cvcl_result_t cvcl_platform_init(const cvcl_platform_t *p) {
    if (!p)            return CVCL_ERR_NULL_PTR;
    if (!p->malloc)    return CVCL_ERR_INVALID_ARG;
    if (!p->free)      return CVCL_ERR_INVALID_ARG;
    if (!p->memcpy)    return CVCL_ERR_INVALID_ARG;
    if (!p->memset)    return CVCL_ERR_INVALID_ARG;

    g_platform      = *p;
    g_platform_init = 1;

    /* Fill in no-op defaults for optional fields */
    if (!g_platform.memmove) g_platform.memmove = g_platform.memcpy;
    if (!g_platform.realloc) {
        /* realloc emulation: alloc new, copy, free old.
         * Size tracking not available without stdlib -- caller must
         * provide realloc if they use functions that realloc (io_png). */
        g_platform.realloc = NULL;
    }

    return CVCL_OK;
}

const cvcl_platform_t *cvcl_platform_get(void) {
    return &g_platform;
}

#else

/* Stdlib mode: no-op init, get returns NULL (macros don't use it) */
cvcl_result_t cvcl_platform_init(const cvcl_platform_t *p) {
    (void)p;
    return CVCL_OK;
}

const cvcl_platform_t *cvcl_platform_get(void) {
    return NULL;  /* never called -- macros resolve to stdlib directly */
}

#endif /* CVCL_NO_STDLIB */
