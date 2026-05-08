/**
 * @file test_filter.c
 * @brief Unit tests for convolution and blur operations
 */

#include <cvcl/cvcl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>

static int g_tests_run = 0, g_tests_failed = 0;
#define TEST(name) static void name(void)
#define RUN(name)  do { printf("  %-40s", #name); g_tests_run++; name(); printf("PASS\n"); } while(0)
#define ASSERT_EQ(a,b)  do { if((a)!=(b)){printf("FAIL\n    %s:%d\n",__FILE__,__LINE__);g_tests_failed++;return;} } while(0)
#define ASSERT_NEAR(a,b,tol) do { \
    if (fabs((double)(a)-(double)(b)) > (tol)) { \
        printf("FAIL\n    |%g - %g| > %g at %s:%d\n",(double)(a),(double)(b),(double)(tol),__FILE__,__LINE__);\
        g_tests_failed++; return; } } while(0)

TEST(test_identity_kernel) {
    /* 3x3 identity kernel: passes image through unchanged */
    static const cvcl_f32 identity[9] = {
        0,0,0,
        0,1,0,
        0,0,0
    };
    cvcl_image_t src, dst;
    cvcl_image_create(&src, 16, 16, 1, CVCL_DEPTH_U8, NULL);
    cvcl_image_create(&dst, 16, 16, 1, CVCL_DEPTH_U8, NULL);
    for (cvcl_i32 y = 0; y < 16; y++)
        for (cvcl_i32 x = 0; x < 16; x++)
            cvcl_set_u8(&src, x, y, 0, (cvcl_u8)((x * 7 + y * 3) % 256));

    cvcl_result_t rc = cvcl_convolve2d(&dst, &src, identity, 3, 3,
                                         CVCL_BORDER_REPLICATE);
    ASSERT_EQ(rc, CVCL_OK);

    /* Interior pixels (avoid border effects) should be unchanged */
    for (cvcl_i32 y = 1; y < 15; y++)
        for (cvcl_i32 x = 1; x < 15; x++)
            ASSERT_EQ(cvcl_get_u8(&dst, x, y, 0),
                      cvcl_get_u8(&src, x, y, 0));

    cvcl_image_free(&src, NULL);
    cvcl_image_free(&dst, NULL);
}

TEST(test_gaussian_blur_uniform) {
    /* Uniform image: Gaussian blur should leave it unchanged */
    cvcl_image_t src, dst;
    cvcl_image_create(&src, 32, 32, 1, CVCL_DEPTH_U8, NULL);
    cvcl_image_create(&dst, 32, 32, 1, CVCL_DEPTH_U8, NULL);
    for (cvcl_i32 y = 0; y < 32; y++)
        for (cvcl_i32 x = 0; x < 32; x++)
            cvcl_set_u8(&src, x, y, 0, 128);

    cvcl_result_t rc = cvcl_blur_gaussian(&dst, &src, 5, 1.0f,
                                           CVCL_BORDER_REPLICATE);
    ASSERT_EQ(rc, CVCL_OK);

    /* Interior pixels should remain 128 */
    for (cvcl_i32 y = 3; y < 29; y++)
        for (cvcl_i32 x = 3; x < 29; x++)
            ASSERT_EQ(cvcl_get_u8(&dst, x, y, 0), 128);

    cvcl_image_free(&src, NULL);
    cvcl_image_free(&dst, NULL);
}

TEST(test_gaussian_blur_reduces_noise) {
    /* Blur should reduce variance (bring extreme pixels closer to mean) */
    cvcl_image_t src, dst;
    cvcl_image_create(&src, 64, 64, 1, CVCL_DEPTH_U8, NULL);
    cvcl_image_create(&dst, 64, 64, 1, CVCL_DEPTH_U8, NULL);

    /* Checkerboard: extreme 0/255 pattern */
    for (cvcl_i32 y = 0; y < 64; y++)
        for (cvcl_i32 x = 0; x < 64; x++)
            cvcl_set_u8(&src, x, y, 0, (cvcl_u8)(((x + y) % 2) ? 255 : 0));

    cvcl_blur_gaussian(&dst, &src, 9, 2.0f, CVCL_BORDER_REPLICATE);

    /* Interior pixels of dst should be closer to 127 than 0 or 255 */
    cvcl_u8 center = cvcl_get_u8(&dst, 32, 32, 0); CVCL_UNUSED(center);
    assert(center > 50 && center < 200);

    cvcl_image_free(&src, NULL);
    cvcl_image_free(&dst, NULL);
}

TEST(test_add_clamped) {
    cvcl_image_t a, b, dst;
    cvcl_image_create(&a,   4, 1, 1, CVCL_DEPTH_U8, NULL);
    cvcl_image_create(&b,   4, 1, 1, CVCL_DEPTH_U8, NULL);
    cvcl_image_create(&dst, 4, 1, 1, CVCL_DEPTH_U8, NULL);

    cvcl_set_u8(&a, 0,0,0, 200);
    cvcl_set_u8(&b, 0,0,0, 100);   /* 200+100=300 -> clamped to 255 */
    cvcl_set_u8(&a, 1,0,0, 10);
    cvcl_set_u8(&b, 1,0,0, 20);    /* 30 */
    cvcl_set_u8(&a, 2,0,0, 0);
    cvcl_set_u8(&b, 2,0,0, 0);     /* 0 */
    cvcl_set_u8(&a, 3,0,0, 127);
    cvcl_set_u8(&b, 3,0,0, 127);   /* 254 */

    cvcl_result_t rc = cvcl_add(&dst, &a, &b);
    ASSERT_EQ(rc, CVCL_OK);
    ASSERT_EQ(cvcl_get_u8(&dst, 0,0,0), 255);
    ASSERT_EQ(cvcl_get_u8(&dst, 1,0,0), 30);
    ASSERT_EQ(cvcl_get_u8(&dst, 2,0,0), 0);
    ASSERT_EQ(cvcl_get_u8(&dst, 3,0,0), 254);

    cvcl_image_free(&a, NULL);
    cvcl_image_free(&b, NULL);
    cvcl_image_free(&dst, NULL);
}

TEST(test_sub_clamped) {
    cvcl_image_t a, b, dst;
    cvcl_image_create(&a,   2, 1, 1, CVCL_DEPTH_U8, NULL);
    cvcl_image_create(&b,   2, 1, 1, CVCL_DEPTH_U8, NULL);
    cvcl_image_create(&dst, 2, 1, 1, CVCL_DEPTH_U8, NULL);

    cvcl_set_u8(&a, 0,0,0, 50);
    cvcl_set_u8(&b, 0,0,0, 100);   /* 50-100 = -50 -> clamped to 0 */
    cvcl_set_u8(&a, 1,0,0, 200);
    cvcl_set_u8(&b, 1,0,0, 50);    /* 150 */

    cvcl_sub(&dst, &a, &b);
    ASSERT_EQ(cvcl_get_u8(&dst, 0,0,0), 0);
    ASSERT_EQ(cvcl_get_u8(&dst, 1,0,0), 150);

    cvcl_image_free(&a, NULL);
    cvcl_image_free(&b, NULL);
    cvcl_image_free(&dst, NULL);
}

int main(void) {
    printf("=== test_filter ===\n");
    RUN(test_identity_kernel);
    RUN(test_gaussian_blur_uniform);
    RUN(test_gaussian_blur_reduces_noise);
    RUN(test_add_clamped);
    RUN(test_sub_clamped);
    printf("\n%d/%d tests passed.\n", g_tests_run - g_tests_failed, g_tests_run);
    return g_tests_failed > 0 ? 1 : 0;
}
