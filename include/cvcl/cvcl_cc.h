/**
 * @file cvcl_cc.h
 * @brief Connected components labeling (two-pass, union-find, 4-connectivity)
 */

#ifndef CVCL_CC_H
#define CVCL_CC_H

#include "cvcl_image.h"
#include "cvcl_alloc.h"
#include "cvcl_error.h"
#include "cvcl_types.h"

/** Result of connected components analysis */
typedef struct {
    cvcl_u16 *labels;      /**< Label map: same WxH as input. 0=background. */
    cvcl_i32  num_labels;  /**< Number of distinct connected regions found.  */
    cvcl_i32  width;
    cvcl_i32  height;
} cvcl_cc_result_t;

/**
 * @brief Label connected regions in a binary U8 image (4-connectivity).
 *
 * Input: single-channel U8. Non-zero = foreground.
 * Output: label map where each connected region has a unique ID 1..N.
 *
 * @param result   Output. Call cvcl_cc_free when done.
 * @param src      Binary source image (U8, 1 channel).
 * @param alloc    Allocator for label buffer. NULL = default.
 */
cvcl_result_t cvcl_connected_components(cvcl_cc_result_t      *result,
                                          const cvcl_image_t    *src,
                                          const cvcl_allocator_t *alloc);

/** Free label buffer. */
void cvcl_cc_free(cvcl_cc_result_t *result, const cvcl_allocator_t *alloc);

/**
 * @brief Compute axis-aligned bounding box for each labeled region.
 *
 * @param[out] boxes  Allocated array of num_labels rectangles. Caller must free().
 * @param[out] count  Number of boxes (== cc->num_labels).
 */
cvcl_result_t cvcl_cc_bboxes(cvcl_rect_t          **boxes,
                               cvcl_i32              *count,
                               const cvcl_cc_result_t *cc,
                               const cvcl_allocator_t *alloc);

#endif /* CVCL_CC_H */
