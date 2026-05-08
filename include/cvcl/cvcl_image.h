/**
 * @file cvcl_image.h
 * @brief Core image descriptor and lifecycle functions
 *
 * The cvcl_image_t struct is the central data structure of CVCL.
 * It describes a 2D pixel buffer without owning or enforcing any particular
 * memory strategy. Ownership is explicit and always caller-managed.
 *
 * Memory layout (interleaved, default):
 *
 *   row 0: [C0|C1|C2] [C0|C1|C2] ... (width pixels)
 *   row 1: [C0|C1|C2] [C0|C1|C2] ...
 *   ...
 *   stride >= width * channels * depth_bytes
 *
 * The stride may be larger than width*channels*depth to accommodate:
 *   - Row alignment requirements (e.g., 64-byte SIMD alignment)
 *   - Sub-image / ROI views into a larger buffer (zero-copy crop)
 *   - Camera / DMA buffer stride constraints
 */

#ifndef CVCL_IMAGE_H
#define CVCL_IMAGE_H

#include "cvcl_types.h"
#include "cvcl_alloc.h"
#include "cvcl_error.h"

/* -------------------------------------------------------------------------
 * Image descriptor
 * ---------------------------------------------------------------------- */
typedef struct {
    cvcl_u8      *data;      /**< Pointer to first pixel of row 0            */
    cvcl_i32      width;     /**< Logical width in pixels                    */
    cvcl_i32      height;    /**< Logical height in pixels                   */
    cvcl_i32      channels;  /**< Number of channels (1=gray,3=RGB,4=RGBA)   */
    cvcl_i32      stride;    /**< Bytes per row (>= width*channels*depth)    */
    cvcl_depth_t  depth;     /**< Bytes per channel element                  */
    cvcl_layout_t layout;    /**< Interleaved or planar channel order        */
} cvcl_image_t;

/* -------------------------------------------------------------------------
 * Lifecycle
 * ---------------------------------------------------------------------- */

/**
 * @brief Allocate and initialise an image.
 *
 * @param[out] img       Descriptor to populate.
 * @param      width     Must be > 0.
 * @param      height    Must be > 0.
 * @param      channels  1, 2, 3, or 4.
 * @param      depth     CVCL_DEPTH_U8, U16, or F32.
 * @param      alloc     Allocator to use; NULL uses default (malloc).
 * @return CVCL_OK or an error code.
 */
cvcl_result_t cvcl_image_create(cvcl_image_t          *img,
                                 cvcl_i32               width,
                                 cvcl_i32               height,
                                 cvcl_i32               channels,
                                 cvcl_depth_t           depth,
                                 const cvcl_allocator_t *alloc);

/**
 * @brief Free the image pixel buffer.
 *
 * Does NOT free the cvcl_image_t struct itself (caller owns it).
 * Sets img->data to NULL after freeing.
 *
 * @param img   Image to free. If NULL or img->data==NULL, no-op.
 * @param alloc Must match the allocator used in cvcl_image_create.
 */
void cvcl_image_free(cvcl_image_t *img, const cvcl_allocator_t *alloc);

/**
 * @brief Create a zero-copy sub-image (region of interest).
 *
 * The returned view shares the same pixel buffer as src.
 * Do NOT call cvcl_image_free on a view -- free the original instead.
 *
 * @param[out] view  Descriptor to populate.
 * @param      src   Source image.
 * @param      roi   Rectangle within src. Clamped to src bounds.
 * @return CVCL_OK or error.
 */
cvcl_result_t cvcl_image_view(cvcl_image_t       *view,
                               const cvcl_image_t *src,
                               cvcl_rect_t         roi);

/**
 * @brief Deep-copy src into dst (must already be allocated and compatible).
 */
cvcl_result_t cvcl_image_copy(cvcl_image_t       *dst,
                               const cvcl_image_t *src);

/**
 * @brief Clone src into a newly allocated image.
 */
cvcl_result_t cvcl_image_clone(cvcl_image_t          *dst,
                                const cvcl_image_t    *src,
                                const cvcl_allocator_t *alloc);

/**
 * @brief Zero-fill image pixels.
 */
cvcl_result_t cvcl_image_zero(cvcl_image_t *img);

/* -------------------------------------------------------------------------
 * Queries
 * ---------------------------------------------------------------------- */

/** Total bytes in the allocated buffer (stride * height). */
CVCL_INLINE cvcl_size cvcl_image_buffer_size(const cvcl_image_t *img) {
    CVCL_ASSERT(img);
    return (cvcl_size)img->stride * (cvcl_size)img->height;
}

/** Pointer to the start of row y. No bounds check in release builds. */
CVCL_INLINE cvcl_u8 *cvcl_image_row(const cvcl_image_t *img, cvcl_i32 y) {
    CVCL_ASSERT(img && y >= 0 && y < img->height);
    return img->data + (cvcl_size)y * (cvcl_size)img->stride;
}

/** Pixel byte offset for (x, y) in an interleaved image. */
CVCL_INLINE cvcl_size cvcl_image_pixel_offset(const cvcl_image_t *img,
                                                cvcl_i32 x, cvcl_i32 y) {
    CVCL_ASSERT(img && x >= 0 && x < img->width && y >= 0 && y < img->height);
    return (cvcl_size)y * (cvcl_size)img->stride
         + (cvcl_size)x * (cvcl_size)img->channels * (cvcl_size)img->depth;
}

/** Returns 1 if two images have identical dimensions, depth, and layout. */
CVCL_INLINE int cvcl_image_compatible(const cvcl_image_t *a,
                                       const cvcl_image_t *b) {
    return a && b
        && a->width    == b->width
        && a->height   == b->height
        && a->channels == b->channels
        && a->depth    == b->depth
        && a->layout   == b->layout;
}

/* -------------------------------------------------------------------------
 * Channel count helpers
 * ---------------------------------------------------------------------- */
#define CVCL_CH_GRAY  1
#define CVCL_CH_GRAYA 2
#define CVCL_CH_RGB   3
#define CVCL_CH_RGBA  4

#endif /* CVCL_IMAGE_H */
