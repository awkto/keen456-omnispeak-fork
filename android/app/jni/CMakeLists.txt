# SPDX-License-Identifier: GPL-2.0-or-later
# Android native build (#102): SDL2 from source (as SDL's android-project
# template does) plus the engine as libmain.so, built by the repo's own
# CMakeLists.txt with ANDROID set.
cmake_minimum_required(VERSION 3.10)
project(keen456omni C)

if (NOT SDL2_SOURCE_DIR OR NOT EXISTS "${SDL2_SOURCE_DIR}/CMakeLists.txt")
	message(FATAL_ERROR "SDL2_SOURCE_DIR not set or not an SDL2 tree (tools/build-android.sh fetches it)")
endif ()

# libSDL2.so: what SDLActivity loads first (getLibraries).
set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TEST OFF CACHE BOOL "" FORCE)
add_subdirectory(${SDL2_SOURCE_DIR} SDL2)

# Hand the engine's CMakeLists the SDL target instead of a find_package result.
set(SDL2_LIBRARIES SDL2::SDL2)
set(SDL2_INCLUDE_DIRS ${SDL2_SOURCE_DIR}/include)
set(RENDERER "sdl2" CACHE STRING "" FORCE)
set(OMNISPEAK_ANDROID_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/keen_android.c)
get_filename_component(OMNI_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
add_subdirectory(${OMNI_ROOT} omnispeak)
target_include_directories(omnispeak PRIVATE ${OMNI_ROOT}/src)
target_link_libraries(omnispeak log android)
