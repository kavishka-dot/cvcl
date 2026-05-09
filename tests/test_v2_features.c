/**
 * @file test_v2_features.c
 * @brief Tests for v2 features: Harris, bilateral, CLAHE, pyramid
 */

#include <cvcl/cvcl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static int g_run=0, g_fail=0;

#define RUN(name) do { \
    int _fb=g_fail; printf("  %-52s",#name); fflush(stdout); \
    g_run++; name(); if(g_fail==_fb) printf("PASS\n"); \
} while(0)
#define ASSERT_EQ(a,b)   do { if((a)!=(b)){printf("FAIL\n    %s:%d got=%d exp=%d\n",__FILE__,__LINE__,(int)(a),(int)(b));g_fail++;return;} } while(0)
#define ASSERT_TRUE(c)   do { if(!(c)){printf("FAIL\n    %s:%d: %s\n",__FILE__,__LINE__,#c);g_fail++;return;} } while(0)
#define ASSERT_NEAR(a,b,t) do { if(fabs((double)(a)-(double)(b))>(t)){printf("FAIL\n    %s:%d |%g-%g|>%g\n",__FILE__,__LINE__,(double)(a),(double)(b),(double)(t));g_fail++;return;} } while(0)

/* -------------------------------------------------------------------------
 * Harris corner detector
 * ---------------------------------------------------------------------- */

static void test_harris_detects_corners(void) {
    /* Black square on white background -- 4 clear L-shaped corners */
    cvcl_image_t img;
    cvcl_image_create(&img, 64, 64, 1, CVCL_DEPTH_U8, NULL);
    cvcl_fill(&img, 255);
    for (cvcl_i32 y=16; y<48; y++) {
        cvcl_u8 *r=cvcl_image_row(&img,y);
        for (cvcl_i32 x=16; x<48; x++) r[x]=0;
    }

    cvcl_keypoint_t *kpts=NULL; cvcl_i32 count=0;
    cvcl_result_t rc = cvcl_harris(&kpts, &count, &img,
                                    0.04f, 1e5f, 3, 1000, NULL);
    ASSERT_EQ(rc, CVCL_OK);
    ASSERT_TRUE(count > 0);   /* must find at least some corners */
    ASSERT_TRUE(count < 1000); /* sanity cap */

    /* All keypoints must be within image bounds */
    for (cvcl_i32 i=0; i<count; i++) {
        ASSERT_TRUE(kpts[i].x >= 0 && kpts[i].x < 64);
        ASSERT_TRUE(kpts[i].y >= 0 && kpts[i].y < 64);
        ASSERT_TRUE(kpts[i].response > 0.f);
    }

    /* Response must be sorted descending */
    for (cvcl_i32 i=1; i<count; i++)
        ASSERT_TRUE(kpts[i].response <= kpts[i-1].response);

    cvcl_keypoints_free(kpts, NULL);
    cvcl_image_free(&img, NULL);
}

static void test_harris_uniform_no_corners(void) {
    /* Uniform image has no gradient -- no corners */
    cvcl_image_t img;
    cvcl_image_create(&img, 32, 32, 1, CVCL_DEPTH_U8, NULL);
    for (cvcl_i32 y=0;y<32;y++){cvcl_u8*r=cvcl_image_row(&img,y);for(cvcl_i32 x=0;x<32;x++)r[x]=128;}

    cvcl_keypoint_t *kpts=NULL; cvcl_i32 count=0;
    cvcl_harris(&kpts, &count, &img, 0.04f, 1e6f, 3, 1000, NULL);
    ASSERT_EQ(count, 0);

    cvcl_keypoints_free(kpts, NULL);
    cvcl_image_free(&img, NULL);
}

static void test_harris_returns_ok_on_valid_input(void) {
    cvcl_image_t img;
    cvcl_image_create(&img, 32, 32, 1, CVCL_DEPTH_U8, NULL);
    for (cvcl_i32 y=0;y<32;y++){cvcl_u8*r=cvcl_image_row(&img,y);for(cvcl_i32 x=0;x<32;x++)r[x]=(cvcl_u8)(x*8);}

    cvcl_keypoint_t *kpts=NULL; cvcl_i32 count=0;
    ASSERT_EQ(cvcl_harris(&kpts,&count,&img,0.04f,0.f,3,100, NULL), CVCL_OK);
    cvcl_keypoints_free(kpts, NULL);
    cvcl_image_free(&img, NULL);
}

/* -------------------------------------------------------------------------
 * Bilateral filter
 * ---------------------------------------------------------------------- */

static void test_bilateral_preserves_uniform(void) {
    /* Uniform image must pass through unchanged */
    cvcl_image_t src, dst;
    cvcl_image_create(&src, 32, 32, 1, CVCL_DEPTH_U8, NULL);
    cvcl_image_create(&dst, 32, 32, 1, CVCL_DEPTH_U8, NULL);
    for (cvcl_i32 y=0;y<32;y++){cvcl_u8*r=cvcl_image_row(&src,y);for(cvcl_i32 x=0;x<32;x++)r[x]=128;}

    ASSERT_EQ(cvcl_blur_bilateral(&dst,&src,5,3.f,30.f), CVCL_OK);

    /* Interior must be 128 ±1 */
    for (cvcl_i32 y=3;y<29;y++)
        for (cvcl_i32 x=3;x<29;x++) {
            int v=cvcl_get_u8(&dst,x,y,0);
            if(abs(v-128)>1) { printf("FAIL\n    bilateral uniform: got %d\n",v); g_fail++; return; }
        }
    cvcl_image_free(&src,NULL); cvcl_image_free(&dst,NULL);
}

static void test_bilateral_preserves_edge(void) {
    /* Step edge: bilateral should keep a sharp boundary */
    cvcl_image_t src, dst;
    cvcl_image_create(&src, 32, 32, 1, CVCL_DEPTH_U8, NULL);
    cvcl_image_create(&dst, 32, 32, 1, CVCL_DEPTH_U8, NULL);
    for (cvcl_i32 y=0;y<32;y++){cvcl_u8*r=cvcl_image_row(&src,y);for(cvcl_i32 x=0;x<32;x++)r[x]=(x<16)?0:255;}

    ASSERT_EQ(cvcl_blur_bilateral(&dst,&src,5,2.f,20.f), CVCL_OK);

    /* Far from edge: left side ~0, right side ~255 */
    for (cvcl_i32 y=5;y<27;y++) {
        int _lv=cvcl_get_u8(&dst, 2,y,0); ASSERT_TRUE(_lv < 30);
        int _rv=cvcl_get_u8(&dst,29,y,0); ASSERT_TRUE(_rv > 225);
    }
    cvcl_image_free(&src,NULL); cvcl_image_free(&dst,NULL);
}

static void test_bilateral_rgb(void) {
    /* Should work on 3-channel images without crashing */
    cvcl_image_t src, dst;
    cvcl_image_create(&src, 16, 16, 3, CVCL_DEPTH_U8, NULL);
    cvcl_image_create(&dst, 16, 16, 3, CVCL_DEPTH_U8, NULL);
    for (cvcl_i32 y=0;y<16;y++){cvcl_u8*r=cvcl_image_row(&src,y);for(cvcl_i32 x=0;x<16*3;x++)r[x]=(cvcl_u8)(x*13%256);}
    ASSERT_EQ(cvcl_blur_bilateral(&dst,&src,3,2.f,30.f), CVCL_OK);
    cvcl_image_free(&src,NULL); cvcl_image_free(&dst,NULL);
}

/* -------------------------------------------------------------------------
 * CLAHE
 * ---------------------------------------------------------------------- */

static void test_clahe_increases_contrast(void) {
    /* Low-contrast image should have higher std deviation after CLAHE */
    cvcl_image_t src, dst;
    cvcl_image_create(&src, 64, 64, 1, CVCL_DEPTH_U8, NULL);
    cvcl_image_create(&dst, 64, 64, 1, CVCL_DEPTH_U8, NULL);

    /* Narrow intensity range: 100-155 */
    for (cvcl_i32 y=0;y<64;y++){cvcl_u8*r=cvcl_image_row(&src,y);for(cvcl_i32 x=0;x<64;x++)r[x]=(cvcl_u8)(100+(x+y)%56);}

    ASSERT_EQ(cvcl_clahe(&dst,&src,8,8,2.f), CVCL_OK);

    /* Output range should be wider */
    cvcl_u8 src_min=255,src_max=0,dst_min=255,dst_max=0;
    for (cvcl_i32 y=0;y<64;y++) for (cvcl_i32 x=0;x<64;x++) {
        cvcl_u8 sv=cvcl_get_u8(&src,x,y,0), dv=cvcl_get_u8(&dst,x,y,0);
        if(sv<src_min){src_min=sv;} if(sv>src_max){src_max=sv;}
        if(dv<dst_min){dst_min=dv;} if(dv>dst_max){dst_max=dv;}
    }
    ASSERT_TRUE((dst_max-dst_min) >= (src_max-src_min));
    cvcl_image_free(&src,NULL); cvcl_image_free(&dst,NULL);
}

static void test_clahe_uniform_no_change(void) {
    /* Completely uniform image: CLAHE output should also be uniform */
    cvcl_image_t src, dst;
    cvcl_image_create(&src,32,32,1,CVCL_DEPTH_U8,NULL);
    cvcl_image_create(&dst,32,32,1,CVCL_DEPTH_U8,NULL);
    for (cvcl_i32 y=0;y<32;y++){cvcl_u8*r=cvcl_image_row(&src,y);for(cvcl_i32 x=0;x<32;x++)r[x]=128;}
    ASSERT_EQ(cvcl_clahe(&dst,&src,8,8,2.f), CVCL_OK);
    /* All output values should be identical */
    cvcl_u8 v0=cvcl_get_u8(&dst,0,0,0);
    for (cvcl_i32 y=0;y<32;y++) for (cvcl_i32 x=0;x<32;x++)
        ASSERT_EQ(cvcl_get_u8(&dst,x,y,0), v0);
    cvcl_image_free(&src,NULL); cvcl_image_free(&dst,NULL);
}

/* -------------------------------------------------------------------------
 * Image pyramid
 * ---------------------------------------------------------------------- */

static void test_pyramid_gaussian_sizes(void) {
    cvcl_image_t src;
    cvcl_image_create(&src,64,64,1,CVCL_DEPTH_U8,NULL);
    for (cvcl_i32 y=0;y<64;y++){cvcl_u8*r=cvcl_image_row(&src,y);for(cvcl_i32 x=0;x<64;x++)r[x]=(cvcl_u8)(x+y);}

    cvcl_image_t levels[4];
    memset(levels,0,sizeof(levels));
    ASSERT_EQ(cvcl_pyramid_gaussian(levels,4,&src,NULL), CVCL_OK);

    ASSERT_EQ(levels[0].width, 64);
    ASSERT_EQ(levels[0].height, 64);
    ASSERT_EQ(levels[1].width, 32);
    ASSERT_EQ(levels[1].height, 32);
    ASSERT_EQ(levels[2].width, 16);
    ASSERT_EQ(levels[2].height, 16);
    ASSERT_EQ(levels[3].width, 8);
    ASSERT_EQ(levels[3].height, 8);

    cvcl_pyramid_free(levels, 4, NULL);
    cvcl_image_free(&src, NULL);
}

static void test_pyramid_gaussian_level0_equals_src(void) {
    cvcl_image_t src;
    cvcl_image_create(&src,32,32,3,CVCL_DEPTH_U8,NULL);
    for (cvcl_i32 y=0;y<32;y++){cvcl_u8*r=cvcl_image_row(&src,y);for(cvcl_i32 x=0;x<32*3;x++)r[x]=(cvcl_u8)(x*7+y*3);}

    cvcl_image_t levels[3];
    memset(levels,0,sizeof(levels));
    cvcl_pyramid_gaussian(levels,3,&src,NULL);

    /* Level 0 must be identical to src */
    for (cvcl_i32 y=0;y<32;y++) for (cvcl_i32 x=0;x<32;x++) for (cvcl_i32 c=0;c<3;c++)
        ASSERT_EQ(cvcl_get_u8(&levels[0],x,y,c), cvcl_get_u8(&src,x,y,c));

    cvcl_pyramid_free(levels,3,NULL);
    cvcl_image_free(&src,NULL);
}

static void test_pyramid_laplacian_dimensions(void) {
    cvcl_image_t src;
    cvcl_image_create(&src,64,64,1,CVCL_DEPTH_U8,NULL);
    for (cvcl_i32 y=0;y<64;y++){cvcl_u8*r=cvcl_image_row(&src,y);for(cvcl_i32 x=0;x<64;x++)r[x]=(cvcl_u8)(x*2+y);}

    cvcl_image_t levels[4];
    memset(levels,0,sizeof(levels));
    ASSERT_EQ(cvcl_pyramid_laplacian(levels,4,&src,NULL), CVCL_OK);

    ASSERT_EQ(levels[0].width, 64);
    ASSERT_EQ(levels[1].width, 32);
    ASSERT_EQ(levels[2].width, 16);
    ASSERT_EQ(levels[3].width,  8);  /* residual */

    cvcl_pyramid_free(levels,4,NULL);
    cvcl_image_free(&src,NULL);
}

/* -------------------------------------------------------------------------
 * Entry point
 * ---------------------------------------------------------------------- */
int main(void) {
    printf("=== test_v2_features ===\n");

    printf("\nHarris corner detector:\n");
    RUN(test_harris_detects_corners);
    RUN(test_harris_uniform_no_corners);
    RUN(test_harris_returns_ok_on_valid_input);

    printf("\nBilateral filter:\n");
    RUN(test_bilateral_preserves_uniform);
    RUN(test_bilateral_preserves_edge);
    RUN(test_bilateral_rgb);

    printf("\nCLAHE:\n");
    RUN(test_clahe_increases_contrast);
    RUN(test_clahe_uniform_no_change);

    printf("\nImage pyramid:\n");
    RUN(test_pyramid_gaussian_sizes);
    RUN(test_pyramid_gaussian_level0_equals_src);
    RUN(test_pyramid_laplacian_dimensions);

    printf("\n%d/%d tests passed.\n", g_run-g_fail, g_run);
    return g_fail>0?1:0;
}
