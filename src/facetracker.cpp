#include "facetracker.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <sys/stat.h>

#include <opencv2/imgproc.hpp>

namespace olc {

namespace {
constexpr int kDetectWidth = 320; // downscale width for detection
bool fileExists(const std::string& p) {
    struct stat st{};
    return !p.empty() && ::stat(p.c_str(), &st) == 0;
}

// One-euro filter helpers. `alpha` for a given cutoff and sample rate.
double euroAlpha(double cutoff, double freq) {
    double te = 1.0 / std::max(1.0, freq);
    double tau = 1.0 / (2.0 * CV_PI * cutoff);
    return 1.0 / (1.0 + tau / te);
}
} // namespace

std::vector<std::string> FaceTracker::defaultCascadePaths() {
    return {
        "/usr/share/opencv4/haarcascades/haarcascade_frontalface_default.xml",
        "/usr/local/share/opencv4/haarcascades/haarcascade_frontalface_default.xml",
        "models/haarcascade_frontalface_default.xml",
    };
}

std::vector<std::string> FaceTracker::defaultModelPaths() {
    std::string home = std::getenv("HOME") ? std::getenv("HOME") : ".";
    return {
        "models/lbfmodel.yaml",
        home + "/.local/share/open-lego-camera/lbfmodel.yaml",
        "/usr/share/open-lego-camera/lbfmodel.yaml",
    };
}

std::vector<std::string> FaceTracker::defaultYunetPaths() {
    std::string home = std::getenv("HOME") ? std::getenv("HOME") : ".";
    return {
        "models/face_detection_yunet_2023mar.onnx",
        home + "/.local/share/open-lego-camera/face_detection_yunet_2023mar.onnx",
        "/usr/share/open-lego-camera/face_detection_yunet_2023mar.onnx",
    };
}

std::string FaceTracker::firstExisting(const std::vector<std::string>& paths) {
    for (const auto& p : paths)
        if (fileExists(p)) return p;
    return "";
}

bool FaceTracker::init(const std::string& cascadePath,
                       const std::string& lbfModelPath,
                       const std::string& yunetPath) {
    std::string mp = fileExists(lbfModelPath) ? lbfModelPath
                                              : firstExisting(defaultModelPaths());
    if (mp.empty()) {
        std::cerr << "filter: no landmark model found. Fetch it with "
                     "scripts/get-models.sh or pass --face-model.\n";
        return false;
    }

    // Prefer the YuNet DNN detector; fall back to Haar.
    std::string yp = fileExists(yunetPath) ? yunetPath
                                           : firstExisting(defaultYunetPaths());
    if (!yp.empty()) {
        try {
            yunet_ = cv::FaceDetectorYN::create(yp, "", cv::Size(kDetectWidth, kDetectWidth),
                                                0.7f, 0.3f, 5000);
            useYunet_ = !yunet_.empty();
        } catch (const cv::Exception& e) {
            std::cerr << "filter: YuNet load failed (" << e.what()
                      << "); using Haar\n";
        }
    }
    if (!useYunet_) {
        std::string cp = fileExists(cascadePath) ? cascadePath
                                                 : firstExisting(defaultCascadePaths());
        if (cp.empty() || !cascade_.load(cp)) {
            std::cerr << "filter: no face detector (YuNet model missing and Haar "
                         "cascade unavailable)\n";
            return false;
        }
    }

    try {
        facemark_ = cv::face::FacemarkLBF::create();
        facemark_->loadModel(mp);
    } catch (const cv::Exception& e) {
        std::cerr << "filter: failed to load landmark model " << mp << ": "
                  << e.what() << "\n";
        return false;
    }
    std::cout << "filter: detector=" << (useYunet_ ? "YuNet" : "Haar")
              << ", landmarks=LBF (" << mp << ")\n";
    available_ = true;
    return true;
}

// Detect the largest face; returns its box in full-frame coordinates.
bool FaceTracker::detectFace(const cv::Mat& bgr, cv::Rect& box) {
    double scale = std::min(1.0, (double)kDetectWidth / bgr.cols);
    cv::Mat small;
    cv::resize(bgr, small, cv::Size(), scale, scale, cv::INTER_AREA);
    double inv = 1.0 / scale;

    if (useYunet_) {
        // YuNet requires input dimensions that are multiples of 32; pad the
        // bottom/right with black so the top-left origin (and thus the box
        // coords) are unaffected.
        int pw = (small.cols + 31) / 32 * 32, ph = (small.rows + 31) / 32 * 32;
        cv::Mat padded;
        cv::copyMakeBorder(small, padded, 0, ph - small.rows, 0, pw - small.cols,
                           cv::BORDER_CONSTANT, cv::Scalar(0, 0, 0));
        try {
            yunet_->setInputSize(padded.size());
            cv::Mat faces;
            yunet_->detect(padded, faces);
            if (faces.rows == 0) return false;
            int best = 0;
            float bestArea = 0;
            for (int i = 0; i < faces.rows; ++i) {
                float w = faces.at<float>(i, 2), h = faces.at<float>(i, 3);
                if (w * h > bestArea) { bestArea = w * h; best = i; }
            }
            box = cv::Rect((int)(faces.at<float>(best, 0) * inv),
                           (int)(faces.at<float>(best, 1) * inv),
                           (int)(faces.at<float>(best, 2) * inv),
                           (int)(faces.at<float>(best, 3) * inv));
            return true;
        } catch (const cv::Exception& e) {
            // Some OpenCV/model combinations are incompatible; drop to Haar.
            std::cerr << "filter: YuNet detect failed (" << e.what()
                      << "); switching to Haar\n";
            useYunet_ = false;
            std::string cp = firstExisting(defaultCascadePaths());
            if (cp.empty() || !cascade_.load(cp)) return false;
        }
    }

    if (cascade_.empty()) return false;
    cv::Mat gray;
    cv::cvtColor(small, gray, cv::COLOR_BGR2GRAY);
    cv::equalizeHist(gray, gray);
    std::vector<cv::Rect> faces;
    cascade_.detectMultiScale(gray, faces, 1.2, 3, 0, cv::Size(40, 40));
    if (faces.empty()) return false;
    cv::Rect b = faces[0];
    for (const auto& f : faces) if (f.area() > b.area()) b = f;
    box = cv::Rect((int)(b.x * inv), (int)(b.y * inv), (int)(b.width * inv),
                   (int)(b.height * inv));
    return true;
}

void FaceTracker::resetSmoothing() {
    ex_.clear();
    ey_.clear();
    prev_.clear();
    haveTime_ = false;
}

// One-euro filter over the landmark set: low lag when moving, low jitter at rest.
void FaceTracker::smooth(std::vector<cv::Point2f>& pts, double freq) {
    const double mincut = 1.2, beta = 0.02, dcut = 1.0;
    if (ex_.size() != pts.size()) { ex_.assign(pts.size(), {}); ey_.assign(pts.size(), {}); }
    auto step = [&](Euro& e, double x) {
        double dx = e.init ? (x - e.s) * freq : 0.0;
        e.ds = e.init ? euroAlpha(dcut, freq) * dx + (1 - euroAlpha(dcut, freq)) * e.ds : dx;
        double cutoff = mincut + beta * std::fabs(e.ds);
        double a = euroAlpha(cutoff, freq);
        e.s = e.init ? a * x + (1 - a) * e.s : x;
        e.init = true;
        return e.s;
    };
    for (size_t i = 0; i < pts.size(); ++i) {
        pts[i].x = (float)step(ex_[i], pts[i].x);
        pts[i].y = (float)step(ey_[i], pts[i].y);
    }
}

// Expand a box by `frac` on each side, clamped to the image.
static cv::Rect expandBox(cv::Rect b, double frac, cv::Size sz) {
    int dx = (int)(b.width * frac), dy = (int)(b.height * frac);
    b.x -= dx; b.y -= dy; b.width += 2 * dx; b.height += 2 * dy;
    return b & cv::Rect(0, 0, sz.width, sz.height);
}

bool FaceTracker::track(const cv::Mat& bgr, std::vector<cv::Point2f>& landmarks) {
    if (!available_ || bgr.empty()) return false;

    cv::Mat gray;
    cv::cvtColor(bgr, gray, cv::COLOR_BGR2GRAY);

    // Between periodic detector runs, drive the landmark fit from the previous
    // frame's landmark box -- smoother and more robust to head turns than
    // re-detecting every frame. Re-detect on loss or every kRedetect frames.
    auto fit = [&](const cv::Rect& box, std::vector<cv::Point2f>& out) {
        std::vector<cv::Rect> boxes{expandBox(box, 0.125, bgr.size())};
        std::vector<std::vector<cv::Point2f>> shapes;
        if (facemark_->fit(gray, boxes, shapes) && !shapes.empty()) {
            out = shapes[0];
            return true;
        }
        return false;
    };

    bool wasLost = last_.empty() || missed_ > 0;
    bool redetect = wasLost || sinceDetect_ >= kRedetect;

    std::vector<cv::Point2f> raw;
    cv::Rect box;
    if (redetect) {
        if (detectFace(bgr, box)) { sinceDetect_ = 0; fit(box, raw); }
    } else {
        // Track from the previous landmarks; if that fit fails, re-detect once.
        if (!fit(cv::boundingRect(last_), raw) && detectFace(bgr, box)) {
            sinceDetect_ = 0;
            fit(box, raw);
        }
    }

    if (raw.empty() || raw.size() < 68) {
        if (!last_.empty() && missed_ < kKeepFrames) {
            ++missed_;
            landmarks = last_;
            return true;
        }
        resetSmoothing();
        return false;
    }

    if (wasLost) resetSmoothing(); // re-acquired: don't lag from stale state
    missed_ = 0;
    ++sinceDetect_;

    // Sample rate for the filter, from the wall-clock gap between frames.
    double freq = 30.0;
    auto now = std::chrono::steady_clock::now();
    if (haveTime_) {
        double dt = std::chrono::duration<double>(now - lastTime_).count();
        if (dt > 1e-3) freq = std::min(120.0, 1.0 / dt);
    }
    lastTime_ = now;
    haveTime_ = true;

    smooth(raw, freq);
    last_ = raw;
    landmarks = raw;
    return true;
}

} // namespace olc
