cmake_minimum_required(VERSION 3.15)
project(planet_renderer C)

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

# Planet renderer library
add_library(planet_renderer
    src/math_utils.c
    src/quadtree.c
    src/cubic_quadtree.c
    src/chunk.c
    src/planet.c
    src/chunk_utils.c
    src/noise.c
)

target_include_directories(planet_renderer PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Link math library on Unix-like systems (not needed on Windows)
if (UNIX)
    target_link_libraries(planet_renderer PUBLIC raylib m)
else()
    target_link_libraries(planet_renderer PUBLIC raylib)
endif()

# Simple planet example
add_executable(simple_planet
    examples/simple_planet.c
)

# Link math library on Unix-like systems (not needed on Windows)
if (UNIX)
    target_link_libraries(simple_planet PRIVATE planet_renderer raylib m)
else()
    target_link_libraries(simple_planet PRIVATE planet_renderer raylib)
endif()

# Copy shader files to build directory for simple_planet
add_custom_command(TARGET simple_planet POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:simple_planet>/shaders
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/examples/shaders
        $<TARGET_FILE_DIR:simple_planet>/shaders
    COMMENT "Copying shaders to build directory"
)

# Flat plane LOD example
add_executable(flat_plane_lod
    examples/flat_plane_lod.c
)

# Link math library on Unix-like systems (not needed on Windows)
if (UNIX)
    target_link_libraries(flat_plane_lod PRIVATE planet_renderer raylib m)
else()
    target_link_libraries(flat_plane_lod PRIVATE planet_renderer raylib)
endif()

# Web platform settings (Emscripten)
if (EMSCRIPTEN)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -s USE_GLFW=3 -s ASSERTIONS=1 -s WASM=1 -s ASYNCIFY")
    set(CMAKE_EXECUTABLE_SUFFIX ".html")
endif()

# Installation
install(TARGETS planet_renderer simple_planet flat_plane_lod
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)

install(DIRECTORY include/ DESTINATION include/planet_renderer)
