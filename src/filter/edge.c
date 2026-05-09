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
 *  5. L1 gradient magnitude      -- |gx|+|gy| replaces CVCL_SQRTF(gx²+gy²).
 *                                   ~3x faster, sufficient for NMS/threshold.
 *  6. Iterative hysteresis DFS   -- replaces recursive DFS with an explicit
 *                                   stack. Safe on embedded targets with
 *                                   limited stack depth.
 *  7. restrict pointers          -- enables auto-vectorization.
 */

#include <cvcl/cvcl_platform.h>
#include <cvcl/cvcl_filter.h>
#include <cvcl/cvcl_pixel.h>

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

    /* Top row (y=0) and bottom row (y=h-1) */
    for (cvcl_i32 pass = 0; pass < 2; pass++) {
        cvcl_i32 y = (pass == 0) ? 0 : h - 1;
        for (cvcl_i32 x = 0; x < w; x++) {
            cvcl_i32 acc = 0;
            for (cvcl_i32 ky = -1; ky <= 1; ky++)
                for (cvcl_i32 kx = -1; kx <= 1; kx++)
                    acc += kern[(ky+1)*3+(kx+1)]
                         * fetch_rep(data, x+kx, y+ky, w, h, stride);
            out[(cvcl_size)y*w+x] = acc;
        }
    }

    /* Left column (x=0) and right column (x=w-1), interior rows only */
    for (cvcl_i32 y = 1; y < h-1; y++) {
        for (cvcl_i32 pass = 0; pass < 2; pass++) {
            cvcl_i32 x = (pass == 0) ? 0 : w - 1;
            cvcl_i32 acc = 0;
            for (cvcl_i32 ky = -1; ky <= 1; ky++)
                for (cvcl_i32 kx = -1; kx <= 1; kx++)
                    acc += kern[(ky+1)*3+(kx+1)]
                         * fetch_rep(data, x+kx, y+ky, w, h, stride);
            out[(cvcl_size)y*w+x] = acc;
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
    const cvcl_allocator_t *a = cvcl_image_allocator(dst);
    cvcl_i32 *tmp = (cvcl_i32 *)cvcl_alloc(a, (cvcl_size)w * h * sizeof(cvcl_i32));
    if (!tmp) return CVCL_ERR_ALLOC;

    sobel_pass(tmp, src, SOBEL_KX_I);

    for (cvcl_i32 y = 0; y < h; y++)
        for (cvcl_i32 x = 0; x < w; x++)
            cvcl_set_f32(dst, x, y, 0, (cvcl_f32)tmp[(cvcl_size)y*w+x]);

    cvcl_free(a, tmp);
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
    const cvcl_allocator_t *a = cvcl_image_allocator(dst);
    cvcl_i32 *tmp = (cvcl_i32 *)cvcl_alloc(a, (cvcl_size)w * h * sizeof(cvcl_i32));
    if (!tmp) return CVCL_ERR_ALLOC;

    sobel_pass(tmp, src, SOBEL_KY_I);

    for (cvcl_i32 y = 0; y < h; y++)
        for (cvcl_i32 x = 0; x < w; x++)
            cvcl_set_f32(dst, x, y, 0, (cvcl_f32)tmp[(cvcl_size)y*w+x]);

    cvcl_free(a, tmp);
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
    cvcl_i64 agx = gx < 0 ? -(cvcl_i64)gx : (cvcl_i64)gx;
    cvcl_i64 agy = gy < 0 ? -(cvcl_i64)gy : (cvcl_i64)gy;

    /* tan(22.5deg) ~= 2/5 -- integer multiply in 64-bit to avoid overflow */
    if (agy * 5 < agx * 2)  return 0;    /* ~horizontal */
    if (agx * 5 < agy * 2)  return 90;   /* ~vertical   */
    /* Diagonal: sign of gx*gy determines which diagonal */
    return ((cvcl_i64)gx * (cvcl_i64)gy > 0) ? 45 : 135;
}

/* -------------------------------------------------------------------------
 * Combined Sobel X/Y, Magnitude, and Direction Pass for Canny
 * ---------------------------------------------------------------------- */
static void sobel_mag_dir_interior(
        cvcl_i32       * CVCL_RESTRICT mag,
        cvcl_u8        * CVCL_RESTRICT dir,
        const cvcl_u8  * CVCL_RESTRICT data,
        cvcl_i32 w, cvcl_i32 h, cvcl_i32 stride) {

    for (cvcl_i32 y = 1; y < h-1; y++) {
        const cvcl_u8 * CVCL_RESTRICT r0 = data + (cvcl_size)(y-1) * stride;
        const cvcl_u8 * CVCL_RESTRICT r1 = data + (cvcl_size)(y  ) * stride;
        const cvcl_u8 * CVCL_RESTRICT r2 = data + (cvcl_size)(y+1) * stride;
        cvcl_i32 * CVCL_RESTRICT mrow  = mag + (cvcl_size)y * w;
        cvcl_u8  * CVCL_RESTRICT drow  = dir + (cvcl_size)y * w;

        for (cvcl_i32 x = 1; x < w-1; x++) {
            cvcl_i32 gx =
                -1 * r0[x-1] + 1 * r0[x+1] +
                -2 * r1[x-1] + 2 * r1[x+1] +
                -1 * r2[x-1] + 1 * r2[x+1];
            cvcl_i32 gy =
                -1 * r0[x-1] - 2 * r0[x] - 1 * r0[x+1] +
                 1 * r2[x-1] + 2 * r2[x] + 1 * r2[x+1];
            
            mrow[x] = (gx < 0 ? -gx : gx) + (gy < 0 ? -gy : gy);
            drow[x] = quantize_angle_int(gx, gy);
        }
    }
}

static void sobel_mag_dir_border(
        cvcl_i32       * CVCL_RESTRICT mag,
        cvcl_u8        * CVCL_RESTRICT dir,
        const cvcl_u8  * CVCL_RESTRICT data,
        cvcl_i32 w, cvcl_i32 h, cvcl_i32 stride) {
    const cvcl_i32 kx[9] = { -1,0,1, -2,0,2, -1,0,1 };
    const cvcl_i32 ky[9] = { -1,-2,-1, 0,0,0, 1,2,1 };
    
    /* Top row (y=0) and bottom row (y=h-1) */
    for (cvcl_i32 pass = 0; pass < 2; pass++) {
        cvcl_i32 y = (pass == 0) ? 0 : h - 1;
        for (cvcl_i32 x = 0; x < w; x++) {
            cvcl_i32 gx = 0, gy = 0;
            for (cvcl_i32 dy = -1; dy <= 1; dy++)
                for (cvcl_i32 dx = -1; dx <= 1; dx++) {
                    cvcl_i32 p = fetch_rep(data, x+dx, y+dy, w, h, stride);
                    gx += kx[(dy+1)*3+(dx+1)] * p;
                    gy += ky[(dy+1)*3+(dx+1)] * p;
                }
            mag[(cvcl_size)y*w+x] = (gx < 0 ? -gx : gx) + (gy < 0 ? -gy : gy);
            dir[(cvcl_size)y*w+x] = quantize_angle_int(gx, gy);
        }
    }

    /* Left column (x=0) and right column (x=w-1), interior rows only */
    for (cvcl_i32 y = 1; y < h-1; y++) {
        for (cvcl_i32 pass = 0; pass < 2; pass++) {
            cvcl_i32 x = (pass == 0) ? 0 : w - 1;
            cvcl_i32 gx = 0, gy = 0;
            for (cvcl_i32 dy = -1; dy <= 1; dy++)
                for (cvcl_i32 dx = -1; dx <= 1; dx++) {
                    cvcl_i32 p = fetch_rep(data, x+dx, y+dy, w, h, stride);
                    gx += kx[(dy+1)*3+(dx+1)] * p;
                    gy += ky[(dy+1)*3+(dx+1)] * p;
                }
            mag[(cvcl_size)y*w+x] = (gx < 0 ? -gx : gx) + (gy < 0 ? -gy : gy);
            dir[(cvcl_size)y*w+x] = quantize_angle_int(gx, gy);
        }
    }
}

/* -------------------------------------------------------------------------
 * NMS -- non-maximum suppression (integer magnitude)
 * ---------------------------------------------------------------------- */
static void nms_int(cvcl_i32       * CVCL_RESTRICT mag_nms,
                    const cvcl_i32 * CVCL_RESTRICT mag,
                    const cvcl_u8  * CVCL_RESTRICT dir,
                    cvcl_i32 w, cvcl_i32 h) {
    /* Zero border */
    CVCL_MEMSET(mag_nms, 0, (cvcl_size)w * h * sizeof(cvcl_i32));

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
        cvcl_i32 lo_thresh, cvcl_i32 hi_thresh,
        const cvcl_allocator_t *alloc) {

    static const cvcl_i32 dx[8] = {-1,0,1,-1,1,-1,0,1};
    static const cvcl_i32 dy[8] = {-1,-1,-1,0,0,1,1,1};

    /* Stack: worst case w*h pixels (fully connected edge image) */
    cvcl_size cap = (cvcl_size)w * h;
    cvcl_pt_t *stk = (cvcl_pt_t *)cvcl_alloc(alloc, cap * sizeof(cvcl_pt_t));
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

    cvcl_free(alloc, stk);
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
    const cvcl_allocator_t *a = alloc ? alloc : cvcl_default_allocator();

    /* Step 1: Gaussian blur */
    cvcl_image_t blurred;
    CVCL_CHECK(cvcl_image_create(&blurred, w, h, 1, CVCL_DEPTH_U8, a));
    cvcl_result_t rc = cvcl_blur_gaussian(&blurred, src, 5, 1.4f,
                                           CVCL_BORDER_REPLICATE);
    if (rc != CVCL_OK) { cvcl_image_free(&blurred, a); return rc; }

    /* Step 2: Combined Sobel + Magnitude + Direction */
    cvcl_i32 *mag = (cvcl_i32 *)cvcl_alloc(a, n * sizeof(cvcl_i32));
    cvcl_u8  *dir = (cvcl_u8  *)cvcl_alloc(a, n);

    if (!mag || !dir) {
        cvcl_free(a, mag); cvcl_free(a, dir);
        cvcl_image_free(&blurred, a);
        return CVCL_ERR_ALLOC;
    }

    sobel_mag_dir_interior(mag, dir, blurred.data, w, h, blurred.stride);
    sobel_mag_dir_border(mag, dir, blurred.data, w, h, blurred.stride);
    cvcl_image_free(&blurred, a);

    /* Step 3: NMS */
    cvcl_i32 *mag_nms = (cvcl_i32 *)cvcl_alloc(a, n * sizeof(cvcl_i32));
    if (!mag_nms) { cvcl_free(a, mag); cvcl_free(a, dir); return CVCL_ERR_ALLOC; }
    nms_int(mag_nms, mag, dir, w, h);
    cvcl_free(a, mag); cvcl_free(a, dir);

    /* Step 4: Allocate output */
    rc = cvcl_image_create(dst, w, h, 1, CVCL_DEPTH_U8, a);
    if (rc != CVCL_OK) { cvcl_free(a, mag_nms); return rc; }

    /* Scale thresholds: L1 Sobel max = 4*(255) = 1020 */
    /* Input thresholds are 0-255 range, scale to L1 space */
    cvcl_i32 lo_i = (cvcl_i32)(lo_thresh * 4.f);
    cvcl_i32 hi_i = (cvcl_i32)(hi_thresh * 4.f);

    /* Step 5: Iterative hysteresis */
    rc = hysteresis_iterative(dst->data, mag_nms,
                               w, h, dst->stride, lo_i, hi_i, a);
    cvcl_free(a, mag_nms);
    return rc;
}
