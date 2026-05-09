/**
 * @file bench_blur.c
 * @brief Microbenchmark for blur operations
 *
 * Usage: ./bench_blur [width] [height] [iterations]
 * Default: 1920x1080, 100 iterations
 */

#include <cvcl/cvcl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * Portable timer
 * ---------------------------------------------------------------------- */
#if defined(_WIN32)
#  include <windows.h>
static double now_ms(void) {
    LARGE_INTEGER freq, count;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&count);
    return (double)count.QuadPart / (double)freq.QuadPart * 1000.0;
}
#else
#  include <time.h>
static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}
#endif

/* -------------------------------------------------------------------------
 * Benchmark helpers
 * ---------------------------------------------------------------------- */

typedef cvcl_result_t (*bench_fn)(cvcl_image_t *dst, const cvcl_image_t *src,
                                   void *param);

static void run_bench(const char *name, bench_fn fn,
                      cvcl_image_t *dst, const cvcl_image_t *src,
                      void *param, int iters) {
    /* Warmup */
    fn(dst, src, param);

    double t0 = now_ms();
    for (int i = 0; i < iters; i++) fn(dst, src, param);
    double t1 = now_ms();

    double ms_per  = (t1 - t0) / iters;
    double mpix_s  = ((double)src->width * src->height / 1e6) / (ms_per / 1000.0);
    printf("  %-30s  %7.3f ms/frame   %6.1f Mpix/s\n",
           name, ms_per, mpix_s);
}

/* Adapters */
static cvcl_result_t bench_gauss5(cvcl_image_t *d, const cvcl_image_t *s, void *p) {
    (void)p; return cvcl_blur_gaussian(d, s, 5, 0.f, CVCL_BORDER_REPLICATE);
}
static cvcl_result_t bench_gauss15(cvcl_image_t *d, const cvcl_image_t *s, void *p) {
    (void)p; return cvcl_blur_gaussian(d, s, 15, 0.f, CVCL_BORDER_REPLICATE);
}
static cvcl_result_t bench_gauss31(cvcl_image_t *d, const cvcl_image_t *s, void *p) {
    (void)p; return cvcl_blur_gaussian(d, s, 31, 0.f, CVCL_BORDER_REPLICATE);
}
static cvcl_result_t bench_box5(cvcl_image_t *d, const cvcl_image_t *s, void *p) {
    (void)p; return cvcl_blur_box(d, s, 5, CVCL_BORDER_REPLICATE);
}
static cvcl_result_t bench_box31(cvcl_image_t *d, const cvcl_image_t *s, void *p) {
    (void)p; return cvcl_blur_box(d, s, 31, CVCL_BORDER_REPLICATE);
}
static cvcl_result_t bench_box63(cvcl_image_t *d, const cvcl_image_t *s, void *p) {
    (void)p; return cvcl_blur_box(d, s, 63, CVCL_BORDER_REPLICATE);
}
static cvcl_result_t bench_add(cvcl_image_t *d, const cvcl_image_t *s, void *p) {
    return cvcl_add(d, s, (const cvcl_image_t *)p);
}
static cvcl_result_t bench_erode(cvcl_image_t *d, const cvcl_image_t *s, void *p) {
    (void)p; return cvcl_erode(d, s, 5, 5, CVCL_BORDER_REPLICATE);
}

/* -------------------------------------------------------------------------
 * Main
 * ---------------------------------------------------------------------- */

int main(int argc, char *argv[]) {
    int W     = argc > 1 ? atoi(argv[1]) : 1920;
    int H     = argc > 2 ? atoi(argv[2]) : 1080;
    int iters = argc > 3 ? atoi(argv[3]) : 50;

    printf("CVCL v%s Performance Benchmark\n", CVCL_VERSION_STRING);
    printf("Image: %dx%d RGB  |  %d iterations\n\n", W, H, iters);

    cvcl_image_t src, dst, src2;
    cvcl_image_create(&src,  W, H, 3, CVCL_DEPTH_U8, NULL);
    cvcl_image_create(&dst,  W, H, 3, CVCL_DEPTH_U8, NULL);
    cvcl_image_create(&src2, W, H, 3, CVCL_DEPTH_U8, NULL);

    /* Fill with pseudo-random data */
    for (cvcl_i32 i = 0; i < src.stride * src.height; i++)
        src.data[i] = (cvcl_u8)(i * 2654435761u >> 24);
    cvcl_image_clone(&src2, &src, NULL);

    printf("Gaussian blur (RGB, fixed-point separable):\n");
    run_bench("gaussian k=5",  bench_gauss5,  &dst, &src, NULL, iters);
    run_bench("gaussian k=15", bench_gauss15, &dst, &src, NULL, iters);
    run_bench("gaussian k=31", bench_gauss31, &dst, &src, NULL, iters);

    printf("\nBox blur (RGB, O(1) sliding window):\n");
    run_bench("box k=5",  bench_box5,  &dst, &src, NULL, iters);
    run_bench("box k=31", bench_box31, &dst, &src, NULL, iters);
    run_bench("box k=63", bench_box63, &dst, &src, NULL, iters);

    printf("\nPixel-wise (SIMD accelerated):\n");
    run_bench("add (saturating)", bench_add,   &dst, &src, &src2, iters);

    /* Grayscale for morphology */
    cvcl_image_t gray, gdst;
    cvcl_image_create(&gray, W, H, 1, CVCL_DEPTH_U8, NULL);
    cvcl_image_create(&gdst, W, H, 1, CVCL_DEPTH_U8, NULL);
    cvcl_convert_channels(&gray, &src);

    printf("\nMorphology (grayscale, separable min/max):\n");
    run_bench("erode 5x5", bench_erode, &gdst, &gray, NULL, iters);

    printf("\nBox blur key insight: k=5 and k=63 have nearly identical cost.\n");
    printf("Gaussian cost grows with k (separable but still O(k)).\n");

    cvcl_image_free(&src,  NULL);
    cvcl_image_free(&dst,  NULL);
    cvcl_image_free(&src2, NULL);
    cvcl_image_free(&gray, NULL);
    cvcl_image_free(&gdst, NULL);
    return 0;
}
