#!/usr/bin/env bash
# Run all CVCL tutorials in order
# Usage: ./run_all.sh

set -e

BINDIR="./build/examples/tutorial"

if [ ! -f "$BINDIR/01_hello_image" ]; then
    echo "Tutorials not built. Run:"
    echo "  cmake -B build -DCMAKE_BUILD_TYPE=Release"
    echo "  cmake --build build"
    exit 1
fi

run() {
    echo ""
    echo "============================================"
    echo " $1"
    echo "============================================"
    shift
    "$@"
}

run "Tutorial 01: Hello Image"          $BINDIR/01_hello_image
run "Tutorial 02: Load and Inspect"     $BINDIR/02_load_and_inspect hello.ppm
run "Tutorial 03: Channel Conversion"   $BINDIR/03_channels hello.ppm
run "Tutorial 04: Blur"                 $BINDIR/04_blur hello.ppm
run "Tutorial 05: Edge Detection"       $BINDIR/05_edges hello.ppm
run "Tutorial 06: Morphology"           $BINDIR/06_morphology hello.ppm
run "Tutorial 07: Threshold"            $BINDIR/07_threshold hello.ppm
run "Tutorial 08: Custom Allocator"     $BINDIR/08_custom_allocator

echo ""
echo "============================================"
echo " All tutorials completed successfully."
echo "============================================"
echo ""
echo "Output files in current directory:"
ls -lh *.ppm *.pgm 2>/dev/null || echo "  (none found)"
