#pragma once

#include <chrono>
#include <string>
#include <vector>

#include <opencv2/core.hpp>
#include <opencv2/objdetect.hpp>

#include "types.hpp"

namespace olc {

// Live, WhatsApp-style face filters. Finds faces in each BGR frame with a Haar
// cascade and draws a cartoon overlay on top (in place), so the effect shows in
// the preview and is baked into photos and recordings. Degrades gracefully: if
// no cascade file can be found, `available()` is false and `apply()` is a no-op.
class Filters {
public:
    Filters();

    // Draw `filter`'s overlay onto every detected face in `frame` (BGR, edited
    // in place). Does nothing for Filter::None, an empty frame, or when no
    // cascade is loaded.
    void apply(cv::Mat& frame, Filter filter);

    // Draw one filter's overlay on an explicit face box (skips detection). Used
    // by apply() per face and by the offline preview/test tools.
    void drawOverlay(cv::Mat& frame, const cv::Rect& face, Filter filter) const;

    // True once a face cascade has been loaded successfully.
    bool available() const { return !face_.empty(); }

private:
    std::vector<cv::Rect> detectFaces(const cv::Mat& frame);
    double clockSeconds() const; // monotonic seconds since construction (for tears)

    cv::CascadeClassifier face_;
    std::chrono::steady_clock::time_point start_;
};

} // namespace olc
