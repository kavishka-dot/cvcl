/**
 * @file io_png_native.c
 * @brief Native PNG read and write -- zero external dependencies
 *
 * Writer: uncompressed DEFLATE stored blocks (valid PNG, larger files).
 * Reader: full DEFLATE decompression (handles all standard PNG files).
 *
 * Supports:
 *   Read:  8-bit gray, 8-bit gray+alpha, 8-bit RGB, 8-bit RGBA
 *          Non-interlaced only. Standard DEFLATE compression.
 *   Write: 8-bit gray, 8-bit RGB, 8-bit RGBA (uncompressed)
 */

#ifndef CVCL_NO_STDLIB
/* File I/O is unavailable in freestanding mode (no filesystem) */

#include <cvcl/cvcl_platform.h>
#include <cvcl/cvcl_io.h>
#include <stdio.h>

/* =========================================================================
 * Shared utilities
 * ====================================================================== */

static cvcl_u32 crc32_table[256];
static int crc32_ready = 0;

static void crc32_init_table(void) {
    for (cvcl_u32 n = 0; n < 256; n++) {
        cvcl_u32 c = n;
        for (int k = 0; k < 8; k++)
            c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        crc32_table[n] = c;
    }
    crc32_ready = 1;
}

static cvcl_u32 crc32_buf(cvcl_u32 crc, const cvcl_u8 *buf, cvcl_size n) {
    crc = ~crc;
    for (cvcl_size i = 0; i < n; i++)
        crc = crc32_table[(crc ^ buf[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

static cvcl_u32 adler32_buf(cvcl_u32 adler, const cvcl_u8 *buf, cvcl_size n) {
    cvcl_u32 s1 = adler & 0xFFFF, s2 = adler >> 16;
    for (cvcl_size i = 0; i < n; i++) {
        s1 = (s1 + buf[i]) % 65521;
        s2 = (s2 + s1)     % 65521;
    }
    return (s2 << 16) | s1;
}

static cvcl_u32 read_u32be(const cvcl_u8 *p) {
    return ((cvcl_u32)p[0]<<24)|((cvcl_u32)p[1]<<16)|
           ((cvcl_u32)p[2]<<8)|(cvcl_u32)p[3];
}

static void write_u32be_f(FILE *f, cvcl_u32 v) {
    cvcl_u8 b[4]={(cvcl_u8)(v>>24),(cvcl_u8)(v>>16),(cvcl_u8)(v>>8),(cvcl_u8)v};
    fwrite(b,1,4,f);
}

static void write_chunk(FILE *f, const char *type,
                         const cvcl_u8 *data, cvcl_u32 len) {
    write_u32be_f(f, len);
    fwrite(type, 1, 4, f);
    cvcl_u32 crc = crc32_buf(0, (const cvcl_u8*)type, 4);
    if (data && len > 0) { fwrite(data,1,len,f); crc=crc32_buf(crc,data,len); }
    write_u32be_f(f, crc);
}

/* =========================================================================
 * DEFLATE decompressor (RFC 1951) -- handles stored, fixed, dynamic blocks
 * ====================================================================== */

typedef struct {
    const cvcl_u8 *src;
    cvcl_size      src_len;
    cvcl_size      src_pos;
    cvcl_u32       bit_buf;
    int            bit_cnt;
    cvcl_u8       *dst;
    cvcl_size      dst_cap;
    cvcl_size      dst_pos;
    int            error;
} deflate_t;

static void dfl_init(deflate_t *d, const cvcl_u8 *src, cvcl_size slen,
                      cvcl_u8 *dst, cvcl_size dcap) {
    CVCL_MEMSET(d, 0, sizeof(*d));
    d->src = src; d->src_len = slen;
    d->dst = dst; d->dst_cap = dcap;
}

static cvcl_u8 dfl_byte(deflate_t *d) {
    if (d->src_pos >= d->src_len) { d->error = 1; return 0; }
    return d->src[d->src_pos++];
}

static void dfl_fill(deflate_t *d, int need) {
    while (d->bit_cnt < need && !d->error) {
        d->bit_buf |= (cvcl_u32)dfl_byte(d) << d->bit_cnt;
        d->bit_cnt += 8;
    }
}

static cvcl_u32 dfl_bits(deflate_t *d, int n) {
    if (n == 0) return 0;
    dfl_fill(d, n);
    cvcl_u32 v = d->bit_buf & ((1u << n) - 1);
    d->bit_buf >>= n; d->bit_cnt -= n;
    return v;
}

static void dfl_emit(deflate_t *d, cvcl_u8 b) {
    if (d->dst_pos >= d->dst_cap) { d->error = 1; return; }
    d->dst[d->dst_pos++] = b;
}

static void dfl_copy(deflate_t *d, cvcl_size dist, cvcl_size len) {
    for (cvcl_size i = 0; i < len; i++) {
        if (d->dst_pos < dist) { d->error = 1; return; }
        dfl_emit(d, d->dst[d->dst_pos - dist]);
    }
}

/* Huffman decoder */
typedef struct {
    cvcl_u16 count[16];
    cvcl_u16 symbol[288];
} huffman_t;

static void huff_build(huffman_t *h, const cvcl_u8 *lengths, int n) {
    CVCL_MEMSET(h, 0, sizeof(*h));
    int offs[16] = {0};
    for (int i = 0; i < n; i++) if (lengths[i]) h->count[lengths[i]]++;
    for (int i = 1; i < 15; i++) offs[i+1] = offs[i] + h->count[i];
    for (int i = 0; i < n; i++)
        if (lengths[i]) h->symbol[offs[lengths[i]]++] = (cvcl_u16)i;
}

static int huff_decode(deflate_t *d, const huffman_t *h) {
    int code = 0, first = 0, idx = 0;
    for (int len = 1; len <= 15; len++) {
        dfl_fill(d, len);
        int bit = (d->bit_buf >> (len-1)) & 1;
        code = (code << 1) | bit;
        int count = h->count[len];
        if (code - count < first) {
            d->bit_buf >>= len; d->bit_cnt -= len;
            return h->symbol[idx + (code - first)];
        }
        idx += count; first = (first + count) << 1;
    }
    d->error = 1; return -1;
}

/* Fixed Huffman tables (RFC 1951 section 3.2.6) */
static void build_fixed(huffman_t *lit, huffman_t *dst_h) {
    cvcl_u8 llen[288], dlen[32];
    int i;
    for (i=0;   i<144; i++) llen[i]=8;
    for (i=144; i<256; i++) llen[i]=9;
    for (i=256; i<280; i++) llen[i]=7;
    for (i=280; i<288; i++) llen[i]=8;
    for (i=0;   i<32;  i++) dlen[i]=5;
    huff_build(lit, llen, 288);
    huff_build(dst_h, dlen, 32);
}

static const int len_base[29]  = {3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
static const int len_extra[29] = {0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
static const int dist_base[30] = {1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
static const int dist_extra[30]= {0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};

static void dfl_decode_block(deflate_t *d, const huffman_t *lit, const huffman_t *dst_h) {
    while (!d->error) {
        int sym = huff_decode(d, lit);
        if (sym < 0 || d->error) break;
        if (sym < 256) { dfl_emit(d, (cvcl_u8)sym); continue; }
        if (sym == 256) break;
        int li = sym - 257;
        if (li < 0 || li > 28) { d->error=1; break; }
        int len  = len_base[li]  + (int)dfl_bits(d, len_extra[li]);
        int dc   = huff_decode(d, dst_h);
        if (dc < 0 || dc > 29) { d->error=1; break; }
        int dist = dist_base[dc] + (int)dfl_bits(d, dist_extra[dc]);
        dfl_copy(d, (cvcl_size)dist, (cvcl_size)len);
    }
}

static void dfl_dynamic(deflate_t *d) {
    int hlit  = (int)dfl_bits(d,5) + 257;
    int hdist = (int)dfl_bits(d,5) + 1;
    int hclen = (int)dfl_bits(d,4) + 4;

    static const int order[19]={16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};
    cvcl_u8 clens[19]={0};
    for (int i=0; i<hclen; i++) clens[order[i]]=(cvcl_u8)dfl_bits(d,3);

    huffman_t cl;
    huff_build(&cl, clens, 19);

    cvcl_u8 lens[320]={0};
    int total = hlit + hdist, i = 0;
    while (i < total && !d->error) {
        int sym = huff_decode(d, &cl);
        if (sym < 16) { lens[i++]=(cvcl_u8)sym; }
        else if (sym==16) { int rep=3+(int)dfl_bits(d,2); cvcl_u8 v=i?lens[i-1]:0; while(rep--&&i<total)lens[i++]=v; }
        else if (sym==17) { int rep=3+(int)dfl_bits(d,3); while(rep--&&i<total)lens[i++]=0; }
        else              { int rep=11+(int)dfl_bits(d,7);while(rep--&&i<total)lens[i++]=0; }
    }

    huffman_t lit, dst_h;
    huff_build(&lit,   lens,       hlit);
    huff_build(&dst_h, lens+hlit,  hdist);
    dfl_decode_block(d, &lit, &dst_h);
}

/* Top-level inflate: skip zlib header, process DEFLATE blocks */
static int inflate_zlib(const cvcl_u8 *src, cvcl_size slen,
                         cvcl_u8 *dst, cvcl_size dcap,
                         cvcl_size *out_len) {
    if (slen < 2) { return -1; }
    /* Skip zlib header (2 bytes CMF+FLG) */
    deflate_t d;
    dfl_init(&d, src+2, slen-2, dst, dcap);

    int done = 0;
    while (!done && !d.error) {
        int bfinal = (int)dfl_bits(&d, 1);
        int btype  = (int)dfl_bits(&d, 2);
        if (btype == 0) {
            /* Stored block */
            d.bit_buf = 0; d.bit_cnt = 0;  /* byte-align */
            cvcl_u16 len  = (cvcl_u16)dfl_byte(&d) | ((cvcl_u16)dfl_byte(&d)<<8);
            /* cvcl_u16 nlen = */ dfl_byte(&d); dfl_byte(&d);  /* skip nlen */
            for (cvcl_u16 i=0; i<len && !d.error; i++)
                dfl_emit(&d, dfl_byte(&d));
        } else if (btype == 1) {
            huffman_t lit, dst_h;
            build_fixed(&lit, &dst_h);
            dfl_decode_block(&d, &lit, &dst_h);
        } else if (btype == 2) {
            dfl_dynamic(&d);
        } else {
            d.error = 1;
        }
        if (bfinal) done = 1;
    }
    if (d.error) return -1;
    if (out_len) *out_len = d.dst_pos;
    return 0;
}

/* =========================================================================
 * PNG filter reconstruction (per-row)
 * ====================================================================== */

static void png_unfilter(cvcl_u8 *row, const cvcl_u8 *prev,
                          int width, int bpp, int filter) {
    switch (filter) {
        case 0: /* None */ break;
        case 1: /* Sub */
            for (int x=bpp; x<width*bpp; x++)
                row[x] += row[x-bpp];
            break;
        case 2: /* Up */
            for (int x=0; x<width*bpp; x++)
                row[x] += prev[x];
            break;
        case 3: /* Average */
            for (int x=0; x<width*bpp; x++) {
                int a = x>=bpp ? row[x-bpp] : 0;
                row[x] += (cvcl_u8)((a + prev[x]) >> 1);
            }
            break;
        case 4: /* Paeth */
            for (int x=0; x<width*bpp; x++) {
                int a = x>=bpp ? row[x-bpp] : 0;
                int b = prev[x];
                int c = x>=bpp ? prev[x-bpp] : 0;
                int p = a+b-c;
                int pa=p-a; if(pa<0)pa=-pa;
                int pb=p-b; if(pb<0)pb=-pb;
                int pc=p-c; if(pc<0)pc=-pc;
                row[x] += (cvcl_u8)(pa<=pb&&pa<=pc ? a : pb<=pc ? b : c);
            }
            break;
    }
}

/* =========================================================================
 * Public: Read PNG
 * ====================================================================== */

cvcl_result_t cvcl_io_read_png_native(cvcl_image_t          *img,
                                       const char            *path,
                                       const cvcl_allocator_t *alloc) {
    CVCL_CHECK_NULL(img); CVCL_CHECK_NULL(path);
    if (!crc32_ready) crc32_init_table();

    FILE *f = fopen(path, "rb");
    if (!f) return CVCL_ERR_IO;

    /* Read entire file */
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (fsize < 8) { fclose(f); return CVCL_ERR_FORMAT; }

    cvcl_u8 *raw = (cvcl_u8 *)CVCL_MALLOC((cvcl_size)fsize);
    if (!raw) { fclose(f); return CVCL_ERR_ALLOC; }
    if ((long)fread(raw, 1, (cvcl_size)fsize, f) != fsize) {
        CVCL_FREE(raw); fclose(f); return CVCL_ERR_IO;
    }
    fclose(f);

    /* Check PNG signature */
    static const cvcl_u8 sig[8] = {137,80,78,71,13,10,26,10};
    if (memcmp(raw, sig, 8) != 0) { CVCL_FREE(raw); return CVCL_ERR_FORMAT; }

    /* Parse chunks */
    cvcl_i32 width=0, height=0, bit_depth=0, color_type=0;
    cvcl_u8 *idat_buf = NULL;
    cvcl_size idat_len = 0, idat_cap = 0;
    cvcl_result_t result = CVCL_ERR_FORMAT;

    cvcl_size pos = 8;
    while (pos + 12 <= (cvcl_size)fsize) {
        cvcl_u32 chunk_len  = read_u32be(raw + pos);
        const cvcl_u8 *type = raw + pos + 4;
        const cvcl_u8 *data = raw + pos + 8;
        pos += 12 + chunk_len;
        if (pos > (cvcl_size)fsize) break;

        if (memcmp(type, "IHDR", 4) == 0 && chunk_len >= 13) {
            width      = (cvcl_i32)read_u32be(data);
            height     = (cvcl_i32)read_u32be(data+4);
            bit_depth  = data[8];
            color_type = data[9];
            int interlace = data[12];
            if (interlace != 0) { result = CVCL_ERR_UNSUPPORTED; goto done; }
            if (bit_depth != 8 && bit_depth != 16) { result = CVCL_ERR_UNSUPPORTED; goto done; }
            if (color_type != 0 && color_type != 2 &&
                color_type != 3 && color_type != 4 && color_type != 6) {
                result = CVCL_ERR_UNSUPPORTED; goto done;
            }
        } else if (memcmp(type, "IDAT", 4) == 0) {
            /* Concatenate all IDAT chunks */
            if (idat_len + chunk_len > idat_cap) {
                idat_cap = (idat_len + chunk_len) * 2 + 65536;
                cvcl_u8 *nb = (cvcl_u8 *)CVCL_REALLOC(idat_buf, idat_cap);
                if (!nb) { result = CVCL_ERR_ALLOC; goto done; }
                idat_buf = nb;
            }
            CVCL_MEMCPY(idat_buf + idat_len, data, chunk_len);
            idat_len += chunk_len;
        } else if (memcmp(type, "IEND", 4) == 0) {
            break;
        }
        /* Skip PLTE, tEXt, etc. */
    }

    if (!width || !height || !idat_len) { goto done; }

    /* Channels from color type (bpp = bytes per pixel at given bit depth) */
    int samples; /* samples per pixel */
    switch (color_type) {
        case 0: samples=1; break;  /* gray */
        case 2: samples=3; break;  /* RGB */
        case 3: samples=3; break;  /* palette */
        case 4: samples=2; break;  /* gray+alpha */
        case 6: samples=4; break;  /* RGBA */
        default: result = CVCL_ERR_UNSUPPORTED; goto done;
    }
    int bpp = samples * (bit_depth == 16 ? 2 : 1);

    /* Decompress IDAT */
    cvcl_size raw_size = (cvcl_size)(width * bpp + 1) * (cvcl_size)height;
    cvcl_u8 *filtered = (cvcl_u8 *)CVCL_MALLOC(raw_size);
    if (!filtered) { result = CVCL_ERR_ALLOC; goto done; }

    cvcl_size out_len = 0;
    if (inflate_zlib(idat_buf, idat_len, filtered, raw_size, &out_len) != 0) {
        CVCL_FREE(filtered); result = CVCL_ERR_FORMAT; goto done;
    }

    /* Output channels (always 8-bit output) */
    int out_ch = (color_type == 4) ? 1 :
                 (color_type == 0) ? 1 :
                 (color_type == 2) ? 3 :
                 (color_type == 3) ? 3 :
                 (color_type == 6) ? 4 : samples;

    /* Allocate output image */
    result = cvcl_image_create(img, width, height, out_ch, CVCL_DEPTH_U8, alloc);
    if (result != CVCL_OK) { CVCL_FREE(filtered); goto done; }

    /* Reconstruct filters and copy to image */
    cvcl_u8 *zero_row = (cvcl_u8 *)CVCL_CALLOC((cvcl_size)width * bpp, 1);
    if (!zero_row) { CVCL_FREE(filtered); cvcl_image_free(img, alloc); result=CVCL_ERR_ALLOC; goto done; }

    cvcl_size fpos = 0;
    for (cvcl_i32 y = 0; y < height; y++) {
        int filter_byte = filtered[fpos++];
        cvcl_u8 *row    = filtered + fpos;
        cvcl_u8 *prev   = (y == 0) ? zero_row
                         : filtered + (cvcl_size)(y-1) * (width*bpp+1) + 1;
        png_unfilter(row, prev, width, bpp, filter_byte);

        cvcl_u8 *dst_row = cvcl_image_row(img, y);
        if (bit_depth == 16) {
            /* 16-bit: take high byte of each sample */
            for (cvcl_i32 x=0; x<width*out_ch; x++)
                dst_row[x] = row[x*2];
        } else if (color_type == 4) {
            /* 8-bit gray+alpha: keep gray, discard alpha */
            for (cvcl_i32 x=0; x<width; x++) dst_row[x] = row[x*2];
        } else {
            CVCL_MEMCPY(dst_row, row, (cvcl_size)width * out_ch);
        }
        fpos += (cvcl_size)width * bpp;
    }

    CVCL_FREE(zero_row);
    CVCL_FREE(filtered);
    result = CVCL_OK;

done:
    CVCL_FREE(idat_buf);
    CVCL_FREE(raw);
    return result;
}

/* =========================================================================
 * Public: Write PNG (uncompressed)
 * ====================================================================== */

cvcl_result_t cvcl_io_write_png_native(const cvcl_image_t *img,
                                        const char         *path) {
    CVCL_CHECK_NULL(img); CVCL_CHECK_NULL(img->data); CVCL_CHECK_NULL(path);
    CVCL_CHECK_ARG(img->depth == CVCL_DEPTH_U8);
    CVCL_CHECK_ARG(img->channels==1||img->channels==3||img->channels==4);

    if (!crc32_ready) crc32_init_table();

    FILE *f = fopen(path, "wb");
    if (!f) return CVCL_ERR_IO;

    cvcl_i32 w=img->width, h=img->height, ch=img->channels;
    cvcl_i32 row_bytes = w * ch;

    static const cvcl_u8 sig[8]={137,80,78,71,13,10,26,10};
    fwrite(sig,1,8,f);

    cvcl_u8 ihdr[13];
    ihdr[0]=(cvcl_u8)(w>>24);ihdr[1]=(cvcl_u8)(w>>16);
    ihdr[2]=(cvcl_u8)(w>>8); ihdr[3]=(cvcl_u8)(w);
    ihdr[4]=(cvcl_u8)(h>>24);ihdr[5]=(cvcl_u8)(h>>16);
    ihdr[6]=(cvcl_u8)(h>>8); ihdr[7]=(cvcl_u8)(h);
    ihdr[8]=8;
    ihdr[9]=(ch==1)?0:(ch==3)?2:6;
    ihdr[10]=ihdr[11]=ihdr[12]=0;
    write_chunk(f,"IHDR",ihdr,13);

    cvcl_size filtered_row=(cvcl_size)row_bytes+1;
    cvcl_size raw_size=filtered_row*(cvcl_size)h;
    cvcl_size block_payload=65535;
    cvcl_size n_blocks=(raw_size+block_payload-1)/block_payload;
    cvcl_size idat_size=2+n_blocks*5+raw_size+4;

    cvcl_u8 *idat=(cvcl_u8*)CVCL_MALLOC(idat_size);
    if(!idat){fclose(f);return CVCL_ERR_ALLOC;}

    cvcl_size pos=0;
    idat[pos++]=0x78; idat[pos++]=0x01;

    cvcl_u32 adler=1;
    cvcl_size bytes_left=raw_size;
    cvcl_size row_idx=0; cvcl_i32 col=-1;

    while(bytes_left>0){
        cvcl_size blen=(bytes_left<block_payload)?bytes_left:block_payload;
        int last=(bytes_left==blen)?1:0;
        idat[pos++]=(cvcl_u8)last;
        idat[pos++]=(cvcl_u8)(blen&0xFF);
        idat[pos++]=(cvcl_u8)(blen>>8);
        idat[pos++]=(cvcl_u8)(~blen&0xFF);
        idat[pos++]=(cvcl_u8)(~blen>>8);
        for(cvcl_size bi=0;bi<blen;bi++){
            cvcl_u8 byte;
            if(col==-1){byte=0;col=0;}
            else{
                const cvcl_u8 *row=cvcl_image_row(img,(cvcl_i32)row_idx);
                byte=row[col++];
                if(col==row_bytes){col=-1;row_idx++;}
            }
            idat[pos++]=byte;
            adler=adler32_buf(adler,&byte,1);  /* filter bytes included */
        }
        bytes_left-=blen;
    }
    idat[pos++]=(cvcl_u8)(adler>>24);
    idat[pos++]=(cvcl_u8)(adler>>16);
    idat[pos++]=(cvcl_u8)(adler>>8);
    idat[pos++]=(cvcl_u8)(adler);

    write_chunk(f,"IDAT",idat,(cvcl_u32)pos);
    CVCL_FREE(idat);
    write_chunk(f,"IEND",NULL,0);
    fclose(f);
    return CVCL_OK;
}


#endif /* CVCL_NO_STDLIB */

/* Prevent ISO C empty translation unit warning */
typedef int cvcl_io_translation_unit_not_empty;
