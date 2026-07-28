#!/usr/bin/env bash
#
# Builds open-lego-camera for aarch64 (64-bit Raspberry Pi OS) and assembles a
# self-contained, redistributable bundle.
#
# Runs inside a debian:bookworm arm64 container (invoked by the GitHub Actions
# workflow on an ubuntu-24.04-arm runner) so the binary links against glibc 2.36
# and OpenCV 4.6 -- matching a Raspberry Pi Zero 2 W (Bookworm) exactly.
#
# Inputs (environment):
#   OLC_VERSION   version string baked into the package name (default: dev)
#   HOST_UID/GID  ownership to restore on the output tree (default: 0:0)
#
# Expects the MediaPipe runtime .deb from
#   https://github.com/sepinedas/media-pipe-builder
# to be mounted at /work/mp/*.deb (the workflow fetches it from that repo's
# release). The .deb provides libmediapipe_tasks.so + headers and pulls OpenCV
# and ffmpeg in via its Depends.
#
# Output:
#   ${GITHUB_WORKSPACE}/dist/<pkg>/           staging tree
#   ${GITHUB_WORKSPACE}/<pkg>.tar.gz(.sha256) the downloadable bundle
set -euxo pipefail

export DEBIAN_FRONTEND=noninteractive
VER="${OLC_VERSION:-dev}"
HOST_UID="${HOST_UID:-0}"
HOST_GID="${HOST_GID:-0}"
WORK="${GITHUB_WORKSPACE:-/work}"

echo "==> Installing build dependencies"
apt-get update
apt-get install -y --no-install-recommends \
    build-essential cmake pkg-config git curl ca-certificates patchelf \
    libsdl2-dev libsdl2-gfx-dev
update-ca-certificates || true

echo "==> Installing the MediaPipe runtime (.deb pulls OpenCV + ffmpeg)"
apt-get install -y "${WORK}"/mp/*.deb

# Newest installed MediaPipe payload (for bundling libmediapipe_tasks.so).
MP_ROOT="$(ls -d /opt/mediapipe/*/ 2>/dev/null | sort -V | tail -n1)"
MP_ROOT="${MP_ROOT%/}"
[ -n "${MP_ROOT}" ] || { echo "!! MediaPipe payload not found under /opt/mediapipe" >&2; exit 1; }
echo "    MediaPipe payload: ${MP_ROOT}"

cd "${WORK}"
git config --global --add safe.directory "${WORK}" || true

echo "==> Building open-lego-camera"
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"

echo "==> Fetching the Face Mesh model"
curl -fsSL -o build/face_landmarker.task \
    https://storage.googleapis.com/mediapipe-models/face_landmarker/face_landmarker/float16/1/face_landmarker.task

echo "==> Assembling the redistributable bundle"
PKG="open-lego-camera-${VER}-aarch64-rpi"
DEST="dist/${PKG}"
rm -rf dist
mkdir -p "${DEST}/bin" "${DEST}/lib" "${DEST}/share"
cp build/open-lego-camera        "${DEST}/bin/"
cp -L "${MP_ROOT}/lib/libmediapipe_tasks.so" "${DEST}/lib/"
cp build/face_landmarker.task    "${DEST}/share/"
cp packaging/install.sh          "${DEST}/install.sh"
cp packaging/RUN.md              "${DEST}/README.md"
chmod +x "${DEST}/install.sh"

# Find the bundled MediaPipe library next to the binary (bin/ and lib/ are
# siblings), so the bundle runs in place or from wherever install.sh copies it.
patchelf --set-rpath '$ORIGIN/../lib' "${DEST}/bin/open-lego-camera"

tar -C dist -czf "${PKG}.tar.gz" "${PKG}"
sha256sum "${PKG}.tar.gz" > "${PKG}.tar.gz.sha256"

echo "==> Restoring ownership"
chown -R "${HOST_UID}:${HOST_GID}" dist "${PKG}.tar.gz" "${PKG}.tar.gz.sha256" || true

echo "==> Done:"
ls -lh "${PKG}.tar.gz"
du -sh "${DEST}"
