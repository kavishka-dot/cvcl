/**
 * @file 04_blur.c
 * @brief Tutorial 04: Box blur and Gaussian blur
 *
 * Concepts covered:
 *   - cvcl_blur_box  (O(w*h) sliding window -- same cost for k=5 and k=63)
 *   - cvcl_blur_gaussian (fixed-point Q15 separable)
 *   - When to use each
 *   - Border handling modes
 *   - Pre-allocating dst
 *
 * Run:
 *   ./04_blur hello.ppm
 *   Output: blur_box5.ppm, blur_box31.ppm, blur_gauss5.ppm, blur_gauss15.ppm
 */

#include <cvcl/cvcl.h>
#include <stdio.h>

static void save(const cvcl_image_t *img, const char *path) {
    cvcl_io_write_png_native(img, path);
    printf("  Saved: %s\n", path);
}

int main(int argc, char *argv[]) {
    printf("=== Tutorial 04: Blur ===\n\n");

    const char *path = (argc > 1) ? argv[1] : "hello.png";

    cvcl_image_t src;
    cvcl_result_t _load_rc = cvcl_io_read_png_native(&src, path, NULL);
    if (_load_rc != CVCL_OK) _load_rc = cvcl_io_read_ppm(&src, path, NULL);
    if (_load_rc != CVCL_OK) _load_rc = cvcl_io_read_ppm(&src, "assets/hello.ppm", NULL);
    if (_load_rc != CVCL_OK) {
        fprintf(stderr, "Cannot load '%s'. Run 01_hello_image first.\n", path);
        return 1;
    }

    /* Pre-allocate destination once -- reuse for all blur operations */
    cvcl_image_t dst;
    cvcl_image_create(&dst, src.width, src.height,
                      src.channels, CVCL_DEPTH_U8, NULL);

    /* ------------------------------------------------------------------
     * Box blur: sliding window O(w*h) regardless of kernel size
     *
     * Key insight: a box blur with k=63 costs the same as k=5.
     * The sliding window adds one pixel and removes one pixel per
     * output position -- no inner kernel loop.
     *
     * Use box blur when:
     *   - You need a fast approximation of Gaussian
     *   - Kernel size is large (> 15)
     *   - You can accept the boxy look (sharp frequency cutoff)
     * ------------------------------------------------------------------ */
    printf("Box blur (O(w*h) sliding window):\n");

    cvcl_blur_box(&dst, &src, 5, CVCL_BORDER_REPLICATE);
    save(&dst, "blur_box5.png");

    cvcl_blur_box(&dst, &src, 31, CVCL_BORDER_REPLICATE);
    save(&dst, "blur_box31.png");
    printf("  Note: k=5 and k=31 have identical compute cost.\n\n");

    /* ------------------------------------------------------------------
     * Gaussian blur: fixed-point Q15 separable
     *
     * Two passes: horizontal then vertical (1D kernel each).
     * Cost is O(w*h*k) -- grows with kernel size.
     * But produces a true Gaussian frequency response (smooth rolloff).
     *
     * Use Gaussian when:
     *   - You need accurate frequency response (before Canny, FFT)
     *   - Kernel size is small (< 15)
     *   - You need the smooth look
     *
     * You can specify either ksize or sigma, or both:
     *   ksize=0 → auto from sigma
     *   sigma=0 → auto from ksize
     * ------------------------------------------------------------------ */
    printf("Gaussian blur (fixed-point Q15 separable):\n");

    cvcl_blur_gaussian(&dst, &src, 5, 0.f, CVCL_BORDER_REPLICATE);
    save(&dst, "blur_gauss5.png");

    cvcl_blur_gaussian(&dst, &src, 15, 0.f, CVCL_BORDER_REPLICATE);
    save(&dst, "blur_gauss15.png");

    /* Specify sigma directly */
    cvcl_blur_gaussian(&dst, &src, 0, 3.0f, CVCL_BORDER_REPLICATE);
    save(&dst, "blur_gauss_sigma3.png");
    printf("  sigma=3.0 auto-computed ksize=%d\n\n",
           (cvcl_i32)(3.0f * 6.f + 1.f) | 1);

    /* ------------------------------------------------------------------
     * Border handling modes
     *
     * CVCL_BORDER_REPLICATE -- extend edge pixel (default, usually best)
     * CVCL_BORDER_ZERO      -- pad with zeros (dark border artifact)
     * CVCL_BORDER_REFLECT   -- mirror at the edge
     * ------------------------------------------------------------------ */
    printf("Border modes (Gaussian k=15):\n");

    cvcl_blur_gaussian(&dst, &src, 15, 0.f, CVCL_BORDER_REPLICATE);
    save(&dst, "blur_border_replicate.png");

    cvcl_blur_gaussian(&dst, &src, 15, 0.f, CVCL_BORDER_ZERO);
    save(&dst, "blur_border_zero.png");

    cvcl_blur_gaussian(&dst, &src, 15, 0.f, CVCL_BORDER_REFLECT);
    save(&dst, "blur_border_reflect.png");

    cvcl_image_free(&src, NULL);
    cvcl_image_free(&dst, NULL);

    printf("\nNext: run 05_edges to detect edges with Sobel and Canny.\n");
    return 0;
}
