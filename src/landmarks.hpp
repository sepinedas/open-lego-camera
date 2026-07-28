#pragma once

#include <cmath>

#include <opencv2/core.hpp>

namespace olc {

// A compact set of facial anchor points that drive the expression filters.
//
// All points are in frame-pixel coordinates and come from MediaPipe's 468-point
// Face Mesh (see mp_landmarker), so every point sits on the real feature -- the
// warp tracks the actual mouth, brows and eyes even when the head is tilted or
// the mouth is an unusual size. "left"/"right" are image-space (left = smaller
// x), so the filters behave the same whether or not the preview is mirrored.
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

} // namespace olc
