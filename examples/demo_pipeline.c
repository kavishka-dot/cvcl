/**
 * @file demo_pipeline.c
 * @brief CVCL demo: load PNG/JPEG, Gaussian blur + Canny edges, save results.
 *
 * Usage:
 *   ./demo_pipeline <input.png|jpg|ppm> <output_prefix>
 *
 * Outputs:
 *   <prefix>_gray.png   -- grayscale
 *   <prefix>_blur.png   -- Gaussian blurred
 *   <prefix>_edges.png  -- Canny edge map
 */

#include <cvcl/cvcl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void make_path(char *buf, size_t n, const char *pre, const char *suf) {
    snprintf(buf, n, "%s%s", pre, suf);
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <input> <output_prefix>\n", argv[0]);
        return 1;
    }

    char path[512];
    cvcl_image_t src, gray, blurred, edges;
    cvcl_result_t rc;

    printf("CVCL v%s\n", CVCL_VERSION_STRING);

    /* 1. Load */
#ifdef CVCL_WITH_STB
    rc = cvcl_io_read(&src, argv[1], 0, NULL);
#else
    rc = cvcl_io_read_ppm(&src, argv[1], NULL);
#endif
    if (rc != CVCL_OK) { fprintf(stderr, "Load failed: %s\n", cvcl_strerror(rc)); return 1; }
    printf("[1/4] Loaded: %dx%d %dch\n", src.width, src.height, src.channels);

    /* 2. Grayscale */
    if (src.channels == 1) {
        rc = cvcl_image_clone(&gray, &src, NULL);
    } else {
        rc = cvcl_image_create(&gray, src.width, src.height, 1, CVCL_DEPTH_U8, NULL);
        if (rc == CVCL_OK) rc = cvcl_convert_channels(&gray, &src);
    }
    if (rc != CVCL_OK) { fprintf(stderr, "Gray failed\n"); return 1; }
    make_path(path, sizeof(path), argv[2], "_gray.png");
#ifdef CVCL_WITH_STB
    cvcl_io_write_png(&gray, path);
#else
    cvcl_io_write_ppm(&gray, path);
#endif
    printf("[2/4] Grayscale -> %s\n", path);

    /* 3. Gaussian blur */
    rc = cvcl_image_create(&blurred, gray.width, gray.height, 1, CVCL_DEPTH_U8, NULL);
    if (rc == CVCL_OK) rc = cvcl_blur_gaussian(&blurred, &gray, 9, 2.0f, CVCL_BORDER_REPLICATE);
    if (rc != CVCL_OK) { fprintf(stderr, "Blur failed\n"); return 1; }
    make_path(path, sizeof(path), argv[2], "_blur.png");
#ifdef CVCL_WITH_STB
    cvcl_io_write_png(&blurred, path);
#else
    cvcl_io_write_ppm(&blurred, path);
#endif
    printf("[3/4] Blur      -> %s\n", path);

    /* 4. Canny */
    rc = cvcl_canny(&edges, &gray, 30.0f, 80.0f, NULL);
    if (rc != CVCL_OK) { fprintf(stderr, "Canny failed: %s\n", cvcl_strerror(rc)); return 1; }
    make_path(path, sizeof(path), argv[2], "_edges.png");
#ifdef CVCL_WITH_STB
    cvcl_io_write_png(&edges, path);
#else
    cvcl_io_write_ppm(&edges, path);
#endif
    printf("[4/4] Edges     -> %s\n", path);

    cvcl_image_free(&src,     NULL);
    cvcl_image_free(&gray,    NULL);
    cvcl_image_free(&blurred, NULL);
    cvcl_image_free(&edges,   NULL);
    return 0;
}
