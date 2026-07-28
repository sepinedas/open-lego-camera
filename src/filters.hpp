#pragma once

#include <string>
#include <vector>

#include <opencv2/core.hpp>
#include <opencv2/objdetect.hpp>

#include "types.hpp"

namespace olc {

// Real-time, WhatsApp-style facial-expression filters.
//
// The expression filters *reshape* the face itself (its pixels are pushed
// around with cv::remap) rather than pasting cartoon graphics over it -- a "big
// smile" is your own mouth stretched into a grin, a "crying" face is your own
// mouth and brows pulled into a frown, the only thing drawn on top being the
// crying tears.
//
// The "pig face" filter instead overlays real 3D models -- mesh ears and a
// protruding snout with nostrils -- rendered by `pig3d` through a perspective
// camera. To make them share the face's orientation and perspective, the head
// pose (roll/yaw/pitch) is estimated from a small set of landmarks: the two eyes
// (a stock eye Haar cascade) give the eye line -> roll and scale, and where the
// eyes sit inside the face box gives a rough turn (yaw) and nod (pitch). The
// meshes are then oriented by that pose, so the snout foreshortens and the ears
// swing around the head instead of sitting on top like stickers.
//
// Faces (and eyes) are found with stock OpenCV Haar cascades (objdetect) and the
// 3D rendering is a self-contained software rasteriser; no landmark-regression
// model, contrib module or GPU is needed, which keeps it light enough for a Pi
// Zero. When no eye cascade is available the pig falls back to the face box
// alone (front-facing, upright).
class FaceFilter {
public:
    // Loads the frontal-face cascade from the usual system locations.
    FaceFilter();

    // Point the detector at an explicit cascade XML (from --face-cascade).
    // Empty is a no-op; a bad path leaves any already-loaded cascade in place.
    void setCascade(const std::string& path);

    // True once a cascade is loaded and filtering can actually do something.
    bool ready() const { return loaded_; }

    // Apply `filter` to `frame` (BGR, 8-bit, 3-channel) in place. `phase` is a
    // free-running per-frame counter that drives the tear animation. A no-op
    // when the filter is None, no cascade loaded, or no face is found.
    void apply(cv::Mat& frame, Filter filter, double phase);

    // --- region-limited API (keeps the NV12 preview off the CPU convert) ---
    //
    // Refresh the detected faces from a luma/grayscale image (the NV12 Y plane
    // is exactly that, so no colour conversion is needed). Honours the
    // detect-every-N-frames cadence internally; call once per frame.
    void updateDetection(const cv::Mat& luma);

    // The single frame-space rectangle covering everything `filter` will modify
    // for the currently-detected faces (face boxes + margin for the warp and
    // tears, clamped to WxH and made even for chroma-subsampled buffers). An
    // empty rect means there is nothing to reshape this frame.
    cv::Rect dirtyRegion(Filter filter, int w, int h) const;

    // Apply `filter` to `roi`, a BGR sub-image whose top-left sits at `origin`
    // in frame space. Only the parts of each face falling inside `roi` are
    // touched, so callers can convert and re-encode just the dirty region.
    void applyRegion(cv::Mat& roi, cv::Point origin, Filter filter, double phase);

    // Draw the 3D pig-face graphics for an explicit face box and eye landmarks,
    // bypassing detection. Pass eye centres (image coords) to orient it; pass
    // (-1,-1) for either to fall back to the box (upright). Used by the mockup
    // tools and tests to preview the effect deterministically.
    void drawPigPreview(cv::Mat& frame, const cv::Rect& face, cv::Point2f leftEye,
                        cv::Point2f rightEye, double phase) const;

private:
    // Landmarks for one face: the two eye centres (full-res frame coords). When
    // `has` is false the eyes were not found this detection and pig-face falls
    // back to the face box for orientation.
    struct FaceEyes {
        bool has = false;
        cv::Point2f left, right; // image-left and image-right eye centres
    };

    void detectLuma(const cv::Mat& luma);        // refresh faces_ (full-res coords)
    // Fill `eyesPerFace_` from the eye cascade, run on the shared downscaled
    // detection image `small` (invScale maps its coords back to full-res).
    void detectEyes(const cv::Mat& small, double invScale,
                    const std::vector<cv::Rect>& facesSmall);
    void applySmile(cv::Mat& frame, const cv::Rect& face);
    void applyCry(cv::Mat& frame, const cv::Rect& face, double phase);
    // Draw the smooth 3D pig ears/snout/cheeks over one face, oriented by its
    // landmarks (or the face box when eyes are unavailable). `phase` drives a
    // gentle ear wiggle. Coords are roi-local (see applyRegion).
    void applyPig(cv::Mat& frame, const cv::Rect& face, const FaceEyes& eyes,
                  double phase) const;

    // Rough 0..1 estimate of how open the mouth is, from the contrast of the
    // central mouth patch (an open mouth = dark cavity next to bright teeth).
    float mouthOpenness(const cv::Mat& frame, const cv::Rect& face) const;
    // Brighten the teeth band toward white; stronger the wider the mouth opens.
    void whitenTeeth(cv::Mat& frame, const cv::Rect& face, float open) const;
    // Draw the falling tears of the crying filter.
    void drawTears(cv::Mat& frame, const cv::Rect& face, double phase) const;

    cv::CascadeClassifier face_;
    cv::CascadeClassifier eyes_;         // for pig-face landmark orientation
    bool loaded_ = false;
    bool eyesLoaded_ = false;            // eye cascade available?
    bool warned_ = false;                // "no cascade" logged only once
    int frameCount_ = 0;                 // detection runs every few frames
    std::vector<cv::Rect> faces_;        // last detection result, full-res
    std::vector<FaceEyes> eyesPerFace_;  // eye landmarks, aligned with faces_
    // Previous detection's smoothed eyes + face centres, used to low-pass the
    // jittery eye boxes across detections (matched to new faces by proximity).
    std::vector<cv::Point2f> prevCentres_;
    std::vector<FaceEyes> prevEyes_;
};

// Try to load the eye Haar cascade that sits next to a given face-cascade path
// (same directory, "haarcascade_eye.xml"). Exposed for reuse/testing; returns
// true and fills `out` on success.
bool loadSiblingEyeCascade(const std::string& faceCascadePath,
                           cv::CascadeClassifier& out);

// Cycle order for the on-screen filter button: None -> BigSmile -> Crying ->.
Filter nextFilter(Filter f);
// Short human label for the brief on-screen filter name.
const char* filterName(Filter f);

} // namespace olc
