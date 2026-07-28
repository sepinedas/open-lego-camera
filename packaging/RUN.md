# open-lego-camera — prebuilt Raspberry Pi (aarch64) bundle

A ready-to-run build of **open-lego-camera** for **64-bit Raspberry Pi OS
(Bookworm)** on a **Raspberry Pi Zero 2 W** (and newer 64-bit Pi boards). Built
natively on arm64 inside a Debian Bookworm container, so it matches the Pi's
glibc 2.36 and OpenCV 4.6.

## Contents

```
bin/open-lego-camera        the app (rpath finds ../lib/libmediapipe_tasks.so)
lib/libmediapipe_tasks.so   the MediaPipe Face Mesh runtime
share/face_landmarker.task  the facial-filter model
install.sh                  installs runtime libs + the app
```

## Install

```sh
tar xzf open-lego-camera-*-aarch64-rpi.tar.gz
cd open-lego-camera-*-aarch64-rpi
./install.sh
```

`install.sh` apt-installs the remaining runtime libraries (OpenCV, SDL2, ffmpeg,
and libcamera/GStreamer for the Pi camera), copies the bundle to
`/opt/open-lego-camera`, links `open-lego-camera` onto your `PATH`, and drops the
model at `/usr/share/mediapipe/face_landmarker.task` (where the app looks for it
automatically).

## Run

Run it on the Pi's **HDMI console**, not over SSH (it draws straight to the
display via DRM/KMS):

```sh
open-lego-camera
```

Useful options:

```sh
open-lego-camera --camera picam        # force the Pi camera (or: webcam)
open-lego-camera --rotate 90           # rotate the whole UI for a rotated panel
open-lego-camera --face-landmarker /path/to/face_landmarker.task
open-lego-camera --help
```

## Run in place (without installing)

The binary's rpath points at the sibling `lib/`, so you can run it straight from
the extracted folder once the apt runtime libraries are present:

```sh
sudo apt install -y libsdl2-2.0-0 libsdl2-gfx-1.0-0 libopencv-dev ffmpeg \
    gstreamer1.0-libcamera gstreamer1.0-plugins-good gstreamer1.0-plugins-base
./bin/open-lego-camera --face-landmarker ./share/face_landmarker.task
```

## Troubleshooting

- `error while loading shared libraries: libopencv_core.so.406` — the OpenCV
  runtime isn't installed; run `sudo apt install libopencv-dev && sudo ldconfig`.
- Black screen over SSH — that's expected; use the HDMI console, or see the main
  project README's "Debugging HDMI / no display" section.
- Filters do nothing — the model wasn't found; pass `--face-landmarker PATH`.
