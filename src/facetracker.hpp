#pragma once

#include <chrono>
#include <string>
#include <vector>

#include <opencv2/core.hpp>
#include <opencv2/objdetect.hpp>
#include <opencv2/face.hpp>

namespace olc {

// Detects a face and fits 68 facial landmarks (the "face mesh" the 3D assets
// are anchored to). Quality-focused OpenCV pipeline:
//   * detection with YuNet (a DNN detector) when its model is present -- far
//     more robust to pose/lighting/scale than the Haar fallback;
//   * FacemarkLBF fits the 68 landmarks in the detected box;
//   * a one-euro filter smooths the landmarks over time, removing the jitter
//     that otherwise makes the assets shake.
// If the face is briefly lost the last landmarks are reused for a few frames.
class FaceTracker {
public:
    // Loads the detector (YuNet if `yunetPath` resolves, else the Haar cascade)
    // and the LBF landmark model. available() stays false if the landmark model
    // can't be loaded -- the app then runs without the filter.
    bool init(const std::string& cascadePath, const std::string& lbfModelPath,
              const std::string& yunetPath);
    bool available() const { return available_; }

    // Fit landmarks on a BGR frame. Returns true and fills `landmarks` (68 pts,
    // temporally smoothed) when a face is tracked.
    bool track(const cv::Mat& bgr, std::vector<cv::Point2f>& landmarks);

    static std::vector<std::string> defaultCascadePaths();
    static std::vector<std::string> defaultModelPaths();
    static std::vector<std::string> defaultYunetPaths();
    static std::string firstExisting(const std::vector<std::string>& paths);

private:
    bool detectFace(const cv::Mat& bgr, cv::Rect& box);
    void resetSmoothing();
    void smooth(std::vector<cv::Point2f>& pts, double freq);

    bool available_ = false;
    bool useYunet_ = false;
    cv::Ptr<cv::FaceDetectorYN> yunet_;
    cv::CascadeClassifier cascade_;
    cv::Ptr<cv::face::Facemark> facemark_;

    std::vector<cv::Point2f> last_;
    int missed_ = 0;
    int sinceDetect_ = 1 << 30; // frames since a full detector run
    static constexpr int kKeepFrames = 6;
    static constexpr int kRedetect = 12; // re-run the detector this often

    // One-euro smoothing state (two scalar filters per landmark).
    struct Euro { double s = 0, ds = 0; bool init = false; };
    std::vector<Euro> ex_, ey_;
    std::vector<cv::Point2f> prev_;
    std::chrono::steady_clock::time_point lastTime_{};
    bool haveTime_ = false;
};

} // namespace olc
