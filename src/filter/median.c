/**
 * @file filter_median.c
 * @brief Median filter -- insertion sort (small k) or histogram sliding (large k)
 */

#include <cvcl/cvcl_filter.h>
#include <cvcl/cvcl_pixel.h>
#include <stdlib.h>
#include <string.h>

static void isort(cvcl_u8 *arr, cvcl_i32 n) {
    for (cvcl_i32 i = 1; i < n; i++) {
        cvcl_u8 key = arr[i]; cvcl_i32 j = i-1;
        while (j >= 0 && arr[j] > key) { arr[j+1]=arr[j]; j--; }
        arr[j+1] = key;
    }
}

static cvcl_result_t median_small(cvcl_image_t *dst, const cvcl_image_t *src,
                                    cvcl_i32 ksize) {
    cvcl_i32 half=ksize/2, w=src->width, h=src->height, ch=src->channels;
    cvcl_i32 wlen=ksize*ksize;
    cvcl_u8  win[81];
    for (cvcl_i32 y=0; y<h; y++) {
        cvcl_u8 *dr=cvcl_image_row(dst,y);
        for (cvcl_i32 x=0; x<w; x++) {
            for (cvcl_i32 c=0; c<ch; c++) {
                cvcl_i32 idx=0;
                for (cvcl_i32 ky=-half; ky<=half; ky++) {
                    cvcl_i32 sy=CVCL_CLAMP(y+ky,0,h-1);
                    const cvcl_u8 *sr=cvcl_image_row(src,sy);
                    for (cvcl_i32 kx=-half; kx<=half; kx++) {
                        win[idx++]=sr[CVCL_CLAMP(x+kx,0,w-1)*ch+c];
                    }
                }
                isort(win,wlen);
                dr[x*ch+c]=win[wlen/2];
            }
        }
    }
    return CVCL_OK;
}

static cvcl_u8 hist_median(cvcl_u32 *hist, cvcl_i32 threshold) {
    cvcl_i32 count=0;
    for (cvcl_i32 i=0; i<256; i++) {
        count+=(cvcl_i32)hist[i];
        if (count>=threshold) return (cvcl_u8)i;
    }
    return 255;
}

static cvcl_result_t median_hist_ch(cvcl_image_t *dst, const cvcl_image_t *src,
                                     cvcl_i32 ksize) {
    cvcl_i32 half=ksize/2, w=src->width, h=src->height;
    cvcl_i32 threshold=ksize*ksize/2+1;
    cvcl_u32 *hist=(cvcl_u32*)calloc(256,sizeof(cvcl_u32));
    if (!hist) return CVCL_ERR_ALLOC;

    for (cvcl_i32 y=0; y<h; y++) {
        memset(hist,0,256*sizeof(cvcl_u32));
        for (cvcl_i32 ky=-half; ky<=half; ky++) {
            cvcl_i32 sy=CVCL_CLAMP(y+ky,0,h-1);
            const cvcl_u8 *sr=cvcl_image_row(src,sy);
            for (cvcl_i32 kx=-half; kx<=half; kx++)
                hist[sr[CVCL_CLAMP(kx,0,w-1)]]++;
        }
        cvcl_u8 *dr=cvcl_image_row(dst,y);
        dr[0]=hist_median(hist,threshold);
        for (cvcl_i32 x=1; x<w; x++) {
            cvcl_i32 rem=CVCL_CLAMP(x-half-1,0,w-1);
            cvcl_i32 add=CVCL_CLAMP(x+half,  0,w-1);
            for (cvcl_i32 ky=-half; ky<=half; ky++) {
                cvcl_i32 sy=CVCL_CLAMP(y+ky,0,h-1);
                const cvcl_u8 *sr=cvcl_image_row(src,sy);
                hist[sr[rem]]--; hist[sr[add]]++;
            }
            dr[x]=hist_median(hist,threshold);
        }
    }
    free(hist);
    return CVCL_OK;
}

cvcl_result_t cvcl_blur_median(cvcl_image_t *dst, const cvcl_image_t *src,
                                cvcl_i32 ksize) {
    CVCL_CHECK_NULL(dst); CVCL_CHECK_NULL(src);
    CVCL_CHECK_NULL(dst->data); CVCL_CHECK_NULL(src->data);
    CVCL_CHECK_ARG(src->depth==CVCL_DEPTH_U8);
    CVCL_CHECK_ARG(ksize>=1);
    CVCL_CHECK_ARG(cvcl_image_compatible(dst,src));
    if (ksize%2==0) ksize++;

    if (ksize<=9) return median_small(dst,src,ksize);

    /* Large kernel: histogram per channel */
    cvcl_result_t rc=CVCL_OK;
    for (cvcl_i32 c=0; c<src->channels && rc==CVCL_OK; c++) {
        cvcl_image_t cs, cd;
        cvcl_image_create(&cs,src->width,src->height,1,CVCL_DEPTH_U8,NULL);
        cvcl_image_create(&cd,src->width,src->height,1,CVCL_DEPTH_U8,NULL);
        for (cvcl_i32 y=0;y<src->height;y++) {
            const cvcl_u8 *sr=cvcl_image_row(src,y);
            cvcl_u8 *dr=cvcl_image_row(&cs,y);
            for (cvcl_i32 x=0;x<src->width;x++) dr[x]=sr[x*src->channels+c];
        }
        rc=median_hist_ch(&cd,&cs,ksize);
        for (cvcl_i32 y=0;y<dst->height;y++) {
            const cvcl_u8 *sr=cvcl_image_row(&cd,y);
            cvcl_u8 *dr=cvcl_image_row(dst,y);
            for (cvcl_i32 x=0;x<dst->width;x++) dr[x*dst->channels+c]=sr[x];
        }
        cvcl_image_free(&cs,NULL); cvcl_image_free(&cd,NULL);
    }
    return rc;
}
