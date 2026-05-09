/**
 * @file test_image.c
 * @brief Unit tests for cvcl_image_t lifecycle and utilities
 */

#include <cvcl/cvcl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

static int g_tests_run    = 0;
static int g_tests_failed = 0;

#define TEST(name) static void name(void)
#define RUN(name)  do { \
    int _fb = g_tests_failed; \
    printf("  %-40s", #name); \
    g_tests_run++; \
    name(); \
    if (g_tests_failed == _fb) printf("PASS\n"); \
} while(0)

#define ASSERT_EQ(a, b) do { \
    if ((a) != (b)) { \
        printf("FAIL\n    Expected %s == %s at %s:%d\n", #a, #b, __FILE__, __LINE__); \
        g_tests_failed++; return; \
    } \
} while(0)

#define ASSERT_NE(a, b) do { \
    if ((a) == (b)) { \
        printf("FAIL\n    Expected %s != %s at %s:%d\n", #a, #b, __FILE__, __LINE__); \
        g_tests_failed++; return; \
    } \
} while(0)

#define ASSERT_NULL(p)     ASSERT_EQ((p), NULL)
#define ASSERT_NOT_NULL(p) ASSERT_NE((p), NULL)

TEST(test_create_free) {
    cvcl_image_t img;
    cvcl_result_t rc = cvcl_image_create(&img, 64, 48, 3, CVCL_DEPTH_U8, NULL);
    ASSERT_EQ(rc, CVCL_OK);
    ASSERT_NOT_NULL(img.data);
    ASSERT_EQ(img.width,    64);
    ASSERT_EQ(img.height,   48);
    ASSERT_EQ(img.channels, 3);
    ASSERT_EQ(img.depth,    CVCL_DEPTH_U8);
    assert(img.stride >= img.width * img.channels);
    assert(img.stride % 64 == 0);
    cvcl_image_free(&img, NULL);
    ASSERT_NULL(img.data);
}

TEST(test_create_invalid_args) {
    cvcl_image_t img;
    ASSERT_EQ(cvcl_image_create(&img, 0,  48, 3, CVCL_DEPTH_U8, NULL), CVCL_ERR_INVALID_ARG);
    ASSERT_EQ(cvcl_image_create(&img, 64,  0, 3, CVCL_DEPTH_U8, NULL), CVCL_ERR_INVALID_ARG);
    ASSERT_EQ(cvcl_image_create(&img, 64, 48, 0, CVCL_DEPTH_U8, NULL), CVCL_ERR_INVALID_ARG);
    ASSERT_EQ(cvcl_image_create(&img, 64, 48, 5, CVCL_DEPTH_U8, NULL), CVCL_ERR_INVALID_ARG);
    ASSERT_EQ(cvcl_image_create(NULL, 64, 48, 3, CVCL_DEPTH_U8, NULL), CVCL_ERR_NULL_PTR);
}

TEST(test_zero_initialized) {
    cvcl_image_t img;
    cvcl_image_create(&img, 32, 32, 3, CVCL_DEPTH_U8, NULL);
    cvcl_size i;
    for (i = 0; i < cvcl_image_buffer_size(&img); i++)
        assert(img.data[i] == 0);
    cvcl_image_free(&img, NULL);
}

TEST(test_view_zero_copy) {
    cvcl_image_t src, view;
    cvcl_rect_t roi;
    cvcl_image_create(&src, 100, 80, 3, CVCL_DEPTH_U8, NULL);
    cvcl_set_u8(&src, 10, 5, 0, 42);

    roi.x = 5; roi.y = 3; roi.w = 50; roi.h = 40;
    cvcl_result_t rc = cvcl_image_view(&view, &src, roi);
    ASSERT_EQ(rc, CVCL_OK);
    ASSERT_EQ(view.width,  50);
    ASSERT_EQ(view.height, 40);
    ASSERT_EQ(view.stride, src.stride);

    ASSERT_EQ(cvcl_get_u8(&view, 10 - roi.x, 5 - roi.y, 0), 42);

    cvcl_set_u8(&view, 0, 0, 1, 99);
    ASSERT_EQ(cvcl_get_u8(&src, roi.x, roi.y, 1), 99);

    cvcl_image_free(&src, NULL);
}

TEST(test_clone) {
    cvcl_image_t src, dst;
    cvcl_image_create(&src, 16, 16, 1, CVCL_DEPTH_U8, NULL);
    cvcl_set_u8(&src, 7, 7, 0, 123);

    cvcl_result_t rc = cvcl_image_clone(&dst, &src, NULL);
    ASSERT_EQ(rc, CVCL_OK);
    ASSERT_NE(dst.data, src.data);
    ASSERT_EQ(cvcl_get_u8(&dst, 7, 7, 0), 123);

    cvcl_set_u8(&src, 7, 7, 0, 0);
    ASSERT_EQ(cvcl_get_u8(&dst, 7, 7, 0), 123);

    cvcl_image_free(&src, NULL);
    cvcl_image_free(&dst, NULL);
}

TEST(test_custom_allocator) {
    cvcl_image_t img;
    cvcl_image_create(&img, 8, 8, 3, CVCL_DEPTH_U8, cvcl_default_allocator());
    ASSERT_NOT_NULL(img.data);
    cvcl_image_free(&img, cvcl_default_allocator());
    ASSERT_NULL(img.data);

    cvcl_image_create(&img, 4, 4, 1, CVCL_DEPTH_U8, NULL);
    ASSERT_NOT_NULL(img.data);
    cvcl_image_free(&img, NULL);
    ASSERT_NULL(img.data);
}

TEST(test_pixel_rw_u8) {
    cvcl_image_t img;
    cvcl_i32 x, y;
    cvcl_image_create(&img, 10, 10, 4, CVCL_DEPTH_U8, NULL);
    for (y = 0; y < 10; y++) {
        for (x = 0; x < 10; x++) {
            cvcl_set_u8(&img, x, y, 0, (cvcl_u8)(x + y * 10));
            cvcl_set_u8(&img, x, y, 3, 255);
        }
    }
    for (y = 0; y < 10; y++) {
        for (x = 0; x < 10; x++) {
            ASSERT_EQ(cvcl_get_u8(&img, x, y, 0), (cvcl_u8)(x + y * 10));
            ASSERT_EQ(cvcl_get_u8(&img, x, y, 3), 255);
        }
    }
    cvcl_image_free(&img, NULL);
}

TEST(test_buffer_size) {
    cvcl_image_t img;
    cvcl_image_create(&img, 100, 50, 3, CVCL_DEPTH_U8, NULL);
    cvcl_size buf = cvcl_image_buffer_size(&img); CVCL_UNUSED(buf);
    assert(buf >= (cvcl_size)100 * 50 * 3);
    assert(buf == (cvcl_size)img.stride * img.height);
    cvcl_image_free(&img, NULL);
}

int main(void) {
    printf("=== test_image ===\n");
    RUN(test_create_free);
    RUN(test_create_invalid_args);
    RUN(test_zero_initialized);
    RUN(test_view_zero_copy);
    RUN(test_clone);
    RUN(test_custom_allocator);
    RUN(test_pixel_rw_u8);
    RUN(test_buffer_size);
    printf("\n%d/%d tests passed.\n", g_tests_run - g_tests_failed, g_tests_run);
    return g_tests_failed > 0 ? 1 : 0;
}
