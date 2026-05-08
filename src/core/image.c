/**
 * @file image.c
 * @brief Image descriptor lifecycle and utility functions
 */

#include <cvcl/cvcl_image.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * Internal helpers
 * ---------------------------------------------------------------------- */

static const cvcl_allocator_t *resolve_alloc(const cvcl_allocator_t *a) {
    return a ? a : cvcl_default_allocator();
}

static cvcl_i32 compute_stride(cvcl_i32 width, cvcl_i32 channels,
                                cvcl_depth_t depth) {
    /* Align rows to 64 bytes for SIMD friendliness */
    cvcl_i32 row_bytes = width * channels * (cvcl_i32)depth;
    return (cvcl_i32)CVCL_ALIGN_UP((cvcl_size)row_bytes, 64);
}

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */

cvcl_result_t cvcl_image_create(cvcl_image_t          *img,
                                 cvcl_i32               width,
                                 cvcl_i32               height,
                                 cvcl_i32               channels,
                                 cvcl_depth_t           depth,
                                 const cvcl_allocator_t *alloc) {
    CVCL_CHECK_NULL(img);
    CVCL_CHECK_ARG(width  > 0);
    CVCL_CHECK_ARG(height > 0);
    CVCL_CHECK_ARG(channels >= 1 && channels <= 4);
    CVCL_CHECK_ARG(depth == CVCL_DEPTH_U8 ||
                   depth == CVCL_DEPTH_U16 ||
                   depth == CVCL_DEPTH_F32);

    const cvcl_allocator_t *a = resolve_alloc(alloc);

    img->width    = width;
    img->height   = height;
    img->channels = channels;
    img->depth    = depth;
    img->layout   = CVCL_LAYOUT_INTERLEAVED;
    img->stride   = compute_stride(width, channels, depth);

    cvcl_size buf_size = (cvcl_size)img->stride * (cvcl_size)height;
    img->data = (cvcl_u8 *)cvcl_alloc(a, buf_size);
    if (CVCL_UNLIKELY(!img->data)) {
        return CVCL_ERR_ALLOC;
    }
    memset(img->data, 0, buf_size);
    return CVCL_OK;
}

void cvcl_image_free(cvcl_image_t *img, const cvcl_allocator_t *alloc) {
    if (!img || !img->data) return;
    const cvcl_allocator_t *a = resolve_alloc(alloc);
    cvcl_free(a, img->data);
    img->data = NULL;
}

cvcl_result_t cvcl_image_view(cvcl_image_t       *view,
                               const cvcl_image_t *src,
                               cvcl_rect_t         roi) {
    CVCL_CHECK_NULL(view);
    CVCL_CHECK_NULL(src);
    CVCL_CHECK_NULL(src->data);
    CVCL_CHECK_ARG(roi.w > 0 && roi.h > 0);

    /* Clamp to source bounds */
    cvcl_i32 x0 = CVCL_CLAMP(roi.x, 0, src->width  - 1);
    cvcl_i32 y0 = CVCL_CLAMP(roi.y, 0, src->height - 1);
    cvcl_i32 x1 = CVCL_CLAMP(roi.x + roi.w, 0, src->width);
    cvcl_i32 y1 = CVCL_CLAMP(roi.y + roi.h, 0, src->height);

    CVCL_CHECK_ARG(x1 > x0 && y1 > y0);

    view->width    = x1 - x0;
    view->height   = y1 - y0;
    view->channels = src->channels;
    view->depth    = src->depth;
    view->stride   = src->stride;  /* Share parent stride -- key for zero-copy */
    view->layout   = src->layout;

    /* Point data to the top-left corner of the ROI */
    view->data = src->data
               + (cvcl_size)y0 * (cvcl_size)src->stride
               + (cvcl_size)x0 * (cvcl_size)src->channels * (cvcl_size)src->depth;

    return CVCL_OK;
}

cvcl_result_t cvcl_image_copy(cvcl_image_t       *dst,
                               const cvcl_image_t *src) {
    CVCL_CHECK_NULL(dst);
    CVCL_CHECK_NULL(src);
    CVCL_CHECK_NULL(dst->data);
    CVCL_CHECK_NULL(src->data);

    if (!cvcl_image_compatible(dst, src)) return CVCL_ERR_SIZE_MISMATCH;

    /* Row-by-row copy handles differing strides (e.g. views) */
    cvcl_i32 row_bytes = src->width * src->channels * (cvcl_i32)src->depth;
    for (cvcl_i32 y = 0; y < src->height; y++) {
        memcpy(cvcl_image_row(dst, y), cvcl_image_row(src, y),
               (cvcl_size)row_bytes);
    }
    return CVCL_OK;
}

cvcl_result_t cvcl_image_clone(cvcl_image_t          *dst,
                                const cvcl_image_t    *src,
                                const cvcl_allocator_t *alloc) {
    CVCL_CHECK_NULL(dst);
    CVCL_CHECK_NULL(src);
    CVCL_CHECK_NULL(src->data);

    CVCL_CHECK(cvcl_image_create(dst, src->width, src->height,
                                   src->channels, src->depth, alloc));
    return cvcl_image_copy(dst, src);
}

cvcl_result_t cvcl_image_zero(cvcl_image_t *img) {
    CVCL_CHECK_NULL(img);
    CVCL_CHECK_NULL(img->data);
    memset(img->data, 0, cvcl_image_buffer_size(img));
    return CVCL_OK;
}
