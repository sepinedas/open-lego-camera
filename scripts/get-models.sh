#!/usr/bin/env bash
# Fetch the models the dog filter needs.
#   default : OpenCV LBF landmark model (~54 MB) -- required for the 68-point mesh
#   --yunet : also fetch the YuNet DNN face detector (~230 KB). YuNet is preferred
#             when it loads, but the 2023mar model needs OpenCV >= 4.8; on older
#             OpenCV (e.g. Raspberry Pi OS Bookworm's 4.6) the app falls back to
#             the Haar detector automatically.
# The Haar cascade itself ships with libopencv-dev.
set -euo pipefail

dir="$(cd "$(dirname "$0")/.." && pwd)/models"
mkdir -p "$dir"

fetch() {  # url  outfile
  local url="$1" out="$2"
  if [ -f "$out" ]; then echo "already present: $out"; return; fi
  echo "downloading -> $out"
  if command -v curl >/dev/null 2>&1; then curl -L --fail -o "$out" "$url"
  elif command -v wget >/dev/null 2>&1; then wget -O "$out" "$url"
  else echo "need curl or wget" >&2; exit 1; fi
}

fetch "https://raw.githubusercontent.com/kurnianggoro/GSOC2017/master/data/lbfmodel.yaml" \
      "$dir/lbfmodel.yaml"

if [ "${1:-}" = "--yunet" ]; then
  fetch "https://media.githubusercontent.com/media/opencv/opencv_zoo/main/models/face_detection_yunet/face_detection_yunet_2023mar.onnx" \
        "$dir/face_detection_yunet_2023mar.onnx"
fi

echo
echo "done. Enable the filter with:  build/open-lego-camera --filter"
