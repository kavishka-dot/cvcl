/**
 * @file bilateral.c
 * @brief Bilateral filter -- edge-preserving smoothing
 *
 * Each output pixel is a weighted average of neighbors where weights
 * depend on both spatial distance (Gaussian) and intensity similarity.
 * Edges are preserved because pixels across strong edges have low
 * intensity similarity weight.
 *
 * Complexity: O(w*h*k^2) -- slow for large k.
 * For k > 9 consider iterating with smaller k.
 */

#include <cvcl/cvcl_platform.h>
#include <cvcl/cvcl_filter.h>

cvcl_result_t cvcl_blur_bilateral(cvcl_image_t       *dst,
                                    const cvcl_image_t *src,
                                    cvcl_i32            ksize,
                                    cvcl_f32            sigma_space,
                                    cvcl_f32            sigma_color) {
    CVCL_CHECK_NULL(dst); CVCL_CHECK_NULL(src);
    CVCL_CHECK_NULL(dst->data); CVCL_CHECK_NULL(src->data);
    CVCL_CHECK_ARG(src->depth == CVCL_DEPTH_U8);
    CVCL_CHECK_ARG(cvcl_image_compatible(dst, src));
    CVCL_CHECK_ARG(ksize >= 1);

    if (ksize % 2 == 0) ksize++;
    cvcl_i32 half = ksize / 2;
    cvcl_i32 w = src->width, h = src->height, ch = src->channels;

    const cvcl_allocator_t *a = cvcl_image_allocator(dst);

    /* Precompute spatial Gaussian weights */
    cvcl_f32 *space_w = (cvcl_f32 *)cvcl_alloc(a,
        (cvcl_size)ksize * ksize * sizeof(cvcl_f32));
    if (!space_w) return CVCL_ERR_ALLOC;

    cvcl_f32 inv_2ss = 1.f / (2.f * sigma_space * sigma_space);
    for (cvcl_i32 ky = -half; ky <= half; ky++)
        for (cvcl_i32 kx = -half; kx <= half; kx++)
            space_w[(ky+half)*ksize+(kx+half)] =
                CVCL_EXPF(-(cvcl_f32)(kx*kx+ky*ky) * inv_2ss);

    cvcl_f32 inv_2sc = 1.f / (2.f * sigma_color * sigma_color);

    for (cvcl_i32 y = 0; y < h; y++) {
        cvcl_u8 * CVCL_RESTRICT dr = cvcl_image_row(dst, y);
        const cvcl_u8 *cr = cvcl_image_row(src, y);

        for (cvcl_i32 x = 0; x < w; x++) {
            for (cvcl_i32 c = 0; c < ch; c++) {
                cvcl_f32 sum_w = 0.f, sum_val = 0.f;
                cvcl_f32 center = (cvcl_f32)cr[x*ch+c];

                for (cvcl_i32 ky = -half; ky <= half; ky++) {
                    cvcl_i32 sy = CVCL_CLAMP(y+ky, 0, h-1);
                    const cvcl_u8 *nr = cvcl_image_row(src, sy);
                    for (cvcl_i32 kx = -half; kx <= half; kx++) {
                        cvcl_i32 sx = CVCL_CLAMP(x+kx, 0, w-1);
                        cvcl_f32 val  = (cvcl_f32)nr[sx*ch+c];
                        cvcl_f32 diff = val - center;
                        cvcl_f32 wc   = CVCL_EXPF(-diff*diff * inv_2sc);
                        cvcl_f32 ws   = space_w[(ky+half)*ksize+(kx+half)];
                        cvcl_f32 w_total = ws * wc;
                        sum_val += w_total * val;
                        sum_w   += w_total;
                    }
                }
                dr[x*ch+c] = (cvcl_u8)CVCL_CLAMP(
                    (cvcl_i32)(sum_val/sum_w + 0.5f), 0, 255);
            }
        }
    }

    cvcl_free(a, space_w);
    return CVCL_OK;
}
