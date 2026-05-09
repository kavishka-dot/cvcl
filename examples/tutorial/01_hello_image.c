/**
 * @file 01_hello_image.c
 * @brief Tutorial 01: Create, fill, and save your first image
 *
 * Concepts covered:
 *   - cvcl_image_t descriptor (width, height, channels, stride, depth)
 *   - cvcl_image_create / cvcl_image_free
 *   - cvcl_fill and cvcl_set_u8
 *   - cvcl_io_write_png_native (zero-dependency PNG writer)
 *   - Why stride >= width*channels (64-byte alignment for SIMD)
 *
 * Run:
 *   ./01_hello_image
 *   Output: hello.png (open directly in any image viewer)
 */

#include <cvcl/cvcl.h>
#include <stdio.h>

int main(void) {
    printf("=== Tutorial 01: Hello Image ===\n\n");

    /* ------------------------------------------------------------------
     * Step 1: Create an image
     * ------------------------------------------------------------------ */
    cvcl_image_t img;
    cvcl_result_t rc = cvcl_image_create(&img, 256, 256, 3, CVCL_DEPTH_U8, NULL);
    if (rc != CVCL_OK) {
        fprintf(stderr, "Failed to create image: %s\n", cvcl_strerror(rc));
        return 1;
    }

    printf("Created:  %dx%d, %d channels, depth=%d\n",
           img.width, img.height, img.channels, img.depth);
    printf("Stride:   %d bytes/row (width*ch=%d, padded to 64-byte boundary)\n",
           img.stride, img.width * img.channels);
    printf("Buffer:   %zu bytes total\n\n", cvcl_image_buffer_size(&img));

    /* ------------------------------------------------------------------
     * Step 2: Paint a gradient using the row pointer pattern
     * ------------------------------------------------------------------ */
    cvcl_fill(&img, 0);

    for (cvcl_i32 y = 0; y < img.height; y++) {
        cvcl_u8 *row = cvcl_image_row(&img, y);
        for (cvcl_i32 x = 0; x < img.width; x++) {
            row[x * 3 + 0] = (cvcl_u8)(x);      /* R */
            row[x * 3 + 1] = (cvcl_u8)(y);      /* G */
            row[x * 3 + 2] = 128;               /* B */
        }
    }
    printf("Painted a red/green gradient.\n");

    /* ------------------------------------------------------------------
     * Step 3: Draw a white cross in the center
     * ------------------------------------------------------------------ */
    cvcl_i32 cx = img.width / 2, cy = img.height / 2;
    for (cvcl_i32 i = -20; i <= 20; i++) {
        cvcl_set_u8(&img, cx + i, cy, 0, 255);
        cvcl_set_u8(&img, cx + i, cy, 1, 255);
        cvcl_set_u8(&img, cx + i, cy, 2, 255);
        cvcl_set_u8(&img, cx, cy + i, 0, 255);
        cvcl_set_u8(&img, cx, cy + i, 1, 255);
        cvcl_set_u8(&img, cx, cy + i, 2, 255);
    }
    printf("Drew a white cross at center (%d,%d).\n\n", cx, cy);

    /* ------------------------------------------------------------------
     * Step 4: Save as PNG -- no stb_image required
     *
     * cvcl_io_write_png_native writes a valid PNG file using
     * uncompressed DEFLATE. No zlib, no external dependencies.
     * Files are slightly larger than compressed PNG but open in
     * every image viewer on every platform.
     * ------------------------------------------------------------------ */
    rc = cvcl_io_write_png_native(&img, "hello.png");
    if (rc != CVCL_OK) {
        fprintf(stderr, "Failed to write PNG: %s\n", cvcl_strerror(rc));
        cvcl_image_free(&img, NULL);
        return 1;
    }
    printf("Saved:    hello.png  (open directly -- no conversion needed)\n\n");

    /* Also save PPM as fallback */
    cvcl_io_write_ppm(&img, "hello.ppm");
    printf("Saved:    hello.ppm  (fallback for PPM viewers)\n\n");

    cvcl_image_free(&img, NULL);
    printf("Image freed. All done.\n");
    printf("\nNext: run 02_load_and_inspect to read it back.\n");
    return 0;
}
