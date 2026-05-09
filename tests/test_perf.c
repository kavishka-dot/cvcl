/**
 * @file test_perf.c
 * @brief Performance regression tests -- minimum throughput guarantees
 */

#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#  define _POSIX_C_SOURCE 199309L
#endif
#include <cvcl/cvcl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  include <windows.h>
static double now_ms(void) {
    LARGE_INTEGER f,c; QueryPerformanceFrequency(&f); QueryPerformanceCounter(&c);
    return (double)c.QuadPart/(double)f.QuadPart*1000.0;
}
#else
#  include <time.h>
static double now_ms(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC,&ts);
    return ts.tv_sec*1000.0+ts.tv_nsec/1e6;
}
#endif

static int g_run=0, g_fail=0;

typedef void(*bench_fn)(cvcl_image_t*,const cvcl_image_t*,void*);

static double mpixs(bench_fn fn, cvcl_image_t *d, const cvcl_image_t *s, void *p, int n) {
    fn(d,s,p);
    double t0=now_ms();
    for(int i=0;i<n;i++) fn(d,s,p);
    double ms=(now_ms()-t0)/n;
    return ((double)s->width*s->height/1e6)/(ms/1000.0);
}

#define PERF(label,fn,d,s,p,n,min) do { \
    printf("  %-45s",label); g_run++; \
    double r=mpixs(fn,d,s,p,n); \
    if(r<(min)){printf("FAIL %.1f<%.1f Mpix/s\n",r,(double)(min));g_fail++;} \
    else printf("PASS %.1f Mpix/s\n",r); \
} while(0)

static void fn_g5(cvcl_image_t*d,const cvcl_image_t*s,void*p){(void)p;cvcl_blur_gaussian(d,s,5,0.f,CVCL_BORDER_REPLICATE);}
static void fn_g15(cvcl_image_t*d,const cvcl_image_t*s,void*p){(void)p;cvcl_blur_gaussian(d,s,15,0.f,CVCL_BORDER_REPLICATE);}
static void fn_b5(cvcl_image_t*d,const cvcl_image_t*s,void*p){(void)p;cvcl_blur_box(d,s,5,CVCL_BORDER_REPLICATE);}
static void fn_b63(cvcl_image_t*d,const cvcl_image_t*s,void*p){(void)p;cvcl_blur_box(d,s,63,CVCL_BORDER_REPLICATE);}
static void fn_add(cvcl_image_t*d,const cvcl_image_t*s,void*p){cvcl_add(d,s,(const cvcl_image_t*)p);}
static void fn_erode(cvcl_image_t*d,const cvcl_image_t*s,void*p){(void)p;cvcl_erode(d,s,5,5,CVCL_BORDER_REPLICATE);}
static void fn_thr(cvcl_image_t*d,const cvcl_image_t*s,void*p){(void)p;cvcl_threshold(d,s,128.f,255.f,CVCL_THRESH_BINARY,NULL);}

int main(void) {
    printf("=== test_perf ===\n1920x1080 RGB\n\n");

    cvcl_image_t src,dst,src2,gray,gdst;
    cvcl_image_create(&src, 1920,1080,3,CVCL_DEPTH_U8,NULL);
    cvcl_image_create(&dst, 1920,1080,3,CVCL_DEPTH_U8,NULL);
    cvcl_image_create(&src2,1920,1080,3,CVCL_DEPTH_U8,NULL);
    cvcl_image_create(&gray,1920,1080,1,CVCL_DEPTH_U8,NULL);
    cvcl_image_create(&gdst,1920,1080,1,CVCL_DEPTH_U8,NULL);
    for(cvcl_i32 i=0;i<src.stride*src.height;i++) src.data[i]=(cvcl_u8)(i*2654435761u>>24);
    cvcl_convert_channels(&gray,&src);

    int N=5;
    printf("Gaussian blur:\n");
    PERF("gaussian k=5  RGB",  fn_g5,  &dst, &src, NULL,  N, 3.0);
    PERF("gaussian k=15 RGB",  fn_g15, &dst, &src, NULL,  N, 1.0);

    printf("\nBox blur (O(1) sliding window):\n");
    PERF("box k=5   RGB",      fn_b5,  &dst, &src, NULL,  N, 5.0);
    PERF("box k=63  RGB",      fn_b63, &dst, &src, NULL,  N, 5.0);

    printf("\nPixel-wise:\n");
    PERF("add saturating RGB", fn_add, &dst, &src, &src2, N, 50.0);

    printf("\nMorphology (grayscale):\n");
    PERF("erode 5x5 gray",     fn_erode, &gdst, &gray, NULL, N, 3.0);

    printf("\nThreshold (grayscale):\n");
    PERF("threshold binary",   fn_thr, &gdst, &gray, NULL, N, 20.0);

    /* O(1) invariant: box k=63 within 3x of k=5 */
    {
        fn_b5(&dst,&src,NULL); double t0=now_ms();
        for(int i=0;i<N;i++) fn_b5(&dst,&src,NULL);
        double t5=(now_ms()-t0)/N;

        fn_b63(&dst,&src,NULL); t0=now_ms();
        for(int i=0;i<N;i++) fn_b63(&dst,&src,NULL);
        double t63=(now_ms()-t0)/N;

        printf("\n  %-45s","box k=63 within 3x cost of k=5");
        g_run++;
        if(t63>t5*3.0){printf("FAIL ratio=%.1fx\n",t63/t5);g_fail++;}
        else printf("PASS ratio=%.1fx\n",t63/t5);
    }

    cvcl_image_free(&src,NULL);cvcl_image_free(&dst,NULL);
    cvcl_image_free(&src2,NULL);cvcl_image_free(&gray,NULL);cvcl_image_free(&gdst,NULL);

    printf("\n%d/%d tests passed.\n",g_run-g_fail,g_run);
    return g_fail>0?1:0;
}
