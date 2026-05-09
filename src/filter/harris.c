/**
 * @file harris.c
 * @brief Harris corner detector
 *
 * Algorithm:
 *   1. Compute Ix, Iy via Sobel
 *   2. Compute Ix^2, Iy^2, IxIy (structure tensor components)
 *   3. Gaussian smooth each component
 *   4. R = det(M) - k * trace(M)^2  where M = [[Ixx Ixy][Ixy Iyy]]
 *   5. Non-maximum suppression in local window
 *   6. Threshold R > threshold → corner
 *
 * Output: list of keypoints sorted by response strength.
 */

#include <cvcl/cvcl_platform.h>
#include <cvcl/cvcl_filter.h>
#include <cvcl/cvcl_pixel.h>

/* Comparison for qsort -- descending response */
static int kp_cmp(const void *a, const void *b) {
    const cvcl_keypoint_t *ka = (const cvcl_keypoint_t *)a;
    const cvcl_keypoint_t *kb = (const cvcl_keypoint_t *)b;
    if (kb->response > ka->response) return  1;
    if (kb->response < ka->response) return -1;
    return 0;
}

cvcl_result_t cvcl_harris(cvcl_keypoint_t      **kpts,
                            cvcl_i32              *count,
                            const cvcl_image_t    *src,
                            cvcl_f32               k,
                            cvcl_f32               threshold,
                            cvcl_i32               nms_radius,
                            cvcl_i32               max_kpts,
                            const cvcl_allocator_t *alloc) {
    CVCL_CHECK_NULL(kpts); CVCL_CHECK_NULL(count);
    CVCL_CHECK_NULL(src);  CVCL_CHECK_NULL(src->data);
    CVCL_CHECK_ARG(src->depth    == CVCL_DEPTH_U8);
    CVCL_CHECK_ARG(src->channels == 1);

    if (k          <= 0.f) k          = 0.04f;
    if (threshold  <= 0.f) threshold  = 1e6f;
    if (nms_radius <= 0)   nms_radius = 3;
    if (max_kpts   <= 0)   max_kpts   = 10000;

    cvcl_i32 w = src->width, h = src->height;
    *kpts  = NULL;
    *count = 0;

    /* Step 1: Sobel gradients */
    cvcl_image_t gx, gy;
    const cvcl_allocator_t *a = alloc ? alloc : cvcl_default_allocator();
    CVCL_CHECK(cvcl_image_create(&gx, w, h, 1, CVCL_DEPTH_F32, a));
    if (cvcl_image_create(&gy, w, h, 1, CVCL_DEPTH_F32, a) != CVCL_OK) {
        cvcl_image_free(&gx, a); return CVCL_ERR_ALLOC;
    }
    cvcl_sobel_x(&gx, src, CVCL_BORDER_REPLICATE);
    cvcl_sobel_y(&gy, src, CVCL_BORDER_REPLICATE);

    /* Step 2: Structure tensor components Ixx, Iyy, Ixy (F32) */
    cvcl_image_t Ixx, Iyy, Ixy;
    if (cvcl_image_create(&Ixx, w, h, 1, CVCL_DEPTH_F32, a) != CVCL_OK ||
        cvcl_image_create(&Iyy, w, h, 1, CVCL_DEPTH_F32, a) != CVCL_OK ||
        cvcl_image_create(&Ixy, w, h, 1, CVCL_DEPTH_F32, a) != CVCL_OK) {
        cvcl_image_free(&gx,a); cvcl_image_free(&gy,a);
        cvcl_image_free(&Ixx,a); cvcl_image_free(&Iyy,a);
        return CVCL_ERR_ALLOC;
    }

    for (cvcl_i32 y = 0; y < h; y++) {
        const cvcl_f32 *rx = (const cvcl_f32 *)(cvcl_image_row(&gx, y));
        const cvcl_f32 *ry = (const cvcl_f32 *)(cvcl_image_row(&gy, y));
        cvcl_f32 *rxx = (cvcl_f32 *)(cvcl_image_row(&Ixx, y));
        cvcl_f32 *ryy = (cvcl_f32 *)(cvcl_image_row(&Iyy, y));
        cvcl_f32 *rxy = (cvcl_f32 *)(cvcl_image_row(&Ixy, y));
        for (cvcl_i32 x = 0; x < w; x++) {
            rxx[x] = rx[x] * rx[x];
            ryy[x] = ry[x] * ry[x];
            rxy[x] = rx[x] * ry[x];
        }
    }
    cvcl_image_free(&gx, a);
    cvcl_image_free(&gy, a);

    /* Step 3: Gaussian smooth each component (5x5, sigma=1.0) */
    cvcl_image_t Sxx, Syy, Sxy;
    if (cvcl_image_create(&Sxx, w, h, 1, CVCL_DEPTH_F32, a) != CVCL_OK ||
        cvcl_image_create(&Syy, w, h, 1, CVCL_DEPTH_F32, a) != CVCL_OK ||
        cvcl_image_create(&Sxy, w, h, 1, CVCL_DEPTH_F32, a) != CVCL_OK) {
        cvcl_image_free(&Ixx,a); cvcl_image_free(&Iyy,a);
        cvcl_image_free(&Ixy,a); return CVCL_ERR_ALLOC;
    }
    /* Simple box smooth as approximation -- faster than Gaussian on F32 */
    cvcl_i32 half = 2;
    for (cvcl_i32 y = 0; y < h; y++) {
        cvcl_f32 *sxx = (cvcl_f32*)cvcl_image_row(&Sxx,y);
        cvcl_f32 *syy = (cvcl_f32*)cvcl_image_row(&Syy,y);
        cvcl_f32 *sxy = (cvcl_f32*)cvcl_image_row(&Sxy,y);
        for (cvcl_i32 x = 0; x < w; x++) {
            cvcl_f32 axx=0,ayy=0,axy=0; cvcl_i32 cnt=0;
            for (cvcl_i32 ky=-half; ky<=half; ky++) {
                cvcl_i32 sy=CVCL_CLAMP(y+ky,0,h-1);
                const cvcl_f32 *rxx=(cvcl_f32*)cvcl_image_row(&Ixx,sy);
                const cvcl_f32 *ryy=(cvcl_f32*)cvcl_image_row(&Iyy,sy);
                const cvcl_f32 *rxy=(cvcl_f32*)cvcl_image_row(&Ixy,sy);
                for (cvcl_i32 kx=-half; kx<=half; kx++) {
                    cvcl_i32 sx=CVCL_CLAMP(x+kx,0,w-1);
                    axx+=rxx[sx]; ayy+=ryy[sx]; axy+=rxy[sx]; cnt++;
                }
            }
            sxx[x]=axx/cnt; syy[x]=ayy/cnt; sxy[x]=axy/cnt;
        }
    }
    cvcl_image_free(&Ixx,a); cvcl_image_free(&Iyy,a); cvcl_image_free(&Ixy,a);

    /* Step 4: Compute Harris response R = det - k*trace^2 */
    cvcl_f32 *R = (cvcl_f32 *)cvcl_alloc(a, (cvcl_size)w * h * sizeof(cvcl_f32));
    if (!R) {
        cvcl_image_free(&Sxx,a); cvcl_image_free(&Syy,a);
        cvcl_image_free(&Sxy,a); return CVCL_ERR_ALLOC;
    }

    for (cvcl_i32 y = 0; y < h; y++) {
        const cvcl_f32 *sxx=(cvcl_f32*)cvcl_image_row(&Sxx,y);
        const cvcl_f32 *syy=(cvcl_f32*)cvcl_image_row(&Syy,y);
        const cvcl_f32 *sxy=(cvcl_f32*)cvcl_image_row(&Sxy,y);
        for (cvcl_i32 x = 0; x < w; x++) {
            cvcl_f32 det   = sxx[x]*syy[x] - sxy[x]*sxy[x];
            cvcl_f32 trace = sxx[x] + syy[x];
            R[y*w+x] = det - k * trace * trace;
        }
    }
    cvcl_image_free(&Sxx,a); cvcl_image_free(&Syy,a); cvcl_image_free(&Sxy,a);

    /* Step 5 & 6: NMS + threshold -- collect candidates */
    cvcl_keypoint_t *buf = (cvcl_keypoint_t *)cvcl_alloc(a, 
        (cvcl_size)max_kpts * sizeof(cvcl_keypoint_t));
    if (!buf) { cvcl_free(a, R); return CVCL_ERR_ALLOC; }

    cvcl_i32 found = 0;
    cvcl_i32 border = nms_radius + 1;

    for (cvcl_i32 y = border; y < h-border && found < max_kpts; y++) {
        for (cvcl_i32 x = border; x < w-border && found < max_kpts; x++) {
            cvcl_f32 v = R[y*w+x];
            if (v < threshold) continue;

            /* NMS: must be local maximum */
            int is_max = 1;
            for (cvcl_i32 ky = -nms_radius; ky <= nms_radius && is_max; ky++)
                for (cvcl_i32 kx = -nms_radius; kx <= nms_radius && is_max; kx++) {
                    if (ky==0 && kx==0) continue;
                    if (R[(y+ky)*w+(x+kx)] > v) is_max = 0;
                }

            if (is_max) {
                buf[found].x        = x;
                buf[found].y        = y;
                buf[found].response = v;
                buf[found].scale    = 1.f;
                found++;
            }
        }
    }

    cvcl_free(a, R);

    /* Sort by response descending */
    CVCL_QSORT(buf, (cvcl_size)found, sizeof(cvcl_keypoint_t), kp_cmp);

    *kpts  = buf;
    *count = found;
    return CVCL_OK;
}

void cvcl_keypoints_free(cvcl_keypoint_t *kpts, const cvcl_allocator_t *alloc) {
    const cvcl_allocator_t *a = alloc ? alloc : cvcl_default_allocator();
    cvcl_free(a, kpts);
}
