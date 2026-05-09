/**
 * @file integral.c
 * @brief Integral image (summed area table) -- O(1) box sum of any size
 *
 * sat[y][x] = sum of all pixels in rect (0,0)→(x-1,y-1)
 *
 * Box sum for rect (x0,y0)→(x1,y1):
 *   sat[y1+1][x1+1] - sat[y0][x1+1] - sat[y1+1][x0] + sat[y0][x0]
 *
 * Applications:
 *   - O(1) box blur of any kernel size
 *   - Fast local statistics (mean, variance)
 *   - Viola-Jones face detection
 *   - BRIEF / FREAK descriptors
 */

#include <cvcl/cvcl_platform.h>
#include <cvcl/cvcl_integral.h>

/* -------------------------------------------------------------------------
 * Build integral image
 * ---------------------------------------------------------------------- */
cvcl_result_t cvcl_integral(cvcl_integral_t       *sat,
                              const cvcl_image_t    *src,
                              const cvcl_allocator_t *alloc) {
    CVCL_CHECK_NULL(sat); CVCL_CHECK_NULL(src); CVCL_CHECK_NULL(src->data);
    CVCL_CHECK_ARG(src->depth    == CVCL_DEPTH_U8);
    CVCL_CHECK_ARG(src->channels == 1);

    cvcl_i32 w = src->width, h = src->height;
    const cvcl_allocator_t *a = alloc ? alloc : cvcl_default_allocator();

    /* Allocate (w+1) x (h+1) table */
    sat->width  = w + 1;
    sat->height = h + 1;
    sat->data   = (cvcl_u32 *)cvcl_alloc(a,
        (cvcl_size)sat->width * sat->height * sizeof(cvcl_u32));
    if (!sat->data) return CVCL_ERR_ALLOC;

    /* Zero top row and left column */
    CVCL_MEMSET(sat->data, 0, (cvcl_size)sat->width * sizeof(cvcl_u32));
    for (cvcl_i32 y = 1; y <= h; y++)
        sat->data[(cvcl_size)y * sat->width] = 0;

    /* Fill SAT */
    for (cvcl_i32 y = 0; y < h; y++) {
        const cvcl_u8 * CVCL_RESTRICT row = cvcl_image_row(src, y);
        cvcl_u32 row_sum = 0;
        for (cvcl_i32 x = 0; x < w; x++) {
            row_sum += row[x];
            cvcl_u32 above = sat->data[(cvcl_size)y * sat->width + (x+1)];
            sat->data[(cvcl_size)(y+1) * sat->width + (x+1)] = row_sum + above;
        }
    }
    return CVCL_OK;
}

void cvcl_integral_free(cvcl_integral_t *sat, const cvcl_allocator_t *alloc) {
    if (!sat || !sat->data) return;
    const cvcl_allocator_t *a = alloc ? alloc : cvcl_default_allocator();
    cvcl_free(a, sat->data);
    sat->data = NULL;
}

/* -------------------------------------------------------------------------
 * Query: sum of rectangle (x0,y0) → (x1,y1) inclusive
 * ---------------------------------------------------------------------- */
cvcl_u32 cvcl_integral_sum(const cvcl_integral_t *sat,
                             cvcl_i32 x0, cvcl_i32 y0,
                             cvcl_i32 x1, cvcl_i32 y1) {
    /* Clamp to valid range */
    x0 = CVCL_CLAMP(x0, 0, sat->width  - 2);
    y0 = CVCL_CLAMP(y0, 0, sat->height - 2);
    x1 = CVCL_CLAMP(x1, 0, sat->width  - 2);
    y1 = CVCL_CLAMP(y1, 0, sat->height - 2);

    cvcl_i32 W = sat->width;
    return sat->data[(y1+1)*W + (x1+1)]
         - sat->data[y0    *W + (x1+1)]
         - sat->data[(y1+1)*W + x0    ]
         + sat->data[y0    *W + x0    ];
}

/* -------------------------------------------------------------------------
 * O(1) box blur using SAT
 * ---------------------------------------------------------------------- */
cvcl_result_t cvcl_blur_box_sat(cvcl_image_t          *dst,
                                  const cvcl_image_t    *src,
                                  cvcl_i32               ksize,
                                  const cvcl_allocator_t *alloc) {
    CVCL_CHECK_NULL(dst); CVCL_CHECK_NULL(src);
    CVCL_CHECK_ARG(src->depth == CVCL_DEPTH_U8);
    CVCL_CHECK_ARG(src->channels == 1);
    CVCL_CHECK_ARG(cvcl_image_compatible(dst, src));
    CVCL_CHECK_ARG(ksize >= 1);
    if (ksize % 2 == 0) ksize++;

    cvcl_integral_t sat;
    CVCL_CHECK(cvcl_integral(&sat, src, alloc));

    cvcl_i32 half = ksize / 2;
    cvcl_i32 w = src->width, h = src->height;
    cvcl_u32 area = (cvcl_u32)(ksize * ksize);

    for (cvcl_i32 y = 0; y < h; y++) {
        cvcl_u8 * CVCL_RESTRICT dr = cvcl_image_row(dst, y);
        for (cvcl_i32 x = 0; x < w; x++) {
            cvcl_u32 s = cvcl_integral_sum(&sat, x-half, y-half, x+half, y+half);
            dr[x] = (cvcl_u8)(s / area);
        }
    }

    cvcl_integral_free(&sat, alloc);
    return CVCL_OK;
}
