/**
 * @file test_morph.c
 * @brief Unit tests for morphology and Otsu thresholding
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
#define ASSERT_TRUE(c) ASSERT_EQ(!!(c), 1)

/* -------------------------------------------------------------------------
 * Morphology tests
 * ---------------------------------------------------------------------- */

TEST(test_dilate_expands_bright) {
    /* Single bright pixel in center -- dilation should expand it */
    cvcl_image_t src, dst;
    cvcl_image_create(&src, 5, 5, 1, CVCL_DEPTH_U8, NULL);
    cvcl_image_create(&dst, 5, 5, 1, CVCL_DEPTH_U8, NULL);

    cvcl_set_u8(&src, 2, 2, 0, 255);  /* single white pixel at center */

    cvcl_result_t rc = cvcl_dilate(&dst, &src, 3, 3, CVCL_BORDER_REPLICATE);
    ASSERT_EQ(rc, CVCL_OK);

    /* Center and its 8 neighbors should now be 255 */
    ASSERT_EQ(cvcl_get_u8(&dst, 2, 2, 0), 255);
    ASSERT_EQ(cvcl_get_u8(&dst, 1, 1, 0), 255);
    ASSERT_EQ(cvcl_get_u8(&dst, 3, 3, 0), 255);
    ASSERT_EQ(cvcl_get_u8(&dst, 1, 3, 0), 255);
    ASSERT_EQ(cvcl_get_u8(&dst, 3, 1, 0), 255);

    /* Corners should still be 0 (3x3 SE from center can't reach) */
    ASSERT_EQ(cvcl_get_u8(&dst, 0, 0, 0), 0);
    ASSERT_EQ(cvcl_get_u8(&dst, 4, 4, 0), 0);

    cvcl_image_free(&src, NULL);
    cvcl_image_free(&dst, NULL);
}

TEST(test_erode_shrinks_bright) {
    /* All-white image with a single black pixel -- erode should spread black */
    cvcl_image_t src, dst;
    cvcl_image_create(&src, 5, 5, 1, CVCL_DEPTH_U8, NULL);
    cvcl_image_create(&dst, 5, 5, 1, CVCL_DEPTH_U8, NULL);

    /* Fill all white */
    for (cvcl_i32 y = 0; y < 5; y++)
        for (cvcl_i32 x = 0; x < 5; x++)
            cvcl_set_u8(&src, x, y, 0, 255);

    cvcl_set_u8(&src, 2, 2, 0, 0);  /* single black pixel at center */

    cvcl_result_t rc = cvcl_erode(&dst, &src, 3, 3, CVCL_BORDER_REPLICATE);
    ASSERT_EQ(rc, CVCL_OK);

    /* Center and neighbors should be eroded to 0 */
    ASSERT_EQ(cvcl_get_u8(&dst, 2, 2, 0), 0);
    ASSERT_EQ(cvcl_get_u8(&dst, 1, 1, 0), 0);
    ASSERT_EQ(cvcl_get_u8(&dst, 3, 3, 0), 0);

    /* Corners still white */
    ASSERT_EQ(cvcl_get_u8(&dst, 0, 0, 0), 255);
    ASSERT_EQ(cvcl_get_u8(&dst, 4, 4, 0), 255);

    cvcl_image_free(&src, NULL);
    cvcl_image_free(&dst, NULL);
}

TEST(test_dilate_then_erode_identity_on_full) {
    /* Fully white image: dilate then erode should give back all white */
    cvcl_image_t src, tmp, dst;
    cvcl_image_create(&src, 16, 16, 1, CVCL_DEPTH_U8, NULL);
    cvcl_image_create(&tmp, 16, 16, 1, CVCL_DEPTH_U8, NULL);
    cvcl_image_create(&dst, 16, 16, 1, CVCL_DEPTH_U8, NULL);

    for (cvcl_i32 y = 0; y < 16; y++)
        for (cvcl_i32 x = 0; x < 16; x++)
            cvcl_set_u8(&src, x, y, 0, 255);

    cvcl_dilate(&tmp, &src, 3, 3, CVCL_BORDER_REPLICATE);
    cvcl_erode(&dst, &tmp, 3, 3, CVCL_BORDER_REPLICATE);

    for (cvcl_i32 y = 2; y < 14; y++)
        for (cvcl_i32 x = 2; x < 14; x++)
            ASSERT_EQ(cvcl_get_u8(&dst, x, y, 0), 255);

    cvcl_image_free(&src, NULL);
    cvcl_image_free(&tmp, NULL);
    cvcl_image_free(&dst, NULL);
}

TEST(test_open_removes_speck) {
    /* Open should remove an isolated bright speck */
    cvcl_image_t src, dst;
    cvcl_image_create(&src, 16, 16, 1, CVCL_DEPTH_U8, NULL);
    cvcl_image_create(&dst, 16, 16, 1, CVCL_DEPTH_U8, NULL);

    /* Single isolated pixel -- smaller than SE */
    cvcl_set_u8(&src, 8, 8, 0, 255);

    cvcl_result_t rc = cvcl_morph_open(&dst, &src, 3, 3, NULL);
    ASSERT_EQ(rc, CVCL_OK);

    /* Speck should be gone after open */
    ASSERT_EQ(cvcl_get_u8(&dst, 8, 8, 0), 0);

    cvcl_image_free(&src, NULL);
    cvcl_image_free(&dst, NULL);
}

TEST(test_close_fills_hole) {
    /* Close should fill a small black hole in a white region */
    cvcl_image_t src, dst;
    cvcl_image_create(&src, 16, 16, 1, CVCL_DEPTH_U8, NULL);
    cvcl_image_create(&dst, 16, 16, 1, CVCL_DEPTH_U8, NULL);

    /* All white */
    for (cvcl_i32 y = 0; y < 16; y++)
        for (cvcl_i32 x = 0; x < 16; x++)
            cvcl_set_u8(&src, x, y, 0, 255);

    /* Single black hole */
    cvcl_set_u8(&src, 8, 8, 0, 0);

    cvcl_result_t rc = cvcl_morph_close(&dst, &src, 3, 3, NULL);
    ASSERT_EQ(rc, CVCL_OK);

    /* Hole should be filled */
    ASSERT_EQ(cvcl_get_u8(&dst, 8, 8, 0), 255);

    cvcl_image_free(&src, NULL);
    cvcl_image_free(&dst, NULL);
}

/* -------------------------------------------------------------------------
 * Threshold / Otsu tests
 * ---------------------------------------------------------------------- */

TEST(test_threshold_binary) {
    cvcl_image_t src, dst;
    cvcl_image_create(&src, 4, 1, 1, CVCL_DEPTH_U8, NULL);
    cvcl_image_create(&dst, 4, 1, 1, CVCL_DEPTH_U8, NULL);

    cvcl_set_u8(&src, 0, 0, 0, 50);
    cvcl_set_u8(&src, 1, 0, 0, 100);
    cvcl_set_u8(&src, 2, 0, 0, 150);
    cvcl_set_u8(&src, 3, 0, 0, 200);

    cvcl_result_t rc = cvcl_threshold(&dst, &src, 128.f, 255.f,
                                       CVCL_THRESH_BINARY, NULL);
    ASSERT_EQ(rc, CVCL_OK);
    ASSERT_EQ(cvcl_get_u8(&dst, 0, 0, 0), 0);    /* 50  <= 128 */
    ASSERT_EQ(cvcl_get_u8(&dst, 1, 0, 0), 0);    /* 100 <= 128 */
    ASSERT_EQ(cvcl_get_u8(&dst, 2, 0, 0), 255);  /* 150 >  128 */
    ASSERT_EQ(cvcl_get_u8(&dst, 3, 0, 0), 255);  /* 200 >  128 */

    cvcl_image_free(&src, NULL);
    cvcl_image_free(&dst, NULL);
}

TEST(test_threshold_binary_inv) {
    cvcl_image_t src, dst;
    cvcl_image_create(&src, 2, 1, 1, CVCL_DEPTH_U8, NULL);
    cvcl_image_create(&dst, 2, 1, 1, CVCL_DEPTH_U8, NULL);
    cvcl_set_u8(&src, 0, 0, 0, 50);
    cvcl_set_u8(&src, 1, 0, 0, 200);

    cvcl_threshold(&dst, &src, 128.f, 255.f, CVCL_THRESH_BINARY_INV, NULL);
    ASSERT_EQ(cvcl_get_u8(&dst, 0, 0, 0), 255);  /* inverted */
    ASSERT_EQ(cvcl_get_u8(&dst, 1, 0, 0), 0);

    cvcl_image_free(&src, NULL);
    cvcl_image_free(&dst, NULL);
}

TEST(test_otsu_bimodal) {
    /* Bimodal image: 50% dark (50), 50% bright (200)
     * Otsu should find a threshold between them */
    cvcl_image_t src, dst;
    cvcl_image_create(&src, 100, 1, 1, CVCL_DEPTH_U8, NULL);
    cvcl_image_create(&dst, 100, 1, 1, CVCL_DEPTH_U8, NULL);

    for (cvcl_i32 x = 0; x < 50; x++)  cvcl_set_u8(&src, x, 0, 0, 50);
    for (cvcl_i32 x = 50; x < 100; x++) cvcl_set_u8(&src, x, 0, 0, 200);

    cvcl_f32 t = 0.f;
    cvcl_result_t rc = cvcl_threshold(&dst, &src, 0.f, 255.f,
                                       CVCL_THRESH_OTSU, &t);
    ASSERT_EQ(rc, CVCL_OK);
    /* Threshold should be at or between the two clusters */
    ASSERT_TRUE(t >= 50.f && t < 200.f);
    /* Dark pixels -> 0, bright pixels -> 255 */
    ASSERT_EQ(cvcl_get_u8(&dst, 0,  0, 0), 0);
    ASSERT_EQ(cvcl_get_u8(&dst, 99, 0, 0), 255);

    cvcl_image_free(&src, NULL);
    cvcl_image_free(&dst, NULL);
}

TEST(test_histogram_counts) {
    cvcl_image_t img;
    cvcl_image_create(&img, 4, 1, 1, CVCL_DEPTH_U8, NULL);
    cvcl_set_u8(&img, 0, 0, 0, 0);
    cvcl_set_u8(&img, 1, 0, 0, 0);
    cvcl_set_u8(&img, 2, 0, 0, 255);
    cvcl_set_u8(&img, 3, 0, 0, 128);

    cvcl_u32 hist[256];
    cvcl_result_t rc = cvcl_histogram(&img, hist);
    ASSERT_EQ(rc, CVCL_OK);
    ASSERT_EQ(hist[0],   2);
    ASSERT_EQ(hist[128], 1);
    ASSERT_EQ(hist[255], 1);

    cvcl_image_free(&img, NULL);
}

TEST(test_equalize_hist_uniform) {
    /* Uniform image -- equalization should leave it near-unchanged */
    cvcl_image_t img;
    cvcl_image_create(&img, 256, 1, 1, CVCL_DEPTH_U8, NULL);
    for (cvcl_i32 x = 0; x < 256; x++)
        cvcl_set_u8(&img, x, 0, 0, (cvcl_u8)x);

    cvcl_result_t rc = cvcl_equalize_hist(&img);
    ASSERT_EQ(rc, CVCL_OK);
    /* After equalization of a perfectly uniform histogram, 
     * values should span [0,255] */
    ASSERT_EQ(cvcl_get_u8(&img, 0,   0, 0), 0);
    ASSERT_EQ(cvcl_get_u8(&img, 255, 0, 0), 255);

    cvcl_image_free(&img, NULL);
}

/* -------------------------------------------------------------------------
 * Entry point
 * ---------------------------------------------------------------------- */

int main(void) {
    printf("=== test_morph ===\n");
    RUN(test_dilate_expands_bright);
    RUN(test_erode_shrinks_bright);
    RUN(test_dilate_then_erode_identity_on_full);
    RUN(test_open_removes_speck);
    RUN(test_close_fills_hole);
    RUN(test_threshold_binary);
    RUN(test_threshold_binary_inv);
    RUN(test_otsu_bimodal);
    RUN(test_histogram_counts);
    RUN(test_equalize_hist_uniform);
    printf("\n%d/%d tests passed.\n", g_tests_run - g_tests_failed, g_tests_run);
    return g_tests_failed > 0 ? 1 : 0;
}
