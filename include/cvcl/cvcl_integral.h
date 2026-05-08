/**
 * @file cvcl_integral.h
 * @brief Integral image (summed area table) for O(1) box statistics
 */

#ifndef CVCL_INTEGRAL_H
#define CVCL_INTEGRAL_H

#include "cvcl_image.h"
#include "cvcl_alloc.h"
#include "cvcl_error.h"

/** Integral image descriptor -- (w+1) x (h+1) u32 table */
typedef struct {
    cvcl_u32 *data;
    cvcl_i32  width;   /**< src->width  + 1 */
    cvcl_i32  height;  /**< src->height + 1 */
} cvcl_integral_t;

/** Build integral image from single-channel U8 source. */
cvcl_result_t cvcl_integral(cvcl_integral_t       *sat,
                              const cvcl_image_t    *src,
                              const cvcl_allocator_t *alloc);

/** Free integral image buffer. */
void cvcl_integral_free(cvcl_integral_t *sat, const cvcl_allocator_t *alloc);

/**
 * @brief O(1) sum of pixels in rectangle (x0,y0)→(x1,y1) inclusive.
 * Coordinates are automatically clamped to image bounds.
 */
cvcl_u32 cvcl_integral_sum(const cvcl_integral_t *sat,
                             cvcl_i32 x0, cvcl_i32 y0,
                             cvcl_i32 x1, cvcl_i32 y1);

/**
 * @brief O(1) box blur using SAT -- same cost for k=5 and k=255.
 * Single-channel U8 only. Use when kernel is very large (k > 63).
 */
cvcl_result_t cvcl_blur_box_sat(cvcl_image_t          *dst,
                                  const cvcl_image_t    *src,
                                  cvcl_i32               ksize,
                                  const cvcl_allocator_t *alloc);

#endif /* CVCL_INTEGRAL_H */
