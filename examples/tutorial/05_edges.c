/**
 * @file 05_edges.c
 * @brief Tutorial 05: Sobel gradients and Canny edge detection
 *
 * Concepts covered:
 *   - cvcl_sobel_x / cvcl_sobel_y  (integer Sobel, F32 output)
 *   - Gradient magnitude from Gx and Gy
 *   - cvcl_canny  (full pipeline: blur → Sobel → NMS → hysteresis)
 *   - Threshold tuning for Canny
 *   - Why input must be single-channel
 *
 * Run:
 *   ./05_edges hello.ppm
 *   Output: sobel_x.ppm, sobel_mag.ppm, edges_canny.ppm
 */

#include <cvcl/cvcl.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>

int main(int argc, char *argv[]) {
    printf("=== Tutorial 05: Edge Detection ===\n\n");

    const char *path = (argc > 1) ? argv[1] : "hello.png";

    /* Load and convert to grayscale -- edge detection requires 1 channel */
    cvcl_image_t src_rgb;
    cvcl_result_t _load_rc = cvcl_io_read_png_native(&src_rgb, path, NULL);
    if (_load_rc != CVCL_OK) _load_rc = cvcl_io_read_ppm(&src_rgb, path, NULL);
    if (_load_rc != CVCL_OK) _load_rc = cvcl_io_read_ppm(&src_rgb, "assets/hello.ppm", NULL);
    if (_load_rc != CVCL_OK) {
        fprintf(stderr, "Cannot load '%s'. Run 01_hello_image first.\n", path);
        return 1;
    }

    cvcl_image_t src;
    if (src_rgb.channels == 1) {
        cvcl_image_clone(&src, &src_rgb, NULL);
    } else {
        cvcl_image_create(&src, src_rgb.width, src_rgb.height,
                          1, CVCL_DEPTH_U8, NULL);
        cvcl_convert_channels(&src, &src_rgb);
    }
    cvcl_image_free(&src_rgb, NULL);
    printf("Working on: %dx%d grayscale\n\n", src.width, src.height);

    cvcl_i32 w = src.width, h = src.height;

    /* ------------------------------------------------------------------
     * Step 1: Sobel X and Y gradients
     *
     * Sobel kernels (3x3):
     *   Kx = [-1  0  1]    Ky = [-1 -2 -1]
     *        [-2  0  2]         [ 0  0  0]
     *        [-1  0  1]         [ 1  2  1]
     *
     * Output depth is F32 (signed floats, range ~-1020 to +1020 for U8 input).
     * CVCL uses integer arithmetic internally -- no float in the inner loop.
     * ------------------------------------------------------------------ */
    cvcl_image_t gx, gy;
    cvcl_image_create(&gx, w, h, 1, CVCL_DEPTH_F32, NULL);
    cvcl_image_create(&gy, w, h, 1, CVCL_DEPTH_F32, NULL);

    cvcl_sobel_x(&gx, &src, CVCL_BORDER_REPLICATE);
    cvcl_sobel_y(&gy, &src, CVCL_BORDER_REPLICATE);
    printf("Sobel gradients computed (F32 output).\n");

    /* Sample gradient values and find the range */
    cvcl_f32 gx_sample = cvcl_get_f32(&gx, w/2, h/2, 0);
    cvcl_f32 gy_sample = cvcl_get_f32(&gy, w/2, h/2, 0);
    cvcl_f32 global_max = 0.f;
    for (cvcl_i32 yi=0; yi<h; yi++)
        for (cvcl_i32 xi=0; xi<w; xi++) {
            cvcl_f32 v = cvcl_get_f32(&gx, xi, yi, 0);
            if (v < 0) v = -v;
            if (v > global_max) global_max = v;
        }
    printf("  Center pixel Gx=%.1f  Gy=%.1f\n", gx_sample, gy_sample);
    printf("  Max |Gx| in image: %.1f  (range depends on image contrast)\n\n",
           global_max);

    /* ------------------------------------------------------------------
     * Step 2: Visualize Sobel X as U8 (normalize to 0-255)
     *
     * Raw Sobel values are signed -- to visualize:
     *   1. Take absolute value
     *   2. Scale to 0-255
     * ------------------------------------------------------------------ */
    cvcl_image_t vis_gx;
    cvcl_image_create(&vis_gx, w, h, 1, CVCL_DEPTH_U8, NULL);

    /* Find max absolute value for normalization */
    cvcl_f32 max_val = 1.f;
    for (cvcl_i32 y = 0; y < h; y++) {
        for (cvcl_i32 x = 0; x < w; x++) {
            cvcl_f32 v = cvcl_get_f32(&gx, x, y, 0);
            if (v < 0) v = -v;
            if (v > max_val) max_val = v;
        }
    }

    for (cvcl_i32 y = 0; y < h; y++) {
        cvcl_u8 *dr = cvcl_image_row(&vis_gx, y);
        for (cvcl_i32 x = 0; x < w; x++) {
            cvcl_f32 v = cvcl_get_f32(&gx, x, y, 0);
            if (v < 0) v = -v;
            dr[x] = (cvcl_u8)(v / max_val * 255.f + 0.5f);
        }
    }
    cvcl_io_write_png_native(&vis_gx, "sobel_x.png");
    printf("Saved: sobel_x.png (absolute Sobel X, normalized)\n");

    /* ------------------------------------------------------------------
     * Step 3: Gradient magnitude image
     *
     * mag = sqrt(Gx^2 + Gy^2)
     * Canny internally uses L1 norm (|Gx|+|Gy|) which is faster.
     * For visualization we use true Euclidean magnitude.
     * ------------------------------------------------------------------ */
    cvcl_image_t vis_mag;
    cvcl_image_create(&vis_mag, w, h, 1, CVCL_DEPTH_U8, NULL);

    cvcl_f32 max_mag = 1.f;
    for (cvcl_i32 y = 0; y < h; y++) {
        for (cvcl_i32 x = 0; x < w; x++) {
            cvcl_f32 gxi = cvcl_get_f32(&gx, x, y, 0);
            cvcl_f32 gyi = cvcl_get_f32(&gy, x, y, 0);
            cvcl_f32 m   = sqrtf(gxi*gxi + gyi*gyi);
            if (m > max_mag) max_mag = m;
        }
    }
    for (cvcl_i32 y = 0; y < h; y++) {
        cvcl_u8 *dr = cvcl_image_row(&vis_mag, y);
        for (cvcl_i32 x = 0; x < w; x++) {
            cvcl_f32 gxi = cvcl_get_f32(&gx, x, y, 0);
            cvcl_f32 gyi = cvcl_get_f32(&gy, x, y, 0);
            cvcl_f32 m   = sqrtf(gxi*gxi + gyi*gyi);
            dr[x] = (cvcl_u8)(m / max_mag * 255.f + 0.5f);
        }
    }
    cvcl_io_write_png_native(&vis_mag, "sobel_mag.png");
    printf("Saved: sobel_mag.png (gradient magnitude)\n\n");

    /* ------------------------------------------------------------------
     * Step 4: Full Canny pipeline
     *
     * Canny internally runs:
     *   1. Gaussian blur  (k=5, sigma=1.4) -- reduce noise
     *   2. Integer Sobel  -- compute gradients
     *   3. NMS            -- thin edges to 1px
     *   4. Double threshold + hysteresis -- keep strong, trace weak
     *
     * Threshold tuning:
     *   lo_thresh: weak edge threshold (connected to strong edges)
     *   hi_thresh: strong edge threshold
     *   Ratio lo:hi typically 1:2 or 1:3
     *
     * Output: binary U8 image (0=no edge, 255=edge)
     * ------------------------------------------------------------------ */
    printf("Canny edge detection:\n");

    cvcl_image_t edges;

    /* Tight thresholds -- only strong edges */
    cvcl_canny(&edges, &src, 50.f, 150.f, NULL);
    cvcl_io_write_png_native(&edges, "edges_tight.png");
    printf("  Saved: edges_tight.png  (lo=50, hi=150)\n");
    cvcl_image_free(&edges, NULL);

    /* Loose thresholds -- more edges including weak ones */
    cvcl_canny(&edges, &src, 20.f, 60.f, NULL);
    cvcl_io_write_png_native(&edges, "edges_loose.png");
    printf("  Saved: edges_loose.png  (lo=20, hi=60)\n");
    cvcl_image_free(&edges, NULL);

    /* Balanced -- usually the best starting point */
    cvcl_canny(&edges, &src, 30.f, 80.f, NULL);
    cvcl_io_write_png_native(&edges, "edges_canny.png");
    printf("  Saved: edges_canny.png  (lo=30, hi=80) -- recommended default\n\n");

    printf("Tip: thresholds scale with image contrast.\n");
    printf("     For dark images use lo=10,hi=30. For bright use lo=50,hi=150.\n");

    cvcl_image_free(&src,     NULL);
    cvcl_image_free(&gx,      NULL);
    cvcl_image_free(&gy,      NULL);
    cvcl_image_free(&vis_gx,  NULL);
    cvcl_image_free(&vis_mag, NULL);
    cvcl_image_free(&edges,   NULL);

    printf("\nNext: run 06_morphology to process binary images.\n");
    return 0;
}
