/**
 * @file cvcl_draw.h
 * @brief Rasterization primitives: lines, rectangles, circles, text
 */

#ifndef CVCL_DRAW_H
#define CVCL_DRAW_H

#include "cvcl_image.h"
#include "cvcl_error.h"

/* -------------------------------------------------------------------------
 * Draw API -- all functions modify img in-place
 * ---------------------------------------------------------------------- */

/**
 * @brief Draw an anti-aliased line using Xiaolin Wu's algorithm.
 */
cvcl_result_t cvcl_draw_line(cvcl_image_t     *img,
                              cvcl_point2i_t    p0,
                              cvcl_point2i_t    p1,
                              cvcl_color_t      color,
                              cvcl_i32          thickness);

/**
 * @brief Draw a filled or stroked rectangle.
 * @param filled  Non-zero to fill, zero to draw outline only.
 */
cvcl_result_t cvcl_draw_rect(cvcl_image_t  *img,
                              cvcl_rect_t    rect,
                              cvcl_color_t   color,
                              cvcl_i32       thickness,
                              int            filled);

/**
 * @brief Draw a filled or stroked circle.
 */
cvcl_result_t cvcl_draw_circle(cvcl_image_t  *img,
                                cvcl_point2i_t center,
                                cvcl_i32       radius,
                                cvcl_color_t   color,
                                cvcl_i32       thickness,
                                int            filled);

/**
 * @brief Draw a polyline (sequence of connected line segments).
 * @param pts     Array of points.
 * @param n_pts   Number of points.
 * @param closed  If non-zero, connect last point back to first.
 */
cvcl_result_t cvcl_draw_polyline(cvcl_image_t         *img,
                                  const cvcl_point2i_t *pts,
                                  cvcl_i32              n_pts,
                                  cvcl_color_t          color,
                                  cvcl_i32              thickness,
                                  int                   closed);

/**
 * @brief Blit (copy) a patch src_patch onto img at position (x, y).
 * Alpha-compositing applied if src_patch has 4 channels.
 */
cvcl_result_t cvcl_draw_image(cvcl_image_t       *img,
                               const cvcl_image_t *src_patch,
                               cvcl_i32            x,
                               cvcl_i32            y);

/* -------------------------------------------------------------------------
 * Bitmap font text rendering (built-in 8x8 ASCII font, zero dependencies)
 * ---------------------------------------------------------------------- */

/**
 * @brief Render ASCII text onto img using the built-in 8x8 bitmap font.
 *
 * @param img    Target image.
 * @param text   Null-terminated ASCII string.
 * @param x, y  Top-left origin of the first glyph.
 * @param color Foreground color.
 * @param scale Integer scale factor (1 = 8x8px, 2 = 16x16px, ...).
 */
cvcl_result_t cvcl_draw_text(cvcl_image_t  *img,
                              const char    *text,
                              cvcl_i32       x,
                              cvcl_i32       y,
                              cvcl_color_t   color,
                              cvcl_i32       scale);

#endif /* CVCL_DRAW_H */
