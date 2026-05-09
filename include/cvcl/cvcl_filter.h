/**
 * @file cvcl_filter.h
 * @brief Convolution, separable filters, morphology, and edge detection
 */

#ifndef CVCL_FILTER_H
#define CVCL_FILTER_H

#include "cvcl_image.h"
#include "cvcl_error.h"

/* -------------------------------------------------------------------------
 * Generic 2D convolution
 * ---------------------------------------------------------------------- */

/**
 * @brief Apply a 2D convolution kernel to src, writing result to dst.
 *
 * @param dst     Pre-allocated destination (same dimensions as src).
 * @param src     Source image. U8 or F32 depth.
 * @param kernel  Row-major kernel values (float).
 * @param kw      Kernel width (must be odd).
 * @param kh      Kernel height (must be odd).
 * @param border  Border handling.
 */
cvcl_result_t cvcl_convolve2d(cvcl_image_t       *dst,
                               const cvcl_image_t *src,
                               const cvcl_f32     *kernel,
                               cvcl_i32            kw,
                               cvcl_i32            kh,
                               cvcl_border_t       border);

/**
 * @brief Separable convolution (faster for decomposable kernels).
 *
 * Applies row_kernel then col_kernel (two 1D passes).
 *
 * @param tmp     Scratch image (same dims as src, allocated by caller).
 */
cvcl_result_t cvcl_convolve_sep(cvcl_image_t       *dst,
                                 cvcl_image_t       *tmp,
                                 const cvcl_image_t *src,
                                 const cvcl_f32     *row_kernel,
                                 cvcl_i32            row_ksize,
                                 const cvcl_f32     *col_kernel,
                                 cvcl_i32            col_ksize,
                                 cvcl_border_t       border);

/* -------------------------------------------------------------------------
 * Common blur kernels (SIMD-accelerated on supported platforms)
 * ---------------------------------------------------------------------- */

/**
 * @brief Box (mean) blur.
 * @param ksize  Kernel size (odd, >= 1). ksize=1 is identity.
 */
cvcl_result_t cvcl_blur_box(cvcl_image_t       *dst,
                             const cvcl_image_t *src,
                             cvcl_i32            ksize,
                             cvcl_border_t       border);

/**
 * @brief Gaussian blur.
 * @param ksize  Kernel size (odd). Pass 0 to auto-compute from sigma.
 * @param sigma  Standard deviation. Pass 0.0 to auto-compute from ksize.
 */
cvcl_result_t cvcl_blur_gaussian(cvcl_image_t       *dst,
                                  const cvcl_image_t *src,
                                  cvcl_i32            ksize,
                                  cvcl_f32            sigma,
                                  cvcl_border_t       border);

/**
 * @brief Median filter.
 * @param ksize  Kernel size (odd). Currently only supports ksize <= 9.
 */
cvcl_result_t cvcl_blur_median(cvcl_image_t       *dst,
                                const cvcl_image_t *src,
                                cvcl_i32            ksize);

/* -------------------------------------------------------------------------
 * Edge detection
 * ---------------------------------------------------------------------- */

/** Sobel gradient in X direction. dst must be F32 or I16. */
cvcl_result_t cvcl_sobel_x(cvcl_image_t       *dst,
                            const cvcl_image_t *src,
                            cvcl_border_t       border);

/** Sobel gradient in Y direction. dst must be F32 or I16. */
cvcl_result_t cvcl_sobel_y(cvcl_image_t       *dst,
                            const cvcl_image_t *src,
                            cvcl_border_t       border);

/**
 * @brief Canny edge detector.
 * @param lo_thresh  Low hysteresis threshold (0-255 for U8 input).
 * @param hi_thresh  High hysteresis threshold.
 * @param dst        U8 binary edge map (same WxH as src, 1 channel).
 */
cvcl_result_t cvcl_canny(cvcl_image_t          *dst,
                          const cvcl_image_t    *src,
                          cvcl_f32               lo_thresh,
                          cvcl_f32               hi_thresh,
                          const cvcl_allocator_t *alloc);

/* -------------------------------------------------------------------------
 * Morphology
 * ---------------------------------------------------------------------- */

/** Binary or grayscale erosion with a rectangular structuring element. */
cvcl_result_t cvcl_erode(cvcl_image_t       *dst,
                          const cvcl_image_t *src,
                          cvcl_i32            kw,
                          cvcl_i32            kh,
                          cvcl_border_t       border);

/** Binary or grayscale dilation with a rectangular structuring element. */
cvcl_result_t cvcl_dilate(cvcl_image_t       *dst,
                           const cvcl_image_t *src,
                           cvcl_i32            kw,
                           cvcl_i32            kh,
                           cvcl_border_t       border);

/* Open = erode then dilate */
cvcl_result_t cvcl_morph_open(cvcl_image_t          *dst,
                               const cvcl_image_t    *src,
                               cvcl_i32               kw,
                               cvcl_i32               kh,
                               const cvcl_allocator_t *alloc);

/* Close = dilate then erode */
cvcl_result_t cvcl_morph_close(cvcl_image_t          *dst,
                                const cvcl_image_t    *src,
                                cvcl_i32               kw,
                                cvcl_i32               kh,
                                const cvcl_allocator_t *alloc);

/* -------------------------------------------------------------------------
 * Pixel-wise arithmetic
 * ---------------------------------------------------------------------- */

/** dst = a + b (clamped) */
cvcl_result_t cvcl_add(cvcl_image_t *dst,
                        const cvcl_image_t *a,
                        const cvcl_image_t *b);

/** dst = a - b (clamped) */
cvcl_result_t cvcl_sub(cvcl_image_t *dst,
                        const cvcl_image_t *a,
                        const cvcl_image_t *b);

/** dst = a * scalar */
cvcl_result_t cvcl_scale(cvcl_image_t *dst,
                          const cvcl_image_t *a,
                          cvcl_f32 scalar);

/** dst = alpha*a + (1-alpha)*b */
cvcl_result_t cvcl_blend(cvcl_image_t *dst,
                          const cvcl_image_t *a,
                          const cvcl_image_t *b,
                          cvcl_f32 alpha);

/* -------------------------------------------------------------------------
 * Harris corner detector
 * ---------------------------------------------------------------------- */

/** Detected keypoint */
typedef struct {
    cvcl_i32 x, y;       /**< Pixel coordinates */
    cvcl_f32 response;   /**< Harris R value -- higher = stronger corner */
    cvcl_f32 scale;      /**< Always 1.0 for basic Harris */
} cvcl_keypoint_t;

/**
 * @brief Harris corner detector.
 *
 * @param kpts       Output array (caller must free with cvcl_keypoints_free).
 * @param count      Number of keypoints found.
 * @param src        Single-channel U8 input.
 * @param k          Harris sensitivity (default 0.04).
 * @param threshold  Minimum R value to accept (default 1e6).
 * @param nms_radius Non-maximum suppression window radius (default 3).
 * @param max_kpts   Maximum keypoints to return (default 10000).
 */
cvcl_result_t cvcl_harris(cvcl_keypoint_t      **kpts,
                            cvcl_i32              *count,
                            const cvcl_image_t    *src,
                            cvcl_f32               k,
                            cvcl_f32               threshold,
                            cvcl_i32               nms_radius,
                            cvcl_i32               max_kpts,
                            const cvcl_allocator_t *alloc);

/** Free keypoint array returned by cvcl_harris. */
void cvcl_keypoints_free(cvcl_keypoint_t *kpts, const cvcl_allocator_t *alloc);

/* -------------------------------------------------------------------------
 * Bilateral filter
 * ---------------------------------------------------------------------- */

/**
 * @brief Bilateral filter -- edge-preserving smoothing.
 *
 * @param sigma_space  Spatial Gaussian sigma (controls spatial extent).
 * @param sigma_color  Color Gaussian sigma (controls edge sensitivity).
 *                     Large sigma_color → approaches Gaussian blur.
 *                     Small sigma_color → strong edge preservation.
 */
cvcl_result_t cvcl_blur_bilateral(cvcl_image_t       *dst,
                                    const cvcl_image_t *src,
                                    cvcl_i32            ksize,
                                    cvcl_f32            sigma_space,
                                    cvcl_f32            sigma_color);

/* -------------------------------------------------------------------------
 * CLAHE
 * ---------------------------------------------------------------------- */

/**
 * @brief CLAHE -- Contrast Limited Adaptive Histogram Equalization.
 *
 * Single-channel U8 only.
 *
 * @param tile_w      Tile width in pixels (default 8).
 * @param tile_h      Tile height in pixels (default 8).
 * @param clip_limit  Histogram clip limit (default 2.0).
 *                    Higher = more contrast enhancement, more noise.
 */
cvcl_result_t cvcl_clahe(cvcl_image_t       *dst,
                           const cvcl_image_t *src,
                           cvcl_i32            tile_w,
                           cvcl_i32            tile_h,
                           cvcl_f32            clip_limit);

#endif /* CVCL_FILTER_H */
