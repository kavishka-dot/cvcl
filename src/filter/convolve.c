/**
 * @file filter_convolve.c
 * @brief Optimized generic 2D convolution and separable convolution
 *
 * Optimization layers applied:
 *
 *  1. restrict pointers         -- enables auto-vectorization, no aliasing
 *  2. Interior/border split     -- eliminates border_idx() call for 90%+ of
 *                                  pixels. Border region is a thin ring;
 *                                  interior is the hot path.
 *  3. Row pointer caching       -- computes y*stride once per kernel row
 *                                  instead of once per pixel per kernel tap.
 *  4. Fixed-point Q15 kernel    -- converts float weights to int32 once
 *                                  before the loops. Inner loop becomes
 *                                  integer multiply + shift, no FPU needed.
 *  5. Channel-specialized paths -- ch=1 and ch=3 fast paths eliminate the
 *                                  inner channel loop and allow unrolling.
 *  6. Prefetch hints            -- hides memory latency on Cortex-A by
 *                                  prefetching the next source row.
 */

#include <cvcl/cvcl_filter.h>
#include <cvcl/cvcl_pixel.h>
#include <stdlib.h>
#include <string.h>

/* Prefetch */
#if defined(__GNUC__) || defined(__clang__)
#  define CVCL_PREFETCH(p) __builtin_prefetch((p), 0, 1)
#else
#  define CVCL_PREFETCH(p) ((void)(p))
#endif

/* -------------------------------------------------------------------------
 * Fixed-point kernel conversion  (Q15: 1.0 -> 32768)
 * ---------------------------------------------------------------------- */
#define CONV_SHIFT 15
#define CONV_ONE   (1 << CONV_SHIFT)
#define CONV_HALF  (1 << (CONV_SHIFT - 1))

static void kernel_to_fp(const cvcl_f32 * CVCL_RESTRICT kf,
                          cvcl_i32       * CVCL_RESTRICT ki,
                          cvcl_i32 n) {
    cvcl_i32 sum = 0;
    for (cvcl_i32 i = 0; i < n; i++) {
        ki[i] = (cvcl_i32)(kf[i] * CONV_ONE + (kf[i] >= 0.f ? 0.5f : -0.5f));
        sum  += ki[i];
    }
    /* Distribute rounding error to the center tap */
    if (sum != 0 && sum != CONV_ONE) {
        ki[n / 2] += CONV_ONE - sum;
    }
}

/* -------------------------------------------------------------------------
 * Border-safe pixel fetch (replicate)
 * ---------------------------------------------------------------------- */
CVCL_INLINE cvcl_u8 fetch_border(
        const cvcl_u8 * CVCL_RESTRICT data,
        cvcl_i32 x, cvcl_i32 y,
        cvcl_i32 w, cvcl_i32 h,
        cvcl_i32 stride, cvcl_i32 ch, cvcl_i32 c,
        cvcl_border_t border) {

    if (border == CVCL_BORDER_ZERO &&
        (x < 0 || x >= w || y < 0 || y >= h)) return 0;

    if (border == CVCL_BORDER_REFLECT) {
        if (x < 0) x = -x - 1; else if (x >= w) x = 2*w - x - 1;
        if (y < 0) y = -y - 1; else if (y >= h) y = 2*h - y - 1;
    } else if (border == CVCL_BORDER_WRAP) {
        x = ((x % w) + w) % w;
        y = ((y % h) + h) % h;
    }
    x = CVCL_CLAMP(x, 0, w-1);
    y = CVCL_CLAMP(y, 0, h-1);
    return data[(cvcl_size)y * stride + (cvcl_size)x * ch + c];
}

/* -------------------------------------------------------------------------
 * Interior convolution -- no border checks, fixed-point, row-cached
 *
 * Called only for pixels where the full kernel fits inside the image.
 * This covers the vast majority of pixels (all but a hh-pixel ring).
 * ---------------------------------------------------------------------- */

/* ch=1 interior fast path */
static void convolve_interior_ch1(
        cvcl_u8       * CVCL_RESTRICT dst_row,
        const cvcl_u8 * CVCL_RESTRICT src_data,
        const cvcl_i32* CVCL_RESTRICT kfp,
        cvcl_i32 x0, cvcl_i32 x1,
        cvcl_i32 y, cvcl_i32 kw, cvcl_i32 kh,
        cvcl_i32 hw, cvcl_i32 hh,
        cvcl_i32 stride) {

    for (cvcl_i32 x = x0; x < x1; x++) {
        cvcl_i32 acc = 0;
        for (cvcl_i32 ky = 0; ky < kh; ky++) {
            const cvcl_u8 * CVCL_RESTRICT sr =
                src_data + (cvcl_size)(y + ky - hh) * stride;
            CVCL_PREFETCH(sr + stride);
            const cvcl_i32 *krow = kfp + ky * kw;
            const cvcl_u8  *prow = sr  + (x - hw);
            for (cvcl_i32 kx = 0; kx < kw; kx++)
                acc += krow[kx] * (cvcl_i32)prow[kx];
        }
        dst_row[x] = (cvcl_u8)CVCL_CLAMP((acc + CONV_HALF) >> CONV_SHIFT, 0, 255);
    }
}

/* ch=3 interior fast path */
static void convolve_interior_ch3(
        cvcl_u8       * CVCL_RESTRICT dst_row,
        const cvcl_u8 * CVCL_RESTRICT src_data,
        const cvcl_i32* CVCL_RESTRICT kfp,
        cvcl_i32 x0, cvcl_i32 x1,
        cvcl_i32 y, cvcl_i32 kw, cvcl_i32 kh,
        cvcl_i32 hw, cvcl_i32 hh,
        cvcl_i32 stride) {

    for (cvcl_i32 x = x0; x < x1; x++) {
        cvcl_i32 a0=0, a1=0, a2=0;
        for (cvcl_i32 ky = 0; ky < kh; ky++) {
            const cvcl_u8  * CVCL_RESTRICT sr =
                src_data + (cvcl_size)(y + ky - hh) * stride;
            CVCL_PREFETCH(sr + stride);
            const cvcl_i32 *krow = kfp  + ky * kw;
            const cvcl_u8  *prow = sr   + (x - hw) * 3;
            for (cvcl_i32 kx = 0; kx < kw; kx++, prow += 3) {
                cvcl_i32 kv = krow[kx];
                a0 += kv * prow[0];
                a1 += kv * prow[1];
                a2 += kv * prow[2];
            }
        }
        dst_row[x*3+0]=(cvcl_u8)CVCL_CLAMP((a0+CONV_HALF)>>CONV_SHIFT,0,255);
        dst_row[x*3+1]=(cvcl_u8)CVCL_CLAMP((a1+CONV_HALF)>>CONV_SHIFT,0,255);
        dst_row[x*3+2]=(cvcl_u8)CVCL_CLAMP((a2+CONV_HALF)>>CONV_SHIFT,0,255);
    }
}

/* Generic ch interior fast path */
static void convolve_interior_gen(
        cvcl_u8       * CVCL_RESTRICT dst_row,
        const cvcl_u8 * CVCL_RESTRICT src_data,
        const cvcl_i32* CVCL_RESTRICT kfp,
        cvcl_i32 x0, cvcl_i32 x1,
        cvcl_i32 y, cvcl_i32 kw, cvcl_i32 kh,
        cvcl_i32 hw, cvcl_i32 hh,
        cvcl_i32 stride, cvcl_i32 ch) {

    for (cvcl_i32 x = x0; x < x1; x++) {
        for (cvcl_i32 c = 0; c < ch; c++) {
            cvcl_i32 acc = 0;
            for (cvcl_i32 ky = 0; ky < kh; ky++) {
                const cvcl_u8  *sr   = src_data + (cvcl_size)(y+ky-hh)*stride;
                const cvcl_i32 *krow = kfp + ky * kw;
                for (cvcl_i32 kx = 0; kx < kw; kx++)
                    acc += krow[kx] * (cvcl_i32)sr[(x+kx-hw)*ch+c];
            }
            dst_row[x*ch+c]=(cvcl_u8)CVCL_CLAMP(
                (acc+CONV_HALF)>>CONV_SHIFT, 0, 255);
        }
    }
}

/* -------------------------------------------------------------------------
 * Border pixel -- slow path, called only for the thin border ring
 * ---------------------------------------------------------------------- */
static void convolve_border_pixel(
        cvcl_u8       * CVCL_RESTRICT dst_row,
        const cvcl_u8 * CVCL_RESTRICT src_data,
        const cvcl_i32* CVCL_RESTRICT kfp,
        cvcl_i32 x, cvcl_i32 y,
        cvcl_i32 kw, cvcl_i32 kh,
        cvcl_i32 hw, cvcl_i32 hh,
        cvcl_i32 w, cvcl_i32 h,
        cvcl_i32 stride, cvcl_i32 ch,
        cvcl_border_t border) {

    for (cvcl_i32 c = 0; c < ch; c++) {
        cvcl_i32 acc = 0;
        for (cvcl_i32 ky = 0; ky < kh; ky++) {
            for (cvcl_i32 kx = 0; kx < kw; kx++) {
                cvcl_u8 pix = fetch_border(src_data,
                    x+kx-hw, y+ky-hh, w, h, stride, ch, c, border);
                acc += kfp[ky*kw+kx] * (cvcl_i32)pix;
            }
        }
        dst_row[x*ch+c]=(cvcl_u8)CVCL_CLAMP(
            (acc+CONV_HALF)>>CONV_SHIFT, 0, 255);
    }
}

/* -------------------------------------------------------------------------
 * Public: cvcl_convolve2d
 * ---------------------------------------------------------------------- */
cvcl_result_t cvcl_convolve2d(cvcl_image_t       * CVCL_RESTRICT dst,
                               const cvcl_image_t * CVCL_RESTRICT src,
                               const cvcl_f32     * CVCL_RESTRICT kernel,
                               cvcl_i32            kw,
                               cvcl_i32            kh,
                               cvcl_border_t       border) {
    CVCL_CHECK_NULL(dst);
    CVCL_CHECK_NULL(src);
    CVCL_CHECK_NULL(kernel);
    CVCL_CHECK_NULL(dst->data);
    CVCL_CHECK_NULL(src->data);
    CVCL_CHECK_ARG(kw % 2 == 1 && kh % 2 == 1);
    CVCL_CHECK_ARG(cvcl_image_compatible(dst, src));
    CVCL_CHECK_ARG(src->depth == CVCL_DEPTH_U8);

    cvcl_i32 hw = kw / 2, hh = kh / 2;
    cvcl_i32 w  = src->width, h = src->height, ch = src->channels;
    cvcl_i32 n  = kw * kh;

    /* Convert float kernel to fixed-point once */
    cvcl_i32 *kfp = (cvcl_i32 *)malloc((cvcl_size)n * sizeof(cvcl_i32));
    if (!kfp) return CVCL_ERR_ALLOC;
    kernel_to_fp(kernel, kfp, n);

    /* Interior bounds: rows and columns where full kernel fits */
    cvcl_i32 y_inner0 = hh,     y_inner1 = h - hh;
    cvcl_i32 x_inner0 = hw,     x_inner1 = w - hw;

    for (cvcl_i32 y = 0; y < h; y++) {
        cvcl_u8 * CVCL_RESTRICT dr = cvcl_image_row(dst, y);
        int is_interior_y = (y >= y_inner0 && y < y_inner1);

        if (is_interior_y) {
            /* ---- Interior row: no border check for interior columns ---- */

            /* Left border columns */
            for (cvcl_i32 x = 0; x < x_inner0; x++)
                convolve_border_pixel(dr, src->data, kfp, x, y,
                    kw, kh, hw, hh, w, h, src->stride, ch, border);

            /* Interior columns -- hot path */
            if      (ch == 1)
                convolve_interior_ch1(dr, src->data, kfp,
                    x_inner0, x_inner1, y, kw, kh, hw, hh, src->stride);
            else if (ch == 3)
                convolve_interior_ch3(dr, src->data, kfp,
                    x_inner0, x_inner1, y, kw, kh, hw, hh, src->stride);
            else
                convolve_interior_gen(dr, src->data, kfp,
                    x_inner0, x_inner1, y, kw, kh, hw, hh, src->stride, ch);

            /* Right border columns */
            for (cvcl_i32 x = x_inner1; x < w; x++)
                convolve_border_pixel(dr, src->data, kfp, x, y,
                    kw, kh, hw, hh, w, h, src->stride, ch, border);
        } else {
            /* ---- Border row: all columns use slow path ---- */
            for (cvcl_i32 x = 0; x < w; x++)
                convolve_border_pixel(dr, src->data, kfp, x, y,
                    kw, kh, hw, hh, w, h, src->stride, ch, border);
        }
    }

    free(kfp);
    return CVCL_OK;
}

/* -------------------------------------------------------------------------
 * Public: cvcl_convolve_sep
 * ---------------------------------------------------------------------- */
cvcl_result_t cvcl_convolve_sep(cvcl_image_t       * CVCL_RESTRICT dst,
                                 cvcl_image_t       * CVCL_RESTRICT tmp,
                                 const cvcl_image_t * CVCL_RESTRICT src,
                                 const cvcl_f32     * CVCL_RESTRICT row_kernel,
                                 cvcl_i32            row_ksize,
                                 const cvcl_f32     * CVCL_RESTRICT col_kernel,
                                 cvcl_i32            col_ksize,
                                 cvcl_border_t       border) {
    CVCL_CHECK_NULL(dst); CVCL_CHECK_NULL(tmp); CVCL_CHECK_NULL(src);
    CVCL_CHECK_NULL(row_kernel); CVCL_CHECK_NULL(col_kernel);

    /* Horizontal pass: src -> tmp */
    CVCL_CHECK(cvcl_convolve2d(tmp, src, row_kernel, row_ksize, 1, border));
    /* Vertical pass: tmp -> dst */
    CVCL_CHECK(cvcl_convolve2d(dst, tmp, col_kernel, 1, col_ksize, border));
    return CVCL_OK;
}
