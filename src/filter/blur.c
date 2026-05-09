/**
 * @file filter_blur.c
 * @brief Optimized blur kernels - v2
 *
 * Optimizations over v1:
 *
 *  7. Gaussian vertical pass row-pointer cache  -- all ksize row pointers
 *     pre-built before the x-loop. Zero clamp/multiply inside the kernel.
 *  8. Gaussian horizontal pass restructured     -- explicit left/interior/
 *     right loops eliminate the per-pixel branch from the hot path.
 *  9. Box blur reciprocal division              -- precompute (1<<20)/ksize
 *     once; replace per-pixel integer divide with multiply+shift.
 * 10. Box blur flat accumulator                 -- horizontal pass processes
 *     all channels as a single w*ch flat array, improving vectorization.
 * 11. cvcl_scale SIMD                           -- SSE2/AVX2/NEON paths.
 * 12. cvcl_blend SSE2/AVX2                      -- widening multiply paths.
 *
 * Earlier layers (v1):
 *  1. restrict pointers
 *  2. Channel-specialized Gaussian (ch=1/3/4)
 *  3. Sliding-window box blur O(w*h)
 *  4. Fixed-point Q15 Gaussian kernel
 *  5. SIMD add/sub
 *  6. Prefetch hints
 */

#include <cvcl/cvcl_filter.h>
#include <cvcl/cvcl_pixel.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>

/* -------------------------------------------------------------------------
 * SIMD detection
 * ---------------------------------------------------------------------- */
#if !defined(CVCL_NO_SIMD)
#  if defined(__AVX2__)
#    include <immintrin.h>
#    define CVCL_HAVE_AVX2 1
#  elif defined(__SSE2__)
#    include <emmintrin.h>
#    define CVCL_HAVE_SSE2 1
#  elif defined(__ARM_NEON)
#    include <arm_neon.h>
#    define CVCL_HAVE_NEON 1
#  endif
#endif

#if defined(__GNUC__) || defined(__clang__)
#  define CVCL_PREFETCH(p) __builtin_prefetch((p), 0, 1)
#else
#  define CVCL_PREFETCH(p) ((void)(p))
#endif

/* -------------------------------------------------------------------------
 * Fixed-point Gaussian kernel (Q15)
 * ---------------------------------------------------------------------- */
#define GAUSS_SHIFT 15
#define GAUSS_ONE   (1 << GAUSS_SHIFT)
#define GAUSS_HALF  (1 << (GAUSS_SHIFT - 1))
#define MAX_KSIZE   65

static void make_gauss_fp(cvcl_i32 *kfp, cvcl_i32 ksize, cvcl_f32 sigma) {
    cvcl_i32 half = ksize / 2;
    cvcl_f32 fk[MAX_KSIZE], sum = 0.f;
    for (cvcl_i32 i = 0; i < ksize; i++) {
        cvcl_f32 x = (cvcl_f32)(i - half);
        fk[i] = expf(-0.5f * x * x / (sigma * sigma));
        sum  += fk[i];
    }
    cvcl_i32 fp_sum = 0;
    for (cvcl_i32 i = 0; i < ksize; i++) {
        kfp[i]  = (cvcl_i32)(fk[i] / sum * GAUSS_ONE + 0.5f);
        fp_sum += kfp[i];
    }
    kfp[half] += GAUSS_ONE - fp_sum;
}

/* -------------------------------------------------------------------------
 * Border-safe fetch (replicate) -- used only for border pixels
 * ---------------------------------------------------------------------- */
CVCL_INLINE cvcl_u8 fetch_rep(const cvcl_u8 *data,
                                cvcl_i32 x, cvcl_i32 y,
                                cvcl_i32 w, cvcl_i32 h,
                                cvcl_i32 stride, cvcl_i32 ch, cvcl_i32 c) {
    x = CVCL_CLAMP(x, 0, w-1);
    y = CVCL_CLAMP(y, 0, h-1);
    return data[(cvcl_size)y * stride + (cvcl_size)x * ch + c];
}

/* -------------------------------------------------------------------------
 * Gaussian blur
 * ---------------------------------------------------------------------- */
cvcl_result_t cvcl_blur_gaussian(cvcl_image_t       *dst,
                                  const cvcl_image_t *src,
                                  cvcl_i32            ksize,
                                  cvcl_f32            sigma,
                                  cvcl_border_t       border) {
    CVCL_CHECK_NULL(dst); CVCL_CHECK_NULL(src);
    CVCL_CHECK_NULL(dst->data); CVCL_CHECK_NULL(src->data);
    CVCL_CHECK_ARG(src->depth == CVCL_DEPTH_U8);
    CVCL_CHECK_ARG(cvcl_image_compatible(dst, src));

    if (ksize == 0 && sigma <= 0.f) return CVCL_ERR_INVALID_ARG;
    if (ksize == 0) ksize = (cvcl_i32)(sigma * 6.f + 1.f) | 1;
    if (sigma  <= 0.f) sigma = 0.3f*((cvcl_f32)(ksize-1)*0.5f-1.f)+0.8f;
    if (ksize % 2 == 0) ksize++;
    if (ksize > MAX_KSIZE) return CVCL_ERR_INVALID_ARG;
    CVCL_UNUSED(border);

    cvcl_i32 kfp[MAX_KSIZE];
    make_gauss_fp(kfp, ksize, sigma);

    cvcl_i32 w = src->width, h = src->height, ch = src->channels;
    cvcl_i32 half = ksize / 2;

    cvcl_image_t mid;
    /* Temp image uses default allocator. Future: accept workspace parameter. */
    CVCL_CHECK(cvcl_image_create(&mid, w, h, ch, CVCL_DEPTH_U8, NULL));

    /* ------------------------------------------------------------------ */
    /* Horizontal pass -- three explicit regions, no per-pixel branch      */
    /* ------------------------------------------------------------------ */
    for (cvcl_i32 y = 0; y < h; y++) {
        const cvcl_u8 * CVCL_RESTRICT sr = cvcl_image_row(src, y);
        cvcl_u8       * CVCL_RESTRICT mr = cvcl_image_row(&mid, y);
        if (y+1 < h) CVCL_PREFETCH(cvcl_image_row(src, y+1));

        /* Left border [0, half) */
        for (cvcl_i32 x = 0; x < half && x < w; x++) {
            for (cvcl_i32 c = 0; c < ch; c++) {
                cvcl_i32 acc = 0;
                for (cvcl_i32 k = 0; k < ksize; k++)
                    acc += kfp[k] * fetch_rep(src->data, x+k-half, y,
                                               w, h, src->stride, ch, c);
                mr[x*ch+c] = (cvcl_u8)((acc+GAUSS_HALF)>>GAUSS_SHIFT);
            }
        }

        /* Interior [half, w-half) -- hot path, no border check */
        if (ch == 1) {
            for (cvcl_i32 x = half; x < w-half; x++) {
                cvcl_i32 acc = 0;
                const cvcl_u8 *p = sr + x - half;
                for (cvcl_i32 k = 0; k < ksize; k++) acc += kfp[k] * p[k];
                mr[x] = (cvcl_u8)((acc+GAUSS_HALF)>>GAUSS_SHIFT);
            }
        } else if (ch == 3) {
            for (cvcl_i32 x = half; x < w-half; x++) {
                cvcl_i32 a0=0,a1=0,a2=0;
                const cvcl_u8 *p = sr + (x-half)*3;
                for (cvcl_i32 k = 0; k < ksize; k++,p+=3) {
                    a0+=kfp[k]*p[0]; a1+=kfp[k]*p[1]; a2+=kfp[k]*p[2];
                }
                mr[x*3+0]=(cvcl_u8)((a0+GAUSS_HALF)>>GAUSS_SHIFT);
                mr[x*3+1]=(cvcl_u8)((a1+GAUSS_HALF)>>GAUSS_SHIFT);
                mr[x*3+2]=(cvcl_u8)((a2+GAUSS_HALF)>>GAUSS_SHIFT);
            }
        } else if (ch == 4) {
            for (cvcl_i32 x = half; x < w-half; x++) {
                cvcl_i32 a0=0,a1=0,a2=0,a3=0;
                const cvcl_u8 *p = sr + (x-half)*4;
                for (cvcl_i32 k = 0; k < ksize; k++,p+=4) {
                    a0+=kfp[k]*p[0]; a1+=kfp[k]*p[1];
                    a2+=kfp[k]*p[2]; a3+=kfp[k]*p[3];
                }
                mr[x*4+0]=(cvcl_u8)((a0+GAUSS_HALF)>>GAUSS_SHIFT);
                mr[x*4+1]=(cvcl_u8)((a1+GAUSS_HALF)>>GAUSS_SHIFT);
                mr[x*4+2]=(cvcl_u8)((a2+GAUSS_HALF)>>GAUSS_SHIFT);
                mr[x*4+3]=(cvcl_u8)((a3+GAUSS_HALF)>>GAUSS_SHIFT);
            }
        } else {
            for (cvcl_i32 x = half; x < w-half; x++) {
                for (cvcl_i32 c = 0; c < ch; c++) {
                    cvcl_i32 acc = 0;
                    for (cvcl_i32 k = 0; k < ksize; k++)
                        acc += kfp[k] * sr[(x+k-half)*ch+c];
                    mr[x*ch+c]=(cvcl_u8)((acc+GAUSS_HALF)>>GAUSS_SHIFT);
                }
            }
        }

        /* Right border [w-half, w) */
        for (cvcl_i32 x = CVCL_MAX(w-half, half); x < w; x++) {
            for (cvcl_i32 c = 0; c < ch; c++) {
                cvcl_i32 acc = 0;
                for (cvcl_i32 k = 0; k < ksize; k++)
                    acc += kfp[k] * fetch_rep(src->data, x+k-half, y,
                                               w, h, src->stride, ch, c);
                mr[x*ch+c]=(cvcl_u8)((acc+GAUSS_HALF)>>GAUSS_SHIFT);
            }
        }
    }

    /* ------------------------------------------------------------------ */
    /* Vertical pass -- row pointer cache, blocked for L1                  */
    /* Pre-build all ksize row pointers before the x-loop.                 */
    /* Zero clamp/multiply inside the kernel loop.                         */
    /* ------------------------------------------------------------------ */
    #define BLK 32
    const cvcl_u8 *rows[MAX_KSIZE];  /* cached row pointers */

    for (cvcl_i32 bx = 0; bx < w; bx += BLK) {
        cvcl_i32 bx1 = CVCL_MIN(bx + BLK, w);

        for (cvcl_i32 y = 0; y < h; y++) {
            cvcl_u8 * CVCL_RESTRICT dr = cvcl_image_row(dst, y);
            if (y+1 < h) CVCL_PREFETCH(cvcl_image_row(&mid, y+1));

            /* Build row pointer table once per output row */
            for (cvcl_i32 k = 0; k < ksize; k++)
                rows[k] = cvcl_image_row(&mid,
                              CVCL_CLAMP(y + k - half, 0, h-1));

            for (cvcl_i32 x = bx; x < bx1; x++) {
                for (cvcl_i32 c = 0; c < ch; c++) {
                    cvcl_i32 acc = 0;
                    for (cvcl_i32 k = 0; k < ksize; k++)
                        acc += kfp[k] * (cvcl_i32)rows[k][x*ch+c];
                    dr[x*ch+c]=(cvcl_u8)((acc+GAUSS_HALF)>>GAUSS_SHIFT);
                }
            }
        }
    }
    #undef BLK

    cvcl_image_free(&mid, NULL);
    return CVCL_OK;
}

/* -------------------------------------------------------------------------
 * Box blur -- O(w*h) sliding window, reciprocal division
 * ---------------------------------------------------------------------- */
cvcl_result_t cvcl_blur_box(cvcl_image_t       *dst,
                             const cvcl_image_t *src,
                             cvcl_i32            ksize,
                             cvcl_border_t       border) {
    CVCL_CHECK_NULL(dst); CVCL_CHECK_NULL(src);
    CVCL_CHECK_NULL(dst->data); CVCL_CHECK_NULL(src->data);
    CVCL_CHECK_ARG(src->depth == CVCL_DEPTH_U8);
    CVCL_CHECK_ARG(cvcl_image_compatible(dst, src));
    CVCL_CHECK_ARG(ksize >= 1);
    CVCL_UNUSED(border);
    if (ksize % 2 == 0) ksize++;

    cvcl_i32 w = src->width, h = src->height, ch = src->channels;
    cvcl_i32 half = ksize / 2;
    cvcl_i32 n    = w * ch;   /* flat: treat all channels as one wide row */
    /* Reciprocal: (1<<20)/ksize -- avoids per-pixel integer divide */
    //cvcl_u32 inv   = (1u << 20) / (cvcl_u32)ksize;
    //cvcl_u32 shift = 20;
    
    /* Rounded reciprocal for better accuracy */
    cvcl_u32 shift = 20;
    cvcl_u32 inv =
        ((1u << shift) + ((cvcl_u32)ksize >> 1)) /
        (cvcl_u32)ksize;

    #define BOX_RND (1u << (shift - 1))

    /* Intermediate image */
    cvcl_image_t mid;
    /* Temp image uses default allocator. Future: accept workspace parameter. */
    CVCL_CHECK(cvcl_image_create(&mid, w, h, ch, CVCL_DEPTH_U8, NULL));

    /* Flat accumulator -- process all channels together */
    cvcl_u32 *acc = (cvcl_u32 *)malloc((cvcl_size)n * sizeof(cvcl_u32));
    if (!acc) { cvcl_image_free(&mid, NULL); return CVCL_ERR_ALLOC; }

    /* ---- Horizontal pass ---- */
    for (cvcl_i32 y = 0; y < h; y++) {
        const cvcl_u8 * CVCL_RESTRICT sr = cvcl_image_row(src, y);
        cvcl_u8       * CVCL_RESTRICT mr = cvcl_image_row(&mid, y);
        CVCL_PREFETCH(y+1 < h ? cvcl_image_row(src, y+1) : sr);

        /* Init window sum for x=0 across all channels flat */
        for (cvcl_i32 i = 0; i < ch; i++) {
            cvcl_u32 s = 0;
            for (cvcl_i32 k = -half; k <= half; k++)
                s += sr[CVCL_CLAMP(k, 0, w-1) * ch + i];
            acc[i] = s;
            //mr[i]  = (cvcl_u8)((s * inv) >> shift);
            mr[i] = (cvcl_u8)((s * inv + BOX_RND) >> shift);
        }

        /* Slide right -- flat over channels */
        for (cvcl_i32 x = 1; x < w; x++) {
            cvcl_i32 ai = CVCL_CLAMP(x+half,   0, w-1) * ch;
            cvcl_i32 si = CVCL_CLAMP(x-half-1, 0, w-1) * ch;
            for (cvcl_i32 c = 0; c < ch; c++) {
                cvcl_u32 v = acc[(cvcl_size)(x-1)*ch+c] + sr[ai+c] - sr[si+c];
                acc[(cvcl_size)x*ch+c] = v;
                //mr[x*ch+c] = (cvcl_u8)((v * inv) >> shift);
                mr[x*ch+c] = (cvcl_u8)((v * inv + BOX_RND) >> shift);
            }
        }
    }
    free(acc);

    /* ---- Vertical pass ---- */
    cvcl_u32 *cacc = (cvcl_u32 *)calloc((cvcl_size)n, sizeof(cvcl_u32));
    if (!cacc) { cvcl_image_free(&mid, NULL); return CVCL_ERR_ALLOC; }

    /* Init column sums for y=0 */
    for (cvcl_i32 k = -half; k <= half; k++) {
        const cvcl_u8 * CVCL_RESTRICT mr =
            cvcl_image_row(&mid, CVCL_CLAMP(k, 0, h-1));
        for (cvcl_i32 i = 0; i < n; i++) cacc[i] += mr[i];
    }

    /* Write row 0 */
    {
        cvcl_u8 * CVCL_RESTRICT dr = cvcl_image_row(dst, 0);
        for (cvcl_i32 i = 0; i < n; i++)
            //dr[i] = (cvcl_u8)((cacc[i] * inv) >> shift);
            dr[i] = (cvcl_u8)((cacc[i] * inv + BOX_RND) >> shift);
    }

    /* Slide down */
    for (cvcl_i32 y = 1; y < h; y++) {
        const cvcl_u8 * CVCL_RESTRICT ar =
            cvcl_image_row(&mid, CVCL_CLAMP(y+half,   0, h-1));
        const cvcl_u8 * CVCL_RESTRICT sr2 =
            cvcl_image_row(&mid, CVCL_CLAMP(y-half-1, 0, h-1));
        cvcl_u8       * CVCL_RESTRICT dr = cvcl_image_row(dst, y);
        CVCL_PREFETCH(y+1 < h ? cvcl_image_row(&mid, y+1) : ar);

        for (cvcl_i32 i = 0; i < n; i++) {
            cacc[i] += ar[i];
            cacc[i] -= sr2[i];
            //dr[i] = (cvcl_u8)((cacc[i] * inv) >> shift);
            dr[i] = (cvcl_u8)((cacc[i] * inv + BOX_RND) >> shift);
        }
    }

    free(cacc);
    cvcl_image_free(&mid, NULL);
    return CVCL_OK;
}

/* -------------------------------------------------------------------------
 * Pixel-wise add -- SIMD saturating
 * ---------------------------------------------------------------------- */
cvcl_result_t cvcl_add(cvcl_image_t *dst,
                        const cvcl_image_t *a, const cvcl_image_t *b) {
    CVCL_CHECK_NULL(dst); CVCL_CHECK_NULL(a); CVCL_CHECK_NULL(b);
    CVCL_CHECK_NULL(dst->data); CVCL_CHECK_NULL(a->data); CVCL_CHECK_NULL(b->data);
    if (!cvcl_image_compatible(a,b))   return CVCL_ERR_SIZE_MISMATCH;
    if (!cvcl_image_compatible(dst,a)) return CVCL_ERR_SIZE_MISMATCH;
    CVCL_CHECK_ARG(a->depth == CVCL_DEPTH_U8);

    for (cvcl_i32 y = 0; y < a->height; y++) {
        const cvcl_u8 *pa = cvcl_image_row(a,y);
        const cvcl_u8 *pb = cvcl_image_row(b,y);
        cvcl_u8       *pd = cvcl_image_row(dst,y);
        cvcl_i32 n = a->width * a->channels, i = 0;
#if defined(CVCL_HAVE_AVX2)
        for (; i <= n-32; i+=32) {
            __m256i va=_mm256_loadu_si256((const __m256i*)(pa+i));
            __m256i vb=_mm256_loadu_si256((const __m256i*)(pb+i));
            _mm256_storeu_si256((__m256i*)(pd+i),_mm256_adds_epu8(va,vb));
        }
#elif defined(CVCL_HAVE_SSE2)
        for (; i <= n-16; i+=16) {
            __m128i va=_mm_loadu_si128((const __m128i*)(pa+i));
            __m128i vb=_mm_loadu_si128((const __m128i*)(pb+i));
            _mm_storeu_si128((__m128i*)(pd+i),_mm_adds_epu8(va,vb));
        }
#elif defined(CVCL_HAVE_NEON)
        for (; i <= n-16; i+=16)
            vst1q_u8(pd+i, vqaddq_u8(vld1q_u8(pa+i), vld1q_u8(pb+i)));
#endif
        for (; i < n; i++)
            pd[i]=(cvcl_u8)CVCL_MIN((cvcl_i32)pa[i]+(cvcl_i32)pb[i],255);
    }
    return CVCL_OK;
}

/* -------------------------------------------------------------------------
 * Pixel-wise sub -- SIMD saturating
 * ---------------------------------------------------------------------- */
cvcl_result_t cvcl_sub(cvcl_image_t *dst,
                        const cvcl_image_t *a, const cvcl_image_t *b) {
    CVCL_CHECK_NULL(dst); CVCL_CHECK_NULL(a); CVCL_CHECK_NULL(b);
    CVCL_CHECK_NULL(dst->data); CVCL_CHECK_NULL(a->data); CVCL_CHECK_NULL(b->data);
    if (!cvcl_image_compatible(a,b))   return CVCL_ERR_SIZE_MISMATCH;
    if (!cvcl_image_compatible(dst,a)) return CVCL_ERR_SIZE_MISMATCH;
    CVCL_CHECK_ARG(a->depth == CVCL_DEPTH_U8);

    for (cvcl_i32 y = 0; y < a->height; y++) {
        const cvcl_u8 *pa = cvcl_image_row(a,y);
        const cvcl_u8 *pb = cvcl_image_row(b,y);
        cvcl_u8       *pd = cvcl_image_row(dst,y);
        cvcl_i32 n = a->width * a->channels, i = 0;
#if defined(CVCL_HAVE_AVX2)
        for (; i <= n-32; i+=32) {
            __m256i va=_mm256_loadu_si256((const __m256i*)(pa+i));
            __m256i vb=_mm256_loadu_si256((const __m256i*)(pb+i));
            _mm256_storeu_si256((__m256i*)(pd+i),_mm256_subs_epu8(va,vb));
        }
#elif defined(CVCL_HAVE_SSE2)
        for (; i <= n-16; i+=16) {
            __m128i va=_mm_loadu_si128((const __m128i*)(pa+i));
            __m128i vb=_mm_loadu_si128((const __m128i*)(pb+i));
            _mm_storeu_si128((__m128i*)(pd+i),_mm_subs_epu8(va,vb));
        }
#elif defined(CVCL_HAVE_NEON)
        for (; i <= n-16; i+=16)
            vst1q_u8(pd+i, vqsubq_u8(vld1q_u8(pa+i), vld1q_u8(pb+i)));
#endif
        for (; i < n; i++)
            pd[i]=(cvcl_u8)CVCL_MAX((cvcl_i32)pa[i]-(cvcl_i32)pb[i],0);
    }
    return CVCL_OK;
}

/* -------------------------------------------------------------------------
 * Pixel-wise scale -- fixed-point Q8, SIMD widening multiply
 * ---------------------------------------------------------------------- */
cvcl_result_t cvcl_scale(cvcl_image_t *dst,
                          const cvcl_image_t *a, cvcl_f32 scalar) {
    CVCL_CHECK_NULL(dst); CVCL_CHECK_NULL(a);
    CVCL_CHECK_NULL(dst->data); CVCL_CHECK_NULL(a->data);
    if (!cvcl_image_compatible(dst,a)) return CVCL_ERR_SIZE_MISMATCH;
    CVCL_CHECK_ARG(a->depth == CVCL_DEPTH_U8);

    cvcl_i32 sfp = (cvcl_i32)(scalar * 256.f + 0.5f);
    sfp = CVCL_CLAMP(sfp, 0, 65535);

    for (cvcl_i32 y = 0; y < a->height; y++) {
        const cvcl_u8 *pa = cvcl_image_row(a,y);
        cvcl_u8       *pd = cvcl_image_row(dst,y);
        cvcl_i32 n = a->width * a->channels, i = 0;

#if defined(CVCL_HAVE_AVX2)
        /* Widen u8->u16, multiply by sfp, shift right 8, pack back */
        __m256i vsfp = _mm256_set1_epi16((short)sfp);
        for (; i <= n-16; i+=16) {
            __m128i v8  = _mm_loadu_si128((const __m128i*)(pa+i));
            /* Low 8 bytes */
            __m256i v16 = _mm256_cvtepu8_epi16(v8);
            __m256i mul = _mm256_mulhi_epu16(
                _mm256_slli_epi16(v16, 8), vsfp);
            /* Pack 16->8 with unsigned saturation */
            __m128i lo  = _mm256_castsi256_si128(mul);
            __m128i hi  = _mm256_extracti128_si256(mul, 1);
            _mm_storeu_si128((__m128i*)(pd+i), _mm_packus_epi16(lo, hi));
        }
#elif defined(CVCL_HAVE_SSE2)
        __m128i vsfp = _mm_set1_epi16((short)sfp);
        __m128i zero = _mm_setzero_si128();
        for (; i <= n-8; i+=8) {
            __m128i v8  = _mm_loadl_epi64((const __m128i*)(pa+i));
            __m128i v16 = _mm_unpacklo_epi8(v8, zero);
            /* (v16 * sfp) >> 8  via mulhi on (v16<<8) */
            __m128i mul = _mm_mulhi_epu16(_mm_slli_epi16(v16,8), vsfp);
            _mm_storel_epi64((__m128i*)(pd+i),
                             _mm_packus_epi16(mul, zero));
        }
#elif defined(CVCL_HAVE_NEON)
        uint16_t sfp16 = (uint16_t)sfp;
        for (; i <= n-8; i+=8) {
            uint8x8_t  v8  = vld1_u8(pa+i);
            uint16x8_t v16 = vmovl_u8(v8);
            /* multiply then shift right 8 */
            uint16x8_t mul = vshrq_n_u16(vmulq_n_u16(v16, sfp16), 8);
            vst1_u8(pd+i, vqmovn_u16(mul));
        }
#endif
        for (; i < n; i++)
            pd[i]=(cvcl_u8)CVCL_CLAMP((pa[i]*sfp)>>8, 0, 255);
    }
    return CVCL_OK;
}

/* -------------------------------------------------------------------------
 * Alpha blend -- fixed-point Q8, SIMD widening multiply
 * ---------------------------------------------------------------------- */
cvcl_result_t cvcl_blend(cvcl_image_t *dst,
                          const cvcl_image_t *a,
                          const cvcl_image_t *b,
                          cvcl_f32 alpha) {
    CVCL_CHECK_NULL(dst); CVCL_CHECK_NULL(a); CVCL_CHECK_NULL(b);
    if (!cvcl_image_compatible(a,b))   return CVCL_ERR_SIZE_MISMATCH;
    if (!cvcl_image_compatible(dst,a)) return CVCL_ERR_SIZE_MISMATCH;
    CVCL_CHECK_ARG(a->depth == CVCL_DEPTH_U8);

    cvcl_i32 afp = (cvcl_i32)(alpha * 256.f + 0.5f);
    afp = CVCL_CLAMP(afp, 0, 256);
    cvcl_i32 bfp = 256 - afp;

    for (cvcl_i32 y = 0; y < a->height; y++) {
        const cvcl_u8 *pa = cvcl_image_row(a,y);
        const cvcl_u8 *pb = cvcl_image_row(b,y);
        cvcl_u8       *pd = cvcl_image_row(dst,y);
        cvcl_i32 n = a->width * a->channels, i = 0;

#if defined(CVCL_HAVE_AVX2)
        __m256i vaf = _mm256_set1_epi16((short)afp);
        __m256i vbf = _mm256_set1_epi16((short)bfp);
        for (; i <= n-16; i+=16) {
            __m128i a8  = _mm_loadu_si128((const __m128i*)(pa+i));
            __m128i b8  = _mm_loadu_si128((const __m128i*)(pb+i));
            __m256i a16 = _mm256_cvtepu8_epi16(a8);
            __m256i b16 = _mm256_cvtepu8_epi16(b8);
            /* (a16*afp + b16*bfp) >> 8 */
            __m256i r   = _mm256_srli_epi16(
                _mm256_add_epi16(
                    _mm256_mullo_epi16(a16, vaf),
                    _mm256_mullo_epi16(b16, vbf)), 8);
            __m128i lo  = _mm256_castsi256_si128(r);
            __m128i hi  = _mm256_extracti128_si256(r, 1);
            _mm_storeu_si128((__m128i*)(pd+i), _mm_packus_epi16(lo, hi));
        }
#elif defined(CVCL_HAVE_SSE2)
        __m128i vaf = _mm_set1_epi16((short)afp);
        __m128i vbf = _mm_set1_epi16((short)bfp);
        __m128i zero= _mm_setzero_si128();
        for (; i <= n-8; i+=8) {
            __m128i a8  = _mm_loadl_epi64((const __m128i*)(pa+i));
            __m128i b8  = _mm_loadl_epi64((const __m128i*)(pb+i));
            __m128i a16 = _mm_unpacklo_epi8(a8, zero);
            __m128i b16 = _mm_unpacklo_epi8(b8, zero);
            __m128i r   = _mm_srli_epi16(
                _mm_add_epi16(
                    _mm_mullo_epi16(a16, vaf),
                    _mm_mullo_epi16(b16, vbf)), 8);
            _mm_storel_epi64((__m128i*)(pd+i),
                             _mm_packus_epi16(r, zero));
        }
#elif defined(CVCL_HAVE_NEON)
        uint8x8_t vaf8 = vdup_n_u8((cvcl_u8)CVCL_MIN(afp,255));
        uint8x8_t vbf8 = vdup_n_u8((cvcl_u8)CVCL_MIN(bfp,255));
        for (; i <= n-8; i+=8) {
            uint16x8_t r = vmull_u8(vld1_u8(pa+i), vaf8);
            r = vmlal_u8(r, vld1_u8(pb+i), vbf8);
            vst1_u8(pd+i, vshrn_n_u16(r,8));
        }
#endif
        for (; i < n; i++)
            pd[i]=(cvcl_u8)CVCL_CLAMP(
                ((cvcl_i32)pa[i]*afp+(cvcl_i32)pb[i]*bfp)>>8, 0, 255);
    }
    return CVCL_OK;
}
