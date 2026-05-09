/**
 * @file cvcl_transform.h
 * @brief Geometric transforms: resize, crop, flip, rotate, affine
 */

#ifndef CVCL_TRANSFORM_H
#define CVCL_TRANSFORM_H

#include "cvcl_image.h"
#include "cvcl_error.h"

/* -------------------------------------------------------------------------
 * Resize
 * ---------------------------------------------------------------------- */

/**
 * @brief Resize src into dst (must be pre-allocated with target dimensions).
 *
 * @param dst     Pre-allocated destination image.
 * @param src     Source image.
 * @param interp  Interpolation mode.
 */
cvcl_result_t cvcl_resize(cvcl_image_t       *dst,
                           const cvcl_image_t *src,
                           cvcl_interp_t       interp);

/**
 * @brief Allocate and resize in one call.
 */
cvcl_result_t cvcl_resize_alloc(cvcl_image_t          *dst,
                                 const cvcl_image_t    *src,
                                 cvcl_i32               dst_w,
                                 cvcl_i32               dst_h,
                                 cvcl_interp_t          interp,
                                 const cvcl_allocator_t *alloc);

/* -------------------------------------------------------------------------
 * Flip
 * ---------------------------------------------------------------------- */

/** Flip image horizontally in-place. */
cvcl_result_t cvcl_flip_h(cvcl_image_t *img);

/** Flip image vertically in-place. */
cvcl_result_t cvcl_flip_v(cvcl_image_t *img);

/* -------------------------------------------------------------------------
 * Rotate (multiples of 90 degrees, in-place metadata swap)
 * ---------------------------------------------------------------------- */

typedef enum {
    CVCL_ROTATE_90CW  = 0,
    CVCL_ROTATE_90CCW = 1,
    CVCL_ROTATE_180   = 2,
} cvcl_rotate_t;

/**
 * @brief Rotate image 90/180/270 degrees.
 * Allocates a new buffer for 90/270 (width/height swap).
 */
cvcl_result_t cvcl_rotate(cvcl_image_t          *dst,
                           const cvcl_image_t    *src,
                           cvcl_rotate_t          angle,
                           const cvcl_allocator_t *alloc);

/* -------------------------------------------------------------------------
 * Crop (zero-copy view)
 * ---------------------------------------------------------------------- */

/**
 * @brief Return a zero-copy view of a rectangular sub-region.
 * Equivalent to cvcl_image_view but named for discoverability.
 */
cvcl_result_t cvcl_crop(cvcl_image_t       *view,
                         const cvcl_image_t *src,
                         cvcl_rect_t         roi);

/* -------------------------------------------------------------------------
 * Affine transform (2x3 matrix)
 * ---------------------------------------------------------------------- */

/**
 * @brief Apply a 2x3 affine transform M to src, writing into dst.
 *
 * M is row-major: [m00 m01 m02; m10 m11 m12]
 * dst(x,y) = src(M * [x,y,1]^T)  (inverse mapping)
 *
 * @param dst     Pre-allocated destination.
 * @param src     Source image.
 * @param M       6-element float array [m00,m01,m02,m10,m11,m12].
 * @param interp  Interpolation.
 * @param border  Border handling for out-of-bounds samples.
 */
cvcl_result_t cvcl_affine(cvcl_image_t       *dst,
                           const cvcl_image_t *src,
                           const cvcl_f32      M[6],
                           cvcl_interp_t       interp,
                           cvcl_border_t       border);

/* -------------------------------------------------------------------------
 * Transpose
 * ---------------------------------------------------------------------- */
cvcl_result_t cvcl_transpose(cvcl_image_t          *dst,
                              const cvcl_image_t    *src,
                              const cvcl_allocator_t *alloc);

/* -------------------------------------------------------------------------
 * Convert between depth types
 * ---------------------------------------------------------------------- */

/**
 * @brief Convert pixel depth (e.g., U8 -> F32 normalised to [0,1]).
 * dst must be pre-allocated with same dimensions but target depth.
 */
cvcl_result_t cvcl_convert_depth(cvcl_image_t       *dst,
                                  const cvcl_image_t *src);

/* -------------------------------------------------------------------------
 * Convert between channel counts
 * ---------------------------------------------------------------------- */

/**
 * @brief Convert channel count (e.g., RGB->Gray, Gray->RGB, RGB->RGBA).
 * dst must be pre-allocated with same WxH but target channel count.
 */
cvcl_result_t cvcl_convert_channels(cvcl_image_t       *dst,
                                     const cvcl_image_t *src);

#endif /* CVCL_TRANSFORM_H */

/* -------------------------------------------------------------------------
 * Image pyramid
 * ---------------------------------------------------------------------- */

/**
 * @brief Build a Gaussian image pyramid.
 *
 * levels[0] = copy of src (full resolution)
 * levels[i] = levels[i-1] blurred and downsampled by 2x
 *
 * @param levels    Pre-allocated array of n_levels cvcl_image_t (zeroed).
 * @param n_levels  Number of pyramid levels (1..16).
 * @param alloc     Allocator for level images. NULL = default.
 */
cvcl_result_t cvcl_pyramid_gaussian(cvcl_image_t          *levels,
                                      cvcl_i32               n_levels,
                                      const cvcl_image_t    *src,
                                      const cvcl_allocator_t *alloc);

/**
 * @brief Build a Laplacian image pyramid.
 *
 * levels[i]         = gaussian[i] - upsample(gaussian[i+1]) + 128
 * levels[n_levels-1]= residual (smallest Gaussian level)
 *
 * @param levels    Pre-allocated array of n_levels cvcl_image_t (zeroed).
 * @param n_levels  At least 2.
 */
cvcl_result_t cvcl_pyramid_laplacian(cvcl_image_t          *levels,
                                       cvcl_i32               n_levels,
                                       const cvcl_image_t    *src,
                                       const cvcl_allocator_t *alloc);

/** Free all levels in a pyramid array. */
void cvcl_pyramid_free(cvcl_image_t          *levels,
                         cvcl_i32               n_levels,
                         const cvcl_allocator_t *alloc);
