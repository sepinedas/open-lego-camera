#include "filters.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>

#include <opencv2/imgproc.hpp>

namespace olc {
namespace {

// Colours are BGR (OpenCV's channel order).
const cv::Scalar kLip(70, 55, 205);      // warm red lips
const cv::Scalar kCavity(45, 30, 95);    // dark mouth interior
const cv::Scalar kTeeth(248, 248, 248);  // white teeth
const cv::Scalar kToothGap(165, 170, 178);
const cv::Scalar kTongue(110, 110, 235);
const cv::Scalar kTearBody(255, 210, 120); // light blue
const cv::Scalar kTearEdge(235, 165, 70);
const cv::Scalar kShine(255, 255, 255);
const cv::Scalar kSadMouth(60, 45, 120);

int lround_i(double v) { return static_cast<int>(std::lround(v)); }

// Heuristic "is the mouth open" test: an open mouth shows a dark cavity, so a
// large fraction of the mouth region is markedly darker than that region's mean.
bool mouthIsOpen(const cv::Mat& frame, const cv::Rect& face) {
    cv::Rect m(face.x + lround_i(face.width * 0.28),
               face.y + lround_i(face.height * 0.60),
               lround_i(face.width * 0.44), lround_i(face.height * 0.32));
    m &= cv::Rect(0, 0, frame.cols, frame.rows);
    if (m.width < 6 || m.height < 6) return false;

    cv::Mat gray;
    cv::cvtColor(frame(m), gray, cv::COLOR_BGR2GRAY);
    cv::GaussianBlur(gray, gray, cv::Size(3, 3), 0);
    double mean = cv::mean(gray)[0];
    cv::Mat dark = gray < mean * 0.62;
    double frac = static_cast<double>(cv::countNonZero(dark)) / gray.total();
    return frac > 0.16;
}

// A big cartoon grin centred on the face's mouth. Closed: a wide toothy smile.
// Open: a gaping mouth with big upper and lower teeth, a tongue and dark cavity.
void drawSmile(cv::Mat& img, const cv::Rect& face, bool open) {
    int cx = face.x + face.width / 2;
    int cy = face.y + lround_i(face.height * 0.74);
    int mw = std::max(8, lround_i(face.width * 0.30));   // mouth half-width
    int mh = std::max(4, lround_i(face.height * (open ? 0.20 : 0.055)));

    // Work inside a padded ROI so the compositing mask stays small.
    cv::Rect box(cx - mw - 4, cy - mh - 4, 2 * mw + 8, 2 * mh + 8);
    box &= cv::Rect(0, 0, img.cols, img.rows);
    if (box.width < 4 || box.height < 4) return;
    cv::Mat roi = img(box);
    cv::Point c(cx - box.x, cy - box.y);
    cv::Size half(mw, mh);

    // Teeth/cavity are drawn into a layer, then copied back only inside the
    // mouth ellipse (via `mask`) so the edges stay clean and lip-shaped.
    cv::Mat mask(roi.size(), CV_8U, cv::Scalar(0));
    cv::ellipse(mask, c, half, 0, 0, 360, cv::Scalar(255), -1, cv::LINE_AA);
    cv::Mat layer = roi.clone();

    if (open) {
        // Dark cavity fills the gape; a tongue sits low; big teeth cap the top
        // and bottom, leaving a clear open gap between them.
        layer.setTo(kCavity);
        cv::ellipse(layer, cv::Point(c.x, c.y + lround_i(mh * 0.55)),
                    cv::Size(lround_i(mw * 0.62), lround_i(mh * 0.45)),
                    0, 0, 360, kTongue, -1, cv::LINE_AA);
        int upperBot = c.y - lround_i(mh * 0.30); // upper teeth reach to here
        int lowerTop = c.y + lround_i(mh * 0.55); // lower teeth start here
        cv::rectangle(layer, cv::Point(0, 0), cv::Point(roi.cols, upperBot),
                      kTeeth, -1);
        cv::rectangle(layer, cv::Point(0, lowerTop), cv::Point(roi.cols, roi.rows),
                      kTeeth, -1);
        // Tooth separations across both bands (big, well-spaced teeth).
        for (int i = -2; i <= 2; ++i) {
            int x = c.x + i * lround_i(mw * 0.36);
            cv::line(layer, cv::Point(x, c.y - mh), cv::Point(x, upperBot),
                     kToothGap, 1, cv::LINE_AA);
            cv::line(layer, cv::Point(x, lowerTop), cv::Point(x, c.y + mh),
                     kToothGap, 1, cv::LINE_AA);
        }
    } else {
        // Closed grin: a wide white teeth band with the upper lip on top.
        layer.setTo(kTeeth);
        cv::rectangle(layer, cv::Point(0, 0),
                      cv::Point(roi.cols, c.y - lround_i(mh * 0.10)), kLip, -1);
        for (int i = -3; i <= 3; ++i) {
            int x = c.x + i * lround_i(mw * 0.26);
            cv::line(layer, cv::Point(x, c.y), cv::Point(x, c.y + mh), kToothGap,
                     1, cv::LINE_AA);
        }
    }

    layer.copyTo(roi, mask);
    cv::ellipse(roi, c, half, 0, 0, 360, kLip, std::max(2, mw / 12), cv::LINE_AA);
}

// One translucent teardrop (round bottom + pointed top), alpha-blended so it
// can fade in and out along its fall.
void drawTear(cv::Mat& img, cv::Point p, int s, double alpha) {
    alpha = std::clamp(alpha, 0.0, 1.0);
    if (alpha <= 0.02 || s < 2) return;
    cv::Rect box(p.x - s - 2, p.y - 3 * s - 2, 2 * s + 4, 4 * s + 4);
    box &= cv::Rect(0, 0, img.cols, img.rows);
    if (box.width < 3 || box.height < 3) return;

    cv::Mat roi = img(box);
    cv::Mat layer = roi.clone();
    cv::Point c(p.x - box.x, p.y - box.y);
    cv::circle(layer, c, s, kTearBody, -1, cv::LINE_AA);
    cv::Point tip[3] = {{c.x, c.y - 3 * s},
                        {c.x - s, c.y - s / 2},
                        {c.x + s, c.y - s / 2}};
    cv::fillConvexPoly(layer, tip, 3, kTearBody, cv::LINE_AA);
    cv::circle(layer, cv::Point(c.x - s / 3, c.y - s / 4), std::max(1, s / 3),
               kShine, -1, cv::LINE_AA);
    cv::circle(layer, c, s, kTearEdge, 1, cv::LINE_AA);
    cv::addWeighted(layer, alpha, roi, 1.0 - alpha, 0.0, roi);
}

// A sad face: tears welling at each eye and streaming down the cheeks (animated
// by `t`), plus a downturned mouth.
void drawCry(cv::Mat& img, const cv::Rect& face, double t) {
    int eyeY = face.y + lround_i(face.height * 0.42);
    int lx = face.x + lround_i(face.width * 0.32);
    int rx = face.x + lround_i(face.width * 0.68);
    int tearS = std::max(3, lround_i(face.width * 0.055));
    int fall = lround_i(face.height * 0.55);
    int startY = eyeY + lround_i(face.height * 0.10);

    for (int eye = 0; eye < 2; ++eye) {
        int ex = eye ? rx : lx;
        double drift = eye ? 1.0 : -1.0; // tears drift outward as they fall
        drawTear(img, cv::Point(ex, startY), tearS, 0.9); // welling at the lid
        for (int k = 0; k < 2; ++k) {
            double phase = std::fmod(t * 0.6 + eye * 0.25 + k * 0.5, 1.0);
            int y = startY + lround_i(phase * fall);
            int x = ex + lround_i(drift * phase * face.width * 0.06);
            double a = std::min(0.95, 2.5 * std::min(phase, 1.0 - phase) + 0.2);
            drawTear(img, cv::Point(x, y), tearS, a);
        }
    }

    // Downturned (frowning) mouth: the top half of an ellipse below the lips.
    int mcx = face.x + face.width / 2;
    int mcy = face.y + lround_i(face.height * 0.76);
    int mw = lround_i(face.width * 0.22);
    cv::ellipse(img, cv::Point(mcx, mcy + mw / 2), cv::Size(mw, mw / 2), 0, 180,
                360, kSadMouth, std::max(2, mw / 8), cv::LINE_AA);
}

// Candidate directories that ship the OpenCV Haar cascades, plus an override.
std::string findCascade(const std::string& file) {
    std::vector<std::string> dirs;
    if (const char* env = std::getenv("OLC_CASCADE_DIR")) dirs.emplace_back(env);
    dirs.insert(dirs.end(), {
        "/usr/share/opencv4/haarcascades/",
        "/usr/local/share/opencv4/haarcascades/",
        "/usr/share/opencv/haarcascades/",
        "/usr/share/OpenCV/haarcascades/",
    });
    for (std::string d : dirs) {
        if (!d.empty() && d.back() != '/') d += '/';
        cv::CascadeClassifier probe;
        std::string path = d + file;
        if (probe.load(path)) return path;
    }
    return "";
}

} // namespace

Filters::Filters() : start_(std::chrono::steady_clock::now()) {
    // frontalface_default is the most widely shipped cascade; alt is a fallback.
    for (const char* name : {"haarcascade_frontalface_default.xml",
                             "haarcascade_frontalface_alt.xml"}) {
        std::string path = findCascade(name);
        if (!path.empty() && face_.load(path)) break;
    }
    if (face_.empty())
        std::cerr << "filters: no face cascade found (set OLC_CASCADE_DIR); "
                     "face filters disabled\n";
}

double Filters::clockSeconds() const {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                         start_)
        .count();
}

std::vector<cv::Rect> Filters::detectFaces(const cv::Mat& frame) {
    std::vector<cv::Rect> faces;
    if (face_.empty() || frame.empty()) return faces;

    cv::Mat gray;
    cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);
    // Detect on a downscaled image -- much cheaper on a Pi Zero 2 W, and faces
    // that fill a selfie frame are still comfortably large at ~320px wide.
    double scale = 320.0 / std::max(1, frame.cols);
    if (scale > 1.0) scale = 1.0;
    cv::Mat small;
    cv::resize(gray, small, cv::Size(), scale, scale, cv::INTER_AREA);
    cv::equalizeHist(small, small);

    std::vector<cv::Rect> found;
    int minS = std::max(24, small.cols / 5);
    face_.detectMultiScale(small, found, 1.2, 4, 0, cv::Size(minS, minS));
    for (const cv::Rect& r : found)
        faces.emplace_back(lround_i(r.x / scale), lround_i(r.y / scale),
                           lround_i(r.width / scale), lround_i(r.height / scale));
    return faces;
}

void Filters::drawOverlay(cv::Mat& frame, const cv::Rect& face,
                          Filter filter) const {
    switch (filter) {
        case Filter::Smile: drawSmile(frame, face, mouthIsOpen(frame, face)); break;
        case Filter::Cry:   drawCry(frame, face, clockSeconds()); break;
        case Filter::None:  break;
    }
}

void Filters::apply(cv::Mat& frame, Filter filter) {
    if (filter == Filter::None || frame.empty() || frame.type() != CV_8UC3)
        return;
    for (const cv::Rect& f : detectFaces(frame)) drawOverlay(frame, f, filter);
}

} // namespace olc
