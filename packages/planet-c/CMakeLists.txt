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

# Find Threads package for pthread support
find_package(Threads REQUIRED)

# Find raylib
find_package(raylib CONFIG REQUIRED)



# Simple planet example
add_executable(simple_planet
   examples/simple_planet.c
)

# Link math library on Unix-like systems (not needed on Windows)
if (UNIX)
    target_link_libraries(simple_planet PRIVATE m Threads::Threads raylib)
else()
    target_link_libraries(simple_planet PRIVATE Threads::Threads raylib)
endif()



# Installation
install(TARGETS simple_planet
    RUNTIME DESTINATION bin
)
