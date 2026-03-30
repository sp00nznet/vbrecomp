cmake_minimum_required(VERSION 3.16)
project(vbrecomp C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# --- Library ---

add_library(vbrecomp STATIC
    src/vbrecomp.c
    src/cpu.c
    src/mem.c
    src/vip.c
    src/vsu.c
    src/timer.c
    src/gamepad.c
    src/interrupt.c
    src/platform_sdl2.c
)

target_include_directories(vbrecomp PUBLIC include)

# SDL2 is optional - define VBRECOMP_NO_SDL to build without it
find_package(SDL2 QUIET)
if(SDL2_FOUND)
    target_link_libraries(vbrecomp PUBLIC SDL2::SDL2)
    message(STATUS "SDL2 found: platform backend enabled")
else()
    target_compile_definitions(vbrecomp PRIVATE VBRECOMP_NO_SDL)
    message(STATUS "SDL2 not found: platform backend disabled (stub)")
endif()

# --- Tools ---

add_executable(v810recomp
    tools/v810recomp/main.c
    tools/v810recomp/decode.c
    tools/v810recomp/analyze.c
    tools/v810recomp/emit.c
)
target_include_directories(v810recomp PRIVATE tools/v810recomp)

# --- Tests ---

option(VBRECOMP_BUILD_TESTS "Build tests" ON)
if(VBRECOMP_BUILD_TESTS)
    add_executable(test_mem tests/test_mem.c)
    target_link_libraries(test_mem PRIVATE vbrecomp)
    target_compile_definitions(test_mem PRIVATE VBRECOMP_NO_SDL)
endif()
