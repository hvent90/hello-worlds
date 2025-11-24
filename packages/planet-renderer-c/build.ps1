# PowerShell build script for planet-renderer-c (Windows)
# Usage: .\build.ps1 [Release|Debug]

param(
    [string]$BuildType = "Release"
)

$ErrorActionPreference = "Stop"

Write-Host "Planet Renderer Build Script (Windows)" -ForegroundColor Green
Write-Host "=======================================" -ForegroundColor Green
Write-Host ""

# Check for CMake
try {
    $cmakeVersion = cmake --version 2>&1 | Select-Object -First 1
    Write-Host "[✓] CMake found: $cmakeVersion" -ForegroundColor Green
} catch {
    Write-Host "[✗] Error: CMake is not installed or not in PATH" -ForegroundColor Red
    Write-Host "Please install CMake from https://cmake.org/download/" -ForegroundColor Yellow
    exit 1
}

# Check for Visual Studio (MSVC)
$vsWhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$hasMSVC = $false
if (Test-Path $vsWhere) {
    $vsPath = & $vsWhere -latest -property installationPath 2>$null
    if ($vsPath) {
        Write-Host "[✓] Visual Studio found: $vsPath" -ForegroundColor Green
        $hasMSVC = $true
    }
}

# Check for MinGW
$hasMinGW = $false
try {
    $mingwVersion = gcc --version 2>&1 | Select-Object -First 1
    if ($mingwVersion) {
        Write-Host "[✓] MinGW GCC found: $mingwVersion" -ForegroundColor Green
        $hasMinGW = $true
    }
} catch {
    # MinGW not found
}

if (-not $hasMSVC -and -not $hasMinGW) {
    Write-Host "[!] Warning: No compiler detected" -ForegroundColor Yellow
    Write-Host "Please install either:" -ForegroundColor Yellow
    Write-Host "  - Visual Studio with C++ development tools" -ForegroundColor Yellow
    Write-Host "  - MinGW-w64 from https://winlibs.com/" -ForegroundColor Yellow
}

# Configuration
$BuildDir = "build"

Write-Host ""
Write-Host "Build configuration:"
Write-Host "  Build directory: $BuildDir"
Write-Host "  Build type: $BuildType"
Write-Host ""

# Clean build directory if it exists
if (Test-Path $BuildDir) {
    Write-Host "Build directory exists. Cleaning..." -ForegroundColor Yellow
    Remove-Item -Path $BuildDir -Recurse -Force
}

# Create build directory
New-Item -ItemType Directory -Path $BuildDir | Out-Null
Set-Location $BuildDir

# Configure with CMake
Write-Host ""
Write-Host "Configuring with CMake..." -ForegroundColor Green

if ($hasMinGW -and -not $hasMSVC) {
    # Use MinGW if MSVC is not available
    Write-Host "Using MinGW Makefiles generator" -ForegroundColor Cyan
    cmake .. -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=$BuildType
} else {
    # Use default generator (usually Visual Studio on Windows)
    cmake .. -DCMAKE_BUILD_TYPE=$BuildType
}

if ($LASTEXITCODE -ne 0) {
    Write-Host "[✗] CMake configuration failed" -ForegroundColor Red
    Set-Location ..
    exit 1
}

# Build
Write-Host ""
Write-Host "Building project..." -ForegroundColor Green

# Get number of processors for parallel build
$numProcs = (Get-CimInstance Win32_ComputerSystem).NumberOfLogicalProcessors
cmake --build . --config $BuildType --parallel $numProcs

if ($LASTEXITCODE -ne 0) {
    Write-Host "[✗] Build failed" -ForegroundColor Red
    Set-Location ..
    exit 1
}

# Success message
Write-Host ""
Write-Host "========================================" -ForegroundColor Green
Write-Host "Build completed successfully!" -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Green
Write-Host ""

Write-Host "Executables built:" -ForegroundColor Cyan

# Check where executables are located
if (Test-Path "$BuildType\simple_planet.exe") {
    Write-Host "  - simple_planet.exe (in $BuildDir\$BuildType\)" -ForegroundColor White
    Write-Host "  - flat_plane_lod.exe (in $BuildDir\$BuildType\)" -ForegroundColor White
    Write-Host ""
    Write-Host "To run:" -ForegroundColor Yellow
    Write-Host "  cd $BuildDir\$BuildType" -ForegroundColor White
    Write-Host "  .\simple_planet.exe" -ForegroundColor White
} elseif (Test-Path "simple_planet.exe") {
    Write-Host "  - simple_planet.exe (in $BuildDir\)" -ForegroundColor White
    Write-Host "  - flat_plane_lod.exe (in $BuildDir\)" -ForegroundColor White
    Write-Host ""
    Write-Host "To run:" -ForegroundColor Yellow
    Write-Host "  cd $BuildDir" -ForegroundColor White
    Write-Host "  .\simple_planet.exe" -ForegroundColor White
}

Write-Host ""
Write-Host "Controls:" -ForegroundColor Cyan
Write-Host "  WASD + Mouse: Move camera"
Write-Host "  W: Toggle wireframe mode"
Write-Host "  I: Toggle info display"
Write-Host "  ESC: Exit"

Set-Location ..