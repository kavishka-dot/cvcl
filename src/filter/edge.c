/**
 * @file filter_edge.c
 * @brief Optimized Sobel gradient and Canny edge detector
 *
 * Optimization layers applied:
 *
 *  1. Integer Sobel              -- weights are -2,-1,0,1,2 so no float
 *                                   arithmetic needed in the inner loop.
 *  2. Interior/border split      -- eliminates branch per pixel for 90%+
 *                                   of pixels (same pattern as convolve.c).
 *  3. Row pointer caching        -- computes y*stride once per kernel row.
 *  4. atan2f removal             -- replaced with integer ratio comparisons.
 *                                   Eliminates the most expensive trig call.
 *  5. L1 gradient magnitude      -- |gx|+|gy| replaces sqrtf(gx²+gy²).
 *                                   ~3x faster, sufficient for NMS/threshold.
 *  6. Iterative hysteresis DFS   -- replaces recursive DFS with an explicit
 *                                   stack. Safe on embedded targets with
 *                                   limited stack depth.
 *  7. restrict pointers          -- enables auto-vectorization.
 */

#include <cvcl/cvcl_filter.h>
#include <cvcl/cvcl_pixel.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>

/* -------------------------------------------------------------------------
 * Sobel kernels as integers (exact, no float needed)
 *
 *  Kx = [-1  0  1]    Ky = [-1 -2 -1]
 *       [-2  0  2]         [ 0  0  0]
 *       [-1  0  1]         [ 1  2  1]
 * ---------------------------------------------------------------------- */
static const cvcl_i32 SOBEL_KX_I[9] = { -1,0,1, -2,0,2, -1,0,1 };
static const cvcl_i32 SOBEL_KY_I[9] = { -1,-2,-1, 0,0,0, 1,2,1 };

/* -------------------------------------------------------------------------
 * Border-safe fetch -- replicate (used only for border pixels)
 * ---------------------------------------------------------------------- */
CVCL_INLINE cvcl_i32 fetch_rep(
        const cvcl_u8 * CVCL_RESTRICT data,
        cvcl_i32 x, cvcl_i32 y,
        cvcl_i32 w, cvcl_i32 h, cvcl_i32 stride) {
    x = CVCL_CLAMP(x, 0, w-1);
    y = CVCL_CLAMP(y, 0, h-1);
    return (cvcl_i32)data[(cvcl_size)y * stride + x];
}

/* -------------------------------------------------------------------------
 * Integer Sobel pass -- interior fast path (no border check)
 *
 * Processes the interior rectangle [1, w-1) x [1, h-1) with zero
 * branching. Row pointers are cached once per kernel row.
 * ---------------------------------------------------------------------- */
static void sobel_interior(
        cvcl_i32       * CVCL_RESTRICT out,
        const cvcl_u8  * CVCL_RESTRICT data,
        cvcl_i32 w, cvcl_i32 h, cvcl_i32 stride,
        const cvcl_i32 *kern) {

    for (cvcl_i32 y = 1; y < h-1; y++) {
        /* Cache three row pointers -- reused across entire row */
        const cvcl_u8 * CVCL_RESTRICT r0 = data + (cvcl_size)(y-1) * stride;
        const cvcl_u8 * CVCL_RESTRICT r1 = data + (cvcl_size)(y  ) * stride;
        const cvcl_u8 * CVCL_RESTRICT r2 = data + (cvcl_size)(y+1) * stride;
        cvcl_i32 * CVCL_RESTRICT outrow  = out  + (cvcl_size) y    * w;

        for (cvcl_i32 x = 1; x < w-1; x++) {
            outrow[x] =
                kern[0] * r0[x-1] + kern[1] * r0[x] + kern[2] * r0[x+1] +
                kern[3] * r1[x-1] + kern[4] * r1[x] + kern[5] * r1[x+1] +
                kern[6] * r2[x-1] + kern[7] * r2[x] + kern[8] * r2[x+1];
        }
    }
}

/* Border ring (top, bottom, left, right edges) */
static void sobel_border(
        cvcl_i32       * CVCL_RESTRICT out,
        const cvcl_u8  * CVCL_RESTRICT data,
        cvcl_i32 w, cvcl_i32 h, cvcl_i32 stride,
        const cvcl_i32 *kern) {

    /* Top and bottom rows */
    for (cvcl_i32 y = 0; y < h; y += (y == 0 ? 1 : h-1 > 0 ? h-1 : 1)) {
        for (cvcl_i32 x = 0; x < w; x++) {
            cvcl_i32 acc = 0;
            for (cvcl_i32 ky = -1; ky <= 1; ky++)
                for (cvcl_i32 kx = -1; kx <= 1; kx++)
                    acc += kern[(ky+1)*3+(kx+1)]
                         * fetch_rep(data, x+kx, y+ky, w, h, stride);
            out[(cvcl_size)y*w+x] = acc;
        }
        if (y == h-1) break;
    }

    /* Left and right columns (skip corners already done) */
    for (cvcl_i32 y = 1; y < h-1; y++) {
        for (cvcl_i32 x = 0; x < w; x += (x == 0 ? 1 : w-1 > 0 ? w-1 : 1)) {
            cvcl_i32 acc = 0;
            for (cvcl_i32 ky = -1; ky <= 1; ky++)
                for (cvcl_i32 kx = -1; kx <= 1; kx++)
                    acc += kern[(ky+1)*3+(kx+1)]
                         * fetch_rep(data, x+kx, y+ky, w, h, stride);
            out[(cvcl_size)y*w+x] = acc;
            if (x == w-1) break;
        }
    }
}

/* Full Sobel pass combining interior + border */
static cvcl_result_t sobel_pass(
        cvcl_i32       * CVCL_RESTRICT out,
        const cvcl_image_t * CVCL_RESTRICT src,
        const cvcl_i32 *kern) {
    cvcl_i32 w = src->width, h = src->height;
    sobel_interior(out, src->data, w, h, src->stride, kern);
    sobel_border  (out, src->data, w, h, src->stride, kern);
    return CVCL_OK;
}

/* -------------------------------------------------------------------------
 * Public: Sobel X  (output F32 for API compatibility)
 * ---------------------------------------------------------------------- */
cvcl_result_t cvcl_sobel_x(cvcl_image_t       *dst,
                            const cvcl_image_t *src,
                            cvcl_border_t       border) {
    CVCL_CHECK_NULL(dst); CVCL_CHECK_NULL(src);
    CVCL_CHECK_NULL(dst->data); CVCL_CHECK_NULL(src->data);
    CVCL_CHECK_ARG(src->channels == 1);
    CVCL_CHECK_ARG(src->depth == CVCL_DEPTH_U8);
    CVCL_CHECK_ARG(dst->depth == CVCL_DEPTH_F32);
    CVCL_CHECK_ARG(dst->width == src->width && dst->height == src->height);
    CVCL_UNUSED(border); /* uses replicate internally */

    cvcl_i32 w = src->width, h = src->height;
    cvcl_i32 *tmp = (cvcl_i32 *)malloc((cvcl_size)w * h * sizeof(cvcl_i32));
    if (!tmp) return CVCL_ERR_ALLOC;

    sobel_pass(tmp, src, SOBEL_KX_I);

    for (cvcl_i32 y = 0; y < h; y++)
        for (cvcl_i32 x = 0; x < w; x++)
            cvcl_set_f32(dst, x, y, 0, (cvcl_f32)tmp[(cvcl_size)y*w+x]);

    free(tmp);
    return CVCL_OK;
}

/* -------------------------------------------------------------------------
 * Public: Sobel Y
 * ---------------------------------------------------------------------- */
cvcl_result_t cvcl_sobel_y(cvcl_image_t       *dst,
                            const cvcl_image_t *src,
                            cvcl_border_t       border) {
    CVCL_CHECK_NULL(dst); CVCL_CHECK_NULL(src);
    CVCL_CHECK_NULL(dst->data); CVCL_CHECK_NULL(src->data);
    CVCL_CHECK_ARG(src->channels == 1);
    CVCL_CHECK_ARG(src->depth == CVCL_DEPTH_U8);
    CVCL_CHECK_ARG(dst->depth == CVCL_DEPTH_F32);
    CVCL_CHECK_ARG(dst->width == src->width && dst->height == src->height);
    CVCL_UNUSED(border);

    cvcl_i32 w = src->width, h = src->height;
    cvcl_i32 *tmp = (cvcl_i32 *)malloc((cvcl_size)w * h * sizeof(cvcl_i32));
    if (!tmp) return CVCL_ERR_ALLOC;

    sobel_pass(tmp, src, SOBEL_KY_I);

    for (cvcl_i32 y = 0; y < h; y++)
        for (cvcl_i32 x = 0; x < w; x++)
            cvcl_set_f32(dst, x, y, 0, (cvcl_f32)tmp[(cvcl_size)y*w+x]);

    free(tmp);
    return CVCL_OK;
}

/* -------------------------------------------------------------------------
 * Gradient direction -- integer ratio, no atan2f
 *
 * Uses the tangent of the quantization boundaries:
 *   tan(22.5°) ≈ 0.4142  →  multiply to avoid float division
 *
 * All comparisons done in integer arithmetic.
 * ---------------------------------------------------------------------- */
CVCL_INLINE cvcl_u8 quantize_angle_int(cvcl_i32 gx, cvcl_i32 gy) {
    cvcl_i32 agx = gx < 0 ? -gx : gx;
    cvcl_i32 agy = gy < 0 ? -gy : gy;

    /* tan(22.5°) ≈ 2/5 -- use integer multiply to avoid float */
    /* agy < agx * tan(22.5) --> agy*5 < agx*2 */
    if (agy * 5 < agx * 2)  return 0;   /* ~horizontal */
    if (agx * 5 < agy * 2)  return 90;  /* ~vertical   */
    /* Diagonal: sign of gx*gy determines which diagonal */
    return (gx * gy > 0) ? 45 : 135;
}

/* -------------------------------------------------------------------------
 * NMS -- non-maximum suppression (integer magnitude)
 * ---------------------------------------------------------------------- */
static void nms_int(cvcl_i32       * CVCL_RESTRICT mag_nms,
                    const cvcl_i32 * CVCL_RESTRICT mag,
                    const cvcl_u8  * CVCL_RESTRICT dir,
                    cvcl_i32 w, cvcl_i32 h) {
    /* Zero border */
    memset(mag_nms, 0, (cvcl_size)w * h * sizeof(cvcl_i32));

    for (cvcl_i32 y = 1; y < h-1; y++) {
        const cvcl_i32 * CVCL_RESTRICT mrow = mag + (cvcl_size)y * w;
        cvcl_i32       * CVCL_RESTRICT nrow = mag_nms + (cvcl_size)y * w;
        const cvcl_u8  * CVCL_RESTRICT drow = dir + (cvcl_size)y * w;

        for (cvcl_i32 x = 1; x < w-1; x++) {
            cvcl_i32 m = mrow[x];
            cvcl_i32 a, b;
            switch (drow[x]) {
                case 0:
                    a = mrow[x-1]; b = mrow[x+1]; break;
                case 45:
                    a = mag[(cvcl_size)(y-1)*w+(x+1)];
                    b = mag[(cvcl_size)(y+1)*w+(x-1)]; break;
                case 90:
                    a = mag[(cvcl_size)(y-1)*w+x];
                    b = mag[(cvcl_size)(y+1)*w+x]; break;
                default: /* 135 */
                    a = mag[(cvcl_size)(y-1)*w+(x-1)];
                    b = mag[(cvcl_size)(y+1)*w+(x+1)]; break;
            }
            nrow[x] = (m >= a && m >= b) ? m : 0;
        }
    }
}

/* -------------------------------------------------------------------------
 * Iterative hysteresis DFS -- explicit stack, no recursion
 *
 * Replaces the recursive version which risks stack overflow on large images
 * with deep edge chains (e.g., a 4K image with a long diagonal edge).
 * ---------------------------------------------------------------------- */
typedef struct { cvcl_i32 x, y; } cvcl_pt_t;

static cvcl_result_t hysteresis_iterative(
        cvcl_u8        * CVCL_RESTRICT edges,
        const cvcl_i32 * CVCL_RESTRICT mag_nms,
        cvcl_i32 w, cvcl_i32 h,
        cvcl_i32 stride,
        cvcl_i32 lo_thresh, cvcl_i32 hi_thresh) {

    static const cvcl_i32 dx[8] = {-1,0,1,-1,1,-1,0,1};
    static const cvcl_i32 dy[8] = {-1,-1,-1,0,0,1,1,1};

    /* Stack: worst case w*h pixels (fully connected edge image) */
    cvcl_size cap = (cvcl_size)w * h;
    cvcl_pt_t *stk = (cvcl_pt_t *)malloc(cap * sizeof(cvcl_pt_t));
    if (!stk) return CVCL_ERR_ALLOC;

    /* Mark strong edges, zero weak */
    for (cvcl_i32 y = 0; y < h; y++) {
        cvcl_u8       *erow = edges + (cvcl_size)y * stride;
        const cvcl_i32 *mrow = mag_nms + (cvcl_size)y * w;
        for (cvcl_i32 x = 0; x < w; x++)
            erow[x] = (mrow[x] >= hi_thresh) ? 255 : 0;
    }

    /* Iterative DFS from every strong edge pixel */
    for (cvcl_i32 y = 1; y < h-1; y++) {
        cvcl_u8 *erow = edges + (cvcl_size)y * stride;
        for (cvcl_i32 x = 1; x < w-1; x++) {
            if (erow[x] != 255) continue;

            cvcl_size top = 0;
            stk[top].x = x; stk[top].y = y; top++;

            while (top > 0) {
                cvcl_pt_t p = stk[--top];
                for (cvcl_i32 k = 0; k < 8; k++) {
                    cvcl_i32 nx = p.x + dx[k];
                    cvcl_i32 ny = p.y + dy[k];
                    if (nx < 0 || nx >= w || ny < 0 || ny >= h) continue;
                    cvcl_u8 *ne = edges + (cvcl_size)ny * stride + nx;
                    if (*ne == 0 &&
                        mag_nms[(cvcl_size)ny*w+nx] >= lo_thresh) {
                        *ne = 255;
                        if (top < cap) {
                            stk[top].x = nx;
                            stk[top].y = ny;
                            top++;
                        }
                    }
                }
            }
        }
    }

    free(stk);
    return CVCL_OK;
}

/* -------------------------------------------------------------------------
 * Public: Canny edge detector
 * ---------------------------------------------------------------------- */
cvcl_result_t cvcl_canny(cvcl_image_t          *dst,
                          const cvcl_image_t    *src,
                          cvcl_f32               lo_thresh,
                          cvcl_f32               hi_thresh,
                          const cvcl_allocator_t *alloc) {
    CVCL_CHECK_NULL(dst); CVCL_CHECK_NULL(src);
    CVCL_CHECK_NULL(src->data);
    CVCL_CHECK_ARG(src->depth    == CVCL_DEPTH_U8);
    CVCL_CHECK_ARG(src->channels == 1);
    CVCL_CHECK_ARG(lo_thresh >= 0.f && hi_thresh > lo_thresh);

    cvcl_i32 w = src->width, h = src->height;
    cvcl_size n = (cvcl_size)w * h;

    /* Step 1: Gaussian blur */
    cvcl_image_t blurred;
    CVCL_CHECK(cvcl_image_create(&blurred, w, h, 1, CVCL_DEPTH_U8, alloc));
    cvcl_result_t rc = cvcl_blur_gaussian(&blurred, src, 5, 1.4f,
                                           CVCL_BORDER_REPLICATE);
    if (rc != CVCL_OK) { cvcl_image_free(&blurred, alloc); return rc; }

    /* Step 2: Integer Sobel -- gx, gy as int32 */
    cvcl_i32 *igx = (cvcl_i32 *)malloc(n * sizeof(cvcl_i32));
    cvcl_i32 *igy = (cvcl_i32 *)malloc(n * sizeof(cvcl_i32));
    cvcl_i32 *mag = (cvcl_i32 *)malloc(n * sizeof(cvcl_i32));
    cvcl_u8  *dir = (cvcl_u8  *)malloc(n);

    if (!igx || !igy || !mag || !dir) {
        free(igx); free(igy); free(mag); free(dir);
        cvcl_image_free(&blurred, alloc);
        return CVCL_ERR_ALLOC;
    }

    sobel_pass(igx, &blurred, SOBEL_KX_I);
    sobel_pass(igy, &blurred, SOBEL_KY_I);
    cvcl_image_free(&blurred, alloc);

    /* L1 magnitude + integer angle quantization -- no sqrtf, no atan2f */
    for (cvcl_size i = 0; i < n; i++) {
        cvcl_i32 gx = igx[i], gy = igy[i];
        mag[i] = (gx < 0 ? -gx : gx) + (gy < 0 ? -gy : gy);
        dir[i] = quantize_angle_int(gx, gy);
    }
    free(igx); free(igy);

    /* Step 3: NMS */
    cvcl_i32 *mag_nms = (cvcl_i32 *)malloc(n * sizeof(cvcl_i32));
    if (!mag_nms) { free(mag); free(dir); return CVCL_ERR_ALLOC; }
    nms_int(mag_nms, mag, dir, w, h);
    free(mag); free(dir);

    /* Step 4: Allocate output */
    rc = cvcl_image_create(dst, w, h, 1, CVCL_DEPTH_U8, alloc);
    if (rc != CVCL_OK) { free(mag_nms); return rc; }

    /* Scale thresholds: L1 Sobel max = 4*(255) = 1020 */
    /* Input thresholds are 0-255 range, scale to L1 space */
    cvcl_i32 lo_i = (cvcl_i32)(lo_thresh * 4.f);
    cvcl_i32 hi_i = (cvcl_i32)(hi_thresh * 4.f);

    /* Step 5: Iterative hysteresis */
    rc = hysteresis_iterative(dst->data, mag_nms,
                               w, h, dst->stride, lo_i, hi_i);
    free(mag_nms);
    return rc;
}
