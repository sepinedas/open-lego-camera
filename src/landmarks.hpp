#pragma once

#include <cmath>

#include <opencv2/core.hpp>

namespace olc {

// A compact set of facial anchor points that drive the expression filters.
//
// All points are in frame-pixel coordinates. "left"/"right" are image-space
// (left = smaller x), so the filters behave the same whether or not the preview
// is mirrored. Two backends fill this in:
//
//   * the MediaPipe Face Mesh backend places every point on the real feature
//     from the 468-point mesh and sets mesh = true (precise); the warp then
//     tracks the actual mouth, brows and eyes -- even when the head is tilted or
//     the mouth is an unusual size -- which is the whole quality win.
//   * the Haar-cascade fallback derives the points from the face box by fixed
//     proportion (mesh = false) -- lower fidelity, but needs no model file, so
//     the filters still work on a bare install.
//
// Because both backends emit the same struct, applySmile()/applyCry() consume
// landmarks and stay backend-agnostic.
struct FaceLandmarks {
    cv::Rect box;                 // tight face bounding box (frame coords)

    cv::Point2f mouthLeft, mouthRight;   // mouth corners
    cv::Point2f mouthTop, mouthBottom;   // centre of the upper / lower lip
    cv::Point2f mouthCenter;

    cv::Point2f leftEye, rightEye;              // eye centres
    cv::Point2f leftEyeOuter, rightEyeOuter;    // outer eye corners
    cv::Point2f leftTear, rightTear;            // where a tear wells (lower lid)
    cv::Point2f chin;                           // bottom of the chin

    cv::Point2f browLeftInner, browRightInner;  // inner brow tips

    bool mesh = false;            // true when placed by the mesh (precise)

    bool precise() const { return mesh; }

    // Distance between the mouth corners: the natural scale for the grin/frown.
    float mouthWidth() const {
        cv::Point2f d = mouthRight - mouthLeft;
        return std::sqrt(d.x * d.x + d.y * d.y);
    }

    // Unit vector along the mouth (left corner -> right corner): the local
    // "sideways" axis the smile pulls the corners along, so a grin stays aligned
    // with a tilted head instead of always pulling horizontally.
    cv::Point2f mouthAxis() const {
        cv::Point2f d = mouthRight - mouthLeft;
        float n = std::sqrt(d.x * d.x + d.y * d.y);
        return n > 1e-3f ? cv::Point2f(d.x / n, d.y / n) : cv::Point2f(1.f, 0.f);
    }

    // Unit vector pointing "up" the face (perpendicular to the mouth axis).
    cv::Point2f upAxis() const {
        cv::Point2f a = mouthAxis();
        return cv::Point2f(a.y, -a.x); // rotate -90 deg -> toward the brow
    }

    // A copy with every point shifted by `d` (used to move frame-space landmarks
    // into a region-local coordinate frame before reshaping a cropped ROI).
    FaceLandmarks translated(const cv::Point2f& d) const {
        FaceLandmarks L = *this;
        L.box = cv::Rect(box.x + (int)d.x, box.y + (int)d.y, box.width, box.height);
        L.mouthLeft += d;      L.mouthRight += d;
        L.mouthTop += d;       L.mouthBottom += d;   L.mouthCenter += d;
        L.leftEye += d;        L.rightEye += d;
        L.leftEyeOuter += d;   L.rightEyeOuter += d;
        L.leftTear += d;       L.rightTear += d;     L.chin += d;
        L.browLeftInner += d;  L.browRightInner += d;
        return L;
    }
};

// Approximate the anchor points from a Haar face box, reproducing the fixed
// proportions the filters used before landmarks were available. Used as the
// fallback when the MediaPipe mesh is unavailable.
inline FaceLandmarks landmarksFromBox(const cv::Rect& f) {
    const float fx = (float)f.x, fy = (float)f.y;
    const float fw = (float)f.width, fh = (float)f.height;

    FaceLandmarks L;
    L.box = f;
    L.mesh = false;

    const float mcy = fy + 0.74f * fh;         // mouth line
    L.mouthLeft   = {fx + 0.33f * fw, mcy};
    L.mouthRight  = {fx + 0.67f * fw, mcy};
    L.mouthTop    = {fx + 0.50f * fw, mcy - 0.03f * fh};
    L.mouthBottom = {fx + 0.50f * fw, mcy + 0.03f * fh};
    L.mouthCenter = {fx + 0.50f * fw, mcy};

    const float eyeY = fy + 0.45f * fh;
    L.leftEye       = {fx + 0.32f * fw, eyeY};
    L.rightEye      = {fx + 0.68f * fw, eyeY};
    L.leftEyeOuter  = {fx + 0.26f * fw, eyeY};
    L.rightEyeOuter = {fx + 0.74f * fw, eyeY};
    L.leftTear      = {fx + 0.31f * fw, fy + 0.49f * fh};
    L.rightTear     = {fx + 0.69f * fw, fy + 0.49f * fh};
    L.chin          = {fx + 0.50f * fw, fy + 0.98f * fh};

    L.browLeftInner  = {fx + 0.40f * fw, fy + 0.36f * fh};
    L.browRightInner = {fx + 0.60f * fw, fy + 0.36f * fh};
    return L;
}

} // namespace olc
