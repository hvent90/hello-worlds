cmake_minimum_required(VERSION 3.15)
project(planet_renderer C)

set(CMAKE_C_STANDARD 99)

# Find raylib
find_package(raylib QUIET)

if (NOT raylib_FOUND)
    # If raylib is not found, try to use FetchContent to download it
    include(FetchContent)
    FetchContent_Declare(
        raylib
        GIT_REPOSITORY https://github.com/raysan5/raylib.git
        GIT_TAG 5.0
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
)

target_include_directories(planet_renderer PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(planet_renderer PUBLIC raylib m)

# Simple planet example
add_executable(simple_planet
    examples/simple_planet.c
)

target_link_libraries(simple_planet PRIVATE planet_renderer raylib m)

# Web platform settings (Emscripten)
if (EMSCRIPTEN)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -s USE_GLFW=3 -s ASSERTIONS=1 -s WASM=1 -s ASYNCIFY")
    set(CMAKE_EXECUTABLE_SUFFIX ".html")
endif()

# Installation
install(TARGETS planet_renderer simple_planet
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)

install(DIRECTORY include/ DESTINATION include/planet_renderer)
