/**
 * @file connected_components.c
 * @brief Two-pass connected components labeling (union-find)
 *
 * Labels each connected region of non-zero pixels in a binary U8 image.
 * Uses 4-connectivity (up, left, right, down neighbors).
 *
 * Output: label image (U16) where each connected region has a unique ID > 0.
 *         Background (zero pixels) → label 0.
 */

#include <cvcl/cvcl_cc.h>
#include <string.h>
#include <stdlib.h>

/* -------------------------------------------------------------------------
 * Union-Find with path compression and union by rank
 * ---------------------------------------------------------------------- */
static cvcl_i32 uf_find(cvcl_i32 *parent, cvcl_i32 x) {
    while (parent[x] != x) {
        parent[x] = parent[parent[x]];  /* path compression */
        x = parent[x];
    }
    return x;
}

static void uf_union(cvcl_i32 *parent, cvcl_i32 *rank,
                      cvcl_i32 a, cvcl_i32 b) {
    a = uf_find(parent, a);
    b = uf_find(parent, b);
    if (a == b) return;
    if (rank[a] < rank[b]) { cvcl_i32 t=a; a=b; b=t; }
    parent[b] = a;
    if (rank[a] == rank[b]) rank[a]++;
}

/* -------------------------------------------------------------------------
 * Public: connected components
 * ---------------------------------------------------------------------- */
cvcl_result_t cvcl_connected_components(cvcl_cc_result_t      *result,
                                          const cvcl_image_t    *src,
                                          const cvcl_allocator_t *alloc) {
    CVCL_CHECK_NULL(result); CVCL_CHECK_NULL(src); CVCL_CHECK_NULL(src->data);
    CVCL_CHECK_ARG(src->depth    == CVCL_DEPTH_U8);
    CVCL_CHECK_ARG(src->channels == 1);

    cvcl_i32 w = src->width, h = src->height;
    cvcl_size n = (cvcl_size)w * h;
    const cvcl_allocator_t *a = alloc ? alloc : cvcl_default_allocator();

    /* Allocate label image (U16) */
    result->labels = (cvcl_u16 *)cvcl_alloc(a, n * sizeof(cvcl_u16));
    if (!result->labels) return CVCL_ERR_ALLOC;
    memset(result->labels, 0, n * sizeof(cvcl_u16));

    /* Allocate union-find structures (max labels = n/2 + 1) */
    cvcl_size max_labels = n / 2 + 2;
    cvcl_i32 *parent = (cvcl_i32 *)malloc(max_labels * sizeof(cvcl_i32));
    cvcl_i32 *rank   = (cvcl_i32 *)calloc(max_labels,  sizeof(cvcl_i32));
    if (!parent || !rank) {
        free(parent); free(rank);
        cvcl_free(a, result->labels);
        result->labels = NULL;
        return CVCL_ERR_ALLOC;
    }
    for (cvcl_size i = 0; i < max_labels; i++) parent[i] = (cvcl_i32)i;

    cvcl_i32 next_label = 1;

    /* ---- First pass: assign provisional labels ---- */
    for (cvcl_i32 y = 0; y < h; y++) {
        const cvcl_u8 *row = cvcl_image_row(src, y);
        for (cvcl_i32 x = 0; x < w; x++) {
            if (!row[x]) continue;  /* background */

            cvcl_i32 above = (y > 0) ? result->labels[(cvcl_size)(y-1)*w + x] : 0;
            cvcl_i32 left  = (x > 0) ? result->labels[(cvcl_size) y   *w + x-1] : 0;

            if (!above && !left) {
                /* New label */
                if (next_label >= (cvcl_i32)max_labels) {
                    /* Grow (rare) -- just clamp for safety */
                    next_label = (cvcl_i32)max_labels - 1;
                }
                result->labels[(cvcl_size)y*w+x] = (cvcl_u16)next_label;
                next_label++;
            } else if (above && !left) {
                result->labels[(cvcl_size)y*w+x] = (cvcl_u16)above;
            } else if (!above && left) {
                result->labels[(cvcl_size)y*w+x] = (cvcl_u16)left;
            } else {
                /* Both: merge */
                cvcl_i32 ra = uf_find(parent, above);
                cvcl_i32 rl = uf_find(parent, left);
                uf_union(parent, rank, ra, rl);
                result->labels[(cvcl_size)y*w+x] = (cvcl_u16)CVCL_MIN(ra, rl);
            }
        }
    }

    /* ---- Second pass: flatten labels ---- */
    /* Build compact label map */
    cvcl_i32 *remap = (cvcl_i32 *)calloc((cvcl_size)next_label, sizeof(cvcl_i32));
    if (!remap) { free(parent); free(rank); return CVCL_ERR_ALLOC; }

    cvcl_i32 num_labels = 0;
    for (cvcl_size i = 0; i < n; i++) {
        if (!result->labels[i]) continue;
        cvcl_i32 root = uf_find(parent, result->labels[i]);
        if (!remap[root]) remap[root] = ++num_labels;
        result->labels[i] = (cvcl_u16)remap[root];
    }

    result->num_labels = num_labels;
    result->width      = w;
    result->height     = h;

    free(parent); free(rank); free(remap);
    return CVCL_OK;
}

void cvcl_cc_free(cvcl_cc_result_t *result, const cvcl_allocator_t *alloc) {
    if (!result || !result->labels) return;
    const cvcl_allocator_t *a = alloc ? alloc : cvcl_default_allocator();
    cvcl_free(a, result->labels);
    result->labels = NULL;
    result->num_labels = 0;
}

/* -------------------------------------------------------------------------
 * Compute bounding boxes for each label
 * ---------------------------------------------------------------------- */
cvcl_result_t cvcl_cc_bboxes(cvcl_rect_t          **boxes,
                               cvcl_i32              *count,
                               const cvcl_cc_result_t *cc) {
    CVCL_CHECK_NULL(boxes); CVCL_CHECK_NULL(count); CVCL_CHECK_NULL(cc);
    if (cc->num_labels == 0) { *boxes=NULL; *count=0; return CVCL_OK; }

    cvcl_rect_t *bb = (cvcl_rect_t *)malloc(
        (cvcl_size)cc->num_labels * sizeof(cvcl_rect_t));
    if (!bb) return CVCL_ERR_ALLOC;

    /* Initialize bounding boxes to invalid */
    for (cvcl_i32 i = 0; i < cc->num_labels; i++) {
        bb[i].x = cc->width;  bb[i].y = cc->height;
        bb[i].w = 0;          bb[i].h = 0;
    }

    cvcl_i32 w = cc->width, h = cc->height;
    for (cvcl_i32 y = 0; y < h; y++) {
        for (cvcl_i32 x = 0; x < w; x++) {
            cvcl_i32 lbl = cc->labels[(cvcl_size)y*w+x];
            if (!lbl) continue;
            lbl--;
            /* Expand bbox */
            if (x < bb[lbl].x) bb[lbl].x = x;
            if (y < bb[lbl].y) bb[lbl].y = y;
            cvcl_i32 x1 = bb[lbl].x + bb[lbl].w;
            cvcl_i32 y1 = bb[lbl].y + bb[lbl].h;
            if (x > x1) bb[lbl].w = x - bb[lbl].x + 1;
            if (y > y1) bb[lbl].h = y - bb[lbl].y + 1;
        }
    }

    *boxes = bb;
    *count = cc->num_labels;
    return CVCL_OK;
}
