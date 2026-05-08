/**
 * @file cvcl_error.h
 * @brief Error codes and result type
 */

#ifndef CVCL_ERROR_H
#define CVCL_ERROR_H

#include "cvcl_types.h"

/* -------------------------------------------------------------------------
 * Result codes
 * ---------------------------------------------------------------------- */
typedef enum {
    CVCL_OK                 =  0,  /**< Success                               */
    CVCL_ERR_NULL_PTR       = -1,  /**< NULL pointer passed where not allowed */
    CVCL_ERR_INVALID_ARG    = -2,  /**< Argument value out of valid range      */
    CVCL_ERR_ALLOC          = -3,  /**< Memory allocation failed              */
    CVCL_ERR_IO             = -4,  /**< File I/O failure                      */
    CVCL_ERR_UNSUPPORTED    = -5,  /**< Operation not supported for this input */
    CVCL_ERR_SIZE_MISMATCH  = -6,  /**< Image dimensions incompatible         */
    CVCL_ERR_DEPTH_MISMATCH = -7,  /**< Pixel depth mismatch                  */
    CVCL_ERR_FORMAT         = -8,  /**< Unrecognized or malformed file format  */
    CVCL_ERR_OVERFLOW       = -9,  /**< Arithmetic or buffer overflow detected */
    CVCL_ERR_INTERNAL       = -99, /**< Bug in CVCL itself                    */
} cvcl_result_t;

/* -------------------------------------------------------------------------
 * Human-readable error description
 * ---------------------------------------------------------------------- */

/** Returns a static string describing the error code. Never NULL. */
const char *cvcl_strerror(cvcl_result_t err);

/* -------------------------------------------------------------------------
 * Convenience macros
 * ---------------------------------------------------------------------- */

/** Propagate error: if expr != CVCL_OK, return immediately. */
#define CVCL_CHECK(expr)              \
    do {                              \
        cvcl_result_t _r = (expr);   \
        if (CVCL_UNLIKELY(_r != CVCL_OK)) return _r; \
    } while (0)

/** Return CVCL_ERR_NULL_PTR if ptr is NULL. */
#define CVCL_CHECK_NULL(ptr) \
    do { if (CVCL_UNLIKELY((ptr) == NULL)) return CVCL_ERR_NULL_PTR; } while (0)

/** Return CVCL_ERR_INVALID_ARG if condition is false. */
#define CVCL_CHECK_ARG(cond) \
    do { if (CVCL_UNLIKELY(!(cond))) return CVCL_ERR_INVALID_ARG; } while (0)

#endif /* CVCL_ERROR_H */
