# CVCL — Computer Vision C Library

[![CI](https://github.com/kavishka-dot/cvcl/actions/workflows/ci.yml/badge.svg)](https://github.com/kavishka-dot/cvcl/actions/workflows/ci.yml)
[![License](https://img.shields.io/badge/license-Apache%202.0-blue.svg)](LICENSE)
[![C Standard](https://img.shields.io/badge/C-C99-green.svg)]()
[![Platform](https://img.shields.io/badge/platform-x86%20%7C%20ARM%20%7C%20RISC--V-lightgrey.svg)]()

High-performance computer vision primitives in pure C99. Designed for edge deployment, deterministic memory control, and clean FFI consumption from Rust, Python, Go, and Zig.

---

## Design Philosophy

**Zero hidden costs.** Every allocation goes through a caller-provided allocator. Every function returns an explicit error code. No global state, no exceptions, no RTTI.

**Composable, not monolithic.** CVCL provides a kernel library of CV primitives, not a framework. It sits below OpenCV, not beside it.

**SIMD-ready layout.** Row stride is always 64-byte aligned. This is not aesthetic — it means your AVX-512 / NEON / SVE inner loops never straddle a cache line boundary.

---

## Quick Start

```c
#include <cvcl/cvcl.h>

int main(void) {
    // Load an image
    cvcl_image_t img;
    cvcl_io_read_ppm(&img, "photo.ppm", NULL);

    // Gaussian blur
    cvcl_image_t blurred;
    cvcl_image_create(&blurred, img.width, img.height,
                      img.channels, CVCL_DEPTH_U8, NULL);
    cvcl_blur_gaussian(&blurred, &img, 9, 2.0f, CVCL_BORDER_REPLICATE);

    // Save
    cvcl_io_write_ppm(&blurred, "out.ppm");

    // Explicit cleanup
    cvcl_image_free(&img,     NULL);
    cvcl_image_free(&blurred, NULL);
    return 0;
}
```

---

## Build

### Requirements
- CMake >= 3.16
- C99 compiler (GCC, Clang, MSVC, IAR, ARMCC)
- math library (`-lm`)

### Standard build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### With tests and sanitizers (development)

```bash
cmake -B build \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCVCL_BUILD_TESTS=ON \
  -DCVCL_SANITIZE=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

### Cross-compile for ARM Cortex-A (bare-metal example)

```bash
cmake -B build-arm \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-arm-none-eabi.cmake \
  -DCVCL_NO_SIMD=OFF \
  -DCVCL_BUILD_TESTS=OFF
```

---

## Core API

### Image Descriptor

```c
typedef struct {
    uint8_t      *data;      // Raw pixel buffer (64-byte aligned rows)
    int32_t       width;
    int32_t       height;
    int32_t       channels;  // 1=gray, 3=RGB, 4=RGBA
    int32_t       stride;    // Bytes per row (>= width*channels*depth)
    cvcl_depth_t  depth;     // U8, U16, or F32
    cvcl_layout_t layout;    // Interleaved or planar
} cvcl_image_t;
```

The `stride` field decouples logical width from memory layout. This enables:
- Zero-copy crops and ROI views
- Camera / DMA buffer wrapping
- SIMD alignment without padding the public dimensions

### Custom Allocator

```c
// Drop-in arena allocator example
static uint8_t arena[1 << 20];
static size_t  arena_pos = 0;

void *arena_alloc(size_t n, void *ctx) {
    (void)ctx;
    void *p = &arena[arena_pos];
    arena_pos = CVCL_ALIGN_UP(arena_pos + n, 16);
    return p;
}
void arena_free(void *p, void *ctx) { (void)p; (void)ctx; }

cvcl_allocator_t my_alloc = { arena_alloc, arena_free, NULL };

// Use it everywhere
cvcl_image_create(&img, 640, 480, 3, CVCL_DEPTH_U8, &my_alloc);
cvcl_image_free(&img, &my_alloc);
```

### Zero-Copy Crop

```c
cvcl_rect_t roi = {100, 80, 320, 240};
cvcl_image_t view;
cvcl_crop(&view, &src, roi);
// view.data points into src.data — no allocation, no copy
// DO NOT call cvcl_image_free on view
```

---

## Feature Matrix

| Module       | Function                  | Status |
|-------------|---------------------------|--------|
| **Core**     | Image create/free/clone    | Done   |
|              | Zero-copy ROI view         | Done   |
|              | Custom allocator interface | Done   |
| **I/O**      | PPM/PGM read/write         | Done   |
|              | In-memory PPM encode       | Done   |
|              | PNG/JPEG (via stb_image)   | Done    |
| **Transform**| Nearest-neighbor resize    | Done   |
|              | Bilinear resize (U8)       | Done   |
|              | Bicubic resize             | Done    |
|              | Flip H/V                   | Done   |
|              | Affine transform           | Done    |
|              | Rotate 90/180/270          | Done    |
|              | RGB/Gray channel convert   | Done   |
| **Filter**   | Generic 2D convolution     | Done   |
|              | Separable convolution      | Done   |
|              | Box blur                   | Done   |
|              | Gaussian blur              | Done   |
|              | Median filter              | Done    |
|              | Sobel X/Y                  | Done    |
|              | Canny edges                | Done    |
|              | Erode / Dilate / Open / Close | Done    |
| **Pixel**    | U8 / U16 / F32 accessors   | Done    |
|              | Otsu threshold             | Done    |
|              | Histogram equalization     | Done    |
| **Draw**     | Lines, rects, circles      | Done    |
|              | Bitmap text                | Done    |
| **SIMD**     | AVX2 fast paths            | Done    |
| **v2 Core**  | Integral image (SAT)       | Done    |
|              | Connected components       | Done    |
|              | O(1) box blur via SAT      | Done    |
|              | NEON fast paths            | Done    |

---

## Error Handling

All functions return `cvcl_result_t`. Errors propagate explicitly:

```c
cvcl_result_t rc = cvcl_blur_gaussian(&dst, &src, 9, 2.0f, CVCL_BORDER_REPLICATE);
if (rc != CVCL_OK) {
    fprintf(stderr, "Error: %s\n", cvcl_strerror(rc));
}

// Or use the propagation macro inside library code:
CVCL_CHECK(cvcl_image_create(&tmp, w, h, ch, depth, alloc));
```

---

## Compile-Time Flags

| Flag                  | Effect                                      |
|-----------------------|---------------------------------------------|
| `CVCL_NO_SIMD`        | Disable all SIMD intrinsics                 |
| `CVCL_NO_STDLIB`      | Freestanding: disable stdlib includes       |
| `CVCL_WITH_STB`       | Enable PNG/JPEG I/O via stb_image           |
| `CVCL_ASSERT_DISABLE` | Strip all internal assertions               |

---

## FFI Usage

CVCL is designed to be a clean FFI target. The C API is stable and has no C++-only constructs.

**Rust (via bindgen):**
```rust
// cvcl-sys generated bindings
let mut img = cvcl_image_t { ..Default::default() };
unsafe { cvcl_image_create(&mut img, 640, 480, 3, CVCL_DEPTH_U8, std::ptr::null()); }
```

**Python (via ctypes):**
```python
lib = ctypes.CDLL("libcvcl.a")
# ... bind cvcl_image_create, cvcl_io_read_ppm, etc.
```

---

## Project Structure

```
cvcl/
├── include/cvcl/        # Public headers (install target)
│   ├── cvcl.h           # Umbrella include
│   ├── cvcl_types.h     # Fundamental types and platform macros
│   ├── cvcl_image.h     # Image descriptor and lifecycle
│   ├── cvcl_alloc.h     # Pluggable allocator interface
│   ├── cvcl_error.h     # Error codes and CHECK macros
│   ├── cvcl_io.h        # File I/O
│   ├── cvcl_transform.h # Resize, flip, crop, affine
│   ├── cvcl_filter.h    # Convolution, blur, morphology
│   ├── cvcl_draw.h      # Drawing primitives
│   ├── cvcl_pixel.h     # Pixel accessors and histogram
│   └── cvcl_version.h   # Version constants
├── src/
│   ├── core/            # alloc.c, error.c, image.c
│   ├── io/              # io_ppm.c
│   ├── transform/       # resize.c, affine.c
│   └── filter/          # blur.c, convolve.c, morph.c
├── tests/               # Self-contained C unit tests
├── examples/            # Demo programs
├── bench/               # Microbenchmarks
├── cmake/               # Package config helpers
└── CMakeLists.txt
```

---


## Performance (1920x1080 RGB, Release build, x86-64)

| Operation | Throughput | Notes |
|-----------|-----------|-------|
| Gaussian blur k=5 | 47 Mpix/s | Fixed-point Q15 separable |
| Gaussian blur k=31 | 12 Mpix/s | Grows with k (separable) |
| Box blur k=5 | 169 Mpix/s | O(1) sliding window |
| Box blur k=63 | 158 Mpix/s | Same cost as k=5 |
| Pixel-wise add | 2924 Mpix/s | SSE2 saturating adds |
| Erode/Dilate 5x5 | 157 Mpix/s | Monotonic deque O(n) |

Key design wins:
- Box blur k=63 costs the same as k=5 -- sliding window is O(w·h) regardless of kernel size
- Gaussian uses fixed-point Q15 arithmetic -- no float multiply in the inner loop
- Morphology uses monotonic deque -- O(w·h) vs O(w·h·k) separable naive

---

## Memory Safety

CVCL includes constrained-memory tests using a fixed 64 KB arena allocator to verify graceful failure under limited memory.

The `test_resource_constraints` suite verifies:

- Every allocation failure returns `CVCL_ERR_ALLOC` -- never a crash or undefined behavior
- Zero-copy crop (`cvcl_image_view`) produces zero allocations
- Cleanup after a failed `cvcl_image_create` is always safe
- Mid-pipeline OOM leaves no dangling or partially-initialized state
- The arena boundary is exact -- peak usage never exceeds the declared budget

This makes CVCL suitable for deployment on resource-constrained targets (RTOS, bare-metal Cortex-M) where heap exhaustion must be handled gracefully.

---

## Contributing

1. Fork and branch from `main`
2. Run tests: `ctest --test-dir build --output-on-failure`
3. Ensure no new warnings with `-Wall -Wextra -Wpedantic -Werror`
4. Add a test for any new function
5. Open a PR with a clear description of what changed and why

---

## License

Apache License 2.0 — see [LICENSE](LICENSE).
