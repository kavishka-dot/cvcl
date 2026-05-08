/**
 * @file cvcl_pixel.h
 * @brief Fast inline pixel accessors and histogram utilities
 *
 * Optimization layers:
 *  1. restrict on image pointer -- compiler proves no aliasing between
 *     img->data and other pointers, enabling better load scheduling.
 *  2. may_alias F32 accessors -- replaces memcpy with a single typed
 *     load/store while preserving strict aliasing correctness.
 *  3. cvcl_pixel_ptr -- direct pointer to pixel, avoids repeated
 *     stride multiply when the caller already has x,y.
 *  4. cvcl_fill -- SIMD-backed constant fill via memset (single channel)
 *     or a tight loop (multi-channel). Saves users writing their own.
 *  5. cvcl_image_row is already inline -- keep it that way so the
 *     compiler can hoist the stride multiply out of inner loops.
 */

#ifndef CVCL_PIXEL_H
#define CVCL_PIXEL_H

#include "cvcl_image.h"

/* -------------------------------------------------------------------------
 * may_alias type for F32 -- lets the compiler emit a single load/store
 * instead of falling back to a byte-by-byte memcpy sequence.
 * This is standards-compliant: GCC/Clang both support may_alias.
 * MSVC doesn't need it (it doesn't enforce strict aliasing by default).
 * ---------------------------------------------------------------------- */
#if defined(CVCL_COMPILER_GCC) || defined(CVCL_COMPILER_CLANG)
    typedef cvcl_f32 cvcl_f32_alias __attribute__((may_alias));
    typedef cvcl_u16 cvcl_u16_alias __attribute__((may_alias));
#else
    typedef cvcl_f32 cvcl_f32_alias;
    typedef cvcl_u16 cvcl_u16_alias;
#endif

/* -------------------------------------------------------------------------
 * Direct pixel pointer -- avoids repeated stride multiply when iterating.
 *
 * Pattern:
 *   cvcl_u8 *p = cvcl_pixel_ptr(img, x, y);
 *   r = p[0]; g = p[1]; b = p[2];  // no multiply inside the loop
 * ---------------------------------------------------------------------- */
CVCL_INLINE cvcl_u8 *cvcl_pixel_ptr(cvcl_image_t * CVCL_RESTRICT img,
                                      cvcl_i32 x, cvcl_i32 y) {
    return img->data
         + (cvcl_size)y * (cvcl_size)img->stride
         + (cvcl_size)x * (cvcl_size)img->channels;
}

CVCL_INLINE const cvcl_u8 *cvcl_pixel_ptr_c(const cvcl_image_t * CVCL_RESTRICT img,
                                              cvcl_i32 x, cvcl_i32 y) {
    return img->data
         + (cvcl_size)y * (cvcl_size)img->stride
         + (cvcl_size)x * (cvcl_size)img->channels;
}

/* -------------------------------------------------------------------------
 * U8 pixel accessors (interleaved layout)
 * ---------------------------------------------------------------------- */
CVCL_INLINE cvcl_u8 cvcl_get_u8(const cvcl_image_t * CVCL_RESTRICT img,
                                  cvcl_i32 x, cvcl_i32 y, cvcl_i32 c) {
    return img->data[(cvcl_size)y * img->stride
                   + (cvcl_size)x * img->channels + c];
}

CVCL_INLINE void cvcl_set_u8(cvcl_image_t * CVCL_RESTRICT img,
                               cvcl_i32 x, cvcl_i32 y, cvcl_i32 c,
                               cvcl_u8 val) {
    img->data[(cvcl_size)y * img->stride
            + (cvcl_size)x * img->channels + c] = val;
}

/* -------------------------------------------------------------------------
 * F32 pixel accessors -- single load/store via may_alias
 * ---------------------------------------------------------------------- */
CVCL_INLINE cvcl_f32 cvcl_get_f32(const cvcl_image_t * CVCL_RESTRICT img,
                                    cvcl_i32 x, cvcl_i32 y, cvcl_i32 c) {
    const cvcl_u8 *p = img->data
                     + (cvcl_size)y * img->stride
                     + ((cvcl_size)x * img->channels + c) * sizeof(cvcl_f32);
    return *(const cvcl_f32_alias *)p;
}

CVCL_INLINE void cvcl_set_f32(cvcl_image_t * CVCL_RESTRICT img,
                                cvcl_i32 x, cvcl_i32 y, cvcl_i32 c,
                                cvcl_f32 val) {
    cvcl_u8 *p = img->data
               + (cvcl_size)y * img->stride
               + ((cvcl_size)x * img->channels + c) * sizeof(cvcl_f32);
    *(cvcl_f32_alias *)p = val;
}

/* -------------------------------------------------------------------------
 * U16 pixel accessors (same pattern)
 * ---------------------------------------------------------------------- */
CVCL_INLINE cvcl_u16 cvcl_get_u16(const cvcl_image_t * CVCL_RESTRICT img,
                                    cvcl_i32 x, cvcl_i32 y, cvcl_i32 c) {
    const cvcl_u8 *p = img->data
                     + (cvcl_size)y * img->stride
                     + ((cvcl_size)x * img->channels + c) * sizeof(cvcl_u16);
    return *(const cvcl_u16_alias *)p;
}

CVCL_INLINE void cvcl_set_u16(cvcl_image_t * CVCL_RESTRICT img,
                                cvcl_i32 x, cvcl_i32 y, cvcl_i32 c,
                                cvcl_u16 val) {
    cvcl_u8 *p = img->data
               + (cvcl_size)y * img->stride
               + ((cvcl_size)x * img->channels + c) * sizeof(cvcl_u16);
    *(cvcl_u16_alias *)p = val;
}

/* -------------------------------------------------------------------------
 * cvcl_fill -- set all pixels to a constant value
 *
 * Single-channel: uses memset (O(n), compiler-optimized).
 * Multi-channel:  tight row loop with SIMD-friendly layout.
 * ---------------------------------------------------------------------- */
cvcl_result_t cvcl_fill(cvcl_image_t *img, cvcl_u8 value);

/* -------------------------------------------------------------------------
 * Bilinear sample -- F32 output at fractional coordinates
 * ---------------------------------------------------------------------- */
cvcl_f32 cvcl_sample_bilinear(const cvcl_image_t *img,
                               cvcl_f32 x, cvcl_f32 y,
                               cvcl_i32 c,
                               cvcl_border_t border);

/* -------------------------------------------------------------------------
 * Histogram (U8 images only)
 *
 * hist must point to img->channels * 256 * sizeof(cvcl_u32) bytes.
 * ---------------------------------------------------------------------- */
cvcl_result_t cvcl_histogram(const cvcl_image_t *img, cvcl_u32 *hist);

/**
 * @brief Equalize histogram of a single-channel U8 image in-place.
 */
cvcl_result_t cvcl_equalize_hist(cvcl_image_t *img);

/* -------------------------------------------------------------------------
 * Threshold
 * ---------------------------------------------------------------------- */
typedef enum {
    CVCL_THRESH_BINARY     = 0,  /**< dst = (src > t) ? max : 0        */
    CVCL_THRESH_BINARY_INV = 1,  /**< dst = (src > t) ? 0   : max      */
    CVCL_THRESH_TRUNC      = 2,  /**< dst = (src > t) ? t   : src      */
    CVCL_THRESH_TOZERO     = 3,  /**< dst = (src > t) ? src : 0        */
    CVCL_THRESH_OTSU       = 4,  /**< Auto-compute t via Otsu's method */
} cvcl_thresh_type_t;

/**
 * @brief Apply thresholding to a single-channel U8 image.
 * @param[out] out_thresh  Actual threshold used (useful for OTSU). May be NULL.
 */
cvcl_result_t cvcl_threshold(cvcl_image_t       *dst,
                              const cvcl_image_t *src,
                              cvcl_f32            thresh,
                              cvcl_f32            max_val,
                              cvcl_thresh_type_t  type,
                              cvcl_f32           *out_thresh);

#endif /* CVCL_PIXEL_H */
