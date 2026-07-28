#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <opencv2/core.hpp>

#include "landmarks.hpp"
#include "mp_landmarker.hpp"
#include "types.hpp"

namespace olc {

// Real-time, WhatsApp-style facial-expression filters.
//
// The face itself is *reshaped* (its pixels are pushed around with cv::remap)
// rather than having cartoon graphics pasted over it -- a "big smile" is your
// own mouth stretched into a grin, a "crying" face is your own mouth and brows
// pulled into a frown. The only thing actually drawn on top of the frame are
// the falling tears of the crying filter.
//
// The reshaping is driven by a small set of facial anchor points (FaceLandmarks)
// from MediaPipe's 468-point Face Mesh (see mp_landmarker), so the warp is
// pinned to the real mouth corners, lips, brows and eyes -- the grin follows
// your actual mouth (any size, any head tilt) and the tears well from your real
// eyes. The mesh needs the aarch64 MediaPipe library from media-pipe-builder
// plus a face_landmarker.task model at runtime; without a model the filters are
// inert (the preview still runs).
class FaceFilter {
public:
    FaceFilter() = default;

    // Load the MediaPipe Face Mesh from a `.task` model bundle (from
    // --face-landmarker or a default location). Returns true once the mesh is
    // ready. A failed load leaves the filters inert.
    bool setLandmarker(const std::string& modelPath, int maxFaces = 1);

    // True once the mesh model is loaded and filtering can do something.
    bool ready() const { return mp_ != nullptr; }

    // Apply `filter` to `frame` (BGR, 8-bit, 3-channel) in place. `phase` is a
    // free-running per-frame counter that drives the tear animation. A no-op
    // when the filter is None, no model is loaded, or no face is found.
    void apply(cv::Mat& frame, Filter filter, double phase);

    // --- region-limited API (keeps the NV12 preview off the CPU convert) ---
    //
    // Refresh the detected faces from a full NV12 native buffer (Y plane over a
    // 2x2-subsampled UV plane, `h` luma rows). The mesh needs colour, so on the
    // frames it samples this converts a downscaled copy for inference. Honours
    // the detect-every-N-frames cadence internally; call once per frame.
    void updateDetectionNV12(const cv::Mat& nv12);

    // The single frame-space rectangle covering everything `filter` will modify
    // for the currently-detected faces (face boxes + margin for the warp and
    // tears, clamped to WxH and made even for chroma-subsampled buffers). An
    // empty rect means there is nothing to reshape this frame.
    cv::Rect dirtyRegion(Filter filter, int w, int h) const;

    // Apply `filter` to `roi`, a BGR sub-image whose top-left sits at `origin`
    // in frame space. Only the parts of each face falling inside `roi` are
    // touched, so callers can convert and re-encode just the dirty region.
    void applyRegion(cv::Mat& roi, cv::Point origin, Filter filter, double phase);

private:
    bool ensureReady();                          // log-once "no model" guard
    void detectColor(const cv::Mat& bgr);        // mesh -> landmarks_

    void applySmile(cv::Mat& frame, const FaceLandmarks& face);
    void applyCry(cv::Mat& frame, const FaceLandmarks& face, double phase);

    // Rough 0..1 estimate of how open the mouth is, from the contrast of the
    // central mouth patch (an open mouth = dark cavity next to bright teeth).
    float mouthOpenness(const cv::Mat& frame, const FaceLandmarks& face) const;
    // Brighten the teeth band toward white; stronger the wider the mouth opens.
    void whitenTeeth(cv::Mat& frame, const FaceLandmarks& face, float open) const;
    // Draw the falling tears of the crying filter.
    void drawTears(cv::Mat& frame, const FaceLandmarks& face, double phase) const;

    bool warned_ = false;                // "no model" logged only once
    int frameCount_ = 0;                 // detection runs every few frames
    std::vector<FaceLandmarks> landmarks_;  // last detection, full-res coords

    std::unique_ptr<MpFaceLandmarker> mp_;  // null until a model is loaded
    int64_t videoTs_ = 0;                   // monotonic ms for video-mode tracking
};

// Cycle order for the on-screen filter button: None -> BigSmile -> Crying ->.
Filter nextFilter(Filter f);
// Short human label for the brief on-screen filter name.
const char* filterName(Filter f);

} // namespace olc
