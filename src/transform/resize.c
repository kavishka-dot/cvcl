/**
 * @file transform_resize.c
 * @brief Image resize: nearest-neighbor, bilinear, bicubic
 */

#include <cvcl/cvcl_transform.h>
#include <cvcl/cvcl_pixel.h>
#include <math.h>
#include <string.h>

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
        cvcl_u8 *dst_row = cvcl_image_row(dst, dy);
        const cvcl_u8 *src_row = cvcl_image_row(src, sy);

        for (cvcl_i32 dx = 0; dx < dw; dx++) {
            cvcl_i32 sx = (dx * sw) / dw;
            memcpy(dst_row + dx * pixel_bytes,
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
        cvcl_i32 y_fp  = dy * y_ratio;
        cvcl_i32 y0    = y_fp >> 16;
        cvcl_i32 y1    = CVCL_MIN(y0 + 1, sh - 1);
        cvcl_i32 y_frac = y_fp & 0xFFFF;

        cvcl_u8 *dst_row = cvcl_image_row(dst, dy);
        const cvcl_u8 *src_row0 = cvcl_image_row(src, y0);
        const cvcl_u8 *src_row1 = cvcl_image_row(src, y1);

        for (cvcl_i32 dx = 0; dx < dw; dx++) {
            cvcl_i32 x_fp   = dx * x_ratio;
            cvcl_i32 x0     = x_fp >> 16;
            cvcl_i32 x1     = CVCL_MIN(x0 + 1, sw - 1);
            cvcl_i32 x_frac = x_fp & 0xFFFF;

            for (cvcl_i32 c = 0; c < ch; c++) {
                cvcl_i32 p00 = src_row0[x0 * ch + c];
                cvcl_i32 p10 = src_row0[x1 * ch + c];
                cvcl_i32 p01 = src_row1[x0 * ch + c];
                cvcl_i32 p11 = src_row1[x1 * ch + c];

                /* Bilinear interpolation in fixed-point */
                cvcl_i32 top    = p00 + (((p10 - p00) * x_frac) >> 16);
                cvcl_i32 bottom = p01 + (((p11 - p01) * x_frac) >> 16);
                cvcl_i32 result = top  + (((bottom - top) * y_frac) >> 16);

                dst_row[dx * ch + c] = (cvcl_u8)CVCL_CLAMP(result, 0, 255);
            }
        }
    }
    return CVCL_OK;
}

static cvcl_f32 bicubic_kernel(cvcl_f32 t) {
    /* Mitchell-Netravali B=0 C=0.5 (Catmull-Rom) */
    if (t < 0.f) t = -t;
    if (t < 1.f) return 1.5f*t*t*t - 2.5f*t*t + 1.f;
    if (t < 2.f) return -0.5f*t*t*t + 2.5f*t*t - 4.f*t + 2.f;
    return 0.f;
}

static cvcl_result_t resize_bicubic(cvcl_image_t       *dst,
                                     const cvcl_image_t *src) {
    cvcl_i32 dw=dst->width, dh=dst->height;
    cvcl_i32 sw=src->width, sh=src->height, ch=src->channels;
    cvcl_f32 sx_scale=(cvcl_f32)sw/dw, sy_scale=(cvcl_f32)sh/dh;

    for (cvcl_i32 dy=0; dy<dh; dy++) {
        cvcl_u8 *dr = cvcl_image_row(dst,dy);
        for (cvcl_i32 dx=0; dx<dw; dx++) {
            cvcl_f32 fx=(dx+0.5f)*sx_scale-0.5f;
            cvcl_f32 fy=(dy+0.5f)*sy_scale-0.5f;
            cvcl_i32 ix=(cvcl_i32)fx, iy=(cvcl_i32)fy;

            for (cvcl_i32 c=0; c<ch; c++) {
                cvcl_f32 val=0.f, wsum=0.f;
                for (cvcl_i32 m=-1; m<=2; m++) {
                    cvcl_f32 wy=bicubic_kernel(fy-(cvcl_f32)(iy+m));
                    cvcl_i32 sy=CVCL_CLAMP(iy+m,0,sh-1);
                    const cvcl_u8 *row=cvcl_image_row(src,sy);
                    for (cvcl_i32 n=-1; n<=2; n++) {
                        cvcl_f32 wx=bicubic_kernel(fx-(cvcl_f32)(ix+n));
                        cvcl_i32 sxi=CVCL_CLAMP(ix+n,0,sw-1);
                        val+=wx*wy*(cvcl_f32)row[sxi*ch+c];
                        wsum+=wx*wy;
                    }
                }
                if (wsum>0.f) val/=wsum;
                dr[dx*ch+c]=(cvcl_u8)CVCL_CLAMP((cvcl_i32)(val+0.5f),0,255);
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
    if (rc != CVCL_OK) cvcl_image_free(dst, alloc);
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
        for (cvcl_i32 x = 0; x < half_w; x++) {
            cvcl_u8 *a = row + x * pixel_bytes;
            cvcl_u8 *b = row + (img->width - 1 - x) * pixel_bytes;
            for (cvcl_i32 k = 0; k < pixel_bytes; k++) {
                cvcl_u8 tmp = a[k]; a[k] = b[k]; b[k] = tmp;
            }
        }
    }
    return CVCL_OK;
}

cvcl_result_t cvcl_flip_v(cvcl_image_t *img) {
    CVCL_CHECK_NULL(img);
    CVCL_CHECK_NULL(img->data);

    cvcl_i32 row_bytes = img->width * img->channels * (cvcl_i32)img->depth;
    cvcl_i32 half_h = img->height / 2;

    /* Temporary row buffer for efficient row swapping */
    cvcl_u8 *tmp = (cvcl_u8 *)malloc((cvcl_size)row_bytes);
    if (!tmp)
        return CVCL_ERR_ALLOC;

    for (cvcl_i32 y = 0; y < half_h; y++) {
        cvcl_u8 *row_a = cvcl_image_row(img, y);
        cvcl_u8 *row_b = cvcl_image_row(img, img->height - 1 - y);
        /*for (cvcl_i32 k = 0; k < row_bytes; k++) {
            cvcl_u8 tmp = row_a[k]; row_a[k] = row_b[k]; row_b[k] = tmp;
        }*/
        memcpy(tmp,   row_a, (cvcl_size)row_bytes);
        memcpy(row_a, row_b, (cvcl_size)row_bytes);
        memcpy(row_b, tmp,   (cvcl_size)row_bytes);
    }

    free(tmp);
    
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

    cvcl_i32 w = src->width, h = src->height;

    /* RGB -> Gray (BT.601 luma) */
    if (src->channels == 3 && dst->channels == 1) {
        for (cvcl_i32 y = 0; y < h; y++) {
            const cvcl_u8 *sr = cvcl_image_row(src, y);
            cvcl_u8       *dr = cvcl_image_row(dst, y);
            for (cvcl_i32 x = 0; x < w; x++) {
                cvcl_u32 r = sr[x*3+0], g = sr[x*3+1], b = sr[x*3+2];
                dr[x] = (cvcl_u8)((r * 77 + g * 150 + b * 29) >> 8);
            }
        }
        return CVCL_OK;
    }

    /* Gray -> RGB */
    if (src->channels == 1 && dst->channels == 3) {
        for (cvcl_i32 y = 0; y < h; y++) {
            const cvcl_u8 *sr = cvcl_image_row(src, y);
            cvcl_u8       *dr = cvcl_image_row(dst, y);
            for (cvcl_i32 x = 0; x < w; x++) {
                dr[x*3+0] = dr[x*3+1] = dr[x*3+2] = sr[x];
            }
        }
        return CVCL_OK;
    }

    /* RGB -> RGBA (alpha = 255) */
    if (src->channels == 3 && dst->channels == 4) {
        for (cvcl_i32 y = 0; y < h; y++) {
            const cvcl_u8 *sr = cvcl_image_row(src, y);
            cvcl_u8       *dr = cvcl_image_row(dst, y);
            for (cvcl_i32 x = 0; x < w; x++) {
                dr[x*4+0] = sr[x*3+0];
                dr[x*4+1] = sr[x*3+1];
                dr[x*4+2] = sr[x*3+2];
                dr[x*4+3] = 255;
            }
        }
        return CVCL_OK;
    }

    /* RGBA -> RGB (drop alpha) */
    if (src->channels == 4 && dst->channels == 3) {
        for (cvcl_i32 y = 0; y < h; y++) {
            const cvcl_u8 *sr = cvcl_image_row(src, y);
            cvcl_u8       *dr = cvcl_image_row(dst, y);
            for (cvcl_i32 x = 0; x < w; x++) {
                dr[x*3+0] = sr[x*4+0];
                dr[x*3+1] = sr[x*4+1];
                dr[x*3+2] = sr[x*4+2];
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
    CVCL_CHECK_NULL(dst); CVCL_CHECK_NULL(src); CVCL_CHECK_NULL(src->data);
    CVCL_CHECK_ARG(src->depth == CVCL_DEPTH_U8);

    cvcl_i32 w = src->width, h = src->height, ch = src->channels;
    cvcl_result_t rc;

    if (angle == CVCL_ROTATE_180) {
        rc = cvcl_image_create(dst, w, h, ch, CVCL_DEPTH_U8, alloc);
        if (rc != CVCL_OK) return rc;
        for (cvcl_i32 y = 0; y < h; y++) {
            const cvcl_u8 *sr = cvcl_image_row(src, h - 1 - y);
            cvcl_u8       *dr = cvcl_image_row(dst, y);
            for (cvcl_i32 x = 0; x < w; x++) {
                const cvcl_u8 *sp = sr + (w - 1 - x) * ch;
                cvcl_u8       *dp = dr + x * ch;
                for (cvcl_i32 c = 0; c < ch; c++) dp[c] = sp[c];
            }
        }
        return CVCL_OK;
    }

    /* 90 CW: dst(x,y) = src(h-1-y, x)  → dst is h×w */
    /* 90 CCW: dst(x,y) = src(y, w-1-x) → dst is h×w */
    rc = cvcl_image_create(dst, h, w, ch, CVCL_DEPTH_U8, alloc);
    if (rc != CVCL_OK) return rc;

    for (cvcl_i32 y = 0; y < h; y++) {
        const cvcl_u8 *sr = cvcl_image_row(src, y);
        for (cvcl_i32 x = 0; x < w; x++) {
            const cvcl_u8 *sp = sr + x * ch;
            cvcl_u8 *dp;
            if (angle == CVCL_ROTATE_90CW)
                dp = cvcl_image_row(dst, x) + (h - 1 - y) * ch;
            else /* CCW */
                dp = cvcl_image_row(dst, w - 1 - x) + y * ch;
            for (cvcl_i32 c = 0; c < ch; c++) dp[c] = sp[c];
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
    CVCL_CHECK_NULL(dst); CVCL_CHECK_NULL(src);
    CVCL_CHECK_NULL(dst->data); CVCL_CHECK_NULL(src->data);
    CVCL_CHECK_NULL(M);
    CVCL_CHECK_ARG(src->depth == CVCL_DEPTH_U8);

    cvcl_i32 dw = dst->width, dh = dst->height;
    cvcl_i32 sw = src->width, sh = src->height, ch = src->channels;

    /* Inverse map: for each dst pixel, find src pixel */
    for (cvcl_i32 dy = 0; dy < dh; dy++) {
        cvcl_u8 * CVCL_RESTRICT dr = cvcl_image_row(dst, dy);
        for (cvcl_i32 dx = 0; dx < dw; dx++) {
            cvcl_f32 sx = M[0]*(cvcl_f32)dx + M[1]*(cvcl_f32)dy + M[2];
            cvcl_f32 sy = M[3]*(cvcl_f32)dx + M[4]*(cvcl_f32)dy + M[5];

            if (interp == CVCL_INTERP_NEAREST) {
                cvcl_i32 ix = (cvcl_i32)(sx + 0.5f);
                cvcl_i32 iy = (cvcl_i32)(sy + 0.5f);
                if (ix < 0 || ix >= sw || iy < 0 || iy >= sh) {
                    if (border == CVCL_BORDER_ZERO)
                        for (cvcl_i32 c=0;c<ch;c++) dr[dx*ch+c]=0;
                    else {
                        ix = CVCL_CLAMP(ix,0,sw-1);
                        iy = CVCL_CLAMP(iy,0,sh-1);
                        const cvcl_u8 *sp = cvcl_image_row(src,iy)+ix*ch;
                        for (cvcl_i32 c=0;c<ch;c++) dr[dx*ch+c]=sp[c];
                    }
                } else {
                    const cvcl_u8 *sp = cvcl_image_row(src,iy)+ix*ch;
                    for (cvcl_i32 c=0;c<ch;c++) dr[dx*ch+c]=sp[c];
                }
            } else {
                /* Bilinear */
                cvcl_i32 x0=CVCL_CLAMP((cvcl_i32)sx,  0,sw-1);
                cvcl_i32 x1=CVCL_CLAMP((cvcl_i32)sx+1,0,sw-1);
                cvcl_i32 y0=CVCL_CLAMP((cvcl_i32)sy,  0,sh-1);
                cvcl_i32 y1=CVCL_CLAMP((cvcl_i32)sy+1,0,sh-1);
                cvcl_f32 fx=sx-(cvcl_f32)(cvcl_i32)sx;
                cvcl_f32 fy=sy-(cvcl_f32)(cvcl_i32)sy;
                const cvcl_u8 *r0=cvcl_image_row(src,y0);
                const cvcl_u8 *r1=cvcl_image_row(src,y1);
                for (cvcl_i32 c=0;c<ch;c++) {
                    cvcl_f32 v=(r0[x0*ch+c]*(1-fx)+r0[x1*ch+c]*fx)*(1-fy)
                              +(r1[x0*ch+c]*(1-fx)+r1[x1*ch+c]*fx)*fy;
                    dr[dx*ch+c]=(cvcl_u8)CVCL_CLAMP((cvcl_i32)(v+0.5f),0,255);
                }
            }
        }
    }
    return CVCL_OK;
}

/* -------------------------------------------------------------------------
 * Bicubic resize
 * ---------------------------------------------------------------------- */


