#!/bin/bash
# Cross-platform build script for planet-c (Mac/Linux)

set -e  # Exit on error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Default values
BUILD_TYPE="Release"
ENABLE_ASAN="OFF"
BUILD_DIR="build"
CLEAN=false

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --debug)
            BUILD_TYPE="Debug"
            shift
            ;;
        --release)
            BUILD_TYPE="Release"
            shift
            ;;
        --asan)
            ENABLE_ASAN="ON"
            shift
            ;;
        --clean)
            CLEAN=true
            shift
            ;;
        --help)
            echo "Usage: ./build.sh [options]"
            echo ""
            echo "Options:"
            echo "  --debug       Build in Debug mode (default: Release)"
            echo "  --release     Build in Release mode"
            echo "  --asan        Enable Address Sanitizer"
            echo "  --clean       Clean build directory before building"
            echo "  --help        Show this help message"
            exit 0
            ;;
        *)
            echo -e "${RED}Unknown option: $1${NC}"
            echo "Use --help for usage information"
            exit 1
            ;;
    esac
done

echo -e "${GREEN}=== Planet-C Build Script ===${NC}"
echo "Build type: $BUILD_TYPE"
echo "Address Sanitizer: $ENABLE_ASAN"
echo ""

# Check for required tools
echo -e "${YELLOW}Checking dependencies...${NC}"

if ! command -v cmake &> /dev/null; then
    echo -e "${RED}Error: CMake is not installed${NC}"
    echo "Please install CMake:"
    if [[ "$OSTYPE" == "darwin"* ]]; then
        echo "  brew install cmake"
    elif [[ -f /etc/arch-release ]]; then
        echo "  sudo pacman -S cmake"
    else
        echo "  sudo apt-get install cmake  (Debian/Ubuntu)"
    fi
    exit 1
fi

if ! command -v cc &> /dev/null && ! command -v gcc &> /dev/null && ! command -v clang &> /dev/null; then
    echo -e "${RED}Error: No C compiler found${NC}"
    echo "Please install a C compiler (gcc or clang)"
    exit 1
fi

echo -e "${GREEN}✓ All dependencies found${NC}"
echo ""

# Clean if requested
if [ "$CLEAN" = true ]; then
    echo -e "${YELLOW}Cleaning build directory...${NC}"
    rm -rf "$BUILD_DIR"
fi

# Create build directory
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Configure with CMake
echo -e "${YELLOW}Configuring project...${NC}"
cmake .. \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DENABLE_ASAN="$ENABLE_ASAN" \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

# Build
echo -e "${YELLOW}Building project...${NC}"
cmake --build . --config "$BUILD_TYPE" -j$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

echo ""
echo -e "${GREEN}=== Build Complete ===${NC}"
echo -e "Executable: ${GREEN}$BUILD_DIR/planet_example${NC}"
echo ""
echo "To run:"
echo "  ./$BUILD_DIR/planet_example"

./build/planet_example
