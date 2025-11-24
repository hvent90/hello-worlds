#!/bin/bash
# Cross-platform build script for planet-renderer-c
# Works on macOS, Linux (including Arch), and Windows (via Git Bash/WSL)

set -e  # Exit on error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}Planet Renderer Build Script${NC}"
echo "=============================="

# Detect OS
OS="unknown"
if [[ "$OSTYPE" == "linux-gnu"* ]]; then
    OS="linux"
    echo "Detected OS: Linux"
elif [[ "$OSTYPE" == "darwin"* ]]; then
    OS="mac"
    echo "Detected OS: macOS"
elif [[ "$OSTYPE" == "msys" ]] || [[ "$OSTYPE" == "win32" ]] || [[ "$OSTYPE" == "cygwin" ]]; then
    OS="windows"
    echo "Detected OS: Windows (Git Bash/MSYS)"
fi

# Check for CMake
if ! command -v cmake &> /dev/null; then
    echo -e "${RED}Error: CMake is not installed${NC}"
    echo "Please install CMake from https://cmake.org/download/"
    exit 1
fi

echo -e "${GREEN}✓${NC} CMake found: $(cmake --version | head -n1)"

# Check for compiler
COMPILER_FOUND=false
if command -v gcc &> /dev/null; then
    echo -e "${GREEN}✓${NC} GCC found: $(gcc --version | head -n1)"
    COMPILER_FOUND=true
elif command -v clang &> /dev/null; then
    echo -e "${GREEN}✓${NC} Clang found: $(clang --version | head -n1)"
    COMPILER_FOUND=true
elif command -v cl &> /dev/null; then
    echo -e "${GREEN}✓${NC} MSVC found"
    COMPILER_FOUND=true
fi

if [ "$COMPILER_FOUND" = false ]; then
    echo -e "${YELLOW}Warning: No C compiler detected${NC}"
    echo "Please install a C compiler (GCC, Clang, or MSVC)"
fi

# Configuration
BUILD_DIR="build"
BUILD_TYPE="${1:-Release}"  # Default to Release, can be overridden with argument

echo ""
echo "Build configuration:"
echo "  Build directory: $BUILD_DIR"
echo "  Build type: $BUILD_TYPE"
echo ""

# Create build directory
if [ -d "$BUILD_DIR" ]; then
    echo -e "${YELLOW}Build directory exists. Cleaning...${NC}"
    rm -rf "$BUILD_DIR"
fi

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Configure with CMake
echo ""
echo -e "${GREEN}Configuring with CMake...${NC}"
if [[ "$OS" == "windows" ]]; then
    # On Windows with Git Bash, try MinGW first, then default generator
    if command -v mingw32-make &> /dev/null || command -v make &> /dev/null; then
        cmake .. -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
    else
        cmake .. -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
    fi
else
    cmake .. -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
fi

# Build
echo ""
echo -e "${GREEN}Building project...${NC}"
cmake --build . --config "$BUILD_TYPE" -j$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

echo ""
echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}Build completed successfully!${NC}"
echo -e "${GREEN}========================================${NC}"
echo ""
echo "Executables built:"

# Find and list executables
if [[ "$OS" == "windows" ]]; then
    if [ -f "$BUILD_TYPE/simple_planet.exe" ]; then
        echo "  - simple_planet.exe (in $BUILD_DIR/$BUILD_TYPE/)"
        echo "  - flat_plane_lod.exe (in $BUILD_DIR/$BUILD_TYPE/)"
        echo ""
        echo "To run:"
        echo "  cd $BUILD_DIR/$BUILD_TYPE && ./simple_planet.exe"
    elif [ -f "simple_planet.exe" ]; then
        echo "  - simple_planet.exe (in $BUILD_DIR/)"
        echo "  - flat_plane_lod.exe (in $BUILD_DIR/)"
        echo ""
        echo "To run:"
        echo "  cd $BUILD_DIR && ./simple_planet.exe"
    fi
else
    if [ -f "simple_planet" ]; then
        echo "  - simple_planet (in $BUILD_DIR/)"
        echo "  - flat_plane_lod (in $BUILD_DIR/)"
        echo ""
        echo "To run:"
        echo "  cd $BUILD_DIR && ./simple_planet"
    fi
fi

echo ""
echo "Controls:"
echo "  WASD + Mouse: Move camera"
echo "  W: Toggle wireframe mode"
echo "  I: Toggle info display"
echo "  ESC: Exit"