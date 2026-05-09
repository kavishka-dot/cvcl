/**
 * @file test_transform.c
 * @brief Unit tests for geometric transforms
 */

#include <cvcl/cvcl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

static int g_tests_run = 0, g_tests_failed = 0;
#define TEST(name) static void name(void)
#define RUN(name)  do { printf("  %-40s", #name); g_tests_run++; name(); printf("PASS\n"); } while(0)
#define ASSERT_EQ(a,b) do { if((a)!=(b)){printf("FAIL\n    %s:%d\n",__FILE__,__LINE__);g_tests_failed++;return;} } while(0)

TEST(test_resize_nearest_identity) {
    cvcl_image_t src, dst;
    cvcl_image_create(&src, 16, 16, 3, CVCL_DEPTH_U8, NULL);
    /* Fill with known pattern */
    for (cvcl_i32 y = 0; y < 16; y++)
        for (cvcl_i32 x = 0; x < 16; x++)
            cvcl_set_u8(&src, x, y, 0, (cvcl_u8)(x + y));

    cvcl_result_t rc = cvcl_resize_alloc(&dst, &src, 16, 16, CVCL_INTERP_NEAREST, NULL);
    ASSERT_EQ(rc, CVCL_OK);
    ASSERT_EQ(dst.width, 16);
    ASSERT_EQ(dst.height, 16);

    for (cvcl_i32 y = 0; y < 16; y++)
        for (cvcl_i32 x = 0; x < 16; x++)
            ASSERT_EQ(cvcl_get_u8(&dst, x, y, 0), cvcl_get_u8(&src, x, y, 0));

    cvcl_image_free(&src, NULL);
    cvcl_image_free(&dst, NULL);
}

TEST(test_resize_nearest_upscale) {
    cvcl_image_t src, dst;
    cvcl_image_create(&src, 2, 2, 1, CVCL_DEPTH_U8, NULL);
    cvcl_set_u8(&src, 0, 0, 0, 10);
    cvcl_set_u8(&src, 1, 0, 0, 20);
    cvcl_set_u8(&src, 0, 1, 0, 30);
    cvcl_set_u8(&src, 1, 1, 0, 40);

    cvcl_result_t rc = cvcl_resize_alloc(&dst, &src, 4, 4, CVCL_INTERP_NEAREST, NULL);
    ASSERT_EQ(rc, CVCL_OK);
    /* Top-left 2x2 block should map to src(0,0) = 10 */
    ASSERT_EQ(cvcl_get_u8(&dst, 0, 0, 0), 10);
    ASSERT_EQ(cvcl_get_u8(&dst, 1, 0, 0), 10);
    ASSERT_EQ(cvcl_get_u8(&dst, 2, 0, 0), 20);
    ASSERT_EQ(cvcl_get_u8(&dst, 3, 0, 0), 20);

    cvcl_image_free(&src, NULL);
    cvcl_image_free(&dst, NULL);
}

TEST(test_flip_h) {
    cvcl_image_t img;
    cvcl_image_create(&img, 4, 1, 1, CVCL_DEPTH_U8, NULL);
    cvcl_set_u8(&img, 0, 0, 0, 10);
    cvcl_set_u8(&img, 1, 0, 0, 20);
    cvcl_set_u8(&img, 2, 0, 0, 30);
    cvcl_set_u8(&img, 3, 0, 0, 40);

    cvcl_flip_h(&img);

    ASSERT_EQ(cvcl_get_u8(&img, 0, 0, 0), 40);
    ASSERT_EQ(cvcl_get_u8(&img, 1, 0, 0), 30);
    ASSERT_EQ(cvcl_get_u8(&img, 2, 0, 0), 20);
    ASSERT_EQ(cvcl_get_u8(&img, 3, 0, 0), 10);

    cvcl_image_free(&img, NULL);
}

TEST(test_flip_v) {
    cvcl_image_t img;
    cvcl_image_create(&img, 1, 4, 1, CVCL_DEPTH_U8, NULL);
    cvcl_set_u8(&img, 0, 0, 0, 10);
    cvcl_set_u8(&img, 0, 1, 0, 20);
    cvcl_set_u8(&img, 0, 2, 0, 30);
    cvcl_set_u8(&img, 0, 3, 0, 40);

    cvcl_flip_v(&img);

    ASSERT_EQ(cvcl_get_u8(&img, 0, 0, 0), 40);
    ASSERT_EQ(cvcl_get_u8(&img, 0, 1, 0), 30);
    ASSERT_EQ(cvcl_get_u8(&img, 0, 2, 0), 20);
    ASSERT_EQ(cvcl_get_u8(&img, 0, 3, 0), 10);

    cvcl_image_free(&img, NULL);
}

TEST(test_double_flip_identity) {
    cvcl_image_t orig, img;
    cvcl_image_create(&orig, 8, 8, 3, CVCL_DEPTH_U8, NULL);
    for (cvcl_i32 y = 0; y < 8; y++)
        for (cvcl_i32 x = 0; x < 8; x++)
            cvcl_set_u8(&orig, x, y, 0, (cvcl_u8)(x*y));

    cvcl_image_clone(&img, &orig, NULL);
    cvcl_flip_h(&img);
    cvcl_flip_h(&img);

    for (cvcl_i32 y = 0; y < 8; y++)
        for (cvcl_i32 x = 0; x < 8; x++)
            ASSERT_EQ(cvcl_get_u8(&img, x, y, 0), cvcl_get_u8(&orig, x, y, 0));

    cvcl_image_free(&orig, NULL);
    cvcl_image_free(&img, NULL);
}

TEST(test_convert_rgb_to_gray) {
    cvcl_image_t src, dst;
    cvcl_image_create(&src, 1, 1, 3, CVCL_DEPTH_U8, NULL);
    cvcl_image_create(&dst, 1, 1, 1, CVCL_DEPTH_U8, NULL);

    /* Pure white -> should produce 255 */
    cvcl_set_u8(&src, 0, 0, 0, 255);
    cvcl_set_u8(&src, 0, 0, 1, 255);
    cvcl_set_u8(&src, 0, 0, 2, 255);
    cvcl_convert_channels(&dst, &src);
    ASSERT_EQ(cvcl_get_u8(&dst, 0, 0, 0), 255);

    /* Pure black -> 0 */
    cvcl_set_u8(&src, 0, 0, 0, 0);
    cvcl_set_u8(&src, 0, 0, 1, 0);
    cvcl_set_u8(&src, 0, 0, 2, 0);
    cvcl_convert_channels(&dst, &src);
    ASSERT_EQ(cvcl_get_u8(&dst, 0, 0, 0), 0);

    cvcl_image_free(&src, NULL);
    cvcl_image_free(&dst, NULL);
}

TEST(test_crop_zero_copy) {
    cvcl_image_t src, view;
    cvcl_image_create(&src, 100, 100, 3, CVCL_DEPTH_U8, NULL);
    cvcl_set_u8(&src, 50, 50, 1, 77);

    cvcl_rect_t roi = {40, 40, 30, 30};
    cvcl_result_t rc = cvcl_crop(&view, &src, roi);
    ASSERT_EQ(rc, CVCL_OK);
    ASSERT_EQ(view.width,  30);
    ASSERT_EQ(view.height, 30);

    /* Access pixel (10,10) in view = pixel (50,50) in src */
    ASSERT_EQ(cvcl_get_u8(&view, 10, 10, 1), 77);

    cvcl_image_free(&src, NULL);
}

int main(void) {
    printf("=== test_transform ===\n");
    RUN(test_resize_nearest_identity);
    RUN(test_resize_nearest_upscale);
    RUN(test_flip_h);
    RUN(test_flip_v);
    RUN(test_double_flip_identity);
    RUN(test_convert_rgb_to_gray);
    RUN(test_crop_zero_copy);
    printf("\n%d/%d tests passed.\n", g_tests_run - g_tests_failed, g_tests_run);
    return g_tests_failed > 0 ? 1 : 0;
}
