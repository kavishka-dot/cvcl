/**
 * @file clahe.c
 * @brief CLAHE -- Contrast Limited Adaptive Histogram Equalization
 *
 * Divides the image into tiles, equalizes each tile's histogram with
 * a clip limit to prevent noise amplification, then uses bilinear
 * interpolation between tile centers to avoid blocking artifacts.
 *
 * Single-channel U8 only. For RGB, apply per-channel or convert to
 * a luminance-based color space first.
 */

#include <cvcl/cvcl_platform.h>
#include <cvcl/cvcl_filter.h>
#include <cvcl/cvcl_pixel.h>

/* Build clipped + redistributed LUT for one tile */
static void clahe_lut(cvcl_u8 *lut, const cvcl_u32 *hist,
                       cvcl_i32 n_pixels, cvcl_f32 clip_limit) {
    /* Clip limit in absolute counts */
    cvcl_i32 clip = (cvcl_i32)(clip_limit * n_pixels / 256.f);
    if (clip < 1) clip = 1;

    /* Clip and accumulate excess */
    cvcl_u32 clipped[256];
    cvcl_i32 excess = 0;
    for (cvcl_i32 i = 0; i < 256; i++) {
        if ((cvcl_i32)hist[i] > clip) {
            excess += (cvcl_i32)hist[i] - clip;
            clipped[i] = (cvcl_u32)clip;
        } else {
            clipped[i] = hist[i];
        }
    }

    /* Redistribute excess uniformly */
    cvcl_i32 per_bin = excess / 256;
    cvcl_i32 leftover = excess - per_bin * 256;
    for (cvcl_i32 i = 0; i < 256; i++) {
        clipped[i] += (cvcl_u32)per_bin;
        if (i < leftover) clipped[i]++;
    }

    /* Build CDF → LUT */
    cvcl_u32 cdf = 0, cdf_min = 0;
    int found_min = 0;
    for (cvcl_i32 i = 0; i < 256; i++) {
        cdf += clipped[i];
        if (!found_min && clipped[i]) { cdf_min = cdf; found_min = 1; }
        /* Normalize to [0,255] */
        cvcl_i32 v = 0;
        if (n_pixels > (cvcl_i32)cdf_min)
            v = (cvcl_i32)(((cvcl_f32)(cdf - cdf_min) /
                             (cvcl_f32)(n_pixels - cdf_min)) * 255.f + 0.5f);
        lut[i] = (cvcl_u8)CVCL_CLAMP(v, 0, 255);
    }
}

cvcl_result_t cvcl_clahe(cvcl_image_t       *dst,
                           const cvcl_image_t *src,
                           cvcl_i32            tile_w,
                           cvcl_i32            tile_h,
                           cvcl_f32            clip_limit) {
    CVCL_CHECK_NULL(dst); CVCL_CHECK_NULL(src);
    CVCL_CHECK_NULL(dst->data); CVCL_CHECK_NULL(src->data);
    CVCL_CHECK_ARG(src->depth    == CVCL_DEPTH_U8);
    CVCL_CHECK_ARG(src->channels == 1);
    CVCL_CHECK_ARG(cvcl_image_compatible(dst, src));

    cvcl_i32 w = src->width, h = src->height;
    if (tile_w  <= 0) tile_w  = 8;
    if (tile_h  <= 0) tile_h  = 8;
    if (clip_limit <= 0.f) clip_limit = 2.f;

    /* Number of tiles */
    cvcl_i32 nx = (w + tile_w - 1) / tile_w;
    cvcl_i32 ny = (h + tile_h - 1) / tile_h;

    const cvcl_allocator_t *a = cvcl_image_allocator(dst);

    /* Allocate LUT table: ny x nx x 256 */
    cvcl_u8 *luts = (cvcl_u8 *)cvcl_alloc(a,
        (cvcl_size)ny * nx * 256 * sizeof(cvcl_u8));
    if (!luts) return CVCL_ERR_ALLOC;

    /* Build histogram and LUT for each tile */
    for (cvcl_i32 ty = 0; ty < ny; ty++) {
        for (cvcl_i32 tx = 0; tx < nx; tx++) {
            cvcl_u32 hist[256] = {0};
            cvcl_i32 x0 = tx * tile_w, y0 = ty * tile_h;
            cvcl_i32 x1 = CVCL_MIN(x0 + tile_w, w);
            cvcl_i32 y1 = CVCL_MIN(y0 + tile_h, h);
            cvcl_i32 n  = (x1-x0) * (y1-y0);

            for (cvcl_i32 y = y0; y < y1; y++) {
                const cvcl_u8 *row = cvcl_image_row(src, y);
                for (cvcl_i32 x = x0; x < x1; x++)
                    hist[row[x]]++;
            }

            cvcl_u8 *lut = luts + (ty*nx+tx)*256;
            clahe_lut(lut, hist, n, clip_limit);
        }
    }

    /* Apply with bilinear interpolation between tile LUTs */
    for (cvcl_i32 y = 0; y < h; y++) {
        const cvcl_u8 *sr = cvcl_image_row(src, y);
        cvcl_u8 *dr = cvcl_image_row(dst, y);

        /* Which tile row, and fractional position within it */
        cvcl_f32 fy = ((cvcl_f32)y + 0.5f) / tile_h - 0.5f;
        cvcl_i32 ty0 = (cvcl_i32)fy;
        cvcl_i32 ty1 = ty0 + 1;
        cvcl_f32 ay  = fy - ty0;
        ty0 = CVCL_CLAMP(ty0, 0, ny-1);
        ty1 = CVCL_CLAMP(ty1, 0, ny-1);

        for (cvcl_i32 x = 0; x < w; x++) {
            cvcl_u8 pix = sr[x];

            cvcl_f32 fx = ((cvcl_f32)x + 0.5f) / tile_w - 0.5f;
            cvcl_i32 tx0 = (cvcl_i32)fx;
            cvcl_i32 tx1 = tx0 + 1;
            cvcl_f32 ax  = fx - tx0;
            tx0 = CVCL_CLAMP(tx0, 0, nx-1);
            tx1 = CVCL_CLAMP(tx1, 0, nx-1);

            cvcl_f32 v00 = luts[(ty0*nx+tx0)*256+pix];
            cvcl_f32 v10 = luts[(ty0*nx+tx1)*256+pix];
            cvcl_f32 v01 = luts[(ty1*nx+tx0)*256+pix];
            cvcl_f32 v11 = luts[(ty1*nx+tx1)*256+pix];

            cvcl_f32 v = (v00*(1-ax) + v10*ax) * (1-ay)
                       + (v01*(1-ax) + v11*ax) *    ay;

            dr[x] = (cvcl_u8)CVCL_CLAMP((cvcl_i32)(v+0.5f), 0, 255);
        }
    }

    cvcl_free(a, luts);
    return CVCL_OK;
}
