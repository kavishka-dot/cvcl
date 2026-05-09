/**
 * @file test_io.c
 * @brief Unit tests for PPM read/write and in-memory encode
 */

#include <cvcl/cvcl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

static int g_tests_run = 0, g_tests_failed = 0;
#define TEST(name) static void name(void)
#define RUN(name)  do { printf("  %-40s", #name); g_tests_run++; name(); printf("PASS\n"); } while(0)
#define ASSERT_EQ(a,b) do { if ((a)!=(b)){printf("FAIL\n    %s:%d\n",__FILE__,__LINE__);g_tests_failed++;return;} } while(0)

/* -------------------------------------------------------------------------
 * Tests
 * ---------------------------------------------------------------------- */

TEST(test_ppm_rgb_roundtrip) {
    cvcl_image_t src, dst;
    cvcl_image_create(&src, 64, 48, 3, CVCL_DEPTH_U8, NULL);

    /* Fill with a gradient pattern */
    for (cvcl_i32 y = 0; y < src.height; y++) {
        cvcl_u8 *row = cvcl_image_row(&src, y);
        for (cvcl_i32 x = 0; x < src.width; x++) {
            row[x*3+0] = (cvcl_u8)(x * 4);
            row[x*3+1] = (cvcl_u8)(y * 5);
            row[x*3+2] = 128;
        }
    }

    cvcl_result_t rc = cvcl_io_write_ppm(&src, "cvcl_test_rgb.ppm");
    ASSERT_EQ(rc, CVCL_OK);

    rc = cvcl_io_read_ppm(&dst, "cvcl_test_rgb.ppm", NULL);
    ASSERT_EQ(rc, CVCL_OK);
    ASSERT_EQ(dst.width,    src.width);
    ASSERT_EQ(dst.height,   src.height);
    ASSERT_EQ(dst.channels, 3);

    /* Verify pixel values match */
    for (cvcl_i32 y = 0; y < src.height; y++) {
        for (cvcl_i32 x = 0; x < src.width; x++) {
            ASSERT_EQ(cvcl_get_u8(&dst, x, y, 0), cvcl_get_u8(&src, x, y, 0));
            ASSERT_EQ(cvcl_get_u8(&dst, x, y, 1), cvcl_get_u8(&src, x, y, 1));
            ASSERT_EQ(cvcl_get_u8(&dst, x, y, 2), cvcl_get_u8(&src, x, y, 2));
        }
    }

    cvcl_image_free(&src, NULL);
    cvcl_image_free(&dst, NULL);
    remove("cvcl_test_rgb.ppm");
}

TEST(test_pgm_gray_roundtrip) {
    cvcl_image_t src, dst;
    cvcl_image_create(&src, 32, 32, 1, CVCL_DEPTH_U8, NULL);
    for (cvcl_i32 y = 0; y < 32; y++) {
        for (cvcl_i32 x = 0; x < 32; x++) {
            cvcl_set_u8(&src, x, y, 0, (cvcl_u8)((x + y) * 4));
        }
    }

    cvcl_result_t rc = cvcl_io_write_ppm(&src, "cvcl_test_gray.pgm");
    ASSERT_EQ(rc, CVCL_OK);

    rc = cvcl_io_read_ppm(&dst, "cvcl_test_gray.pgm", NULL);
    ASSERT_EQ(rc, CVCL_OK);
    ASSERT_EQ(dst.channels, 1);

    for (cvcl_i32 y = 0; y < 32; y++) {
        for (cvcl_i32 x = 0; x < 32; x++) {
            ASSERT_EQ(cvcl_get_u8(&dst, x, y, 0), (cvcl_u8)((x+y)*4));
        }
    }

    cvcl_image_free(&src, NULL);
    cvcl_image_free(&dst, NULL);
    remove("cvcl_test_gray.pgm");
}

TEST(test_in_memory_encode) {
    cvcl_image_t src;
    cvcl_image_create(&src, 4, 4, 3, CVCL_DEPTH_U8, NULL);
    cvcl_set_u8(&src, 1, 1, 0, 255);

    cvcl_u8 buf[4096];
    cvcl_size written = 0;
    cvcl_result_t rc = cvcl_io_encode_ppm(&src, buf, sizeof(buf), &written);
    ASSERT_EQ(rc, CVCL_OK);
    assert(written > 0);
    assert(written <= sizeof(buf));

    /* Check magic bytes */
    assert(buf[0] == 'P' && buf[1] == '6');

    cvcl_image_free(&src, NULL);
}

TEST(test_read_nonexistent) {
    cvcl_image_t img;
    cvcl_result_t rc = cvcl_io_read_ppm(&img, "/tmp/does_not_exist_cvcl.ppm", NULL);
    ASSERT_EQ(rc, CVCL_ERR_IO);
}

/* -------------------------------------------------------------------------
 * Entry point
 * ---------------------------------------------------------------------- */

int main(void) {
    printf("=== test_io ===\n");
    RUN(test_ppm_rgb_roundtrip);
    RUN(test_pgm_gray_roundtrip);
    RUN(test_in_memory_encode);
    RUN(test_read_nonexistent);
    printf("\n%d/%d tests passed.\n", g_tests_run - g_tests_failed, g_tests_run);
    return g_tests_failed > 0 ? 1 : 0;
}
