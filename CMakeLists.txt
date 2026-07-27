cmake_minimum_required(VERSION 3.16)
project(open_lego_camera_cpp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# --- Optional MediaPipe Face Mesh backend -----------------------------------
# When ON, the facial filters use MediaPipe's 468-point Face Mesh (precise
# mouth/brow/eye tracking) instead of the Haar-cascade approximation. Point it
# at the aarch64 artifacts produced by https://github.com/sepinedas/media-pipe-builder
# (installed under /opt/mediapipe/<ver>, or a tarball you extracted). The mesh
# also needs a face_landmarker.task model at runtime (see --face-landmarker).
option(WITH_MEDIAPIPE "Build the MediaPipe Face Mesh filter backend" OFF)
# Root of the installed MediaPipe payload (contains include/ and lib/). Defaults
# to the newest /opt/mediapipe/<ver>; override with -DMEDIAPIPE_ROOT=/path.
set(MEDIAPIPE_ROOT "" CACHE PATH "Root of the MediaPipe aarch64 payload (include/, lib/)")

# --- Dependencies -----------------------------------------------------------
# SDL2 draws to HDMI via the KMSDRM driver on a headless Pi (no X11 needed).
find_package(SDL2 REQUIRED)
# OpenCV handles V4L2/GStreamer capture, JPEG/H.264 I/O and video playback.
# objdetect provides the Haar cascade classifier used by the facial filters.
find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs videoio objdetect)

# SDL2_gfx (icon primitives) has no CMake config on most distros -> pkg-config.
find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2_GFX REQUIRED SDL2_gfx)

add_executable(open-lego-camera
  src/main.cpp
  src/config.cpp
  src/camera.cpp
  src/recorder.cpp
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

target_link_libraries(open-lego-camera PRIVATE
  ${SDL2_LIBRARIES}
  ${SDL2_GFX_LIBRARIES}
  ${OpenCV_LIBS}
  pthread
)

target_compile_options(open-lego-camera PRIVATE ${SDL2_GFX_CFLAGS_OTHER} -Wall -Wextra)

# --- MediaPipe Face Mesh backend (optional) ---------------------------------
if(WITH_MEDIAPIPE)
  # Resolve the payload root: an explicit -DMEDIAPIPE_ROOT wins, otherwise pick
  # the newest versioned dir under /opt/mediapipe.
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
      "WITH_MEDIAPIPE=ON but no MediaPipe payload found. Install the .deb from "
      "media-pipe-builder, or pass -DMEDIAPIPE_ROOT=/path/to/opt/mediapipe/<ver>.")
  endif()
  find_library(MEDIAPIPE_TASKS_LIB
    NAMES mediapipe_tasks libmediapipe_tasks
    PATHS "${_mp_root}/lib" NO_DEFAULT_PATH)
  if(NOT MEDIAPIPE_TASKS_LIB)
    message(FATAL_ERROR "libmediapipe_tasks not found under ${_mp_root}/lib")
  endif()
  message(STATUS "MediaPipe Face Mesh backend: ${_mp_root}")
  # MediaPipe's exported headers are compiled as C++20 (its api3 framework uses
  # consteval / class-type NTTPs), so raise the standard for the whole target.
  set_target_properties(open-lego-camera PROPERTIES CXX_STANDARD 20)
  # The library is built CPU-only (MEDIAPIPE_DISABLE_GPU=1); define the same
  # macro downstream so headers like image.h don't pull in GPU code paths whose
  # symbols aren't in libmediapipe_tasks.so.
  target_compile_definitions(open-lego-camera PRIVATE
    OLC_WITH_MEDIAPIPE=1 MEDIAPIPE_DISABLE_GPU=1)
  target_include_directories(open-lego-camera PRIVATE "${_mp_root}/include")
  target_link_libraries(open-lego-camera PRIVATE "${MEDIAPIPE_TASKS_LIB}")
endif()

install(TARGETS open-lego-camera RUNTIME DESTINATION bin)
