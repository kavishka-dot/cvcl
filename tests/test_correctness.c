/**
 * @file test_correctness.c
 * @brief Numerical correctness, boundary, draw, memory, pipeline tests
 */

#include <cvcl/cvcl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static int g_run=0, g_fail=0;

/* Cross-platform temp file path */
static void tmp_path(char *buf, int size, const char *name) {
#if defined(_WIN32)
    const char *tmp = getenv("TEMP");
    if (!tmp) tmp = getenv("TMP");
    if (!tmp) tmp = ".";
    snprintf(buf, size, "%s\\%s", tmp, name);
#else
    snprintf(buf, size, "/tmp/%s", name);
#endif
}
#define RUN(name)  do { int _fb=g_fail; printf("  %-52s", #name); g_run++; name(); if(g_fail==_fb) printf("PASS\n"); } while(0)
#define FAIL(msg)  do { printf("FAIL\n    %s  at %s:%d\n",msg,__FILE__,__LINE__); g_fail++; return; } while(0)
#define ASSERT_EQ(a,b)   do { if((a)!=(b)){printf("FAIL\n    %s:%d got=%d exp=%d\n",__FILE__,__LINE__,(int)(a),(int)(b));g_fail++;return;}  } while(0)
#define ASSERT_NEAR(a,b,tol) do { double _d=fabs((double)(a)-(double)(b)); if(_d>(tol)){printf("FAIL\n    %s:%d |%g-%g|=%g>%g\n",__FILE__,__LINE__,(double)(a),(double)(b),_d,(double)(tol));g_fail++;return;} } while(0)
#define ASSERT_TRUE(c)   do { if(!(c)){FAIL(#c);} } while(0)

/* -------------------------------------------------------------------------
 * Numerical correctness
 * ---------------------------------------------------------------------- */

static void test_gaussian_uniform_mean(void) {
    cvcl_image_t s,d;
    cvcl_image_create(&s,64,64,1,CVCL_DEPTH_U8,NULL);
    cvcl_image_create(&d,64,64,1,CVCL_DEPTH_U8,NULL);
    for(cvcl_i32 y=0;y<64;y++){cvcl_u8*r=cvcl_image_row(&s,y);for(cvcl_i32 x=0;x<64;x++)r[x]=128;}
    cvcl_blur_gaussian(&d,&s,9,2.f,CVCL_BORDER_REPLICATE);
    for(cvcl_i32 y=5;y<59;y++) for(cvcl_i32 x=5;x<59;x++) ASSERT_EQ(cvcl_get_u8(&d,x,y,0),128);
    cvcl_image_free(&s,NULL);cvcl_image_free(&d,NULL);
}

static void test_box_matches_naive(void) {
    cvcl_image_t s,f,n;
    cvcl_image_create(&s,32,32,1,CVCL_DEPTH_U8,NULL);
    cvcl_image_create(&f,32,32,1,CVCL_DEPTH_U8,NULL);
    cvcl_image_create(&n,32,32,1,CVCL_DEPTH_U8,NULL);
    for(cvcl_i32 y=0;y<32;y++){cvcl_u8*r=cvcl_image_row(&s,y);for(cvcl_i32 x=0;x<32;x++)r[x]=(cvcl_u8)(x*7+y*3);}
    cvcl_blur_box(&f,&s,5,CVCL_BORDER_REPLICATE);
    for(cvcl_i32 y=0;y<32;y++){
        cvcl_u8*dr=cvcl_image_row(&n,y);
        for(cvcl_i32 x=0;x<32;x++){
            cvcl_u32 sum=0;
            for(cvcl_i32 ky=-2;ky<=2;ky++) for(cvcl_i32 kx=-2;kx<=2;kx++)
                sum+=cvcl_get_u8(&s,CVCL_CLAMP(x+kx,0,31),CVCL_CLAMP(y+ky,0,31),0);
            dr[x]=(cvcl_u8)(sum/25);
        }
    }
    for(cvcl_i32 y=0;y<32;y++) for(cvcl_i32 x=0;x<32;x++){
        int diff=(int)cvcl_get_u8(&f,x,y,0)-(int)cvcl_get_u8(&n,x,y,0);
        if(diff<-2||diff>2) FAIL("box vs naive >2");
    }
    cvcl_image_free(&s,NULL);cvcl_image_free(&f,NULL);cvcl_image_free(&n,NULL);
}

static void test_median_exact(void) {
    cvcl_image_t s,d;
    cvcl_image_create(&s,5,1,1,CVCL_DEPTH_U8,NULL);
    cvcl_image_create(&d,5,1,1,CVCL_DEPTH_U8,NULL);
    cvcl_u8*r=cvcl_image_row(&s,0);
    r[0]=10;r[1]=20;r[2]=30;r[3]=40;r[4]=50;
    cvcl_blur_median(&d,&s,3);
    ASSERT_EQ(cvcl_get_u8(&d,2,0,0),30);
    cvcl_image_free(&s,NULL);cvcl_image_free(&d,NULL);
}

static void test_sobel_ramp(void) {
    cvcl_image_t s,gx,gy;
    cvcl_image_create(&s,32,32,1,CVCL_DEPTH_U8,NULL);
    cvcl_image_create(&gx,32,32,1,CVCL_DEPTH_F32,NULL);
    cvcl_image_create(&gy,32,32,1,CVCL_DEPTH_F32,NULL);
    for(cvcl_i32 y=0;y<32;y++){cvcl_u8*r=cvcl_image_row(&s,y);for(cvcl_i32 x=0;x<32;x++)r[x]=(cvcl_u8)x;}
    cvcl_sobel_x(&gx,&s,CVCL_BORDER_REPLICATE);
    cvcl_sobel_y(&gy,&s,CVCL_BORDER_REPLICATE);
    for(cvcl_i32 y=2;y<30;y++) for(cvcl_i32 x=2;x<30;x++){
        ASSERT_NEAR(cvcl_get_f32(&gx,x,y,0),8.f,0.01f);
        ASSERT_NEAR(cvcl_get_f32(&gy,x,y,0),0.f,0.01f);
    }
    cvcl_image_free(&s,NULL);cvcl_image_free(&gx,NULL);cvcl_image_free(&gy,NULL);
}

static void test_canny_step_edge(void) {
    cvcl_image_t s,e;
    cvcl_image_create(&s,64,64,1,CVCL_DEPTH_U8,NULL);
    for(cvcl_i32 y=0;y<64;y++){cvcl_u8*r=cvcl_image_row(&s,y);for(cvcl_i32 x=0;x<64;x++)r[x]=(x<32)?0:255;}
    cvcl_canny(&e,&s,10.f,50.f,NULL);
    cvcl_i32 ex=-1;
    for(cvcl_i32 x=2;x<62;x++) if(cvcl_get_u8(&e,x,32,0)==255){ex=x;break;}
    ASSERT_TRUE(ex==31||ex==32);
    if(ex>0) ASSERT_EQ(cvcl_get_u8(&e,ex-1,32,0),0);
    cvcl_image_free(&s,NULL);cvcl_image_free(&e,NULL);
}

static void test_affine_identity(void) {
    cvcl_image_t s,d;
    cvcl_image_create(&s,32,32,3,CVCL_DEPTH_U8,NULL);
    cvcl_image_create(&d,32,32,3,CVCL_DEPTH_U8,NULL);
    for(cvcl_i32 y=0;y<32;y++){cvcl_u8*r=cvcl_image_row(&s,y);for(cvcl_i32 x=0;x<32;x++){r[x*3]=(cvcl_u8)(x*7);r[x*3+1]=(cvcl_u8)(y*7);r[x*3+2]=128;}}
    cvcl_f32 M[6]={1.f,0.f,0.f,0.f,1.f,0.f};
    cvcl_affine(&d,&s,M,CVCL_INTERP_NEAREST,CVCL_BORDER_REPLICATE);
    for(cvcl_i32 y=0;y<32;y++) for(cvcl_i32 x=0;x<32;x++) for(cvcl_i32 c=0;c<3;c++)
        ASSERT_EQ(cvcl_get_u8(&d,x,y,c),cvcl_get_u8(&s,x,y,c));
    cvcl_image_free(&s,NULL);cvcl_image_free(&d,NULL);
}

static void test_rotate_4x_identity(void) {
    cvcl_image_t s,r1,r2,r3,r4;
    cvcl_image_create(&s,32,48,3,CVCL_DEPTH_U8,NULL);
    for(cvcl_i32 y=0;y<48;y++){cvcl_u8*r=cvcl_image_row(&s,y);for(cvcl_i32 x=0;x<32;x++){r[x*3]=(cvcl_u8)(x*7);r[x*3+1]=(cvcl_u8)(y*5);r[x*3+2]=42;}}
    cvcl_rotate(&r1,&s, CVCL_ROTATE_90CW,NULL);
    cvcl_rotate(&r2,&r1,CVCL_ROTATE_90CW,NULL);
    cvcl_rotate(&r3,&r2,CVCL_ROTATE_90CW,NULL);
    cvcl_rotate(&r4,&r3,CVCL_ROTATE_90CW,NULL);
    ASSERT_EQ(r4.width,s.width); ASSERT_EQ(r4.height,s.height);
    for(cvcl_i32 y=0;y<48;y++) for(cvcl_i32 x=0;x<32;x++) for(cvcl_i32 c=0;c<3;c++)
        ASSERT_EQ(cvcl_get_u8(&r4,x,y,c),cvcl_get_u8(&s,x,y,c));
    cvcl_image_free(&s,NULL);cvcl_image_free(&r1,NULL);cvcl_image_free(&r2,NULL);cvcl_image_free(&r3,NULL);cvcl_image_free(&r4,NULL);
}

static void test_bicubic_constant(void) {
    cvcl_image_t s,d;
    cvcl_image_create(&s,16,16,3,CVCL_DEPTH_U8,NULL);
    for(cvcl_i32 y=0;y<16;y++){cvcl_u8*r=cvcl_image_row(&s,y);for(cvcl_i32 x=0;x<16;x++){r[x*3]=100;r[x*3+1]=150;r[x*3+2]=200;}}
    cvcl_resize_alloc(&d,&s,48,48,CVCL_INTERP_BICUBIC,NULL);
    for(cvcl_i32 y=4;y<44;y++) for(cvcl_i32 x=4;x<44;x++){
        if(abs((int)cvcl_get_u8(&d,x,y,0)-100)>2) FAIL("bicubic R");
        if(abs((int)cvcl_get_u8(&d,x,y,1)-150)>2) FAIL("bicubic G");
        if(abs((int)cvcl_get_u8(&d,x,y,2)-200)>2) FAIL("bicubic B");
    }
    cvcl_image_free(&s,NULL);cvcl_image_free(&d,NULL);
}

/* -------------------------------------------------------------------------
 * Boundary tests
 * ---------------------------------------------------------------------- */

static void test_1x1_pipeline(void) {
    cvcl_image_t i,d; cvcl_image_create(&i,1,1,1,CVCL_DEPTH_U8,NULL); cvcl_image_create(&d,1,1,1,CVCL_DEPTH_U8,NULL);
    cvcl_set_u8(&i,0,0,0,200);
    ASSERT_EQ(cvcl_blur_gaussian(&d,&i,3,1.f,CVCL_BORDER_REPLICATE),CVCL_OK);
    ASSERT_EQ(cvcl_blur_box(&d,&i,3,CVCL_BORDER_REPLICATE),CVCL_OK);
    ASSERT_EQ(cvcl_blur_median(&d,&i,3),CVCL_OK);
    ASSERT_EQ(cvcl_erode(&d,&i,3,3,CVCL_BORDER_REPLICATE),CVCL_OK);
    ASSERT_EQ(cvcl_dilate(&d,&i,3,3,CVCL_BORDER_REPLICATE),CVCL_OK);
    cvcl_image_t e; ASSERT_EQ(cvcl_canny(&e,&i,10.f,50.f,NULL),CVCL_OK);
    cvcl_image_free(&e,NULL);
    cvcl_image_free(&i,NULL);cvcl_image_free(&d,NULL);
}

static void test_1xN_image(void) {
    cvcl_image_t s,d; cvcl_image_create(&s,64,1,1,CVCL_DEPTH_U8,NULL); cvcl_image_create(&d,64,1,1,CVCL_DEPTH_U8,NULL);
    for(cvcl_i32 x=0;x<64;x++) cvcl_set_u8(&s,x,0,0,(cvcl_u8)(x*4));
    ASSERT_EQ(cvcl_blur_gaussian(&d,&s,5,1.f,CVCL_BORDER_REPLICATE),CVCL_OK);
    ASSERT_EQ(cvcl_blur_box(&d,&s,5,CVCL_BORDER_REPLICATE),CVCL_OK);
    ASSERT_EQ(cvcl_blur_median(&d,&s,5),CVCL_OK);
    ASSERT_EQ(cvcl_erode(&d,&s,5,5,CVCL_BORDER_REPLICATE),CVCL_OK);
    cvcl_image_free(&s,NULL);cvcl_image_free(&d,NULL);
}

static void test_Nx1_image(void) {
    cvcl_image_t s,d; cvcl_image_create(&s,1,64,1,CVCL_DEPTH_U8,NULL); cvcl_image_create(&d,1,64,1,CVCL_DEPTH_U8,NULL);
    for(cvcl_i32 y=0;y<64;y++) cvcl_set_u8(&s,0,y,0,(cvcl_u8)(y*4));
    ASSERT_EQ(cvcl_blur_gaussian(&d,&s,5,1.f,CVCL_BORDER_REPLICATE),CVCL_OK);
    ASSERT_EQ(cvcl_erode(&d,&s,5,5,CVCL_BORDER_REPLICATE),CVCL_OK);
    ASSERT_EQ(cvcl_blur_median(&d,&s,5),CVCL_OK);
    cvcl_image_free(&s,NULL);cvcl_image_free(&d,NULL);
}

static void test_large_kernel_small_image(void) {
    cvcl_image_t s,d; cvcl_image_create(&s,8,8,1,CVCL_DEPTH_U8,NULL); cvcl_image_create(&d,8,8,1,CVCL_DEPTH_U8,NULL);
    for(cvcl_i32 y=0;y<8;y++){cvcl_u8*r=cvcl_image_row(&s,y);for(cvcl_i32 x=0;x<8;x++)r[x]=128;}
    ASSERT_EQ(cvcl_blur_gaussian(&d,&s,15,3.f,CVCL_BORDER_REPLICATE),CVCL_OK);
    ASSERT_EQ(cvcl_blur_box(&d,&s,15,CVCL_BORDER_REPLICATE),CVCL_OK);
    ASSERT_EQ(cvcl_erode(&d,&s,15,15,CVCL_BORDER_REPLICATE),CVCL_OK);
    cvcl_image_free(&s,NULL);cvcl_image_free(&d,NULL);
}

static void test_full_view_equals_original(void) {
    cvcl_image_t s,v; cvcl_image_create(&s,32,32,3,CVCL_DEPTH_U8,NULL);
    for(cvcl_i32 y=0;y<32;y++){cvcl_u8*r=cvcl_image_row(&s,y);for(cvcl_i32 x=0;x<32;x++){r[x*3]=(cvcl_u8)(x*7);r[x*3+1]=(cvcl_u8)(y*7);r[x*3+2]=99;}}
    cvcl_rect_t roi={0,0,32,32}; cvcl_image_view(&v,&s,roi);
    for(cvcl_i32 y=0;y<32;y++) for(cvcl_i32 x=0;x<32;x++) for(cvcl_i32 c=0;c<3;c++)
        ASSERT_EQ(cvcl_get_u8(&v,x,y,c),cvcl_get_u8(&s,x,y,c));
    cvcl_image_free(&s,NULL);
}

/* -------------------------------------------------------------------------
 * Draw correctness
 * ---------------------------------------------------------------------- */

static void test_line_horizontal_pixel_count(void) {
    cvcl_image_t img; cvcl_image_create(&img,64,64,1,CVCL_DEPTH_U8,NULL);
    cvcl_color_t w={1.f,1.f,1.f,1.f};
    cvcl_point2i_t p0={0,32},p1={63,32};
    cvcl_draw_line(&img,p0,p1,w,1);
    cvcl_i32 cnt=0;
    for(cvcl_i32 x=0;x<64;x++) if(cvcl_get_u8(&img,x,32,0)>=250) cnt++;
    ASSERT_EQ(cnt,64);
    cvcl_image_free(&img,NULL);
}

static void test_filled_rect_pixel_count(void) {
    cvcl_image_t img; cvcl_image_create(&img,64,64,1,CVCL_DEPTH_U8,NULL);
    cvcl_color_t w={1.f,1.f,1.f,1.f};
    cvcl_rect_t r={10,10,20,15};
    cvcl_draw_rect(&img,r,w,1,1);
    cvcl_i32 cnt=0;
    for(cvcl_i32 y=0;y<64;y++) for(cvcl_i32 x=0;x<64;x++) if(cvcl_get_u8(&img,x,y,0)>=250) cnt++;
    ASSERT_EQ(cnt,20*15);
    cvcl_image_free(&img,NULL);
}

static void test_circle_within_bounds(void) {
    cvcl_i32 R=20;
    cvcl_image_t img; cvcl_image_create(&img,64,64,1,CVCL_DEPTH_U8,NULL);
    cvcl_color_t w={1.f,1.f,1.f,1.f};
    cvcl_point2i_t c={32,32};
    cvcl_draw_circle(&img,c,R,w,1,0);
    for(cvcl_i32 y=0;y<64;y++) for(cvcl_i32 x=0;x<64;x++){
        if(cvcl_get_u8(&img,x,y,0)<128) continue;
        int dx=x-32,dy=y-32,d2=dx*dx+dy*dy;
        if(d2>(R+2)*(R+2)) FAIL("circle pixel outside bounds");
    }
    cvcl_image_free(&img,NULL);
}

static void test_text_renders_nonzero(void) {
    cvcl_image_t img; cvcl_image_create(&img,64,16,3,CVCL_DEPTH_U8,NULL);
    cvcl_color_t w={1.f,1.f,1.f,1.f};
    cvcl_draw_text(&img,"Hi",2,2,w,1);
    cvcl_i32 cnt=0;
    for(cvcl_i32 y=0;y<16;y++) for(cvcl_i32 x=0;x<64;x++) if(cvcl_get_u8(&img,x,y,0)>0) cnt++;
    ASSERT_TRUE(cnt>0);
    cvcl_image_free(&img,NULL);
}

/* -------------------------------------------------------------------------
 * Memory / determinism
 * ---------------------------------------------------------------------- */

static void test_1000_alloc_free_no_crash(void) {
    for(cvcl_i32 i=0;i<1000;i++){
        cvcl_image_t img;
        cvcl_i32 w=(i%64)+1,h=(i%48)+1,ch=(i%3)+1;
        cvcl_image_create(&img,w,h,ch,CVCL_DEPTH_U8,NULL);
        cvcl_fill(&img,(cvcl_u8)(i&0xFF));
        cvcl_image_free(&img,NULL);
    }
}

static void test_canny_deterministic(void) {
    cvcl_image_t s,e1,e2;
    cvcl_image_create(&s,64,64,1,CVCL_DEPTH_U8,NULL);
    for(cvcl_i32 y=0;y<64;y++){cvcl_u8*r=cvcl_image_row(&s,y);for(cvcl_i32 x=0;x<64;x++)r[x]=(cvcl_u8)(x^y^(x*y));}
    cvcl_canny(&e1,&s,20.f,60.f,NULL);
    cvcl_canny(&e2,&s,20.f,60.f,NULL);
    /* Count edge pixels -- must be identical across runs */
    cvcl_i32 cnt1=0, cnt2=0;
    for(cvcl_i32 y=0;y<64;y++) for(cvcl_i32 x=0;x<64;x++){
        if(cvcl_get_u8(&e1,x,y,0)) cnt1++;
        if(cvcl_get_u8(&e2,x,y,0)) cnt2++;
    }
    ASSERT_EQ(cnt1,cnt2);
    cvcl_image_free(&s,NULL);cvcl_image_free(&e1,NULL);cvcl_image_free(&e2,NULL);
}

static void test_png_roundtrip(void) {
    char path[512]; tmp_path(path, sizeof(path), "cvcl_rt.png");
    cvcl_image_t s,l;
    cvcl_image_create(&s,64,64,3,CVCL_DEPTH_U8,NULL);
    for(cvcl_i32 y=0;y<64;y++){cvcl_u8*r=cvcl_image_row(&s,y);for(cvcl_i32 x=0;x<64;x++){r[x*3]=(cvcl_u8)(x*4);r[x*3+1]=(cvcl_u8)(y*4);r[x*3+2]=(cvcl_u8)(x^y);}}
    cvcl_io_write_png_native(&s,path);
    ASSERT_EQ(cvcl_io_read_png_native(&l,path,NULL),CVCL_OK);
    ASSERT_EQ(l.width,64); ASSERT_EQ(l.height,64); ASSERT_EQ(l.channels,3);
    for(cvcl_i32 y=0;y<64;y++) for(cvcl_i32 x=0;x<64;x++) for(cvcl_i32 c=0;c<3;c++)
        ASSERT_EQ(cvcl_get_u8(&l,x,y,c),cvcl_get_u8(&s,x,y,c));
    cvcl_image_free(&s,NULL);cvcl_image_free(&l,NULL);
    remove(path);
}

/* -------------------------------------------------------------------------
 * Full pipeline
 * ---------------------------------------------------------------------- */

static void test_full_pipeline(void) {
    cvcl_image_t rgb,gray,blur,edges,dil,thr,rel;
    cvcl_image_create(&rgb,128,128,3,CVCL_DEPTH_U8,NULL);
    for(cvcl_i32 y=0;y<128;y++){cvcl_u8*r=cvcl_image_row(&rgb,y);for(cvcl_i32 x=0;x<128;x++){r[x*3]=(cvcl_u8)(x*2);r[x*3+1]=(cvcl_u8)(y*2);r[x*3+2]=100;}}
    cvcl_image_create(&gray,128,128,1,CVCL_DEPTH_U8,NULL);
    ASSERT_EQ(cvcl_convert_channels(&gray,&rgb),CVCL_OK);
    cvcl_image_create(&blur,128,128,1,CVCL_DEPTH_U8,NULL);
    ASSERT_EQ(cvcl_blur_gaussian(&blur,&gray,5,1.f,CVCL_BORDER_REPLICATE),CVCL_OK);
    ASSERT_EQ(cvcl_canny(&edges,&blur,20.f,60.f,NULL),CVCL_OK);
    cvcl_image_create(&dil,128,128,1,CVCL_DEPTH_U8,NULL);
    ASSERT_EQ(cvcl_dilate(&dil,&edges,3,3,CVCL_BORDER_REPLICATE),CVCL_OK);
    cvcl_image_create(&thr,128,128,1,CVCL_DEPTH_U8,NULL);
    ASSERT_EQ(cvcl_threshold(&thr,&dil,0.f,255.f,CVCL_THRESH_OTSU,NULL),CVCL_OK);
    ASSERT_EQ(cvcl_io_write_png_native(&thr,"cvcl_pipe.png"),CVCL_OK);
    ASSERT_EQ(cvcl_io_read_png_native(&rel,"cvcl_pipe.png",NULL),CVCL_OK);
    ASSERT_EQ(rel.width,128);
    cvcl_image_free(&rgb,NULL);cvcl_image_free(&gray,NULL);cvcl_image_free(&blur,NULL);
    cvcl_image_free(&edges,NULL);cvcl_image_free(&dil,NULL);cvcl_image_free(&thr,NULL);
    cvcl_image_free(&rel,NULL);
    remove("cvcl_pipe.png");
}

/* -------------------------------------------------------------------------
 * Entry point
 * ---------------------------------------------------------------------- */

int main(void) {
    printf("=== test_correctness ===\n");

    printf("\nNumerical correctness:\n");
    RUN(test_gaussian_uniform_mean);
    RUN(test_box_matches_naive);
    RUN(test_median_exact);
    RUN(test_sobel_ramp);
    RUN(test_canny_step_edge);
    RUN(test_affine_identity);
    RUN(test_rotate_4x_identity);
    RUN(test_bicubic_constant);

    printf("\nBoundary and edge cases:\n");
    RUN(test_1x1_pipeline);
    RUN(test_1xN_image);
    RUN(test_Nx1_image);
    RUN(test_large_kernel_small_image);
    RUN(test_full_view_equals_original);

    printf("\nDraw correctness:\n");
    RUN(test_line_horizontal_pixel_count);
    RUN(test_filled_rect_pixel_count);
    RUN(test_circle_within_bounds);
    RUN(test_text_renders_nonzero);

    printf("\nMemory and determinism:\n");
    RUN(test_1000_alloc_free_no_crash);
    RUN(test_canny_deterministic);
    RUN(test_png_roundtrip);

    printf("\nCross-module pipeline:\n");
    RUN(test_full_pipeline);

    printf("\n%d/%d tests passed.\n", g_run-g_fail, g_run);
    return g_fail>0?1:0;
}
