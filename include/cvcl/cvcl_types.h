/**
 * @file cvcl_types.h
 * @brief Fundamental types, platform detection, and portability macros
 */

#ifndef CVCL_TYPES_H
#define CVCL_TYPES_H

/* -------------------------------------------------------------------------
 * C standard includes (bypass-able for freestanding environments)
 * ---------------------------------------------------------------------- */
/* stdint.h and stddef.h are freestanding -- always available */
#include <stdint.h>
#include <stddef.h>

/* Hosted-only headers -- skip in freestanding mode */
#ifndef CVCL_NO_STDLIB
#  include <string.h>
#  include <stdlib.h>
#  include <assert.h>
#endif

/* -------------------------------------------------------------------------
 * Platform / compiler detection
 * ---------------------------------------------------------------------- */
#if defined(_MSC_VER)
#  define CVCL_COMPILER_MSVC   1
#elif defined(__clang__)
#  define CVCL_COMPILER_CLANG  1
#elif defined(__GNUC__)
#  define CVCL_COMPILER_GCC    1
#endif

#if defined(__x86_64__) || defined(_M_X64)
#  define CVCL_ARCH_X86_64     1
#elif defined(__i386__) || defined(_M_IX86)
#  define CVCL_ARCH_X86        1
#elif defined(__aarch64__) || defined(_M_ARM64)
#  define CVCL_ARCH_ARM64      1
#elif defined(__arm__) || defined(_M_ARM)
#  define CVCL_ARCH_ARM        1
#elif defined(__riscv)
#  define CVCL_ARCH_RISCV      1
#endif

/* -------------------------------------------------------------------------
 * Compiler hints
 * ---------------------------------------------------------------------- */
#if defined(CVCL_COMPILER_GCC) || defined(CVCL_COMPILER_CLANG)
#  define CVCL_LIKELY(x)      __builtin_expect(!!(x), 1)
#  define CVCL_UNLIKELY(x)    __builtin_expect(!!(x), 0)
#  define CVCL_INLINE         __attribute__((always_inline)) static inline
#  define CVCL_NOINLINE       __attribute__((noinline))
#  define CVCL_RESTRICT       __restrict__
#  define CVCL_ALIGNED(n)     __attribute__((aligned(n)))
#  define CVCL_PURE           __attribute__((pure))
#  define CVCL_MALLOC_ATTR    __attribute__((malloc))
#else
#  define CVCL_LIKELY(x)      (x)
#  define CVCL_UNLIKELY(x)    (x)
#  define CVCL_INLINE         static inline
#  define CVCL_NOINLINE
#  define CVCL_RESTRICT
#  define CVCL_ALIGNED(n)
#  define CVCL_PURE
#  define CVCL_MALLOC_ATTR
#endif

/* -------------------------------------------------------------------------
 * Assertions
 * ---------------------------------------------------------------------- */
#ifndef CVCL_ASSERT_DISABLE
#  ifndef CVCL_NO_STDLIB
#    define CVCL_ASSERT(expr) assert(expr)
#  else
#    define CVCL_ASSERT(expr) ((void)(expr))  /* no assert in freestanding */
#  endif
#else
#  define CVCL_ASSERT(expr) ((void)(expr))
#endif

/* -------------------------------------------------------------------------
 * Scalar types
 * ---------------------------------------------------------------------- */
typedef uint8_t   cvcl_u8;
typedef uint16_t  cvcl_u16;
typedef uint32_t  cvcl_u32;
typedef uint64_t  cvcl_u64;
typedef int8_t    cvcl_i8;
typedef int16_t   cvcl_i16;
typedef int32_t   cvcl_i32;
typedef int64_t   cvcl_i64;
typedef float     cvcl_f32;
typedef double    cvcl_f64;
typedef size_t    cvcl_size;

/* -------------------------------------------------------------------------
 * Pixel depth encoding
 * ---------------------------------------------------------------------- */
typedef enum {
    CVCL_DEPTH_U8  = 1,   /**< unsigned 8-bit  */
    CVCL_DEPTH_U16 = 2,   /**< unsigned 16-bit */
    CVCL_DEPTH_F32 = 4,   /**< 32-bit float    */
} cvcl_depth_t;

/* -------------------------------------------------------------------------
 * Channel layout
 * ---------------------------------------------------------------------- */
typedef enum {
    CVCL_LAYOUT_INTERLEAVED = 0,  /**< RGBRGBRGB... (default)   */
    CVCL_LAYOUT_PLANAR      = 1,  /**< RRR...GGG...BBB...        */
} cvcl_layout_t;

/* -------------------------------------------------------------------------
 * Interpolation modes
 * ---------------------------------------------------------------------- */
typedef enum {
    CVCL_INTERP_NEAREST  = 0,
    CVCL_INTERP_BILINEAR = 1,
    CVCL_INTERP_BICUBIC  = 2,
} cvcl_interp_t;

/* -------------------------------------------------------------------------
 * Border / padding modes
 * ---------------------------------------------------------------------- */
typedef enum {
    CVCL_BORDER_ZERO     = 0,  /**< pad with zeros               */
    CVCL_BORDER_REPLICATE = 1, /**< replicate edge pixels        */
    CVCL_BORDER_REFLECT  = 2,  /**< mirror without edge repeat   */
    CVCL_BORDER_WRAP     = 3,  /**< tile / wrap-around           */
} cvcl_border_t;

/* -------------------------------------------------------------------------
 * 2D integer and float point
 * ---------------------------------------------------------------------- */
typedef struct { cvcl_i32 x, y;        } cvcl_point2i_t;
typedef struct { cvcl_f32 x, y;        } cvcl_point2f_t;
typedef struct { cvcl_i32 x, y, w, h;  } cvcl_rect_t;
typedef struct { cvcl_f32 r, g, b, a;  } cvcl_color_t;

/* -------------------------------------------------------------------------
 * Utility macros
 * ---------------------------------------------------------------------- */
#define CVCL_MIN(a, b)      ((a) < (b) ? (a) : (b))
#define CVCL_MAX(a, b)      ((a) > (b) ? (a) : (b))
#define CVCL_CLAMP(v, lo, hi) CVCL_MIN(CVCL_MAX((v), (lo)), (hi))
#define CVCL_ABS(a)         ((a) < 0 ? -(a) : (a))
#define CVCL_ALIGN_UP(x, n) (((x) + (n) - 1) & ~((n) - 1))
#define CVCL_UNUSED(x)      ((void)(x))
#define CVCL_ARRAY_LEN(arr) (sizeof(arr) / sizeof((arr)[0]))

#endif /* CVCL_TYPES_H */
