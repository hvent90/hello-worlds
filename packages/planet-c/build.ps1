# Minimal build script
if (!(Test-Path build)) {
    mkdir build
}
cd build
cmake ..
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
cmake --build .
cd ..