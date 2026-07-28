#!/usr/bin/env bash
#
# Installs open-lego-camera and its runtime dependencies on a 64-bit Raspberry
# Pi OS (Bookworm, aarch64), e.g. a Raspberry Pi Zero 2 W.
#
# The bundle ships the binary, its MediaPipe library (bin/ + lib/) and the Face
# Mesh model; this script apt-installs the remaining shared libraries (OpenCV,
# SDL2, ffmpeg, libcamera/GStreamer for the Pi camera), copies the bundle into
# /opt/open-lego-camera, and links the binary onto PATH.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
PREFIX="/opt/open-lego-camera"

SUDO=""
[ "$(id -u)" -ne 0 ] && SUDO="sudo"

echo "==> Installing runtime libraries (OpenCV, SDL2, ffmpeg, libcamera)"
$SUDO apt-get update
$SUDO apt-get install -y --no-install-recommends \
    libsdl2-2.0-0 libsdl2-gfx-1.0-0 \
    libopencv-dev ffmpeg \
    gstreamer1.0-libcamera gstreamer1.0-plugins-good gstreamer1.0-plugins-base \
    libcamera-tools

echo "==> Installing open-lego-camera to ${PREFIX}"
$SUDO rm -rf "${PREFIX}"
$SUDO mkdir -p "${PREFIX}"
$SUDO cp -a "${HERE}/bin" "${HERE}/lib" "${HERE}/share" "${PREFIX}/"
$SUDO ln -sf "${PREFIX}/bin/open-lego-camera" /usr/local/bin/open-lego-camera

echo "==> Installing the Face Mesh model where the app looks for it"
$SUDO mkdir -p /usr/share/mediapipe
$SUDO cp "${PREFIX}/share/face_landmarker.task" /usr/share/mediapipe/face_landmarker.task

cat <<'EOF'

Installed. Run it on the Pi's HDMI console (not over SSH):

    open-lego-camera

The facial-filter model is found automatically. See the bundled README.md for
rotation, camera selection and troubleshooting options.
EOF
