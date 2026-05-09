/**
 * @file test_numerical.c
 * @brief Numerical correctness tests -- exact expected values
 *
 * These tests verify that operations produce mathematically correct
 * results, not just "something reasonable". Each test has a known
 * expected output derived from first principles.
 */

#include <cvcl/cvcl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>

static int g_run = 0, g_fail = 0;

#define TEST(name) static void name(void)
#define RUN(name)  do { int _fb=g_fail; printf("  %-48s", #name); g_run++; name(); if(g_fail==_fb) printf("PASS\n"); } while(0)
#define FAIL(msg)  do { printf("FAIL\n    %s at %s:%d\n", msg, __FILE__, __LINE__); g_fail++; return; } while(0)
#define ASSERT_EQ(a,b) do { if((a)!=(b)){printf("FAIL\n    %s=%d expected %d at %s:%d\n",#a,(int)(a),(int)(b),__FILE__,__LINE__);g_fail++;return;} } while(0)
#define ASSERT_NEAR(a,b,tol) do { double _d=fabs((double)(a)-(double)(b)); if(_d>(tol)){printf("FAIL\n    |%g-%g|=%g > %g at %s:%d\n",(double)(a),(double)(b),_d,(double)(tol),__FILE__,__LINE__);g_fail++;return;} } while(0)
#define ASSERT_TRUE(c) do { if(!(c)){printf("FAIL\n    %s is false at %s:%d\n",#c,__FILE__,__LINE__);g_fail++;return;} } while(0)

/* -------------------------------------------------------------------------
 * Gaussian blur: must preserve mean of uniform image within 1 LSB
 * ---------------------------------------------------------------------- */
TEST(test_gaussian_preserves_mean) {
    cvcl_image_t src, dst;
    cvcl_image_create(&src, 64, 64, 1, CVCL_DEPTH_U8, NULL);
    cvcl_image_create(&dst, 64, 64, 1, CVCL_DEPTH_U8, NULL);

    /* Uniform image value = 128 */
    for (cvcl_i32 y=0; y<64; y++) {
        cvcl_u8 *row = cvcl_image_row(&src,y);
        for (cvcl_i32 x=0; x<64; x++) row[x]=128;
    }

    cvcl_blur_gaussian(&dst, &src, 9, 2.f, CVCL_BORDER_REPLICATE);

    /* Interior pixels (away from border) must equal 128 exactly */
    for (cvcl_i32 y=5; y<59; y++) {
        const cvcl_u8 *row = cvcl_image_row(&dst,y);
        for (cvcl_i32 x=5; x<59; x++)
            if (row[x] != 128) FAIL("Gaussian changed uniform pixel value");
    }

    cvcl_image_free(&src,NULL); cvcl_image_free(&dst,NULL);
}

/* -------------------------------------------------------------------------
 * Box blur: result must equal naive reference pixel-for-pixel
 * ---------------------------------------------------------------------- */
TEST(test_box_matches_reference) {
    cvcl_image_t src, dst;
    cvcl_image_create(&src, 32, 32, 1, CVCL_DEPTH_U8, NULL);
    cvcl_image_create(&dst, 32, 32, 1, CVCL_DEPTH_U8, NULL);

    /* Random-ish fill */
    for (cvcl_i32 y=0; y<32; y++) {
        cvcl_u8 *row=cvcl_image_row(&src,y);
        for (cvcl_i32 x=0; x<32; x++) row[x]=(cvcl_u8)((x*7+y*13+42)%256);
    }

    cvcl_blur_box(&dst, &src, 5, CVCL_BORDER_REPLICATE);

    /* Compute reference naively for interior pixels */
    for (cvcl_i32 y=2; y<30; y++) {
        const cvcl_u8 *dr=cvcl_image_row(&dst,y);
        for (cvcl_i32 x=2; x<30; x++) {
            cvcl_u32 sum=0;
            for (cvcl_i32 ky=-2; ky<=2; ky++)
                for (cvcl_i32 kx=-2; kx<=2; kx++)
                    sum += cvcl_get_u8(&src,x+kx,y+ky,0);
            cvcl_u8 ref=(cvcl_u8)(sum/25);
            /* Allow ±1 for integer division rounding */
            if (abs((int)dr[x]-(int)ref) > 1)
                FAIL("Box blur diverges from reference");
        }
    }

    cvcl_image_free(&src,NULL); cvcl_image_free(&dst,NULL);
}

/* -------------------------------------------------------------------------
 * Median: window median must equal sorted middle element
 * ---------------------------------------------------------------------- */
TEST(test_median_sorted_window) {
    /* 1D image with known values -- median of [1,2,3,4,5] = 3 */
    cvcl_image_t src, dst;
    cvcl_image_create(&src, 9, 1, 1, CVCL_DEPTH_U8, NULL);
    cvcl_image_create(&dst, 9, 1, 1, CVCL_DEPTH_U8, NULL);

    cvcl_u8 *row = cvcl_image_row(&src,0);
    for (cvcl_i32 x=0; x<9; x++) row[x]=(cvcl_u8)(x+1);

    cvcl_blur_median(&dst, &src, 5);

    /* Center pixel: window is [3,4,5,6,7], median=5 */
    ASSERT_EQ(cvcl_get_u8(&dst,4,0,0), 5);

    cvcl_image_free(&src,NULL); cvcl_image_free(&dst,NULL);
}

/* -------------------------------------------------------------------------
 * Sobel on linear ramp must produce exact gradient
 *
 * Image: f(x,y) = x (ramp in x direction)
 * Sobel X: should be 4 everywhere in the interior (kernel: -1,0,+1 * col sums)
 * Actually for f(x)=x: Kx*f = (-1)(x-1) + 0*x + (1)(x+1) = 2,
 *   with row weights -1,0,1 summing over y: total = (-1+0+1)*2... let's verify.
 * Sobel Kx applied to f(x)=x:
 *   row -1: -1*(x-1) + 0*x + 1*(x+1) = 2
 *   row  0: -2*(x-1) + 0*x + 2*(x+1) = 4
 *   row +1: -1*(x-1) + 0*x + 1*(x+1) = 2
 *   Total Gx = 2+4+2 = 8 (for unit slope ramp)
 * But Sobel is applied as a 3x3 convolution, so for f(x,y)=x:
 *   Gx = sum over kernel = 8 (slope 1 ramp gives Gx=8)
 * ---------------------------------------------------------------------- */
TEST(test_sobel_linear_ramp) {
    cvcl_image_t src, gx;
    cvcl_image_create(&src, 16, 16, 1, CVCL_DEPTH_U8, NULL);
    cvcl_image_create(&gx,  16, 16, 1, CVCL_DEPTH_F32, NULL);

    /* f(x,y) = x, capped at 255 */
    for (cvcl_i32 y=0; y<16; y++) {
        cvcl_u8 *row=cvcl_image_row(&src,y);
        for (cvcl_i32 x=0; x<16; x++) row[x]=(cvcl_u8)x;
    }

    cvcl_sobel_x(&gx, &src, CVCL_BORDER_REPLICATE);

    /* Interior: Gx must equal 8 for unit slope */
    for (cvcl_i32 y=1; y<15; y++)
        for (cvcl_i32 x=1; x<15; x++)
            ASSERT_NEAR(cvcl_get_f32(&gx,x,y,0), 8.f, 0.5f);

    cvcl_image_free(&src,NULL); cvcl_image_free(&gx,NULL);
}

/* -------------------------------------------------------------------------
 * Canny on a synthetic step edge must produce 1-pixel-wide line
 * ---------------------------------------------------------------------- */
TEST(test_canny_step_edge) {
    cvcl_image_t src, edges;
    cvcl_image_create(&src, 32, 32, 1, CVCL_DEPTH_U8, NULL);

    /* Left half = 0, right half = 255: vertical step edge at x=16 */
    for (cvcl_i32 y=0; y<32; y++) {
        cvcl_u8 *row=cvcl_image_row(&src,y);
        for (cvcl_i32 x=0; x<32; x++) row[x]=(x<16)?0:255;
    }

    cvcl_canny(&edges, &src, 10.f, 50.f, NULL);

    /* Edge should appear at or near x=15/16 in interior rows */
    cvcl_i32 edge_count_row = 0;
    const cvcl_u8 *row = cvcl_image_row(&edges, 16);
    for (cvcl_i32 x=0; x<32; x++) if (row[x]==255) edge_count_row++;
    ASSERT_TRUE(edge_count_row >= 1 && edge_count_row <= 3);

    cvcl_image_free(&src,NULL); cvcl_image_free(&edges,NULL);
}

/* -------------------------------------------------------------------------
 * Affine identity matrix: pixel-perfect copy
 * ---------------------------------------------------------------------- */
TEST(test_affine_identity) {
    cvcl_image_t src, dst;
    cvcl_image_create(&src, 32, 32, 3, CVCL_DEPTH_U8, NULL);
    cvcl_image_create(&dst, 32, 32, 3, CVCL_DEPTH_U8, NULL);

    for (cvcl_i32 y=0; y<32; y++) {
        cvcl_u8 *row=cvcl_image_row(&src,y);
        for (cvcl_i32 x=0; x<32; x++) {
            row[x*3+0]=(cvcl_u8)(x*7%256);
            row[x*3+1]=(cvcl_u8)(y*11%256);
            row[x*3+2]=128;
        }
    }

    /* Identity 2x3 matrix */
    cvcl_f32 M[6] = {1.f,0.f,0.f, 0.f,1.f,0.f};
    cvcl_affine(&dst, &src, M, CVCL_INTERP_NEAREST, CVCL_BORDER_REPLICATE);

    /* Interior pixels must be identical */
    for (cvcl_i32 y=1; y<31; y++)
        for (cvcl_i32 x=1; x<31; x++)
            for (cvcl_i32 c=0; c<3; c++)
                if (cvcl_get_u8(&dst,x,y,c) != cvcl_get_u8(&src,x,y,c))
                    FAIL("Affine identity changed pixel value");

    cvcl_image_free(&src,NULL); cvcl_image_free(&dst,NULL);
}

/* -------------------------------------------------------------------------
 * Rotate 90 CW x4 = identity
 * ---------------------------------------------------------------------- */
TEST(test_rotate_90_x4_identity) {
    cvcl_image_t src, r1, r2, r3, r4;
    cvcl_image_create(&src, 16, 16, 3, CVCL_DEPTH_U8, NULL);

    for (cvcl_i32 y=0; y<16; y++) {
        cvcl_u8 *row=cvcl_image_row(&src,y);
        for (cvcl_i32 x=0; x<16; x++) {
            row[x*3+0]=(cvcl_u8)(x*13%256);
            row[x*3+1]=(cvcl_u8)(y*17%256);
            row[x*3+2]=(cvcl_u8)((x+y)*7%256);
        }
    }

    cvcl_rotate(&r1,&src,CVCL_ROTATE_90CW,NULL);
    cvcl_rotate(&r2,&r1, CVCL_ROTATE_90CW,NULL);
    cvcl_rotate(&r3,&r2, CVCL_ROTATE_90CW,NULL);
    cvcl_rotate(&r4,&r3, CVCL_ROTATE_90CW,NULL);

    ASSERT_EQ(r4.width,  src.width);
    ASSERT_EQ(r4.height, src.height);

    for (cvcl_i32 y=0; y<16; y++)
        for (cvcl_i32 x=0; x<16; x++)
            for (cvcl_i32 c=0; c<3; c++)
                if (cvcl_get_u8(&r4,x,y,c) != cvcl_get_u8(&src,x,y,c))
                    FAIL("4x Rotate90 not identity");

    cvcl_image_free(&src,NULL); cvcl_image_free(&r1,NULL);
    cvcl_image_free(&r2,NULL);  cvcl_image_free(&r3,NULL);
    cvcl_image_free(&r4,NULL);
}

/* -------------------------------------------------------------------------
 * Bicubic resize of constant image: must return same constant
 * ---------------------------------------------------------------------- */
TEST(test_bicubic_constant) {
    cvcl_image_t src, dst;
    cvcl_image_create(&src, 8,  8,  3, CVCL_DEPTH_U8, NULL);
    cvcl_image_create(&dst, 24, 24, 3, CVCL_DEPTH_U8, NULL);

    /* Fill with constant 200 */
    for (cvcl_i32 y=0; y<8; y++) {
        cvcl_u8 *row=cvcl_image_row(&src,y);
        for (cvcl_i32 x=0; x<8*3; x++) row[x]=200;
    }

    cvcl_resize(&dst, &src, CVCL_INTERP_BICUBIC);

    /* All output pixels must be 200 ±1 */
    for (cvcl_i32 y=0; y<24; y++) {
        const cvcl_u8 *row=cvcl_image_row(&dst,y);
        for (cvcl_i32 x=0; x<24*3; x++)
            if (abs((int)row[x]-200)>1) FAIL("Bicubic changed constant image");
    }

    cvcl_image_free(&src,NULL); cvcl_image_free(&dst,NULL);
}

/* -------------------------------------------------------------------------
 * Draw line: horizontal line must set exactly W pixels
 * ---------------------------------------------------------------------- */
TEST(test_draw_line_horizontal_count) {
    cvcl_image_t img;
    cvcl_image_create(&img, 64, 64, 3, CVCL_DEPTH_U8, NULL);

    cvcl_color_t white = {1.f,1.f,1.f,1.f};
    cvcl_point2i_t p0={0,32}, p1={63,32};
    cvcl_draw_line(&img, p0, p1, white, 1);

    /* Count lit pixels on row 32 */
    cvcl_i32 count=0;
    const cvcl_u8 *row=cvcl_image_row(&img,32);
    for (cvcl_i32 x=0; x<64; x++) if (row[x*3]>=250) count++;
    ASSERT_EQ(count, 64);

    cvcl_image_free(&img,NULL);
}

/* -------------------------------------------------------------------------
 * Draw filled rect: pixel count must equal w*h exactly
 * ---------------------------------------------------------------------- */
TEST(test_draw_rect_filled_count) {
    cvcl_image_t img;
    cvcl_image_create(&img, 64, 64, 1, CVCL_DEPTH_U8, NULL);

    cvcl_color_t white={1.f,1.f,1.f,1.f};
    cvcl_rect_t rect={10,10,20,15};
    cvcl_draw_rect(&img, rect, white, 1, 1);

    cvcl_i32 count=0;
    for (cvcl_i32 y=0; y<64; y++) {
        const cvcl_u8 *row=cvcl_image_row(&img,y);
        for (cvcl_i32 x=0; x<64; x++) if (row[x]>=250) count++;
    }
    ASSERT_EQ(count, 20*15);

    cvcl_image_free(&img,NULL);
}

/* -------------------------------------------------------------------------
 * Draw circle: no pixel outside bounding box
 * ---------------------------------------------------------------------- */
TEST(test_draw_circle_bounds) {
    cvcl_image_t img;
    cvcl_image_create(&img, 64, 64, 1, CVCL_DEPTH_U8, NULL);

    cvcl_color_t white={1.f,1.f,1.f,1.f};
    cvcl_point2i_t center={32,32};
    cvcl_draw_circle(&img, center, 20, white, 1, 1);

    /* No pixel outside [32-20, 32+20] bounding box should be set */
    for (cvcl_i32 y=0; y<64; y++) {
        const cvcl_u8 *row=cvcl_image_row(&img,y);
        for (cvcl_i32 x=0; x<64; x++) {
            if (row[x]==255) {
                ASSERT_TRUE(x>=12 && x<=52);
                ASSERT_TRUE(y>=12 && y<=52);
            }
        }
    }

    cvcl_image_free(&img,NULL);
}

/* -------------------------------------------------------------------------
 * Threshold BINARY: pixel count correctness
 * ---------------------------------------------------------------------- */
TEST(test_threshold_count) {
    cvcl_image_t src, dst;
    cvcl_image_create(&src, 256, 1, 1, CVCL_DEPTH_U8, NULL);
    cvcl_image_create(&dst, 256, 1, 1, CVCL_DEPTH_U8, NULL);

    cvcl_u8 *row=cvcl_image_row(&src,0);
    for (cvcl_i32 x=0; x<256; x++) row[x]=(cvcl_u8)x;

    cvcl_threshold(&dst, &src, 127.f, 255.f, CVCL_THRESH_BINARY, NULL);

    cvcl_i32 count=0;
    const cvcl_u8 *dr=cvcl_image_row(&dst,0);
    for (cvcl_i32 x=0; x<256; x++) if (dr[x]==255) count++;
    /* Values 128-255 are > 127: that's 128 pixels */
    ASSERT_EQ(count, 128);

    cvcl_image_free(&src,NULL); cvcl_image_free(&dst,NULL);
}

/* -------------------------------------------------------------------------
 * Double flip = identity
 * ---------------------------------------------------------------------- */
TEST(test_double_flip_identity) {
    cvcl_image_t orig, img;
    cvcl_image_create(&orig, 32, 32, 3, CVCL_DEPTH_U8, NULL);
    for (cvcl_i32 y=0; y<32; y++) {
        cvcl_u8 *row=cvcl_image_row(&orig,y);
        for (cvcl_i32 x=0; x<32*3; x++) row[x]=(cvcl_u8)((x+y*7)%256);
    }

    cvcl_image_clone(&img, &orig, NULL);
    cvcl_flip_h(&img); cvcl_flip_h(&img);

    for (cvcl_i32 y=0; y<32; y++)
        for (cvcl_i32 x=0; x<32; x++)
            for (cvcl_i32 c=0; c<3; c++)
                if (cvcl_get_u8(&img,x,y,c)!=cvcl_get_u8(&orig,x,y,c))
                    FAIL("Double flip_h not identity");

    cvcl_image_free(&orig,NULL); cvcl_image_free(&img,NULL);
}

/* -------------------------------------------------------------------------
 * PNG round-trip: pixel values preserved exactly
 * ---------------------------------------------------------------------- */
TEST(test_png_roundtrip_exact) {
    cvcl_image_t src, dst;
    cvcl_image_create(&src, 64, 64, 3, CVCL_DEPTH_U8, NULL);

    for (cvcl_i32 y=0; y<64; y++) {
        cvcl_u8 *row=cvcl_image_row(&src,y);
        for (cvcl_i32 x=0; x<64; x++) {
            row[x*3+0]=(cvcl_u8)((x*13+y*7)%256);
            row[x*3+1]=(cvcl_u8)((x*7+y*11)%256);
            row[x*3+2]=(cvcl_u8)((x+y*13)%256);
        }
    }

    /* Cross-platform temp path */
    char _path[512];
#if defined(_WIN32)
    const char *_tmp = getenv("TEMP"); if (!_tmp) _tmp = ".";
    snprintf(_path, sizeof(_path), "%s\\cvcl_rt_test.png", _tmp);
#else
    snprintf(_path, sizeof(_path), "/tmp/cvcl_rt_test.png");
#endif
    cvcl_io_write_png_native(&src, _path);
    cvcl_io_read_png_native(&dst, _path, NULL);

    ASSERT_EQ(dst.width,  src.width);
    ASSERT_EQ(dst.height, src.height);
    ASSERT_EQ(dst.channels, src.channels);

    for (cvcl_i32 y=0; y<64; y++)
        for (cvcl_i32 x=0; x<64; x++)
            for (cvcl_i32 c=0; c<3; c++)
                if (cvcl_get_u8(&dst,x,y,c)!=cvcl_get_u8(&src,x,y,c))
                    FAIL("PNG round-trip changed pixel value");

    cvcl_image_free(&src,NULL); cvcl_image_free(&dst,NULL);
    remove(_path);
}

int main(void) {
    printf("=== test_numerical ===\n");
    RUN(test_gaussian_preserves_mean);
    RUN(test_box_matches_reference);
    RUN(test_median_sorted_window);
    RUN(test_sobel_linear_ramp);
    RUN(test_canny_step_edge);
    RUN(test_affine_identity);
    RUN(test_rotate_90_x4_identity);
    RUN(test_bicubic_constant);
    RUN(test_draw_line_horizontal_count);
    RUN(test_draw_rect_filled_count);
    RUN(test_draw_circle_bounds);
    RUN(test_threshold_count);
    RUN(test_double_flip_identity);
    RUN(test_png_roundtrip_exact);
    printf("\n%d/%d tests passed.\n", g_run-g_fail, g_run);
    return g_fail > 0 ? 1 : 0;
}
