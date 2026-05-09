/**
 * @file transform_resize.c
 * @brief Image resize: nearest-neighbor, bilinear, bicubic
 */

#include <cvcl/cvcl_platform.h>
#include <cvcl/cvcl_transform.h>
#include <cvcl/cvcl_pixel.h>

/* -------------------------------------------------------------------------
 * Nearest-neighbor resize (fast path)
 * ---------------------------------------------------------------------- */

static cvcl_result_t resize_nearest(cvcl_image_t       *dst,
                                    const cvcl_image_t *src) {
    cvcl_i32 sw = src->width,  sh = src->height;
    cvcl_i32 dw = dst->width,  dh = dst->height;
    cvcl_i32 ch = src->channels;
    cvcl_i32 depth = (cvcl_i32)src->depth;
    cvcl_i32 pixel_bytes = ch * depth;

    for (cvcl_i32 dy = 0; dy < dh; dy++) {
        cvcl_i32 sy = (dy * sh) / dh;

        cvcl_u8 * CVCL_RESTRICT dst_row = cvcl_image_row(dst, dy);
        const cvcl_u8 * CVCL_RESTRICT src_row = cvcl_image_row(src, sy);

        for (cvcl_i32 dx = 0; dx < dw; dx++) {
            cvcl_i32 sx = (dx * sw) / dw;

            CVCL_MEMCPY(dst_row + dx * pixel_bytes,
                   src_row + sx * pixel_bytes,
                   (cvcl_size)pixel_bytes);
        }
    }

    return CVCL_OK;
}

/* -------------------------------------------------------------------------
 * Bilinear resize (U8 only, RGB or gray)
 * ---------------------------------------------------------------------- */

static cvcl_result_t resize_bilinear_u8(cvcl_image_t       *dst,
                                        const cvcl_image_t *src) {
    cvcl_i32 sw = src->width,  sh = src->height;
    cvcl_i32 dw = dst->width,  dh = dst->height;
    cvcl_i32 ch = src->channels;

    /* Use fixed-point 16.16 for inner loop performance */
    cvcl_i32 x_ratio = ((sw - 1) << 16) / (dw > 1 ? dw - 1 : 1);
    cvcl_i32 y_ratio = ((sh - 1) << 16) / (dh > 1 ? dh - 1 : 1);

    for (cvcl_i32 dy = 0; dy < dh; dy++) {
        cvcl_i32 y_fp   = dy * y_ratio;
        cvcl_i32 y0     = y_fp >> 16;
        cvcl_i32 y1     = CVCL_MIN(y0 + 1, sh - 1);
        cvcl_i32 y_frac = y_fp & 0xFFFF;

        cvcl_u8 * CVCL_RESTRICT dst_row = cvcl_image_row(dst, dy);
        const cvcl_u8 * CVCL_RESTRICT src_row0 = cvcl_image_row(src, y0);
        const cvcl_u8 * CVCL_RESTRICT src_row1 = cvcl_image_row(src, y1);

        for (cvcl_i32 dx = 0; dx < dw; dx++) {
            cvcl_i32 x_fp   = dx * x_ratio;
            cvcl_i32 x0     = x_fp >> 16;
            cvcl_i32 x1     = CVCL_MIN(x0 + 1, sw - 1);
            cvcl_i32 x_frac = x_fp & 0xFFFF;

            cvcl_i32 dst_base = dx * ch;
            cvcl_i32 src_base0 = x0 * ch;
            cvcl_i32 src_base1 = x1 * ch;

            for (cvcl_i32 c = 0; c < ch; c++) {
                cvcl_i32 p00 = src_row0[src_base0 + c];
                cvcl_i32 p10 = src_row0[src_base1 + c];
                cvcl_i32 p01 = src_row1[src_base0 + c];
                cvcl_i32 p11 = src_row1[src_base1 + c];

                /* Bilinear interpolation in fixed-point */
                cvcl_i32 top    = p00 + (((p10 - p00) * x_frac) >> 16);
                cvcl_i32 bottom = p01 + (((p11 - p01) * x_frac) >> 16);
                cvcl_i32 result = top + (((bottom - top) * y_frac) >> 16);

                dst_row[dst_base + c] =
                    (cvcl_u8)CVCL_CLAMP(result, 0, 255);
            }
        }
    }

    return CVCL_OK;
}

static cvcl_f32 bicubic_kernel(cvcl_f32 t) {
    /* Catmull-Rom cubic kernel, equivalent to cubic convolution with a = -0.5 */
    if (t < 0.f) t = -t;

    if (t < 1.f)
        return 1.5f * t * t * t - 2.5f * t * t + 1.f;

    if (t < 2.f)
        return -0.5f * t * t * t + 2.5f * t * t - 4.f * t + 2.f;

    return 0.f;
}

static cvcl_result_t resize_bicubic(cvcl_image_t       *dst,
                                    const cvcl_image_t *src) {
    cvcl_i32 dw = dst->width,  dh = dst->height;
    cvcl_i32 sw = src->width,  sh = src->height;
    cvcl_i32 ch = src->channels;

    cvcl_f32 sx_scale = (cvcl_f32)sw / (cvcl_f32)dw;
    cvcl_f32 sy_scale = (cvcl_f32)sh / (cvcl_f32)dh;

    for (cvcl_i32 dy = 0; dy < dh; dy++) {
        cvcl_u8 * CVCL_RESTRICT dr = cvcl_image_row(dst, dy);

        cvcl_f32 fy = ((cvcl_f32)dy + 0.5f) * sy_scale - 0.5f;
        cvcl_i32 iy = (cvcl_i32)fy;

        cvcl_f32 wy[4];
        cvcl_i32 yidx[4];

        for (cvcl_i32 m = -1; m <= 2; m++) {
            cvcl_i32 k = m + 1;
            wy[k] = bicubic_kernel(fy - (cvcl_f32)(iy + m));
            yidx[k] = CVCL_CLAMP(iy + m, 0, sh - 1);
        }

        for (cvcl_i32 dx = 0; dx < dw; dx++) {
            cvcl_f32 fx = ((cvcl_f32)dx + 0.5f) * sx_scale - 0.5f;
            cvcl_i32 ix = (cvcl_i32)fx;

            cvcl_f32 wx[4];
            cvcl_i32 xidx[4];

            for (cvcl_i32 n = -1; n <= 2; n++) {
                cvcl_i32 k = n + 1;
                wx[k] = bicubic_kernel(fx - (cvcl_f32)(ix + n));
                xidx[k] = CVCL_CLAMP(ix + n, 0, sw - 1);
            }

            cvcl_i32 dst_base = dx * ch;

            for (cvcl_i32 c = 0; c < ch; c++) {
                cvcl_f32 val = 0.f;
                cvcl_f32 wsum = 0.f;

                for (cvcl_i32 m = 0; m < 4; m++) {
                    cvcl_f32 wy_m = wy[m];
                    const cvcl_u8 * CVCL_RESTRICT row =
                        cvcl_image_row(src, yidx[m]);

                    for (cvcl_i32 n = 0; n < 4; n++) {
                        cvcl_f32 w = wx[n] * wy_m;
                        val += w * (cvcl_f32)row[xidx[n] * ch + c];
                        wsum += w;
                    }
                }

                if (wsum > 0.f)
                    val /= wsum;

                dr[dst_base + c] =
                    (cvcl_u8)CVCL_CLAMP((cvcl_i32)(val + 0.5f), 0, 255);
            }
        }
    }

    return CVCL_OK;
}

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */

cvcl_result_t cvcl_resize(cvcl_image_t       *dst,
                          const cvcl_image_t *src,
                          cvcl_interp_t       interp) {
    CVCL_CHECK_NULL(dst);
    CVCL_CHECK_NULL(src);
    CVCL_CHECK_NULL(dst->data);
    CVCL_CHECK_NULL(src->data);
    CVCL_CHECK_ARG(dst->channels == src->channels);
    CVCL_CHECK_ARG(dst->depth    == src->depth);

    switch (interp) {
        case CVCL_INTERP_NEAREST:
            return resize_nearest(dst, src);

        case CVCL_INTERP_BILINEAR:
            if (src->depth == CVCL_DEPTH_U8)
                return resize_bilinear_u8(dst, src);
            return resize_nearest(dst, src);

        case CVCL_INTERP_BICUBIC:
            if (src->depth == CVCL_DEPTH_U8)
                return resize_bicubic(dst, src);
            return resize_nearest(dst, src);

        default:
            return CVCL_ERR_UNSUPPORTED;
    }
}

cvcl_result_t cvcl_resize_alloc(cvcl_image_t          *dst,
                                const cvcl_image_t    *src,
                                cvcl_i32               dst_w,
                                cvcl_i32               dst_h,
                                cvcl_interp_t          interp,
                                const cvcl_allocator_t *alloc) {
    CVCL_CHECK_NULL(dst);
    CVCL_CHECK_NULL(src);
    CVCL_CHECK_ARG(dst_w > 0 && dst_h > 0);

    CVCL_CHECK(cvcl_image_create(dst, dst_w, dst_h,
                                 src->channels, src->depth, alloc));

    cvcl_result_t rc = cvcl_resize(dst, src, interp);

    if (rc != CVCL_OK)
        cvcl_image_free(dst, alloc);

    return rc;
}

/* -------------------------------------------------------------------------
 * Flip operations
 * ---------------------------------------------------------------------- */

cvcl_result_t cvcl_flip_h(cvcl_image_t *img) {
    CVCL_CHECK_NULL(img);
    CVCL_CHECK_NULL(img->data);

    cvcl_i32 pixel_bytes = img->channels * (cvcl_i32)img->depth;
    cvcl_i32 half_w = img->width / 2;

    for (cvcl_i32 y = 0; y < img->height; y++) {
        cvcl_u8 * CVCL_RESTRICT row = cvcl_image_row(img, y);

        if (pixel_bytes == 1) {
            for (cvcl_i32 x = 0; x < half_w; x++) {
                cvcl_u8 *a = row + x;
                cvcl_u8 *b = row + (img->width - 1 - x);

                cvcl_u8 tmp = *a;
                *a = *b;
                *b = tmp;
            }
        } else if (pixel_bytes == 3) {
            for (cvcl_i32 x = 0; x < half_w; x++) {
                cvcl_u8 *a = row + x * 3;
                cvcl_u8 *b = row + (img->width - 1 - x) * 3;

                cvcl_u8 t0 = a[0], t1 = a[1], t2 = a[2];

                a[0] = b[0];
                a[1] = b[1];
                a[2] = b[2];

                b[0] = t0;
                b[1] = t1;
                b[2] = t2;
            }
        } else if (pixel_bytes == 4) {
            for (cvcl_i32 x = 0; x < half_w; x++) {
                cvcl_u8 *a = row + x * 4;
                cvcl_u8 *b = row + (img->width - 1 - x) * 4;

                cvcl_u8 t0 = a[0], t1 = a[1], t2 = a[2], t3 = a[3];

                a[0] = b[0];
                a[1] = b[1];
                a[2] = b[2];
                a[3] = b[3];

                b[0] = t0;
                b[1] = t1;
                b[2] = t2;
                b[3] = t3;
            }
        } else {
            for (cvcl_i32 x = 0; x < half_w; x++) {
                cvcl_u8 *a = row + x * pixel_bytes;
                cvcl_u8 *b = row + (img->width - 1 - x) * pixel_bytes;

                for (cvcl_i32 k = 0; k < pixel_bytes; k++) {
                    cvcl_u8 tmp = a[k];
                    a[k] = b[k];
                    b[k] = tmp;
                }
            }
        }
    }

    return CVCL_OK;
}

cvcl_result_t cvcl_flip_v(cvcl_image_t *img) {
    CVCL_CHECK_NULL(img);
    CVCL_CHECK_NULL(img->data);

    const cvcl_allocator_t *a = cvcl_image_allocator(img);
    cvcl_i32 row_bytes = img->width * img->channels * (cvcl_i32)img->depth;
    cvcl_i32 half_h = img->height / 2;

    /* Temporary row buffer for efficient row swapping */
    cvcl_u8 *tmp = (cvcl_u8 *)cvcl_alloc(a, (cvcl_size)row_bytes);

    if (!tmp)
        return CVCL_ERR_ALLOC;

    for (cvcl_i32 y = 0; y < half_h; y++) {
        cvcl_u8 *row_a = cvcl_image_row(img, y);
        cvcl_u8 *row_b = cvcl_image_row(img, img->height - 1 - y);

        CVCL_MEMCPY(tmp,   row_a, (cvcl_size)row_bytes);
        CVCL_MEMCPY(row_a, row_b, (cvcl_size)row_bytes);
        CVCL_MEMCPY(row_b, tmp,   (cvcl_size)row_bytes);
    }

    cvcl_free(a, tmp);

    return CVCL_OK;
}

/* -------------------------------------------------------------------------
 * Crop (zero-copy view)
 * ---------------------------------------------------------------------- */

cvcl_result_t cvcl_crop(cvcl_image_t       *view,
                        const cvcl_image_t *src,
                        cvcl_rect_t         roi) {
    return cvcl_image_view(view, src, roi);
}

/* -------------------------------------------------------------------------
 * Channel conversion
 * ---------------------------------------------------------------------- */

cvcl_result_t cvcl_convert_channels(cvcl_image_t       *dst,
                                    const cvcl_image_t *src) {
    CVCL_CHECK_NULL(dst);
    CVCL_CHECK_NULL(src);
    CVCL_CHECK_NULL(dst->data);
    CVCL_CHECK_NULL(src->data);
    CVCL_CHECK_ARG(dst->width  == src->width);
    CVCL_CHECK_ARG(dst->height == src->height);
    CVCL_CHECK_ARG(dst->depth  == src->depth);
    CVCL_CHECK_ARG(src->depth  == CVCL_DEPTH_U8); /* Only U8 for now */

    cvcl_i32 w = src->width;
    cvcl_i32 h = src->height;

    /* RGB -> Gray (BT.601 luma) */
    if (src->channels == 3 && dst->channels == 1) {
        for (cvcl_i32 y = 0; y < h; y++) {
            const cvcl_u8 * CVCL_RESTRICT sr = cvcl_image_row(src, y);
            cvcl_u8 * CVCL_RESTRICT dr = cvcl_image_row(dst, y);

            for (cvcl_i32 x = 0; x < w; x++) {
                cvcl_u32 r = sr[0];
                cvcl_u32 g = sr[1];
                cvcl_u32 b = sr[2];

                dr[0] = (cvcl_u8)((r * 77 + g * 150 + b * 29) >> 8);

                sr += 3;
                dr += 1;
            }
        }

        return CVCL_OK;
    }

    /* Gray -> RGB */
    if (src->channels == 1 && dst->channels == 3) {
        for (cvcl_i32 y = 0; y < h; y++) {
            const cvcl_u8 * CVCL_RESTRICT sr = cvcl_image_row(src, y);
            cvcl_u8 * CVCL_RESTRICT dr = cvcl_image_row(dst, y);

            for (cvcl_i32 x = 0; x < w; x++) {
                cvcl_u8 v = sr[0];

                dr[0] = v;
                dr[1] = v;
                dr[2] = v;

                sr += 1;
                dr += 3;
            }
        }

        return CVCL_OK;
    }

    /* RGB -> RGBA (alpha = 255) */
    if (src->channels == 3 && dst->channels == 4) {
        for (cvcl_i32 y = 0; y < h; y++) {
            const cvcl_u8 * CVCL_RESTRICT sr = cvcl_image_row(src, y);
            cvcl_u8 * CVCL_RESTRICT dr = cvcl_image_row(dst, y);

            for (cvcl_i32 x = 0; x < w; x++) {
                dr[0] = sr[0];
                dr[1] = sr[1];
                dr[2] = sr[2];
                dr[3] = 255;

                sr += 3;
                dr += 4;
            }
        }

        return CVCL_OK;
    }

    /* RGBA -> RGB (drop alpha) */
    if (src->channels == 4 && dst->channels == 3) {
        for (cvcl_i32 y = 0; y < h; y++) {
            const cvcl_u8 * CVCL_RESTRICT sr = cvcl_image_row(src, y);
            cvcl_u8 * CVCL_RESTRICT dr = cvcl_image_row(dst, y);

            for (cvcl_i32 x = 0; x < w; x++) {
                dr[0] = sr[0];
                dr[1] = sr[1];
                dr[2] = sr[2];

                sr += 4;
                dr += 3;
            }
        }

        return CVCL_OK;
    }

    return CVCL_ERR_UNSUPPORTED;
}

/* -------------------------------------------------------------------------
 * Rotate 90 / 180 / 270
 * ---------------------------------------------------------------------- */

cvcl_result_t cvcl_rotate(cvcl_image_t          *dst,
                          const cvcl_image_t    *src,
                          cvcl_rotate_t          angle,
                          const cvcl_allocator_t *alloc) {
    CVCL_CHECK_NULL(dst);
    CVCL_CHECK_NULL(src);
    CVCL_CHECK_NULL(src->data);
    CVCL_CHECK_ARG(src->depth == CVCL_DEPTH_U8);

    cvcl_i32 w = src->width;
    cvcl_i32 h = src->height;
    cvcl_i32 ch = src->channels;

    cvcl_result_t rc;

    if (angle == CVCL_ROTATE_180) {
        rc = cvcl_image_create(dst, w, h, ch, CVCL_DEPTH_U8, alloc);

        if (rc != CVCL_OK)
            return rc;

        for (cvcl_i32 y = 0; y < h; y++) {
            const cvcl_u8 * CVCL_RESTRICT sr =
                cvcl_image_row(src, h - 1 - y);
            cvcl_u8 * CVCL_RESTRICT dr = cvcl_image_row(dst, y);

            for (cvcl_i32 x = 0; x < w; x++) {
                const cvcl_u8 *sp = sr + (w - 1 - x) * ch;
                cvcl_u8 *dp = dr + x * ch;

                for (cvcl_i32 c = 0; c < ch; c++)
                    dp[c] = sp[c];
            }
        }

        return CVCL_OK;
    }

    /* 90 CW: dst(x,y) = src(h-1-y, x)  -> dst is h x w */
    /* 90 CCW: dst(x,y) = src(y, w-1-x) -> dst is h x w */
    rc = cvcl_image_create(dst, h, w, ch, CVCL_DEPTH_U8, alloc);

    if (rc != CVCL_OK)
        return rc;

    for (cvcl_i32 y = 0; y < h; y++) {
        const cvcl_u8 * CVCL_RESTRICT sr = cvcl_image_row(src, y);

        for (cvcl_i32 x = 0; x < w; x++) {
            const cvcl_u8 *sp = sr + x * ch;
            cvcl_u8 *dp;

            if (angle == CVCL_ROTATE_90CW)
                dp = cvcl_image_row(dst, x) + (h - 1 - y) * ch;
            else
                dp = cvcl_image_row(dst, w - 1 - x) + y * ch;

            for (cvcl_i32 c = 0; c < ch; c++)
                dp[c] = sp[c];
        }
    }

    return CVCL_OK;
}

/* -------------------------------------------------------------------------
 * Affine transform (2x3 matrix, inverse mapping)
 * ---------------------------------------------------------------------- */

cvcl_result_t cvcl_affine(cvcl_image_t       *dst,
                          const cvcl_image_t *src,
                          const cvcl_f32      M[6],
                          cvcl_interp_t       interp,
                          cvcl_border_t       border) {
    CVCL_CHECK_NULL(dst);
    CVCL_CHECK_NULL(src);
    CVCL_CHECK_NULL(dst->data);
    CVCL_CHECK_NULL(src->data);
    CVCL_CHECK_NULL(M);
    CVCL_CHECK_ARG(src->depth == CVCL_DEPTH_U8);

    cvcl_i32 dw = dst->width;
    cvcl_i32 dh = dst->height;
    cvcl_i32 sw = src->width;
    cvcl_i32 sh = src->height;
    cvcl_i32 ch = src->channels;

    /* Inverse map: for each dst pixel, find src pixel */
    for (cvcl_i32 dy = 0; dy < dh; dy++) {
        cvcl_u8 * CVCL_RESTRICT dr = cvcl_image_row(dst, dy);

        for (cvcl_i32 dx = 0; dx < dw; dx++) {
            cvcl_f32 sx = M[0] * (cvcl_f32)dx +
                          M[1] * (cvcl_f32)dy + M[2];

            cvcl_f32 sy = M[3] * (cvcl_f32)dx +
                          M[4] * (cvcl_f32)dy + M[5];

            cvcl_i32 dst_base = dx * ch;

            if (interp == CVCL_INTERP_NEAREST) {
                cvcl_i32 ix = (cvcl_i32)(sx + 0.5f);
                cvcl_i32 iy = (cvcl_i32)(sy + 0.5f);

                if (ix < 0 || ix >= sw || iy < 0 || iy >= sh) {
                    if (border == CVCL_BORDER_ZERO) {
                        for (cvcl_i32 c = 0; c < ch; c++)
                            dr[dst_base + c] = 0;
                    } else {
                        ix = CVCL_CLAMP(ix, 0, sw - 1);
                        iy = CVCL_CLAMP(iy, 0, sh - 1);

                        const cvcl_u8 *sp =
                            cvcl_image_row(src, iy) + ix * ch;

                        for (cvcl_i32 c = 0; c < ch; c++)
                            dr[dst_base + c] = sp[c];
                    }
                } else {
                    const cvcl_u8 *sp =
                        cvcl_image_row(src, iy) + ix * ch;

                    for (cvcl_i32 c = 0; c < ch; c++)
                        dr[dst_base + c] = sp[c];
                }
            } else {
                /* Bilinear */
                cvcl_i32 sx_i = (cvcl_i32)sx;
                cvcl_i32 sy_i = (cvcl_i32)sy;

                cvcl_i32 x0 = CVCL_CLAMP(sx_i,     0, sw - 1);
                cvcl_i32 x1 = CVCL_CLAMP(sx_i + 1, 0, sw - 1);
                cvcl_i32 y0 = CVCL_CLAMP(sy_i,     0, sh - 1);
                cvcl_i32 y1 = CVCL_CLAMP(sy_i + 1, 0, sh - 1);

                cvcl_f32 fx = sx - (cvcl_f32)sx_i;
                cvcl_f32 fy = sy - (cvcl_f32)sy_i;

                const cvcl_u8 * CVCL_RESTRICT r0 = cvcl_image_row(src, y0);
                const cvcl_u8 * CVCL_RESTRICT r1 = cvcl_image_row(src, y1);

                cvcl_f32 one_minus_fx = 1.f - fx;
                cvcl_f32 one_minus_fy = 1.f - fy;

                cvcl_i32 x0_base = x0 * ch;
                cvcl_i32 x1_base = x1 * ch;

                for (cvcl_i32 c = 0; c < ch; c++) {
                    cvcl_f32 v =
                        (r0[x0_base + c] * one_minus_fx +
                         r0[x1_base + c] * fx) * one_minus_fy +
                        (r1[x0_base + c] * one_minus_fx +
                         r1[x1_base + c] * fx) * fy;

                    dr[dst_base + c] =
                        (cvcl_u8)CVCL_CLAMP((cvcl_i32)(v + 0.5f), 0, 255);
                }
            }
        }
    }

    return CVCL_OK;
}
