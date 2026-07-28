#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <opencv2/core.hpp>

#include "landmarks.hpp"

namespace olc {

// Thin wrapper around MediaPipe's Tasks Vision Face Landmarker (the 468-point
// Face Mesh).
//
// The heavy MediaPipe headers are pulled in only by mp_landmarker.cpp; the rest
// of the app includes just this header. It links libmediapipe_tasks from the
// aarch64 artifacts produced by https://github.com/sepinedas/media-pipe-builder.
class MpFaceLandmarker {
public:
    ~MpFaceLandmarker();

    // Load the `.task` model bundle and build a video-mode landmarker for up to
    // `maxFaces` faces. Returns nullptr when the path is empty, the model cannot
    // be loaded, or the graph fails to start.
    static std::unique_ptr<MpFaceLandmarker> create(const std::string& modelPath,
                                                     int maxFaces = 1);

    // Run the mesh on a BGR frame, appending one FaceLandmarks per detected
    // face. Inference runs on `bgr` (which may be downscaled for speed), but the
    // landmark coordinates are projected onto an `outW` x `outH` space -- pass
    // the full-frame size so points come back in full-resolution pixels.
    // `timestampMs` must strictly increase between calls (video-mode tracking).
    // Returns false on error.
    bool detect(const cv::Mat& bgr, int64_t timestampMs, float outW, float outH,
                std::vector<FaceLandmarks>& out);

private:
    MpFaceLandmarker();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace olc
