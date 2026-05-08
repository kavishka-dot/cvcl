#include <cvcl/cvcl.h>
#include <stdio.h>
#include <time.h>

static double now_ms(void) {
    return (double)clock() * 1000.0 / CLOCKS_PER_SEC;
}

int main(void) {
    cvcl_image_t img;

    const int width  = 4096;
    const int height = 4096;
    const int channels = 4;
    const int iterations = 100;

    printf("=== bench_flip ===\n");
    printf("image: %dx%d ch=%d\n", width, height, channels);
    printf("iterations: %d\n\n", iterations);

    cvcl_result_t rc = cvcl_image_create(
        &img,
        width,
        height,
        channels,
        CVCL_DEPTH_U8,
        NULL
    );

    if (rc != CVCL_OK) {
        fprintf(stderr, "failed to create image\n");
        return 1;
    }

    /* Fill deterministic pattern */
    for (cvcl_i32 y = 0; y < img.height; y++) {
        cvcl_u8 *row = cvcl_image_row(&img, y);

        for (cvcl_i32 x = 0; x < img.width * img.channels; x++) {
            row[x] = (cvcl_u8)((x + y) & 0xFF);
        }
    }

    double start = now_ms();

    for (int i = 0; i < iterations; i++) {
        cvcl_flip_v(&img);
    }

    double end = now_ms();

    double total_ms = end - start;
    double avg_ms   = total_ms / iterations;

    printf("total: %.3f ms\n", total_ms);
    printf("avg  : %.3f ms/flip\n", avg_ms);

    cvcl_image_free(&img, NULL);

    return 0;
}