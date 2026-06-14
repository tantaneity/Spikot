cmake_minimum_required(VERSION 3.20)
project(spikot C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

include(FetchContent)

set(RAYLIB_VERSION 5.5)
FetchContent_Declare(
    raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG ${RAYLIB_VERSION}
    GIT_SHALLOW TRUE
)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(raylib)

file(GLOB_RECURSE SPIKOT_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/*.c)

add_executable(spikot ${SPIKOT_SOURCES})
target_include_directories(spikot PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(spikot PRIVATE raylib)

if(WIN32)
    target_link_libraries(spikot PRIVATE winmm gdi32 opengl32)
endif()
