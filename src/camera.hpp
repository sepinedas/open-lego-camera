#pragma once

#include <memory>
#include <string>

#include <opencv2/core.hpp>
#include <opencv2/videoio.hpp>

#include "config.hpp"

namespace olc {

// A single camera source (Pi camera module or USB webcam). Frames are always
// returned as BGR cv::Mat with the current digital zoom already applied, so
// the rest of the app never has to care which backend is behind it.
class Camera {
public:
    // Opens the requested source. On CameraKind::Auto it tries the Pi camera
    // (libcamera via GStreamer) first, then falls back to a V4L2 webcam.
    // Returns nullptr if nothing could be opened.
    static std::unique_ptr<Camera> open(const Config& cfg);

    // Grab the next frame (BGR), zoom applied. False if the stream ended.
    bool read(cv::Mat& frame);

    void zoomIn();
    void zoomOut();
    void setZoom(double z);       // absolute zoom, clamped to [1, maxZoom()]
    double zoom() const { return zoom_; }
    static double maxZoom();

    int width() const { return width_; }
    int height() const { return height_; }
    double fps() const { return fps_; }
    const std::string& description() const { return desc_; }

private:
    Camera() = default;
    void applyZoom(cv::Mat& frame) const;

    cv::VideoCapture cap_;
    int width_ = 0;
    int height_ = 0;
    double fps_ = 30.0;
    double zoom_ = 1.0;      // 1.0 .. kMaxZoom
    std::string desc_;
};

} // namespace olc
