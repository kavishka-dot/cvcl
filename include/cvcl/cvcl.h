/**
 * @file cvcl.h
 * @brief CVCL - Computer Vision C Library
 *
 * High-performance, zero-dependency computer vision primitives in pure C99.
 * Designed for edge deployment, FFI consumption, and deterministic memory control.
 *
 * @version 0.1.0
 * @copyright Apache License 2.0
 *
 * Usage:
 *   #include <cvcl/cvcl.h>
 *
 * Compile-time feature flags:
 *   CVCL_NO_SIMD        - Disable all SIMD acceleration
 *   CVCL_NO_STDLIB      - Disable stdlib (bring your own allocator)
 *   CVCL_ASSERT_DISABLE - Disable internal assertions
 */

#ifndef CVCL_H
#define CVCL_H

#ifdef __cplusplus
extern "C" {
#endif

#include "cvcl_version.h"
#include "cvcl_types.h"
#include "cvcl_image.h"
#include "cvcl_alloc.h"
#include "cvcl_error.h"
#include "cvcl_io.h"
#include "cvcl_transform.h"
#include "cvcl_filter.h"
#include "cvcl_draw.h"
#include "cvcl_pixel.h"
#include "cvcl_integral.h"
#include "cvcl_cc.h"

#ifdef __cplusplus
}
#endif

#endif /* CVCL_H */
