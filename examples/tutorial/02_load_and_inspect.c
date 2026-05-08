/**
 * @file 02_load_and_inspect.c
 * @brief Tutorial 02: Load an image and inspect its pixels
 *
 * Concepts covered:
 *   - cvcl_io_read_ppm
 *   - cvcl_image_row (the right way to iterate pixels)
 *   - cvcl_get_u8 for random access
 *   - Zero-copy crop via cvcl_image_view
 *   - Why you should NOT free a view
 *
 * Run:
 *   ./02_load_and_inspect hello.ppm
 *   (run 01_hello_image first to generate hello.ppm)
 */

#include <cvcl/cvcl.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[]) {
    printf("=== Tutorial 02: Load and Inspect ===\n\n");

    const char *path = (argc > 1) ? argv[1] : "hello.png";

    /* ------------------------------------------------------------------
     * Step 1: Load the image
     * ------------------------------------------------------------------ */
    cvcl_image_t img;
    cvcl_result_t rc = cvcl_io_read_png_native(&img, path, NULL);
    if (rc != CVCL_OK) rc = cvcl_io_read_ppm(&img, path, NULL);
    if (rc != CVCL_OK) rc = cvcl_io_read_ppm(&img, "assets/hello.ppm", NULL);
    if (rc != CVCL_OK) {
        fprintf(stderr, "Cannot load '%s': %s\n", path, cvcl_strerror(rc));
        fprintf(stderr, "Run 01_hello_image first.\n");
        return 1;
    }

    printf("Loaded:   %s\n", path);
    printf("Size:     %dx%d\n", img.width, img.height);
    printf("Channels: %d (%s)\n", img.channels,
           img.channels == 1 ? "grayscale" :
           img.channels == 3 ? "RGB" : "RGBA");
    printf("Stride:   %d bytes/row\n", img.stride);
    printf("Buffer:   %zu bytes\n\n", cvcl_image_buffer_size(&img));

    /* ------------------------------------------------------------------
     * Step 2: Iterate pixels the right way -- cache the row pointer
     *
     * WRONG (slow): cvcl_get_u8(&img, x, y, c) inside x-loop
     *   Each call recomputes y * stride -- a multiply per pixel.
     *
     * RIGHT (fast): cache cvcl_image_row once per y, then index directly.
     *   The compiler can then vectorize the x-loop.
     * ------------------------------------------------------------------ */
    printf("Corner pixels (R,G,B):\n");
    printf("  Top-left:     (%3d,%3d,%3d)\n",
           cvcl_get_u8(&img, 0, 0, 0),
           cvcl_get_u8(&img, 0, 0, 1),
           cvcl_get_u8(&img, 0, 0, 2));
    printf("  Top-right:    (%3d,%3d,%3d)\n",
           cvcl_get_u8(&img, img.width-1, 0, 0),
           cvcl_get_u8(&img, img.width-1, 0, 1),
           cvcl_get_u8(&img, img.width-1, 0, 2));
    printf("  Bottom-left:  (%3d,%3d,%3d)\n",
           cvcl_get_u8(&img, 0, img.height-1, 0),
           cvcl_get_u8(&img, 0, img.height-1, 1),
           cvcl_get_u8(&img, 0, img.height-1, 2));
    printf("  Bottom-right: (%3d,%3d,%3d)\n\n",
           cvcl_get_u8(&img, img.width-1, img.height-1, 0),
           cvcl_get_u8(&img, img.width-1, img.height-1, 1),
           cvcl_get_u8(&img, img.width-1, img.height-1, 2));

    /* ------------------------------------------------------------------
     * Step 3: Compute average brightness using row pointer pattern
     * ------------------------------------------------------------------ */
    cvcl_u64 sum = 0;
    cvcl_i32 total_samples = img.width * img.height * img.channels;

    for (cvcl_i32 y = 0; y < img.height; y++) {
        const cvcl_u8 *row = cvcl_image_row(&img, y);  /* one multiply here */
        for (cvcl_i32 x = 0; x < img.width * img.channels; x++)
            sum += row[x];  /* no multiply in the inner loop */
    }
    printf("Average pixel value: %.1f / 255\n\n",
           (double)sum / (double)total_samples);

    /* ------------------------------------------------------------------
     * Step 4: Zero-copy crop (view into a sub-region)
     *
     * cvcl_image_view does NOT allocate memory. The view's data pointer
     * points into img's buffer. The stride stays the same -- this is
     * the key insight that makes zero-copy cropping possible.
     *
     * Rule: NEVER call cvcl_image_free on a view. The view doesn't own
     * the buffer. Only free the original image.
     * ------------------------------------------------------------------ */
    cvcl_rect_t roi = {64, 64, 128, 128};  /* x, y, width, height */
    cvcl_image_t view;
    rc = cvcl_image_view(&view, &img, roi);
    if (rc == CVCL_OK) {
        printf("Zero-copy crop: rect {%d,%d,%d,%d}\n",
               roi.x, roi.y, roi.w, roi.h);
        printf("  View size:   %dx%d\n", view.width, view.height);
        printf("  View stride: %d (same as original -- no copy)\n",
               view.stride);
        printf("  View data:   %p\n", (void *)view.data);
        printf("  Img data:    %p\n", (void *)img.data);
        printf("  Offset:      %td bytes\n\n",
               view.data - img.data);

        /* Read a pixel through the view -- same data as original */
        printf("  view(0,0,R) = %d  ==  img(%d,%d,R) = %d\n",
               cvcl_get_u8(&view, 0, 0, 0),
               roi.x, roi.y,
               cvcl_get_u8(&img, roi.x, roi.y, 0));
        /* DO NOT: cvcl_image_free(&view, NULL); */
    }

    /* ------------------------------------------------------------------
     * Step 5: Clone (deep copy into new allocation)
     * ------------------------------------------------------------------ */
    cvcl_image_t clone;
    rc = cvcl_image_clone(&clone, &img, NULL);
    if (rc == CVCL_OK) {
        printf("\nDeep clone: separate buffer at %p\n", (void *)clone.data);
        printf("Modifying clone does not affect original.\n");
        cvcl_set_u8(&clone, 0, 0, 0, 42);
        printf("  clone(0,0,R)=%d   img(0,0,R)=%d  (independent)\n",
               cvcl_get_u8(&clone, 0, 0, 0),
               cvcl_get_u8(&img, 0, 0, 0));
        cvcl_image_free(&clone, NULL);
    }

    cvcl_image_free(&img, NULL);
    printf("\nNext: run 03_channels to convert between color spaces.\n");
    return 0;
}
