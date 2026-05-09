/**
 * @file io_ppm.c
 * @brief Native PPM/PGM reader and writer (no external dependencies)
 */

#ifndef CVCL_NO_STDLIB
/* File I/O is unavailable in freestanding mode (no filesystem) */

#include <cvcl/cvcl_platform.h>
#include <cvcl/cvcl_io.h>
#include <stdio.h>
#include <ctype.h>

/* -------------------------------------------------------------------------
 * Internal helpers
 * ---------------------------------------------------------------------- */

static void skip_whitespace_and_comments(FILE *f) {
    int c;
    while ((c = fgetc(f)) != EOF) {
        if (c == '#') {
            /* Skip entire comment line */
            while ((c = fgetc(f)) != EOF && c != '\n') {}
        } else if (!isspace(c)) {
            ungetc(c, f);
            break;
        }
    }
}

static int read_int(FILE *f, int *out) {
    skip_whitespace_and_comments(f);
    return fscanf(f, "%d", out) == 1;
}

/* -------------------------------------------------------------------------
 * Write PPM / PGM
 * ---------------------------------------------------------------------- */

cvcl_result_t cvcl_io_write_ppm(const cvcl_image_t *img, const char *path) {
    CVCL_CHECK_NULL(img);
    CVCL_CHECK_NULL(img->data);
    CVCL_CHECK_NULL(path);
    CVCL_CHECK_ARG(img->depth == CVCL_DEPTH_U8);
    CVCL_CHECK_ARG(img->channels == 1 || img->channels == 3);

    FILE *f = fopen(path, "wb");
    if (!f) return CVCL_ERR_IO;

    /* Header */
    if (img->channels == 1) {
        fprintf(f, "P5\n%d %d\n255\n", img->width, img->height);
    } else {
        fprintf(f, "P6\n%d %d\n255\n", img->width, img->height);
    }

    /* Pixel data -- row by row to handle stride */
    cvcl_i32 row_bytes = img->width * img->channels;
    for (cvcl_i32 y = 0; y < img->height; y++) {
        if (fwrite(cvcl_image_row(img, y), 1, (size_t)row_bytes, f)
                != (size_t)row_bytes) {
            fclose(f);
            return CVCL_ERR_IO;
        }
    }
    fclose(f);
    return CVCL_OK;
}

/* -------------------------------------------------------------------------
 * Read PPM / PGM
 * ---------------------------------------------------------------------- */

cvcl_result_t cvcl_io_read_ppm(cvcl_image_t          *img,
                                const char            *path,
                                const cvcl_allocator_t *alloc) {
    CVCL_CHECK_NULL(img);
    CVCL_CHECK_NULL(path);

    FILE *f = fopen(path, "rb");
    if (!f) return CVCL_ERR_IO;

    /* Magic number */
    char magic[3] = {0};
    if (fread(magic, 1, 2, f) != 2) { fclose(f); return CVCL_ERR_FORMAT; }

    int channels;
    if      (magic[0]=='P'&&magic[1]=='5'&&magic[2]=='\0') channels = 1;
    else if (magic[0]=='P'&&magic[1]=='6'&&magic[2]=='\0') channels = 3;
    else { fclose(f); return CVCL_ERR_FORMAT; }

    int width, height, maxval;
    if (!read_int(f, &width)  || width  <= 0 ||
        !read_int(f, &height) || height <= 0 ||
        !read_int(f, &maxval) || maxval != 255) {
        fclose(f);
        return CVCL_ERR_FORMAT;
    }

    /* Single whitespace separator before binary data */
    fgetc(f);

    cvcl_result_t rc = cvcl_image_create(img, (cvcl_i32)width, (cvcl_i32)height,
                                           channels, CVCL_DEPTH_U8, alloc);
    if (rc != CVCL_OK) { fclose(f); return rc; }

    cvcl_i32 row_bytes = img->width * img->channels;
    for (cvcl_i32 y = 0; y < img->height; y++) {
        if ((cvcl_i32)fread(cvcl_image_row(img, y), 1,
                            (size_t)row_bytes, f) != row_bytes) {
            cvcl_image_free(img, alloc);
            fclose(f);
            return CVCL_ERR_IO;
        }
    }
    fclose(f);
    return CVCL_OK;
}

/* -------------------------------------------------------------------------
 * In-memory PPM encode
 * ---------------------------------------------------------------------- */

cvcl_result_t cvcl_io_encode_ppm(const cvcl_image_t *img,
                                  cvcl_u8            *buf,
                                  cvcl_size           buf_size,
                                  cvcl_size          *out_size) {
    CVCL_CHECK_NULL(img);
    CVCL_CHECK_NULL(img->data);
    CVCL_CHECK_NULL(buf);
    CVCL_CHECK_NULL(out_size);
    CVCL_CHECK_ARG(img->depth == CVCL_DEPTH_U8);
    CVCL_CHECK_ARG(img->channels == 1 || img->channels == 3);

    /* Estimate header size (conservative) */
    char header[64];
    int hlen;
    if (img->channels == 1) {
        hlen = snprintf(header, sizeof(header),
                        "P5\n%d %d\n255\n", img->width, img->height);
    } else {
        hlen = snprintf(header, sizeof(header),
                        "P6\n%d %d\n255\n", img->width, img->height);
    }
    if (hlen < 0) return CVCL_ERR_INTERNAL;

    cvcl_size data_bytes = (cvcl_size)img->width
                         * (cvcl_size)img->height
                         * (cvcl_size)img->channels;
    cvcl_size total = (cvcl_size)hlen + data_bytes;

    if (buf_size < total) return CVCL_ERR_OVERFLOW;

    CVCL_MEMCPY(buf, header, (cvcl_size)hlen);
    cvcl_size offset = (cvcl_size)hlen;
    cvcl_i32 row_bytes = img->width * img->channels;
    for (cvcl_i32 y = 0; y < img->height; y++) {
        CVCL_MEMCPY(buf + offset, cvcl_image_row(img, y), (cvcl_size)row_bytes);
        offset += (cvcl_size)row_bytes;
    }
    *out_size = total;
    return CVCL_OK;
}


#endif /* CVCL_NO_STDLIB */

/* Prevent ISO C empty translation unit warning */
typedef int cvcl_io_translation_unit_not_empty;
