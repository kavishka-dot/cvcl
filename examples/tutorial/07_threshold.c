/**
 * @file 07_threshold.c
 * @brief Tutorial 07: Manual threshold, Otsu, and histogram equalization
 *
 * Concepts covered:
 *   - cvcl_threshold (all modes: BINARY, BINARY_INV, TRUNC, TOZERO)
 *   - cvcl_threshold with CVCL_THRESH_OTSU (automatic)
 *   - cvcl_histogram  (per-channel bin counts)
 *   - cvcl_equalize_hist (contrast enhancement)
 *
 * Run:
 *   ./07_threshold hello.ppm
 *   Output: thresh_binary.pgm, thresh_otsu.pgm, equalized.pgm, etc.
 */

#include <cvcl/cvcl.h>
#include <stdio.h>

static void print_hist_summary(const cvcl_u32 *hist, cvcl_i32 n_pixels) {
    cvcl_u32 lo_count = 0, mid_count = 0, hi_count = 0;
    for (cvcl_i32 i = 0;   i < 85;  i++) lo_count  += hist[i];
    for (cvcl_i32 i = 85;  i < 170; i++) mid_count += hist[i];
    for (cvcl_i32 i = 170; i < 256; i++) hi_count  += hist[i];
    printf("  Dark  (0-84):   %5.1f%%\n", 100.f*lo_count /n_pixels);
    printf("  Mid  (85-169):  %5.1f%%\n", 100.f*mid_count/n_pixels);
    printf("  Bright(170-255):%5.1f%%\n", 100.f*hi_count /n_pixels);
}

int main(int argc, char *argv[]) {
    printf("=== Tutorial 07: Threshold & Histogram ===\n\n");

    const char *path = (argc > 1) ? argv[1] : "hello.png";

    cvcl_image_t src_rgb;
    cvcl_result_t _load_rc = cvcl_io_read_png_native(&src_rgb, path, NULL);
    if (_load_rc != CVCL_OK) _load_rc = cvcl_io_read_ppm(&src_rgb, path, NULL);
    if (_load_rc != CVCL_OK) _load_rc = cvcl_io_read_ppm(&src_rgb, "assets/hello.ppm", NULL);
    if (_load_rc != CVCL_OK) {
        fprintf(stderr, "Cannot load '%s'. Run 01_hello_image first.\n", path);
        return 1;
    }

    cvcl_image_t src;
    cvcl_image_create(&src, src_rgb.width, src_rgb.height,
                      1, CVCL_DEPTH_U8, NULL);
    if (src_rgb.channels > 1)
        cvcl_convert_channels(&src, &src_rgb);
    else
        cvcl_image_clone(&src, &src_rgb, NULL);
    cvcl_image_free(&src_rgb, NULL);

    cvcl_image_t dst;
    cvcl_image_create(&dst, src.width, src.height, 1, CVCL_DEPTH_U8, NULL);
    cvcl_i32 n_pixels = src.width * src.height;

    /* ------------------------------------------------------------------
     * Step 1: Histogram -- understand the image's tonal distribution
     * ------------------------------------------------------------------ */
    printf("Histogram (single channel):\n");
    cvcl_u32 hist[256];
    cvcl_histogram(&src, hist);
    print_hist_summary(hist, n_pixels);
    printf("\n");

    /* ------------------------------------------------------------------
     * Step 2: Manual threshold modes
     *
     * BINARY:     out = (src > t) ? max : 0
     * BINARY_INV: out = (src > t) ? 0   : max
     * TRUNC:      out = (src > t) ? t   : src   (clip highlights)
     * TOZERO:     out = (src > t) ? src : 0     (zero shadows)
     * ------------------------------------------------------------------ */
    printf("Threshold modes (t=128, max=255):\n");

    cvcl_threshold(&dst, &src, 128.f, 255.f, CVCL_THRESH_BINARY, NULL);
    cvcl_io_write_png_native(&dst, "thresh_binary.png");
    printf("  BINARY:     saved thresh_binary.pgm\n");

    cvcl_threshold(&dst, &src, 128.f, 255.f, CVCL_THRESH_BINARY_INV, NULL);
    cvcl_io_write_png_native(&dst, "thresh_binary_inv.png");
    printf("  BINARY_INV: saved thresh_binary_inv.pgm\n");

    cvcl_threshold(&dst, &src, 128.f, 255.f, CVCL_THRESH_TRUNC, NULL);
    cvcl_io_write_png_native(&dst, "thresh_trunc.png");
    printf("  TRUNC:      saved thresh_trunc.pgm\n");

    cvcl_threshold(&dst, &src, 128.f, 255.f, CVCL_THRESH_TOZERO, NULL);
    cvcl_io_write_png_native(&dst, "thresh_tozero.png");
    printf("  TOZERO:     saved thresh_tozero.pgm\n\n");

    /* ------------------------------------------------------------------
     * Step 3: Otsu automatic threshold
     *
     * Otsu's method finds the threshold t that maximizes inter-class
     * variance between foreground and background pixels.
     * Works best on bimodal histograms (two clear clusters).
     *
     * The algorithm is O(256) -- it scans the histogram, not the image.
     * ------------------------------------------------------------------ */
    printf("Otsu automatic threshold:\n");
    cvcl_f32 t_auto = 0.f;
    cvcl_threshold(&dst, &src, 0.f, 255.f, CVCL_THRESH_OTSU, &t_auto);
    cvcl_io_write_png_native(&dst, "thresh_otsu.png");
    printf("  Auto threshold: %.0f\n", t_auto);
    printf("  Saved: thresh_otsu.pgm\n\n");

    /* ------------------------------------------------------------------
     * Step 4: Histogram equalization
     *
     * Redistributes pixel intensities so the cumulative histogram
     * is approximately linear -- spreads the tonal range.
     * Useful for low-contrast images (fog, underexposure).
     *
     * Applied in-place. The LUT is built from the CDF of the histogram.
     * ------------------------------------------------------------------ */
    printf("Histogram equalization:\n");

    cvcl_image_t eq;
    cvcl_image_clone(&eq, &src, NULL);

    cvcl_u32 hist_before[256], hist_after[256];
    cvcl_histogram(&eq, hist_before);
    cvcl_equalize_hist(&eq);
    cvcl_histogram(&eq, hist_after);

    printf("  Before equalization:\n");
    print_hist_summary(hist_before, n_pixels);
    printf("  After equalization:\n");
    print_hist_summary(hist_after, n_pixels);

    cvcl_io_write_png_native(&eq, "equalized.png");
    printf("  Saved: equalized.pgm\n\n");

    printf("Tip: Run Otsu on equalized image for better segmentation\n");
    printf("     on low-contrast inputs.\n");

    cvcl_image_free(&src, NULL);
    cvcl_image_free(&dst, NULL);
    cvcl_image_free(&eq,  NULL);

    printf("\nNext: run 08_custom_allocator to control every allocation.\n");
    return 0;
}
