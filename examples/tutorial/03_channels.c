/**
 * @file 03_channels.c
 * @brief Tutorial 03: Channel conversion (RGB↔Gray, RGB→RGBA)
 *
 * Concepts covered:
 *   - cvcl_convert_channels
 *   - BT.601 luma formula for RGB→Gray
 *   - How to pre-allocate dst before converting
 *   - Why channel count affects stride
 *
 * Run:
 *   ./03_channels hello.ppm
 *   Output: gray.ppm, back_to_rgb.ppm, rgba.ppm
 */

#include <cvcl/cvcl.h>
#include <stdio.h>

static void save_ppm(const cvcl_image_t *img, const char *path) {
    cvcl_result_t rc = cvcl_io_write_png_native(img, path);
    printf("  Saved: %s (%dx%d ch=%d)\n",
           path, img->width, img->height, img->channels);
    if (rc != CVCL_OK)
        fprintf(stderr, "  WARNING: %s\n", cvcl_strerror(rc));
}

int main(int argc, char *argv[]) {
    printf("=== Tutorial 03: Channel Conversion ===\n\n");

    const char *path = (argc > 1) ? argv[1] : "hello.png";

    cvcl_image_t src;
    cvcl_result_t _load_rc = cvcl_io_read_png_native(&src, path, NULL);
    if (_load_rc != CVCL_OK) _load_rc = cvcl_io_read_ppm(&src, path, NULL);
    if (_load_rc != CVCL_OK) _load_rc = cvcl_io_read_ppm(&src, "assets/hello.ppm", NULL);
    if (_load_rc != CVCL_OK) {
        fprintf(stderr, "Cannot load '%s'. Run 01_hello_image first.\n", path);
        return 1;
    }
    printf("Source: %dx%d RGB\n\n", src.width, src.height);

    /* ------------------------------------------------------------------
     * Step 1: RGB → Grayscale (BT.601 luma)
     *
     * The formula is: Y = 0.299*R + 0.587*G + 0.114*B
     * Implemented in integer as: Y = (77*R + 150*G + 29*B) >> 8
     *
     * dst must be pre-allocated with channels=1 before calling.
     * ------------------------------------------------------------------ */
    cvcl_image_t gray;
    cvcl_image_create(&gray, src.width, src.height, 1, CVCL_DEPTH_U8, NULL);
    cvcl_convert_channels(&gray, &src);
    printf("RGB → Gray (BT.601 luma):\n");
    printf("  Formula: Y = (77*R + 150*G + 29*B) >> 8\n");
    printf("  Source pixel (0,0): R=%d G=%d B=%d\n",
           cvcl_get_u8(&src,  0, 0, 0),
           cvcl_get_u8(&src,  0, 0, 1),
           cvcl_get_u8(&src,  0, 0, 2));
    printf("  Gray  pixel (0,0): Y=%d\n",
           cvcl_get_u8(&gray, 0, 0, 0));
    save_ppm(&gray, "gray.png");
    printf("\n");

    /* ------------------------------------------------------------------
     * Step 2: Gray → RGB (replicate channel)
     *
     * Each output pixel gets R=G=B=Y.
     * Useful for displaying grayscale results in an RGB pipeline.
     * ------------------------------------------------------------------ */
    cvcl_image_t back_rgb;
    cvcl_image_create(&back_rgb, gray.width, gray.height, 3, CVCL_DEPTH_U8, NULL);
    cvcl_convert_channels(&back_rgb, &gray);
    printf("Gray → RGB (replicate):\n");
    printf("  Gray pixel (0,0): Y=%d\n",
           cvcl_get_u8(&gray,     0, 0, 0));
    printf("  RGB  pixel (0,0): R=%d G=%d B=%d\n",
           cvcl_get_u8(&back_rgb, 0, 0, 0),
           cvcl_get_u8(&back_rgb, 0, 0, 1),
           cvcl_get_u8(&back_rgb, 0, 0, 2));
    save_ppm(&back_rgb, "back_to_rgb.png");
    printf("\n");

    /* ------------------------------------------------------------------
     * Step 3: RGB → RGBA (alpha = 255)
     *
     * Adds a fully-opaque alpha channel.
     * Useful before compositing or passing to a renderer.
     * ------------------------------------------------------------------ */
    cvcl_image_t rgba;
    cvcl_image_create(&rgba, src.width, src.height, 4, CVCL_DEPTH_U8, NULL);
    cvcl_convert_channels(&rgba, &src);
    printf("RGB → RGBA (alpha=255):\n");
    printf("  RGBA pixel (0,0): R=%d G=%d B=%d A=%d\n",
           cvcl_get_u8(&rgba, 0, 0, 0),
           cvcl_get_u8(&rgba, 0, 0, 1),
           cvcl_get_u8(&rgba, 0, 0, 2),
           cvcl_get_u8(&rgba, 0, 0, 3));
    printf("  (PPM only supports 1 or 3 channels -- RGBA not saved)\n\n");

    /* ------------------------------------------------------------------
     * Step 4: Stride changes with channel count
     *
     * CVCL always pads rows to 64-byte boundaries for SIMD alignment.
     * This means stride depends on width*channels, rounded up.
     * ------------------------------------------------------------------ */
    printf("Stride comparison (all same width=%d):\n", src.width);
    printf("  1 channel: stride=%d bytes\n", gray.stride);
    printf("  3 channel: stride=%d bytes\n", src.stride);
    printf("  4 channel: stride=%d bytes\n", rgba.stride);

    cvcl_image_free(&src,      NULL);
    cvcl_image_free(&gray,     NULL);
    cvcl_image_free(&back_rgb, NULL);
    cvcl_image_free(&rgba,     NULL);

    printf("\nNext: run 04_blur to apply spatial filtering.\n");
    return 0;
}
