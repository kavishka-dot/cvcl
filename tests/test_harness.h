/**
 * @file test_harness.h
 * @brief Shared test harness for all CVCL tests
 */
#ifndef CVCL_TEST_HARNESS_H
#define CVCL_TEST_HARNESS_H

#include <cvcl/cvcl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static int g_tests_run    = 0;
static int g_tests_failed = 0;
static int g_tests_skipped= 0;

#define TEST(name) static void name(void)

#define RUN(name) do { \
    printf("  %-52s", #name); fflush(stdout); \
    g_tests_run++; name(); printf("PASS\n"); \
} while(0)

#define FAIL_MSG(fmt, ...) do { \
    printf("FAIL\n    " fmt "\n    at %s:%d\n", __VA_ARGS__, __FILE__, __LINE__); \
    g_tests_failed++; return; \
} while(0)

#define ASSERT_EQ(a,b) do { if((a)!=(b)) FAIL_MSG("Expected %s==%s",#a,#b); } while(0)
#define ASSERT_NE(a,b) do { if((a)==(b)) FAIL_MSG("Expected %s != %s",#a,#b); } while(0)
#define ASSERT_NULL(p)     ASSERT_EQ((p),NULL)
#define ASSERT_NOT_NULL(p) ASSERT_NE((p),NULL)
#define ASSERT_TRUE(c)     ASSERT_EQ(!!(c),1)
#define ASSERT_FALSE(c)    ASSERT_EQ(!!(c),0)
#define ASSERT_OK(rc) do { cvcl_result_t _r=(rc); if(_r!=CVCL_OK) FAIL_MSG("Expected CVCL_OK got %s(%d)",cvcl_strerror(_r),_r); } while(0)
#define ASSERT_NEAR(a,b,tol) do { double _d=(double)(a)-(double)(b); if(_d<0)_d=-_d; if(_d>(double)(tol)) FAIL_MSG("|%g-%g|=%g>%g",(double)(a),(double)(b),_d,(double)(tol)); } while(0)
#define ASSERT_PIXEL_EQ(img,x,y,c,val) do { cvcl_u8 _v=cvcl_get_u8((img),(x),(y),(c)); if(_v!=(cvcl_u8)(val)) FAIL_MSG("pixel(%d,%d,%d)=%d expected %d",(x),(y),(c),(int)_v,(int)(val)); } while(0)
#define ASSERT_PIXEL_NEAR(img,x,y,c,val,tol) do { int _v=cvcl_get_u8((img),(x),(y),(c)),_e=(int)(val),_d=_v-_e; if(_d<0)_d=-_d; if(_d>(tol)) FAIL_MSG("pixel(%d,%d,%d)=%d expected %d±%d",(x),(y),(c),_v,_e,(tol)); } while(0)

/* Counting allocator */
typedef struct { cvcl_size bytes, allocs, frees; } alloc_counter_t;
#if defined(__GNUC__) || defined(__clang__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wunused-function"
#endif

static void *_ca_alloc(cvcl_size n, void *ctx) {
    alloc_counter_t *c=(alloc_counter_t*)ctx;
    cvcl_u8 *raw=(cvcl_u8*)malloc(n+sizeof(cvcl_size));
    if(!raw) return NULL;
    memcpy(raw,&n,sizeof(cvcl_size));
    c->bytes+=n; c->allocs++; return raw+sizeof(cvcl_size);
}
static void _ca_free(void *ptr, void *ctx) {
    if(!ptr) return;
    alloc_counter_t *c=(alloc_counter_t*)ctx;
    cvcl_u8 *raw=(cvcl_u8*)ptr-sizeof(cvcl_size);
    cvcl_size n; memcpy(&n,raw,sizeof(cvcl_size));
    c->bytes-=n; c->frees++; free(raw);
}
#if defined(__GNUC__) || defined(__clang__)
#  pragma GCC diagnostic pop
#endif
#define COUNTING_ALLOC(name) \
    alloc_counter_t name##_ctx = {0,0,0}; \
    cvcl_allocator_t name = { _ca_alloc, _ca_free, &name##_ctx }

static int test_summary(const char *suite) {
    int passed = g_tests_run - g_tests_failed - g_tests_skipped;
    printf("\n%s: %d/%d passed", suite, passed, g_tests_run);
    if (g_tests_skipped) printf(", %d skipped", g_tests_skipped);
    printf(".\n");
    return g_tests_failed > 0 ? 1 : 0;
}

#endif /* CVCL_TEST_HARNESS_H */
