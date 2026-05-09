/**
 * @file pyramid.c
 * @brief Image pyramid -- Gaussian and Laplacian
 */

#include <cvcl/cvcl_platform.h>
#include <cvcl/cvcl_transform.h>
#include <cvcl/cvcl_filter.h>

static cvcl_result_t pyrdown(cvcl_image_t *dst, const cvcl_image_t *src,
                               const cvcl_allocator_t *alloc) {
    cvcl_i32 dw = CVCL_MAX(src->width  / 2, 1);
    cvcl_i32 dh = CVCL_MAX(src->height / 2, 1);
    cvcl_image_t blurred;
    CVCL_CHECK(cvcl_image_create(&blurred, src->width, src->height,
                                  src->channels, CVCL_DEPTH_U8, NULL));
    cvcl_result_t rc = cvcl_blur_gaussian(&blurred, src, 5, 1.f, CVCL_BORDER_REPLICATE);
    if (rc != CVCL_OK) { cvcl_image_free(&blurred, NULL); return rc; }
    rc = cvcl_image_create(dst, dw, dh, src->channels, CVCL_DEPTH_U8, alloc);
    if (rc != CVCL_OK) { cvcl_image_free(&blurred, NULL); return rc; }
    rc = cvcl_resize(dst, &blurred, CVCL_INTERP_BILINEAR);
    cvcl_image_free(&blurred, NULL);
    return rc;
}

static cvcl_result_t pyrup_to(cvcl_image_t *dst, const cvcl_image_t *src,
                                cvcl_i32 tw, cvcl_i32 th,
                                const cvcl_allocator_t *alloc) {
    CVCL_CHECK(cvcl_image_create(dst, tw, th, src->channels, CVCL_DEPTH_U8, alloc));
    return cvcl_resize(dst, src, CVCL_INTERP_BILINEAR);
}

cvcl_result_t cvcl_pyramid_gaussian(cvcl_image_t          *levels,
                                      cvcl_i32               n_levels,
                                      const cvcl_image_t    *src,
                                      const cvcl_allocator_t *alloc) {
    CVCL_CHECK_NULL(levels); CVCL_CHECK_NULL(src);
    CVCL_CHECK_ARG(n_levels >= 1 && n_levels <= 16);
    CVCL_CHECK_ARG(src->depth == CVCL_DEPTH_U8);

    CVCL_CHECK(cvcl_image_clone(&levels[0], src, alloc));

    for (cvcl_i32 i = 1; i < n_levels; i++) {
        if (levels[i-1].width < 2 || levels[i-1].height < 2) {
            /* Too small -- copy previous level */
            for (cvcl_i32 j = i; j < n_levels; j++)
                cvcl_image_clone(&levels[j], &levels[i-1], alloc);
            break;
        }
        cvcl_result_t rc = pyrdown(&levels[i], &levels[i-1], alloc);
        if (rc != CVCL_OK) {
            for (cvcl_i32 j = 0; j < i; j++) cvcl_image_free(&levels[j], alloc);
            return rc;
        }
    }
    return CVCL_OK;
}

cvcl_result_t cvcl_pyramid_laplacian(cvcl_image_t          *levels,
                                       cvcl_i32               n_levels,
                                       const cvcl_image_t    *src,
                                       const cvcl_allocator_t *alloc) {
    CVCL_CHECK_NULL(levels); CVCL_CHECK_NULL(src);
    CVCL_CHECK_ARG(n_levels >= 2 && n_levels <= 16);

    /* Build Gaussian pyramid into temp */
    cvcl_image_t *gpyr = (cvcl_image_t *)CVCL_CALLOC(
        (cvcl_size)n_levels, sizeof(cvcl_image_t));
    if (!gpyr) return CVCL_ERR_ALLOC;

    cvcl_result_t rc = cvcl_pyramid_gaussian(gpyr, n_levels, src, NULL);
    if (rc != CVCL_OK) { CVCL_FREE(gpyr); return rc; }

    for (cvcl_i32 i = 0; i < n_levels-1; i++) {
        cvcl_image_t up;
        pyrup_to(&up, &gpyr[i+1], gpyr[i].width, gpyr[i].height, NULL);

        rc = cvcl_image_create(&levels[i], gpyr[i].width, gpyr[i].height,
                                gpyr[i].channels, CVCL_DEPTH_U8, alloc);
        if (rc != CVCL_OK) { cvcl_image_free(&up, NULL); break; }

        for (cvcl_i32 y = 0; y < gpyr[i].height; y++) {
            const cvcl_u8 *g = cvcl_image_row(&gpyr[i], y);
            const cvcl_u8 *u = cvcl_image_row(&up, y);
            cvcl_u8 *d = cvcl_image_row(&levels[i], y);
            for (cvcl_i32 p = 0; p < gpyr[i].width*gpyr[i].channels; p++) {
                cvcl_i32 diff = (cvcl_i32)g[p] - (cvcl_i32)u[p] + 128;
                d[p] = (cvcl_u8)CVCL_CLAMP(diff, 0, 255);
            }
        }
        cvcl_image_free(&up, NULL);
    }
    cvcl_image_clone(&levels[n_levels-1], &gpyr[n_levels-1], alloc);

    for (cvcl_i32 i = 0; i < n_levels; i++) cvcl_image_free(&gpyr[i], NULL);
    CVCL_FREE(gpyr);
    return CVCL_OK;
}

void cvcl_pyramid_free(cvcl_image_t *levels, cvcl_i32 n_levels,
                         const cvcl_allocator_t *alloc) {
    if (!levels) return;
    for (cvcl_i32 i = 0; i < n_levels; i++)
        cvcl_image_free(&levels[i], alloc);
}
