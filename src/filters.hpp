#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <opencv2/core.hpp>
#include <opencv2/objdetect.hpp>

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
// from one of two detectors:
//
//   * MediaPipe's Face Mesh (setLandmarker), which pins the warp to the real
//     mouth corners, lips, brows and eyes from the 468-point mesh -- so the
//     grin follows your actual mouth (any size, any head tilt) and the tears
//     well from your real eyes. This is the higher-quality path, built against
//     the aarch64 MediaPipe artifacts from media-pipe-builder.
//   * a stock OpenCV Haar cascade (objdetect) as the always-available fallback,
//     which approximates the same anchor points from the face box. Lower
//     fidelity, but needs no model file, so the filters still work everywhere.
class FaceFilter {
public:
    // Loads the frontal-face cascade from the usual system locations.
    FaceFilter();

    // Point the Haar detector at an explicit cascade XML (from --face-cascade).
    // Empty is a no-op; a bad path leaves any already-loaded cascade in place.
    void setCascade(const std::string& path);

    // Enable the MediaPipe Face Mesh backend from a `.task` model bundle (from
    // --face-landmarker). When it loads, detection uses the precise mesh; on
    // failure (or when MediaPipe is not compiled in) the cascade stays in use.
    // Returns true if the mesh backend is now active.
    bool setLandmarker(const std::string& modelPath, int maxFaces = 1);

    // True when the precise MediaPipe mesh backend is active.
    bool usesLandmarker() const { return mp_ != nullptr; }

    // True once some detector (mesh or cascade) is available.
    bool ready() const { return mp_ != nullptr || loaded_; }

    // Apply `filter` to `frame` (BGR, 8-bit, 3-channel) in place. `phase` is a
    // free-running per-frame counter that drives the tear animation. A no-op
    // when the filter is None, no detector is available, or no face is found.
    void apply(cv::Mat& frame, Filter filter, double phase);

    // --- region-limited API (keeps the NV12 preview off the CPU convert) ---
    //
    // Refresh the detected faces from a full NV12 native buffer (Y plane over a
    // 2x2-subsampled UV plane, `h` luma rows). The cascade reads the Y plane
    // directly; the mesh backend converts just the frames it samples to colour.
    // Honours the detect-every-N-frames cadence internally; call once per frame.
    void updateDetectionNV12(const cv::Mat& nv12, int h);

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
    bool ensureReady();                          // log-once "no detector" guard
    void detectLuma(const cv::Mat& luma);        // cascade -> landmarks_ (approx)
    void detectColor(const cv::Mat& bgr);        // mesh -> landmarks_ (precise)

    void applySmile(cv::Mat& frame, const FaceLandmarks& face);
    void applyCry(cv::Mat& frame, const FaceLandmarks& face, double phase);

    // Rough 0..1 estimate of how open the mouth is, from the contrast of the
    // central mouth patch (an open mouth = dark cavity next to bright teeth).
    float mouthOpenness(const cv::Mat& frame, const FaceLandmarks& face) const;
    // Brighten the teeth band toward white; stronger the wider the mouth opens.
    void whitenTeeth(cv::Mat& frame, const FaceLandmarks& face, float open) const;
    // Draw the falling tears of the crying filter.
    void drawTears(cv::Mat& frame, const FaceLandmarks& face, double phase) const;

    cv::CascadeClassifier face_;
    bool loaded_ = false;                // Haar cascade loaded
    bool warned_ = false;                // "no detector" logged only once
    int frameCount_ = 0;                 // detection runs every few frames
    std::vector<FaceLandmarks> landmarks_;  // last detection, full-res coords

    std::unique_ptr<MpFaceLandmarker> mp_;  // null unless the mesh is active
    int64_t videoTs_ = 0;                   // monotonic ms for video-mode tracking
};

// Cycle order for the on-screen filter button: None -> BigSmile -> Crying ->.
Filter nextFilter(Filter f);
// Short human label for the brief on-screen filter name.
const char* filterName(Filter f);

} // namespace olc
