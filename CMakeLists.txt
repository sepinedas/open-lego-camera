cmake_minimum_required(VERSION 3.16)
project(open_lego_camera_cpp LANGUAGES CXX)

# MediaPipe's exported headers are compiled as C++20 (its api3 framework uses
# consteval / class-type NTTPs), and the app links MediaPipe unconditionally, so
# the whole project builds as C++20.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Root of the installed MediaPipe payload (contains include/ and lib/), from the
# aarch64 artifacts produced by https://github.com/sepinedas/media-pipe-builder.
# Defaults to the newest /opt/mediapipe/<ver>; override with -DMEDIAPIPE_ROOT=.
set(MEDIAPIPE_ROOT "" CACHE PATH "Root of the MediaPipe aarch64 payload (include/, lib/)")

# --- Dependencies -----------------------------------------------------------
# SDL2 draws to HDMI via the KMSDRM driver on a headless Pi (no X11 needed).
find_package(SDL2 REQUIRED)
# OpenCV handles V4L2/GStreamer capture and JPEG I/O. The extra modules
# (calib3d/features2d/highgui/video) aren't used directly here, but
# libmediapipe_tasks.so links them, so requesting them puts the matching
# .so.406 libraries on the link line and avoids "not found (try -rpath-link)"
# warnings when linking the executable.
find_package(OpenCV REQUIRED COMPONENTS
  core imgproc imgcodecs videoio calib3d features2d highgui video)

# SDL2_gfx (icon primitives) has no CMake config on most distros -> pkg-config.
find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2_GFX REQUIRED SDL2_gfx)

# --- MediaPipe Face Mesh (required) -----------------------------------------
# The facial filters are driven by MediaPipe's 468-point Face Mesh, so the
# library is a hard build dependency. Resolve the payload root: an explicit
# -DMEDIAPIPE_ROOT wins, otherwise pick the newest versioned dir under
# /opt/mediapipe.
set(_mp_root "${MEDIAPIPE_ROOT}")
if(NOT _mp_root)
  file(GLOB _mp_candidates "/opt/mediapipe/*")
  list(SORT _mp_candidates)
  list(REVERSE _mp_candidates)
  foreach(_c ${_mp_candidates})
    if(IS_DIRECTORY "${_c}" AND EXISTS "${_c}/include" AND EXISTS "${_c}/lib")
      set(_mp_root "${_c}")
      break()
    endif()
  endforeach()
endif()
if(NOT _mp_root OR NOT EXISTS "${_mp_root}/include")
  message(FATAL_ERROR
    "MediaPipe payload not found. Install the .deb from media-pipe-builder, or "
    "pass -DMEDIAPIPE_ROOT=/path/to/opt/mediapipe/<ver>.")
endif()
find_library(MEDIAPIPE_TASKS_LIB
  NAMES mediapipe_tasks libmediapipe_tasks
  PATHS "${_mp_root}/lib" NO_DEFAULT_PATH)
if(NOT MEDIAPIPE_TASKS_LIB)
  message(FATAL_ERROR "libmediapipe_tasks not found under ${_mp_root}/lib")
endif()
message(STATUS "MediaPipe Face Mesh: ${_mp_root}")

add_executable(open-lego-camera
  src/main.cpp
  src/config.cpp
  src/camera.cpp
  src/gallery.cpp
  src/icons.cpp
  src/ui.cpp
  src/filters.cpp
  src/mp_landmarker.cpp
  src/app.cpp
)

target_include_directories(open-lego-camera PRIVATE
  ${SDL2_INCLUDE_DIRS}
  ${SDL2_GFX_INCLUDE_DIRS}
  ${OpenCV_INCLUDE_DIRS}
)
# SYSTEM so the compiler treats MediaPipe's (and its bundled deps') headers as
# third-party: it silences the -Wall/-Wextra warning flood from those headers.
target_include_directories(open-lego-camera SYSTEM PRIVATE "${_mp_root}/include")

target_link_libraries(open-lego-camera PRIVATE
  ${SDL2_LIBRARIES}
  ${SDL2_GFX_LIBRARIES}
  ${OpenCV_LIBS}
  "${MEDIAPIPE_TASKS_LIB}"
  pthread
)

# The library is built CPU-only (MEDIAPIPE_DISABLE_GPU=1); define the same macro
# downstream so headers like image.h don't pull in GPU code paths whose symbols
# aren't in libmediapipe_tasks.so.
target_compile_definitions(open-lego-camera PRIVATE MEDIAPIPE_DISABLE_GPU=1)

target_compile_options(open-lego-camera PRIVATE ${SDL2_GFX_CFLAGS_OTHER} -Wall -Wextra)

# mp_landmarker.cpp is the one translation unit that pulls in MediaPipe's heavy
# C++20 headers (protobuf, Abseil, the Tasks API). On a 512 MB Pi Zero 2 W that
# single compile can exhaust RAM and get the compiler OOM-killed
# ("cc1plus: Killed"). Compile just this file at -O1 and, on GCC, tell the
# garbage collector to reclaim memory aggressively, which markedly lowers peak
# memory at a small speed cost. (Still build with `-j1` on the Zero -- see the
# README -- since parallel heavy compiles multiply the memory pressure.)
set(_mp_tu_opts -O1)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  list(APPEND _mp_tu_opts --param ggc-min-expand=20 --param ggc-min-heapsize=32768)
endif()
set_source_files_properties(src/mp_landmarker.cpp PROPERTIES
  COMPILE_OPTIONS "${_mp_tu_opts}")

install(TARGETS open-lego-camera RUNTIME DESTINATION bin)
