@echo off
REM Run all CVCL tutorials in order
REM Usage: run_all.bat

set BINDIR=.\build\examples\tutorial

if not exist "%BINDIR%\01_hello_image.exe" (
    echo Tutorials not built. Run:
    echo   cmake -B build
    echo   cmake --build build --config Release
    exit /b 1
)

echo.
echo ============================================
echo  Tutorial 01: Hello Image
echo ============================================
"%BINDIR%\01_hello_image.exe"

echo.
echo ============================================
echo  Tutorial 02: Load and Inspect
echo ============================================
"%BINDIR%\02_load_and_inspect.exe" hello.ppm

echo.
echo ============================================
echo  Tutorial 03: Channel Conversion
echo ============================================
"%BINDIR%\03_channels.exe" hello.ppm

echo.
echo ============================================
echo  Tutorial 04: Blur
echo ============================================
"%BINDIR%\04_blur.exe" hello.ppm

echo.
echo ============================================
echo  Tutorial 05: Edge Detection
echo ============================================
"%BINDIR%\05_edges.exe" hello.ppm

echo.
echo ============================================
echo  Tutorial 06: Morphology
echo ============================================
"%BINDIR%\06_morphology.exe" hello.ppm

echo.
echo ============================================
echo  Tutorial 07: Threshold
echo ============================================
"%BINDIR%\07_threshold.exe" hello.ppm

echo.
echo ============================================
echo  Tutorial 08: Custom Allocator
echo ============================================
"%BINDIR%\08_custom_allocator.exe"

echo.
echo ============================================
echo  All tutorials completed.
echo ============================================
echo.
echo Output files saved in current directory.
