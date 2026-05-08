/**
 * @file cvcl_io.h
 * @brief Image file I/O (PPM/PGM native; PNG/JPEG via stb_image optional)
 *
 * Native PPM/PGM support is always available (zero external dependencies).
 * PNG and JPEG support requires CVCL_WITH_STB to be defined at compile time
 * and stb_image.h / stb_image_write.h to be on the include path.
 */

#ifndef CVCL_IO_H
#define CVCL_IO_H

#include "cvcl_image.h"
#include "cvcl_error.h"

/* -------------------------------------------------------------------------
 * PPM / PGM (always available)
 * ---------------------------------------------------------------------- */

/**
 * @brief Write image as PPM (RGB) or PGM (grayscale).
 * Supports U8 depth only. RGB images write P6 binary PPM; gray writes P5.
 */
cvcl_result_t cvcl_io_write_ppm(const cvcl_image_t *img, const char *path);

/**
 * @brief Read a binary PPM (P6) or PGM (P5) file into img.
 * img is allocated with the provided allocator (or default if NULL).
 */
cvcl_result_t cvcl_io_read_ppm(cvcl_image_t          *img,
                                const char            *path,
                                const cvcl_allocator_t *alloc);

/* -------------------------------------------------------------------------
 * Native PNG writer -- zero dependencies, uncompressed DEFLATE
 * Works without stb_image. Output is larger but universally readable.
 * ---------------------------------------------------------------------- */

/**
 * @brief Read a PNG file (8-bit gray/RGB/RGBA, non-interlaced).
 * Zero external dependencies -- full DEFLATE decompressor built-in.
 */
cvcl_result_t cvcl_io_read_png_native(cvcl_image_t          *img,
                                       const char            *path,
                                       const cvcl_allocator_t *alloc);

/**
 * @brief Write image as PNG using uncompressed DEFLATE (no zlib required).
 * Supports U8 depth, 1/3/4 channels. Always available -- no CVCL_WITH_STB needed.
 */
cvcl_result_t cvcl_io_write_png_native(const cvcl_image_t *img, const char *path);

/* -------------------------------------------------------------------------
 * PNG / JPEG (requires CVCL_WITH_STB)
 * ---------------------------------------------------------------------- */
#ifdef CVCL_WITH_STB

/**
 * @brief Read any image format supported by stb_image.
 * @param channels_hint  Desired number of output channels (0 = auto).
 */
cvcl_result_t cvcl_io_read(cvcl_image_t          *img,
                            const char            *path,
                            cvcl_i32               channels_hint,
                            const cvcl_allocator_t *alloc);

/**
 * @brief Write image as PNG.
 */
cvcl_result_t cvcl_io_write_png(const cvcl_image_t *img, const char *path);

/**
 * @brief Write image as JPEG.
 * @param quality  1-100 (100 = best quality).
 */
cvcl_result_t cvcl_io_write_jpeg(const cvcl_image_t *img,
                                  const char         *path,
                                  int                 quality);

#endif /* CVCL_WITH_STB */

/* -------------------------------------------------------------------------
 * In-memory buffer I/O
 * ---------------------------------------------------------------------- */

/**
 * @brief Encode image to PPM in a caller-provided buffer.
 * @param[out] out_size  Bytes written to buf.
 * @param      buf       Output buffer.
 * @param      buf_size  Size of buf in bytes.
 */
cvcl_result_t cvcl_io_encode_ppm(const cvcl_image_t *img,
                                  cvcl_u8            *buf,
                                  cvcl_size           buf_size,
                                  cvcl_size          *out_size);

#endif /* CVCL_IO_H */
