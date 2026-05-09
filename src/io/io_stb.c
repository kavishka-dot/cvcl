/**
 * @file io_stb.c
 * @brief PNG / JPEG / BMP / TGA I/O via stb_image (single compilation unit)
 *
 * stb_image is a public domain / MIT single-header library by Sean Barrett.
 * We compile the implementation exactly once here by defining the
 * STB_IMAGE_IMPLEMENTATION and STB_IMAGE_WRITE_IMPLEMENTATION macros.
 *
 * All other translation units that need stb types just include the headers
 * WITHOUT these macros -- they get only the declarations.
 */

#ifndef CVCL_NO_STDLIB
/* File I/O is unavailable in freestanding mode (no filesystem) */

#ifdef CVCL_WITH_STB

/* Suppress warnings from the stb headers themselves */
#if defined(__GNUC__) || defined(__clang__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wunused-function"
#  pragma GCC diagnostic ignored "-Wcast-qual"
#  pragma GCC diagnostic ignored "-Wsign-conversion"
#  pragma GCC diagnostic ignored "-Wdouble-promotion"
#endif

#define STB_IMAGE_IMPLEMENTATION
#define STBI_FAILURE_USERMSG
#include <stb_image.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#if defined(__GNUC__) || defined(__clang__)
#  pragma GCC diagnostic pop
#endif

/* -------------------------------------------------------------------------
 * CVCL wrapper functions
 * ---------------------------------------------------------------------- */

#include <cvcl/cvcl_platform.h>
#include <cvcl/cvcl_io.h>

cvcl_result_t cvcl_io_read(cvcl_image_t          *img,
                            const char            *path,
                            cvcl_i32               channels_hint,
                            const cvcl_allocator_t *alloc) {
    CVCL_CHECK_NULL(img);
    CVCL_CHECK_NULL(path);
    CVCL_CHECK_ARG(channels_hint >= 0 && channels_hint <= 4);

    int w, h, ch_file;
    int req = (channels_hint > 0) ? channels_hint : 0;

    unsigned char *pixels = stbi_load(path, &w, &h, &ch_file, req);
    if (!pixels) return CVCL_ERR_IO;

    int ch_out = (req > 0) ? req : ch_file;

    /* Allocate CVCL image */
    cvcl_result_t rc = cvcl_image_create(img, (cvcl_i32)w, (cvcl_i32)h,
                                          (cvcl_i32)ch_out, CVCL_DEPTH_U8, alloc);
    if (rc != CVCL_OK) {
        stbi_image_free(pixels);
        return rc;
    }

    /* Copy row by row (stb has no stride padding, CVCL may have) */
    cvcl_i32 row_bytes = w * ch_out;
    for (cvcl_i32 y = 0; y < h; y++) {
        CVCL_MEMCPY(cvcl_image_row(img, y),
               pixels + (cvcl_size)y * (cvcl_size)row_bytes,
               (cvcl_size)row_bytes);
    }

    stbi_image_free(pixels);
    return CVCL_OK;
}

cvcl_result_t cvcl_io_write_png(const cvcl_image_t *img, const char *path) {
    CVCL_CHECK_NULL(img);
    CVCL_CHECK_NULL(img->data);
    CVCL_CHECK_NULL(path);
    CVCL_CHECK_ARG(img->depth == CVCL_DEPTH_U8);

    /* stb needs contiguous rows -- if stride == width*ch we can pass directly,
     * otherwise we need to compact. Use stride as the stb stride parameter. */
    int ok = stbi_write_png(path,
                            img->width,
                            img->height,
                            img->channels,
                            img->data,
                            img->stride);
    return ok ? CVCL_OK : CVCL_ERR_IO;
}

cvcl_result_t cvcl_io_write_jpeg(const cvcl_image_t *img,
                                  const char         *path,
                                  int                 quality) {
    CVCL_CHECK_NULL(img);
    CVCL_CHECK_NULL(img->data);
    CVCL_CHECK_NULL(path);
    CVCL_CHECK_ARG(img->depth == CVCL_DEPTH_U8);
    CVCL_CHECK_ARG(quality >= 1 && quality <= 100);
    CVCL_CHECK_ARG(img->channels == 1 || img->channels == 3);

    /* JPEG writer doesn't accept a stride -- compact if needed */
    cvcl_i32 row_bytes = img->width * img->channels;
    if (img->stride == row_bytes) {
        int ok = stbi_write_jpg(path, img->width, img->height,
                                img->channels, img->data, quality);
        return ok ? CVCL_OK : CVCL_ERR_IO;
    }

    /* Need to compact rows */
    unsigned char *buf = (unsigned char *)CVCL_MALLOC(
        (cvcl_size)row_bytes * (cvcl_size)img->height);
    if (!buf) return CVCL_ERR_ALLOC;

    for (cvcl_i32 y = 0; y < img->height; y++) {
        CVCL_MEMCPY(buf + (cvcl_size)y * (cvcl_size)row_bytes,
               cvcl_image_row(img, y),
               (cvcl_size)row_bytes);
    }

    int ok = stbi_write_jpg(path, img->width, img->height,
                            img->channels, buf, quality);
    CVCL_FREE(buf);
    return ok ? CVCL_OK : CVCL_ERR_IO;
}

#endif /* CVCL_WITH_STB */

/* Suppress empty TU warning when CVCL_WITH_STB is not defined */
#ifndef CVCL_WITH_STB
typedef int cvcl_stb_unused;
#endif



#endif /* CVCL_NO_STDLIB */

/* Prevent ISO C empty translation unit warning */
typedef int cvcl_io_translation_unit_not_empty;
