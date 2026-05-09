/**
 * @file pixel_hist.c
 * @brief Optimized histogram, Otsu threshold, equalization, fill
 *
 * Optimization layers:
 *
 *  1. Histogram row pointer cached outside x-loop -- eliminates
 *     stride multiply per pixel. Previously called cvcl_image_row
 *     inside the x loop.
 *
 *  2. Otsu single-pass -- computes sum and histogram in one loop
 *     instead of two separate passes over the image.
 *
 *  3. Threshold loop -- caches src/dst row pointers outside x-loop,
 *     eliminating stride multiply. Uses branchless expressions for
 *     BINARY mode so the compiler can auto-vectorize.
 *
 *  4. cvcl_fill -- single-channel uses CVCL_MEMSET(one instruction on
 *     modern CPUs). Multi-channel uses a pattern-repeat strategy:
 *     build one row with memset+pattern, then memcpy remaining rows.
 *
 *  5. restrict on row pointers -- lets the compiler prove src and dst
 *     rows never alias and generate wider SIMD loads.
 *
 *  6. Histogram equalization -- LUT built once, applied in a single
 *     row-cached pass with branchless clamp.
 */

#include <cvcl/cvcl_platform.h>
#include <cvcl/cvcl_pixel.h>
#include <cvcl/cvcl_image.h>

/* -------------------------------------------------------------------------
 * cvcl_fill
 * ---------------------------------------------------------------------- */
cvcl_result_t cvcl_fill(cvcl_image_t *img, cvcl_u8 value) {
    CVCL_CHECK_NULL(img);
    CVCL_CHECK_NULL(img->data);
    CVCL_CHECK_ARG(img->depth == CVCL_DEPTH_U8);

    if (img->channels == 1) {
        /* Single channel: memset covers the whole buffer including stride padding */
        CVCL_MEMSET(img->data, value, cvcl_image_buffer_size(img));
        return CVCL_OK;
    }

    /* Multi-channel: build pixel pattern then replicate row-by-row */

    /* Fill first row with repeated pixel pattern */
    cvcl_u8 *row0 = cvcl_image_row(img, 0);
    cvcl_i32 row_bytes = img->width * img->channels;
    for (cvcl_i32 x = 0; x < img->width; x++)
        for (cvcl_i32 c = 0; c < img->channels; c++)
            row0[x * img->channels + c] = value;

    /* Copy first row to all remaining rows */
    for (cvcl_i32 y = 1; y < img->height; y++)
        CVCL_MEMCPY(cvcl_image_row(img, y), row0, (cvcl_size)row_bytes);

    return CVCL_OK;
}

/* -------------------------------------------------------------------------
 * Histogram -- row pointer cached, unrolled 4x for single-channel
 * ---------------------------------------------------------------------- */
cvcl_result_t cvcl_histogram(const cvcl_image_t *img, cvcl_u32 *hist) {
    CVCL_CHECK_NULL(img);
    CVCL_CHECK_NULL(img->data);
    CVCL_CHECK_NULL(hist);
    CVCL_CHECK_ARG(img->depth == CVCL_DEPTH_U8);

    cvcl_i32 ch = img->channels;
    CVCL_MEMSET(hist, 0, (cvcl_size)ch * 256 * sizeof(cvcl_u32));

    if (ch == 1) {
        /* Single channel -- tight loop, compiler can auto-vectorize */
        for (cvcl_i32 y = 0; y < img->height; y++) {
            const cvcl_u8 * CVCL_RESTRICT row = cvcl_image_row(img, y);
            cvcl_i32 w = img->width, x = 0;
            /* Unroll 4x to reduce loop overhead */
            for (; x <= w - 4; x += 4) {
                hist[row[x+0]]++;
                hist[row[x+1]]++;
                hist[row[x+2]]++;
                hist[row[x+3]]++;
            }
            for (; x < w; x++) hist[row[x]]++;
        }
    } else {
        /* Multi-channel -- separate histogram per channel */
        for (cvcl_i32 y = 0; y < img->height; y++) {
            const cvcl_u8 * CVCL_RESTRICT row = cvcl_image_row(img, y);
            for (cvcl_i32 x = 0; x < img->width; x++) {
                const cvcl_u8 *px = row + x * ch;
                for (cvcl_i32 c = 0; c < ch; c++)
                    hist[(cvcl_size)c * 256 + px[c]]++;
            }
        }
    }
    return CVCL_OK;
}

/* -------------------------------------------------------------------------
 * Otsu -- single pass: build histogram and compute threshold together
 * ---------------------------------------------------------------------- */
static cvcl_f32 otsu_threshold(const cvcl_image_t *img) {
    cvcl_u32 hist[256] = {0};
    cvcl_i32 total = img->width * img->height;

    /* Single-pass histogram -- row pointer cached */
    for (cvcl_i32 y = 0; y < img->height; y++) {
        const cvcl_u8 * CVCL_RESTRICT row = cvcl_image_row(img, y);
        cvcl_i32 w = img->width, x = 0;
        for (; x <= w - 4; x += 4) {
            hist[row[x+0]]++;
            hist[row[x+1]]++;
            hist[row[x+2]]++;
            hist[row[x+3]]++;
        }
        for (; x < w; x++) hist[row[x]]++;
    }

    /* Compute global intensity sum */
    cvcl_f32 sum_total = 0.f;
    for (cvcl_i32 i = 0; i < 256; i++)
        sum_total += (cvcl_f32)i * (cvcl_f32)hist[i];

    /* Scan thresholds -- O(256) regardless of image size */
    cvcl_f32 sum_bg = 0.f, max_var = 0.f, best_t = 0.f;
    cvcl_i32 w_bg   = 0;

    for (cvcl_i32 t = 0; t < 256; t++) {
        w_bg   += (cvcl_i32)hist[t];
        if (w_bg == 0) continue;
        cvcl_i32 w_fg = total - w_bg;
        if (w_fg == 0) break;

        sum_bg += (cvcl_f32)t * (cvcl_f32)hist[t];
        cvcl_f32 mean_bg = sum_bg / (cvcl_f32)w_bg;
        cvcl_f32 mean_fg = (sum_total - sum_bg) / (cvcl_f32)w_fg;
        cvcl_f32 diff    = mean_bg - mean_fg;
        cvcl_f32 var     = (cvcl_f32)w_bg * (cvcl_f32)w_fg * diff * diff;

        if (var > max_var) { max_var = var; best_t = (cvcl_f32)t; }
    }
    return best_t;
}

/* -------------------------------------------------------------------------
 * Threshold -- row-cached, branchless BINARY for auto-vectorization
 * ---------------------------------------------------------------------- */
cvcl_result_t cvcl_threshold(cvcl_image_t       *dst,
                              const cvcl_image_t *src,
                              cvcl_f32            thresh,
                              cvcl_f32            max_val,
                              cvcl_thresh_type_t  type,
                              cvcl_f32           *out_thresh) {
    CVCL_CHECK_NULL(dst); CVCL_CHECK_NULL(src);
    CVCL_CHECK_NULL(dst->data); CVCL_CHECK_NULL(src->data);
    CVCL_CHECK_ARG(src->depth    == CVCL_DEPTH_U8);
    CVCL_CHECK_ARG(src->channels == 1);
    CVCL_CHECK_ARG(dst->width  == src->width);
    CVCL_CHECK_ARG(dst->height == src->height);
    CVCL_CHECK_ARG(dst->channels == 1);
    CVCL_CHECK_ARG(dst->depth == CVCL_DEPTH_U8);

    if (type == CVCL_THRESH_OTSU) {
        thresh = otsu_threshold(src);
        type   = CVCL_THRESH_BINARY;
    }
    if (out_thresh) *out_thresh = thresh;

    cvcl_u8 t  = (cvcl_u8)CVCL_CLAMP((cvcl_i32)thresh,  0, 255);
    cvcl_u8 mv = (cvcl_u8)CVCL_CLAMP((cvcl_i32)max_val, 0, 255);
    cvcl_i32 w = src->width, h = src->height;

    for (cvcl_i32 y = 0; y < h; y++) {
        const cvcl_u8 * CVCL_RESTRICT sr = cvcl_image_row(src, y);
        cvcl_u8       * CVCL_RESTRICT dr = cvcl_image_row(dst, y);

        switch (type) {
            case CVCL_THRESH_BINARY:
                /* Branchless: compiler can emit pcmpgtb/vblendvb */
                for (cvcl_i32 x = 0; x < w; x++)
                    dr[x] = (sr[x] > t) ? mv : 0;
                break;
            case CVCL_THRESH_BINARY_INV:
                for (cvcl_i32 x = 0; x < w; x++)
                    dr[x] = (sr[x] > t) ? 0 : mv;
                break;
            case CVCL_THRESH_TRUNC:
                for (cvcl_i32 x = 0; x < w; x++)
                    dr[x] = (sr[x] > t) ? t : sr[x];
                break;
            case CVCL_THRESH_TOZERO:
                for (cvcl_i32 x = 0; x < w; x++)
                    dr[x] = (sr[x] > t) ? sr[x] : 0;
                break;
            default:
                for (cvcl_i32 x = 0; x < w; x++) dr[x] = sr[x];
                break;
        }
    }
    return CVCL_OK;
}

/* -------------------------------------------------------------------------
 * Histogram equalization -- LUT-based single pass
 * ---------------------------------------------------------------------- */
cvcl_result_t cvcl_equalize_hist(cvcl_image_t *img) {
    CVCL_CHECK_NULL(img);
    CVCL_CHECK_NULL(img->data);
    CVCL_CHECK_ARG(img->depth    == CVCL_DEPTH_U8);
    CVCL_CHECK_ARG(img->channels == 1);

    /* Build histogram -- single pass with cached row pointers */
    cvcl_u32 hist[256] = {0};
    cvcl_i32 total = img->width * img->height;

    for (cvcl_i32 y = 0; y < img->height; y++) {
        const cvcl_u8 * CVCL_RESTRICT row = cvcl_image_row(img, y);
        for (cvcl_i32 x = 0; x < img->width; x++) hist[row[x]]++;
    }

    /* Build CDF lookup table -- O(256) */
    cvcl_u8  lut[256];
    cvcl_u32 cdf = 0, cdf_min = 0;

    for (cvcl_i32 i = 0; i < 256; i++)
        if (hist[i]) { cdf_min = hist[i]; break; }

    for (cvcl_i32 i = 0; i < 256; i++) {
        cdf += hist[i];
        cvcl_i32 denom = total - (cvcl_i32)cdf_min;
        if (denom <= 0) { lut[i] = 0; continue; }
        cvcl_i32 v = (cvcl_i32)(
            ((cvcl_f32)(cdf - cdf_min) / (cvcl_f32)denom) * 255.f + 0.5f);
        lut[i] = (cvcl_u8)CVCL_CLAMP(v, 0, 255);
    }

    /* Apply LUT in-place -- row pointer cached, compiler vectorizes */
    for (cvcl_i32 y = 0; y < img->height; y++) {
        cvcl_u8 * CVCL_RESTRICT row = cvcl_image_row(img, y);
        for (cvcl_i32 x = 0; x < img->width; x++)
            row[x] = lut[row[x]];
    }
    return CVCL_OK;
}

/* -------------------------------------------------------------------------
 * Bilinear sample -- bounds-safe fractional coordinate access
 * ---------------------------------------------------------------------- */
cvcl_f32 cvcl_sample_bilinear(const cvcl_image_t *img,
                               cvcl_f32 x, cvcl_f32 y,
                               cvcl_i32 c,
                               cvcl_border_t border) {
    cvcl_i32 x0 = (cvcl_i32)x, y0 = (cvcl_i32)y;
    cvcl_i32 x1 = x0 + 1, y1 = y0 + 1;
    cvcl_f32 xf = x - (cvcl_f32)x0, yf = y - (cvcl_f32)y0;

    #define SC(xi,yi) CVCL_CLAMP((xi), 0, img->width -1), \
                      CVCL_CLAMP((yi), 0, img->height-1)

    cvcl_f32 p00, p10, p01, p11;
    CVCL_UNUSED(border);

    if (img->depth == CVCL_DEPTH_U8) {
        p00 = (cvcl_f32)cvcl_get_u8(img, SC(x0,y0), c);
        p10 = (cvcl_f32)cvcl_get_u8(img, SC(x1,y0), c);
        p01 = (cvcl_f32)cvcl_get_u8(img, SC(x0,y1), c);
        p11 = (cvcl_f32)cvcl_get_u8(img, SC(x1,y1), c);
    } else {
        p00 = cvcl_get_f32(img, SC(x0,y0), c);
        p10 = cvcl_get_f32(img, SC(x1,y0), c);
        p01 = cvcl_get_f32(img, SC(x0,y1), c);
        p11 = cvcl_get_f32(img, SC(x1,y1), c);
    }
    #undef SC

    return (p00 + (p10-p00)*xf) + ((p01 + (p11-p01)*xf) - (p00 + (p10-p00)*xf))*yf;
}
