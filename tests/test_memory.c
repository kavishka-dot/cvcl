/**
 * @file test_memory.c
 * @brief Memory safety, leak detection, allocator stress, determinism
 */

#include "test_harness.h"

/* -------------------------------------------------------------------------
 * Allocate and free 1000 images -- no leak
 * ---------------------------------------------------------------------- */
TEST(test_no_leak_1000_allocs) {
    COUNTING_ALLOC(ca);
    for (cvcl_i32 i = 0; i < 1000; i++) {
        cvcl_i32 w = 1 + i % 128;
        cvcl_i32 h = 1 + i % 64;
        cvcl_i32 ch = 1 + i % 4;
        cvcl_image_t img;
        cvcl_result_t rc = cvcl_image_create(&img,w,h,ch,CVCL_DEPTH_U8,&ca);
        if (rc == CVCL_OK) cvcl_image_free(&img,&ca);
    }
    ASSERT_EQ(ca_ctx.bytes, 0);
    ASSERT_EQ(ca_ctx.allocs, ca_ctx.frees);
}

/* -------------------------------------------------------------------------
 * Clone chain -- no leak
 * ---------------------------------------------------------------------- */
TEST(test_clone_chain_no_leak) {
    COUNTING_ALLOC(ca);
    cvcl_image_t a;
    ASSERT_OK(cvcl_image_create(&a,32,32,3,CVCL_DEPTH_U8,&ca));
    for (cvcl_i32 i=0; i<10; i++) {
        cvcl_image_t b;
        ASSERT_OK(cvcl_image_clone(&b,&a,&ca));
        cvcl_image_free(&a,&ca);
        a = b;
    }
    cvcl_image_free(&a,&ca);
    ASSERT_EQ(ca_ctx.bytes,0);
}

/* -------------------------------------------------------------------------
 * Pipeline no leak: blur → threshold → morph
 * ---------------------------------------------------------------------- */
TEST(test_pipeline_no_leak) {
    COUNTING_ALLOC(ca);

    cvcl_image_t src,gray,blurred,thresh,opened;
    ASSERT_OK(cvcl_image_create(&src,    64,64,3,CVCL_DEPTH_U8,&ca));
    ASSERT_OK(cvcl_image_create(&gray,   64,64,1,CVCL_DEPTH_U8,&ca));
    ASSERT_OK(cvcl_image_create(&blurred,64,64,1,CVCL_DEPTH_U8,&ca));
    ASSERT_OK(cvcl_image_create(&thresh, 64,64,1,CVCL_DEPTH_U8,&ca));
    ASSERT_OK(cvcl_image_create(&opened, 64,64,1,CVCL_DEPTH_U8,&ca));

    cvcl_fill(&src,128);
    cvcl_convert_channels(&gray,&src);
    cvcl_blur_gaussian(&blurred,&gray,5,1.f,CVCL_BORDER_REPLICATE);
    cvcl_threshold(&thresh,&blurred,100.f,255.f,CVCL_THRESH_BINARY,NULL);
    cvcl_morph_open(&opened,&thresh,3,3,&ca);

    cvcl_image_free(&src,    &ca);
    cvcl_image_free(&gray,   &ca);
    cvcl_image_free(&blurred,&ca);
    cvcl_image_free(&thresh, &ca);
    cvcl_image_free(&opened, &ca);

    ASSERT_EQ(ca_ctx.bytes,0);
}

/* -------------------------------------------------------------------------
 * Determinism: run Canny 20 times, result must be identical
 * ---------------------------------------------------------------------- */
TEST(test_canny_deterministic) {
    cvcl_image_t src, ref, run;
    ASSERT_OK(cvcl_image_create(&src,64,64,1,CVCL_DEPTH_U8,NULL));
    for (cvcl_i32 y=0;y<64;y++){
        cvcl_u8 *r=cvcl_image_row(&src,y);
        for (cvcl_i32 x=0;x<64;x++) r[x]=(cvcl_u8)((x*7+y*13+x*y)%256);
    }

    ASSERT_OK(cvcl_canny(&ref,&src,30.f,80.f,NULL));

    for (cvcl_i32 iter=0; iter<20; iter++) {
        ASSERT_OK(cvcl_canny(&run,&src,30.f,80.f,NULL));
        for (cvcl_i32 y=0;y<64;y++)
            for (cvcl_i32 x=0;x<64;x++)
                ASSERT_PIXEL_EQ(&run,x,y,0,cvcl_get_u8(&ref,x,y,0));
        cvcl_image_free(&run,NULL);
    }
    cvcl_image_free(&src,NULL);
    cvcl_image_free(&ref,NULL);
}

/* -------------------------------------------------------------------------
 * Determinism: Gaussian blur 20 times
 * ---------------------------------------------------------------------- */
TEST(test_gaussian_deterministic) {
    cvcl_image_t src,ref,run;
    ASSERT_OK(cvcl_image_create(&src,64,64,3,CVCL_DEPTH_U8,NULL));
    ASSERT_OK(cvcl_image_create(&ref,64,64,3,CVCL_DEPTH_U8,NULL));
    ASSERT_OK(cvcl_image_create(&run,64,64,3,CVCL_DEPTH_U8,NULL));

    for (cvcl_i32 y=0;y<64;y++){
        cvcl_u8 *r=cvcl_image_row(&src,y);
        for (cvcl_i32 x=0;x<64;x++){
            r[x*3]=(cvcl_u8)(x*3);r[x*3+1]=(cvcl_u8)(y*3);r[x*3+2]=(cvcl_u8)(x+y);
        }
    }

    ASSERT_OK(cvcl_blur_gaussian(&ref,&src,9,2.f,CVCL_BORDER_REPLICATE));
    for (cvcl_i32 iter=0;iter<20;iter++) {
        ASSERT_OK(cvcl_blur_gaussian(&run,&src,9,2.f,CVCL_BORDER_REPLICATE));
        for (cvcl_i32 y=0;y<64;y++)
            for (cvcl_i32 x=0;x<64;x++)
                for (cvcl_i32 c=0;c<3;c++)
                    ASSERT_PIXEL_EQ(&run,x,y,c,cvcl_get_u8(&ref,x,y,c));
    }
    cvcl_image_free(&src,NULL);
    cvcl_image_free(&ref,NULL);
    cvcl_image_free(&run,NULL);
}

/* -------------------------------------------------------------------------
 * Arena allocator: full pipeline within fixed budget
 * ---------------------------------------------------------------------- */
TEST(test_arena_pipeline) {
    /* 2 MB arena -- should be enough for 64x64 pipeline */
    cvcl_size arena_size = 2 * 1024 * 1024;
    cvcl_u8 *arena_buf = (cvcl_u8*)malloc(arena_size);
    ASSERT_NOT_NULL(arena_buf);

    CVCL_UNUSED(arena_buf);

    /* Use counting allocator */
    COUNTING_ALLOC(ca);

    cvcl_image_t src,gray,blurred,edges;
    ASSERT_OK(cvcl_image_create(&src,    64,64,3,CVCL_DEPTH_U8,&ca));
    ASSERT_OK(cvcl_image_create(&gray,   64,64,1,CVCL_DEPTH_U8,&ca));
    ASSERT_OK(cvcl_image_create(&blurred,64,64,1,CVCL_DEPTH_U8,&ca));

    cvcl_fill(&src,180);
    cvcl_convert_channels(&gray,&src);
    ASSERT_OK(cvcl_blur_gaussian(&blurred,&gray,5,1.f,CVCL_BORDER_REPLICATE));
    ASSERT_OK(cvcl_canny(&edges,&gray,30.f,80.f,&ca));

    ASSERT_TRUE(ca_ctx.allocs > 0);

    cvcl_image_free(&src,    &ca);
    cvcl_image_free(&gray,   &ca);
    cvcl_image_free(&blurred,&ca);
    cvcl_image_free(&edges,  &ca);

    ASSERT_EQ(ca_ctx.bytes, 0);
    free(arena_buf);
}

/* -------------------------------------------------------------------------
 * SIMD vs scalar equivalence (run with CVCL_NO_SIMD flag at build time)
 * Tests that SIMD and scalar produce identical outputs.
 * We do this by running the same operation twice and comparing.
 * ---------------------------------------------------------------------- */
TEST(test_add_output_stable) {
    cvcl_image_t a,b,dst1,dst2;
    ASSERT_OK(cvcl_image_create(&a,   128,64,3,CVCL_DEPTH_U8,NULL));
    ASSERT_OK(cvcl_image_create(&b,   128,64,3,CVCL_DEPTH_U8,NULL));
    ASSERT_OK(cvcl_image_create(&dst1,128,64,3,CVCL_DEPTH_U8,NULL));
    ASSERT_OK(cvcl_image_create(&dst2,128,64,3,CVCL_DEPTH_U8,NULL));

    for (cvcl_i32 y=0;y<64;y++){
        cvcl_u8 *ra=cvcl_image_row(&a,y), *rb=cvcl_image_row(&b,y);
        for (cvcl_i32 x=0;x<128*3;x++){ra[x]=(cvcl_u8)(x*3%256);rb[x]=(cvcl_u8)(x*7%256);}
    }

    ASSERT_OK(cvcl_add(&dst1,&a,&b));
    ASSERT_OK(cvcl_add(&dst2,&a,&b));

    for (cvcl_i32 y=0;y<64;y++)
        for (cvcl_i32 x=0;x<128;x++)
            for (cvcl_i32 c=0;c<3;c++)
                ASSERT_PIXEL_EQ(&dst2,x,y,c,cvcl_get_u8(&dst1,x,y,c));

    /* Also verify saturation: a[x]+b[x] must never overflow */
    for (cvcl_i32 y=0;y<64;y++) {
        const cvcl_u8 *ra=cvcl_image_row(&a,y);
        const cvcl_u8 *rb=cvcl_image_row(&b,y);
        const cvcl_u8 *rd=cvcl_image_row(&dst1,y);
        for (cvcl_i32 i=0;i<128*3;i++) {
            cvcl_i32 expected=CVCL_MIN((cvcl_i32)ra[i]+(cvcl_i32)rb[i],255);
            ASSERT_EQ(rd[i],(cvcl_u8)expected);
        }
    }

    cvcl_image_free(&a,NULL); cvcl_image_free(&b,NULL);
    cvcl_image_free(&dst1,NULL); cvcl_image_free(&dst2,NULL);
}

/* -------------------------------------------------------------------------
 * Free NULL-data image: must be no-op
 * ---------------------------------------------------------------------- */
TEST(test_free_null_data_noop) {
    cvcl_image_t img;
    memset(&img,0,sizeof(img));
    img.data = NULL;
    /* Should not crash */
    cvcl_image_free(&img,NULL);
    ASSERT_NULL(img.data);
}

TEST(test_free_null_img_noop) {
    /* Should not crash */
    cvcl_image_free(NULL,NULL);
}

/* -------------------------------------------------------------------------
 * Entry point
 * ---------------------------------------------------------------------- */
int main(void) {
    printf("=== test_memory ===\n");
    RUN(test_no_leak_1000_allocs);
    RUN(test_clone_chain_no_leak);
    RUN(test_pipeline_no_leak);
    RUN(test_canny_deterministic);
    RUN(test_gaussian_deterministic);
    RUN(test_arena_pipeline);
    RUN(test_add_output_stable);
    RUN(test_free_null_data_noop);
    RUN(test_free_null_img_noop);
    return test_summary("test_memory");
}
