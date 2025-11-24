cmake_minimum_required(VERSION 3.15)
project(planet_renderer_simple C)

set(CMAKE_C_STANDARD 99)

# Option to enable Address Sanitizer for debugging
option(ENABLE_ASAN "Enable Address Sanitizer for memory error detection" OFF)

# Address Sanitizer configuration
if(ENABLE_ASAN)
    message(STATUS "Address Sanitizer enabled")
    if(MSVC)
        # MSVC Address Sanitizer
        add_compile_options(/fsanitize=address)
        add_link_options(/fsanitize=address)
        # Disable incremental linking (incompatible with ASAN)
        add_link_options(/INCREMENTAL:NO)
    else()
        # GCC/Clang Address Sanitizer
        add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
        add_link_options(-fsanitize=address)
    endif()
endif()

# Find Threads package for pthread support
find_package(Threads REQUIRED)

# Find raylib
find_package(raylib QUIET)

if (NOT raylib_FOUND)
    # If raylib is not found, try to use FetchContent to download it
    include(FetchContent)
    FetchContent_Declare(
        raylib
        GIT_REPOSITORY https://github.com/raysan5/raylib.git
        GIT_TAG 5.5
    )
    FetchContent_MakeAvailable(raylib)
endif()



# Simple planet example
add_executable(planet_example
        examples/planet_example.c
        src/mesh.c
        src/noise.c
)

# Include directories
target_include_directories(planet_example PRIVATE src)

# Link math library on Unix-like systems (not needed on Windows)
if (UNIX)
    target_link_libraries(planet_example PRIVATE m Threads::Threads raylib)
else()
    target_link_libraries(planet_example PRIVATE Threads::Threads raylib)
endif()

# Copy shader files to build directory
add_custom_command(TARGET planet_example POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/src/shaders
        ${CMAKE_BINARY_DIR}/shaders)

# Installation
install(TARGETS planet_example
    RUNTIME DESTINATION bin
)
