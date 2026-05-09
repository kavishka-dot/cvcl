/**
 * @file 06_morphology.c
 * @brief Tutorial 06: Erode, dilate, open, close
 *
 * Concepts covered:
 *   - cvcl_erode / cvcl_dilate
 *   - cvcl_morph_open / cvcl_morph_close
 *   - Why these need single-channel U8 input
 *   - When to use open vs close
 *   - The monotonic deque algorithm (O(n) regardless of kernel size)
 *
 * Run:
 *   ./06_morphology hello.ppm
 *   Output: binary.pgm, eroded.pgm, dilated.pgm, opened.pgm, closed.pgm
 */

#include <cvcl/cvcl.h>
#include <stdio.h>

int main(int argc, char *argv[]) {
    printf("=== Tutorial 06: Morphology ===\n\n");

    const char *path = (argc > 1) ? argv[1] : "hello.png";

    /* Load → gray → Canny edges → binary image to work with */
    cvcl_image_t src_rgb;
    cvcl_result_t _load_rc = cvcl_io_read_png_native(&src_rgb, path, NULL);
    if (_load_rc != CVCL_OK) _load_rc = cvcl_io_read_ppm(&src_rgb, path, NULL);
    if (_load_rc != CVCL_OK) _load_rc = cvcl_io_read_ppm(&src_rgb, "assets/hello.ppm", NULL);
    if (_load_rc != CVCL_OK) {
        fprintf(stderr, "Cannot load '%s'. Run 01_hello_image first.\n", path);
        return 1;
    }

    cvcl_image_t gray;
    cvcl_image_create(&gray, src_rgb.width, src_rgb.height,
                      1, CVCL_DEPTH_U8, NULL);
    if (src_rgb.channels > 1)
        cvcl_convert_channels(&gray, &src_rgb);
    else
        cvcl_image_clone(&gray, &src_rgb, NULL);
    cvcl_image_free(&src_rgb, NULL);

    /* ------------------------------------------------------------------
     * Step 1: Create a binary image via Otsu threshold
     *
     * Morphology works on grayscale too, but it's most commonly applied
     * to binary images after thresholding.
     * ------------------------------------------------------------------ */
    cvcl_image_t binary;
    cvcl_image_create(&binary, gray.width, gray.height,
                      1, CVCL_DEPTH_U8, NULL);

    cvcl_f32 t = 0.f;
    cvcl_threshold(&binary, &gray, 0.f, 255.f, CVCL_THRESH_OTSU, &t);
    printf("Otsu threshold: %.0f\n", t);
    cvcl_io_write_png_native(&binary, "binary.png");
    printf("Saved: binary.pgm\n\n");

    cvcl_image_t dst;
    cvcl_image_create(&dst, gray.width, gray.height,
                      1, CVCL_DEPTH_U8, NULL);

    /* ------------------------------------------------------------------
     * Step 2: Erosion -- shrinks bright (white) regions
     *
     * Each output pixel = minimum of all pixels in the kernel window.
     * Bright regions shrink, small bright specks disappear.
     *
     * Algorithm: monotonic deque -- O(w*h) regardless of kw,kh.
     * Erode(k=5) costs the same as Erode(k=51).
     * ------------------------------------------------------------------ */
    printf("Erosion (3x3 SE):\n");
    cvcl_erode(&dst, &binary, 3, 3, CVCL_BORDER_REPLICATE);
    cvcl_io_write_png_native(&dst, "eroded.png");
    printf("  Saved: eroded.pgm  -- white regions shrink\n\n");

    /* ------------------------------------------------------------------
     * Step 3: Dilation -- expands bright (white) regions
     *
     * Each output pixel = maximum of all pixels in the kernel window.
     * Bright regions expand, small dark holes fill in.
     * ------------------------------------------------------------------ */
    printf("Dilation (3x3 SE):\n");
    cvcl_dilate(&dst, &binary, 3, 3, CVCL_BORDER_REPLICATE);
    cvcl_io_write_png_native(&dst, "dilated.png");
    printf("  Saved: dilated.pgm  -- white regions expand\n\n");

    /* ------------------------------------------------------------------
     * Step 4: Opening = Erode then Dilate
     *
     * Removes small bright specks (noise) without significantly
     * changing the shape of larger bright regions.
     *
     * Rule of thumb: removes bright objects smaller than the SE.
     * ------------------------------------------------------------------ */
    printf("Opening = Erode then Dilate (removes small specks):\n");
    cvcl_morph_open(&dst, &binary, 5, 5, NULL);
    cvcl_io_write_png_native(&dst, "opened.png");
    printf("  Saved: opened.pgm\n\n");

    /* ------------------------------------------------------------------
     * Step 5: Closing = Dilate then Erode
     *
     * Fills small dark holes in bright regions without significantly
     * changing the shape of those regions.
     *
     * Rule of thumb: fills dark objects smaller than the SE.
     * ------------------------------------------------------------------ */
    printf("Closing = Dilate then Erode (fills small holes):\n");
    cvcl_morph_close(&dst, &binary, 5, 5, NULL);
    cvcl_io_write_png_native(&dst, "closed.png");
    printf("  Saved: closed.pgm\n\n");

    /* ------------------------------------------------------------------
     * Step 6: Large kernel -- same cost as small kernel
     *
     * Thanks to the monotonic deque, morphology with k=31 costs
     * the same as k=3. Try it:
     * ------------------------------------------------------------------ */
    printf("Large kernel erode (31x31) -- same algorithmic cost as 3x3:\n");
    cvcl_erode(&dst, &binary, 31, 31, CVCL_BORDER_REPLICATE);
    cvcl_io_write_png_native(&dst, "eroded_large.png");
    printf("  Saved: eroded_large.pgm\n\n");

    printf("Summary:\n");
    printf("  Erode  = min filter  -- shrinks bright\n");
    printf("  Dilate = max filter  -- expands bright\n");
    printf("  Open   = erode+dilate -- removes small bright specks\n");
    printf("  Close  = dilate+erode -- fills small dark holes\n");

    cvcl_image_free(&gray,   NULL);
    cvcl_image_free(&binary, NULL);
    cvcl_image_free(&dst,    NULL);

    printf("\nNext: run 07_threshold to explore thresholding techniques.\n");
    return 0;
}
