/**
 * @file test_edge_cases.c
 * @brief Boundary conditions, tiny images, oversized kernels, stress
 */

#include "test_harness.h"

/* -------------------------------------------------------------------------
 * Tiny images (1x1, 1xN, Nx1)
 * ---------------------------------------------------------------------- */
TEST(test_1x1_create_free) {
    cvcl_image_t img;
    ASSERT_OK(cvcl_image_create(&img,1,1,3,CVCL_DEPTH_U8,NULL));
    ASSERT_NOT_NULL(img.data);
    cvcl_set_u8(&img,0,0,0,42);
    ASSERT_PIXEL_EQ(&img,0,0,0,42);
    cvcl_image_free(&img,NULL);
    ASSERT_NULL(img.data);
}

TEST(test_1x1_gaussian) {
    cvcl_image_t src,dst;
    ASSERT_OK(cvcl_image_create(&src,1,1,1,CVCL_DEPTH_U8,NULL));
    ASSERT_OK(cvcl_image_create(&dst,1,1,1,CVCL_DEPTH_U8,NULL));
    cvcl_set_u8(&src,0,0,0,200);
    ASSERT_OK(cvcl_blur_gaussian(&dst,&src,5,1.f,CVCL_BORDER_REPLICATE));
    ASSERT_PIXEL_NEAR(&dst,0,0,0,200,1);
    cvcl_image_free(&src,NULL); cvcl_image_free(&dst,NULL);
}

TEST(test_1x1_canny) {
    cvcl_image_t src,edges;
    ASSERT_OK(cvcl_image_create(&src,1,1,1,CVCL_DEPTH_U8,NULL));
    cvcl_set_u8(&src,0,0,0,128);
    ASSERT_OK(cvcl_canny(&edges,&src,30.f,80.f,NULL));
    ASSERT_EQ(edges.width,1); ASSERT_EQ(edges.height,1);
    cvcl_image_free(&src,NULL); cvcl_image_free(&edges,NULL);
}

TEST(test_1xN_blur) {
    cvcl_image_t src,dst;
    ASSERT_OK(cvcl_image_create(&src,1,64,1,CVCL_DEPTH_U8,NULL));
    ASSERT_OK(cvcl_image_create(&dst,1,64,1,CVCL_DEPTH_U8,NULL));
    for (cvcl_i32 y=0;y<64;y++) cvcl_set_u8(&src,0,y,0,128);
    ASSERT_OK(cvcl_blur_gaussian(&dst,&src,5,1.f,CVCL_BORDER_REPLICATE));
    for (cvcl_i32 y=3;y<61;y++) ASSERT_PIXEL_NEAR(&dst,0,y,0,128,1);
    cvcl_image_free(&src,NULL); cvcl_image_free(&dst,NULL);
}

TEST(test_Nx1_blur) {
    cvcl_image_t src,dst;
    ASSERT_OK(cvcl_image_create(&src,64,1,1,CVCL_DEPTH_U8,NULL));
    ASSERT_OK(cvcl_image_create(&dst,64,1,1,CVCL_DEPTH_U8,NULL));
    cvcl_u8 *r=cvcl_image_row(&src,0);
    for (cvcl_i32 x=0;x<64;x++) r[x]=200;
    ASSERT_OK(cvcl_blur_gaussian(&dst,&src,5,1.f,CVCL_BORDER_REPLICATE));
    cvcl_u8 *dr=cvcl_image_row(&dst,0);
    for (cvcl_i32 x=3;x<61;x++) ASSERT_NEAR(dr[x],200,1);
    cvcl_image_free(&src,NULL); cvcl_image_free(&dst,NULL);
}

/* -------------------------------------------------------------------------
 * Kernel larger than image
 * ---------------------------------------------------------------------- */
TEST(test_large_kernel_small_image) {
    cvcl_image_t src,dst;
    ASSERT_OK(cvcl_image_create(&src,8,8,1,CVCL_DEPTH_U8,NULL));
    ASSERT_OK(cvcl_image_create(&dst,8,8,1,CVCL_DEPTH_U8,NULL));
    for (cvcl_i32 y=0;y<8;y++){
        cvcl_u8 *r=cvcl_image_row(&src,y);
        for (cvcl_i32 x=0;x<8;x++) r[x]=100;
    }
    /* k=15 >> image size=8 -- must not crash */
    ASSERT_OK(cvcl_blur_box(&dst,&src,15,CVCL_BORDER_REPLICATE));
    ASSERT_OK(cvcl_erode(&dst,&src,15,15,CVCL_BORDER_REPLICATE));
    ASSERT_OK(cvcl_dilate(&dst,&src,15,15,CVCL_BORDER_REPLICATE));
    ASSERT_OK(cvcl_blur_median(&dst,&src,7));
    cvcl_image_free(&src,NULL); cvcl_image_free(&dst,NULL);
}

/* -------------------------------------------------------------------------
 * Zero-copy view edge cases
 * ---------------------------------------------------------------------- */
TEST(test_view_full_image) {
    cvcl_image_t src,view;
    ASSERT_OK(cvcl_image_create(&src,32,32,3,CVCL_DEPTH_U8,NULL));
    for (cvcl_i32 y=0;y<32;y++){
        cvcl_u8 *r=cvcl_image_row(&src,y);
        for (cvcl_i32 x=0;x<32;x++){r[x*3]=(cvcl_u8)x;r[x*3+1]=(cvcl_u8)y;r[x*3+2]=0;}
    }
    cvcl_rect_t roi={0,0,32,32};
    ASSERT_OK(cvcl_image_view(&view,&src,roi));
    ASSERT_EQ(view.stride,src.stride);
    ASSERT_EQ(view.data,src.data);
    for (cvcl_i32 y=0;y<32;y++)
        for (cvcl_i32 x=0;x<32;x++)
            ASSERT_PIXEL_EQ(&view,x,y,0,cvcl_get_u8(&src,x,y,0));
    cvcl_image_free(&src,NULL);
}

TEST(test_view_single_pixel) {
    cvcl_image_t src,view;
    ASSERT_OK(cvcl_image_create(&src,16,16,1,CVCL_DEPTH_U8,NULL));
    cvcl_set_u8(&src,7,7,0,99);
    cvcl_rect_t roi={7,7,1,1};
    ASSERT_OK(cvcl_image_view(&view,&src,roi));
    ASSERT_EQ(view.width,1); ASSERT_EQ(view.height,1);
    ASSERT_PIXEL_EQ(&view,0,0,0,99);
    cvcl_image_free(&src,NULL);
}

TEST(test_view_clamped_roi) {
    cvcl_image_t src,view;
    ASSERT_OK(cvcl_image_create(&src,16,16,1,CVCL_DEPTH_U8,NULL));
    /* ROI extends past image boundary -- should clamp */
    cvcl_rect_t roi={10,10,20,20};
    ASSERT_OK(cvcl_image_view(&view,&src,roi));
    ASSERT_EQ(view.width,6);  /* clamped to 16-10=6 */
    ASSERT_EQ(view.height,6);
    cvcl_image_free(&src,NULL);
}

/* -------------------------------------------------------------------------
 * Fill and immediate free (memory safety)
 * ---------------------------------------------------------------------- */
TEST(test_fill_free_clean) {
    COUNTING_ALLOC(ca);
    cvcl_image_t img;
    ASSERT_OK(cvcl_image_create(&img,256,256,4,CVCL_DEPTH_U8,&ca));
    ASSERT_OK(cvcl_fill(&img,0xAA));
    ASSERT_EQ(cvcl_get_u8(&img,0,0,0),0xAA);
    ASSERT_EQ(cvcl_get_u8(&img,255,255,3),0xAA);
    cvcl_image_free(&img,&ca);
    ASSERT_EQ(ca_ctx.bytes,0);  /* no leak */
}

/* -------------------------------------------------------------------------
 * Threshold boundary values
 * ---------------------------------------------------------------------- */
TEST(test_threshold_boundary_values) {
    cvcl_image_t src,dst;
    ASSERT_OK(cvcl_image_create(&src,4,1,1,CVCL_DEPTH_U8,NULL));
    ASSERT_OK(cvcl_image_create(&dst,4,1,1,CVCL_DEPTH_U8,NULL));

    /* Values exactly at threshold t=128: only > t becomes max */
    cvcl_set_u8(&src,0,0,0,127); /* < t: should be 0 */
    cvcl_set_u8(&src,1,0,0,128); /* == t: should be 0 (not >) */
    cvcl_set_u8(&src,2,0,0,129); /* > t: should be 255 */
    cvcl_set_u8(&src,3,0,0,255);

    ASSERT_OK(cvcl_threshold(&dst,&src,128.f,255.f,CVCL_THRESH_BINARY,NULL));
    ASSERT_PIXEL_EQ(&dst,0,0,0,0);
    ASSERT_PIXEL_EQ(&dst,1,0,0,0);   /* == threshold: NOT included */
    ASSERT_PIXEL_EQ(&dst,2,0,0,255);
    ASSERT_PIXEL_EQ(&dst,3,0,0,255);
    cvcl_image_free(&src,NULL); cvcl_image_free(&dst,NULL);
}

/* -------------------------------------------------------------------------
 * Morph idempotency: erode(erode(x)) = erode(x) for binary
 * ---------------------------------------------------------------------- */
TEST(test_erode_idempotent) {
    cvcl_image_t src,e1,e2;
    ASSERT_OK(cvcl_image_create(&src,32,32,1,CVCL_DEPTH_U8,NULL));
    ASSERT_OK(cvcl_image_create(&e1, 32,32,1,CVCL_DEPTH_U8,NULL));
    ASSERT_OK(cvcl_image_create(&e2, 32,32,1,CVCL_DEPTH_U8,NULL));

    /* Checkerboard binary */
    for (cvcl_i32 y=0;y<32;y++){
        cvcl_u8 *r=cvcl_image_row(&src,y);
        for (cvcl_i32 x=0;x<32;x++) r[x]=((x+y)%2)?255:0;
    }
    ASSERT_OK(cvcl_erode(&e1,&src,3,3,CVCL_BORDER_REPLICATE));
    ASSERT_OK(cvcl_erode(&e2,&e1, 3,3,CVCL_BORDER_REPLICATE));

    /* After first erode, result is all-0 (checkerboard erodes to zero) */
    /* Second erode of all-0 = all-0 */
    for (cvcl_i32 y=3;y<29;y++)
        for (cvcl_i32 x=3;x<29;x++)
            ASSERT_PIXEL_EQ(&e2,x,y,0,cvcl_get_u8(&e1,x,y,0));

    cvcl_image_free(&src,NULL);
    cvcl_image_free(&e1,NULL);
    cvcl_image_free(&e2,NULL);
}

/* -------------------------------------------------------------------------
 * Resize: upscale then downscale nearest = identity for power-of-2
 * ---------------------------------------------------------------------- */
TEST(test_resize_up_down_nearest) {
    cvcl_image_t src,up,down;
    ASSERT_OK(cvcl_image_create(&src,16,16,1,CVCL_DEPTH_U8,NULL));
    for (cvcl_i32 y=0;y<16;y++){
        cvcl_u8 *r=cvcl_image_row(&src,y);
        for (cvcl_i32 x=0;x<16;x++) r[x]=(cvcl_u8)(x*y%256);
    }
    ASSERT_OK(cvcl_resize_alloc(&up,  &src,32,32,CVCL_INTERP_NEAREST,NULL));
    ASSERT_OK(cvcl_resize_alloc(&down,&up, 16,16,CVCL_INTERP_NEAREST,NULL));

    for (cvcl_i32 y=0;y<16;y++)
        for (cvcl_i32 x=0;x<16;x++)
            ASSERT_PIXEL_EQ(&down,x,y,0,cvcl_get_u8(&src,x,y,0));

    cvcl_image_free(&src,NULL);
    cvcl_image_free(&up,NULL);
    cvcl_image_free(&down,NULL);
}

/* -------------------------------------------------------------------------
 * Entry point
 * ---------------------------------------------------------------------- */
int main(void) {
    printf("=== test_edge_cases ===\n");
    RUN(test_1x1_create_free);
    RUN(test_1x1_gaussian);
    RUN(test_1x1_canny);
    RUN(test_1xN_blur);
    RUN(test_Nx1_blur);
    RUN(test_large_kernel_small_image);
    RUN(test_view_full_image);
    RUN(test_view_single_pixel);
    RUN(test_view_clamped_roi);
    RUN(test_fill_free_clean);
    RUN(test_threshold_boundary_values);
    RUN(test_erode_idempotent);
    RUN(test_resize_up_down_nearest);
    return test_summary("test_edge_cases");
}
