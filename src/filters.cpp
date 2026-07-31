#include "filters.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>

#include <opencv2/imgproc.hpp>

#include "pig3d.hpp"

namespace olc {

namespace {

// Detection is the expensive part, so it runs on a downscaled grayscale image
// and only every few frames; between detections the last boxes are reused. On a
// hand-held selfie camera the face barely moves frame-to-frame, so this is
// visually seamless while keeping the Pi Zero comfortable.
constexpr int kDetectEvery = 3;
constexpr double kDetectWidth = 320.0; // downscale target for detection
constexpr double kTearSpeed = 0.019;   // tear cycle progress per frame (fall speed)

// Candidate locations for the stock frontal-face Haar cascade, in the order we
// try them. Debian/Raspberry Pi OS ship these in the `opencv-data` package.
const char* kCascadePaths[] = {
    "/usr/share/opencv4/haarcascades/haarcascade_frontalface_default.xml",
    "/usr/share/opencv/haarcascades/haarcascade_frontalface_default.xml",
    "/usr/local/share/opencv4/haarcascades/haarcascade_frontalface_default.xml",
    "/usr/share/OpenCV/haarcascades/haarcascade_frontalface_default.xml",
};

// Eye cascades ship in the same `opencv-data` package as the face cascade. They
// give the pig-face filter the two landmarks it needs to track head roll/scale.
const char* kEyeCascadePaths[] = {
    "/usr/share/opencv4/haarcascades/haarcascade_eye.xml",
    "/usr/share/opencv/haarcascades/haarcascade_eye.xml",
    "/usr/local/share/opencv4/haarcascades/haarcascade_eye.xml",
    "/usr/share/OpenCV/haarcascades/haarcascade_eye.xml",
};

float clamp01(float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }

// Alpha-blend a filled circle onto a bounded ROI of `img` (keeps the cost of
// each tear tiny, and gives the tears their translucent, watery look).
void blendCircle(cv::Mat& img, cv::Point c, int r, cv::Scalar col, double a) {
    if (r < 1) r = 1;
    cv::Rect rc(c.x - r, c.y - r, 2 * r + 1, 2 * r + 1);
    rc &= cv::Rect(0, 0, img.cols, img.rows);
    if (rc.area() <= 0) return;
    cv::Mat roi = img(rc), ov = roi.clone();
    cv::circle(ov, c - rc.tl(), r, col, cv::FILLED, cv::LINE_AA);
    cv::addWeighted(ov, a, roi, 1.0 - a, 0.0, roi);
}

// Alpha-blend a thick line (a tear trail) onto a bounded ROI of `img`.
void blendLine(cv::Mat& img, cv::Point a, cv::Point b, cv::Scalar col, int th,
               double alpha) {
    if (th < 1) th = 1;
    int minx = std::min(a.x, b.x) - th, maxx = std::max(a.x, b.x) + th;
    int miny = std::min(a.y, b.y) - th, maxy = std::max(a.y, b.y) + th;
    cv::Rect rc(minx, miny, maxx - minx + 1, maxy - miny + 1);
    rc &= cv::Rect(0, 0, img.cols, img.rows);
    if (rc.area() <= 0) return;
    cv::Mat roi = img(rc), ov = roi.clone();
    cv::line(ov, a - rc.tl(), b - rc.tl(), col, th, cv::LINE_AA);
    cv::addWeighted(ov, alpha, roi, 1.0 - alpha, 0.0, roi);
}

// Draw a single tear at cycle progress p (0..1) rolling from (ox, oy) down the
// cheek. The motion is modelled on a real tear rather than a raindrop: it first
// *wells* at the eye as a growing bead, then a single droplet releases and rolls
// down, starting slow (held by surface tension) and accelerating, drifting
// slightly outward along the cheek and fading as it dries near the jaw. It
// leaves a thin glistening wet track and carries a bright highlight.
void drawTear(cv::Mat& img, float ox, float oy, float fall, float fw, float dir,
              float p) {
    const cv::Scalar track(235, 210, 165); // BGR: faint bluish wet streak
    const cv::Scalar body(250, 235, 205);  // translucent watery droplet
    const cv::Scalar shine(255, 255, 255);
    const float beadR = std::max(2.f, 0.024f * fw);
    const float wellEnd = 0.22f; // fraction of the cycle spent welling up

    if (p < wellEnd) {
        // Welling: a small bead pools on the lid and swells before it drops.
        float g = p / wellEnd;
        int r = std::max(1, (int)(beadR * (0.35f + 0.55f * g)));
        int by = (int)(oy + r * 0.4f);
        blendCircle(img, {(int)ox, by}, r, body, 0.30 + 0.30 * g);
        blendCircle(img, {(int)(ox - r * 0.35f), (int)(by - r * 0.35f)},
                    std::max(1, r / 3), shine, 0.5 + 0.25 * g);
        return;
    }

    float u = (p - wellEnd) / (1.f - wellEnd); // 0..1 along the roll
    // Ease-in: slow release, then accelerating as the drop runs (u^2-ish).
    float ease = u * u * (1.15f - 0.15f * u);
    float y = oy + fall * ease;
    // Gentle outward bow following the curve of the cheek.
    float x = ox + dir * 0.045f * fw * std::sin(u * 1.5708f);

    // Fade in at release and out as it dries near the jaw.
    float alpha = 1.f;
    if (u < 0.12f) alpha = u / 0.12f;
    else if (u > 0.82f) alpha = std::max(0.f, (1.f - u) / 0.18f);

    // Wet track: a thin streak tracing the droplet's actual path from the eye
    // down to where it is now, faintest at the top (drying) and following the
    // same slight bow.
    const int seg = 8;
    const int th = std::max(1, (int)(beadR * 0.35f));
    float px = ox, py = oy;
    for (int s = 1; s <= seg; ++s) {
        float w = u * (float)s / seg;                 // sample the travelled path
        float we = w * w * (1.15f - 0.15f * w);
        float tx = ox + dir * 0.045f * fw * std::sin(w * 1.5708f);
        float ty = oy + fall * we;
        blendLine(img, {(int)px, (int)py}, {(int)tx, (int)ty}, track, th,
                  0.22 * alpha * (float)s / seg);      // fade toward the eye
        px = tx; py = ty;
    }

    // The droplet: a slightly teardrop-shaped bead with a bright highlight.
    int r = std::max(2, (int)beadR);
    blendCircle(img, {(int)x, (int)y}, r, body, 0.60 * alpha);
    blendCircle(img, {(int)x, (int)(y - r * 0.7f)}, std::max(1, (int)(r * 0.6f)),
                body, 0.55 * alpha); // pointed top -> teardrop silhouette
    blendCircle(img, {(int)(x - r * 0.33f), (int)(y - r * 0.33f)},
                std::max(1, r / 3), shine, 0.85 * alpha);
}

// Locally reshape `img` so that the image feature at each src[i] appears to move
// to dst[i], with a smooth Gaussian falloff of radius sig[i]. Implemented as an
// inverse map for cv::remap: for an output pixel p the source sample is
//   p - sum_i w_i(p) * (dst[i] - src[i]),   w_i(p) = exp(-|p-dst[i]|^2 / 2sig^2)
// so at p == dst[i] the sample is exactly src[i]. Only the affected bounding box
// is remapped, so the work stays proportional to the reshaped region.
void warpRegion(cv::Mat& img, const std::vector<cv::Point2f>& src,
                const std::vector<cv::Point2f>& dst,
                const std::vector<float>& sig) {
    const size_t n = src.size();
    if (n == 0 || dst.size() != n || sig.size() != n) return;

    float minx = 1e9f, miny = 1e9f, maxx = -1e9f, maxy = -1e9f, maxsig = 1.f;
    for (size_t i = 0; i < n; ++i) {
        minx = std::min({minx, dst[i].x, src[i].x});
        miny = std::min({miny, dst[i].y, src[i].y});
        maxx = std::max({maxx, dst[i].x, src[i].x});
        maxy = std::max({maxy, dst[i].y, src[i].y});
        maxsig = std::max(maxsig, sig[i]);
    }
    int pad = (int)std::ceil(3.f * maxsig); // Gaussian is negligible past ~3 sigma
    cv::Rect roi((int)std::floor(minx) - pad, (int)std::floor(miny) - pad,
                 (int)std::ceil(maxx - minx) + 2 * pad,
                 (int)std::ceil(maxy - miny) + 2 * pad);
    roi &= cv::Rect(0, 0, img.cols, img.rows);
    if (roi.width < 3 || roi.height < 3) return;

    // Precompute per-control-point displacement and 1/(2 sigma^2).
    std::vector<float> dX(n), dY(n), inv(n);
    for (size_t i = 0; i < n; ++i) {
        dX[i] = dst[i].x - src[i].x;
        dY[i] = dst[i].y - src[i].y;
        inv[i] = 1.f / (2.f * sig[i] * sig[i]);
    }

    cv::Mat mapx(roi.height, roi.width, CV_32F);
    cv::Mat mapy(roi.height, roi.width, CV_32F);
    for (int yy = 0; yy < roi.height; ++yy) {
        float ay = (float)(roi.y + yy);
        float* mx = mapx.ptr<float>(yy);
        float* my = mapy.ptr<float>(yy);
        for (int xx = 0; xx < roi.width; ++xx) {
            float ax = (float)(roi.x + xx);
            float ox = ax, oy = ay;
            for (size_t i = 0; i < n; ++i) {
                float ex = ax - dst[i].x, ey = ay - dst[i].y;
                float w = std::exp(-(ex * ex + ey * ey) * inv[i]);
                ox -= w * dX[i];
                oy -= w * dY[i];
            }
            mx[xx] = ox;
            my[xx] = oy;
        }
    }

    cv::Mat warped;
    cv::remap(img, warped, mapx, mapy, cv::INTER_LINEAR, cv::BORDER_REPLICATE);
    warped.copyTo(img(roi));
}

} // namespace

bool loadSiblingEyeCascade(const std::string& faceCascadePath,
                           cv::CascadeClassifier& out) {
    auto slash = faceCascadePath.find_last_of("/\\");
    std::string dir =
        slash == std::string::npos ? std::string() : faceCascadePath.substr(0, slash + 1);
    return out.load(dir + "haarcascade_eye.xml");
}

FaceFilter::FaceFilter() {
    for (const char* p : kCascadePaths) {
        if (face_.load(p)) {
            loaded_ = true;
            break;
        }
    }
    for (const char* p : kEyeCascadePaths) {
        if (eyes_.load(p)) {
            eyesLoaded_ = true;
            break;
        }
    }
}

void FaceFilter::setCascade(const std::string& path) {
    if (path.empty()) return;
    cv::CascadeClassifier c;
    if (c.load(path)) {
        face_ = c;
        loaded_ = true;
        // Prefer an eye cascade sitting beside the given face cascade; keep any
        // already-loaded one otherwise (the pig filter still works without it).
        cv::CascadeClassifier e;
        if (loadSiblingEyeCascade(path, e)) {
            eyes_ = e;
            eyesLoaded_ = true;
        }
    } else {
        std::cerr << "filters: could not load face cascade '" << path << "'\n";
    }
}

void FaceFilter::detectLuma(const cv::Mat& luma) {
    // `luma` is already single-channel (a grayscale frame, or an NV12 Y plane),
    // so unlike a BGR frame it needs no colour conversion before detection.
    double scale = kDetectWidth / std::max(1, luma.cols);
    if (scale > 1.0) scale = 1.0;
    cv::Mat small;
    if (scale < 1.0)
        cv::resize(luma, small, cv::Size(), scale, scale, cv::INTER_AREA);
    else
        small = luma.clone();
    cv::equalizeHist(small, small);

    std::vector<cv::Rect> found;
    int minSide = std::max(24, std::min(small.cols, small.rows) / 6);
    face_.detectMultiScale(small, found, 1.2, 4, 0, cv::Size(minSide, minSide));

    faces_.clear();
    double inv = 1.0 / scale;
    for (const cv::Rect& r : found)
        faces_.emplace_back((int)std::lround(r.x * inv), (int)std::lround(r.y * inv),
                            (int)std::lround(r.width * inv),
                            (int)std::lround(r.height * inv));

    // Eye landmarks (for the pig-face filter) are found on the same downscaled,
    // equalised image, reusing the just-computed small-space face boxes.
    detectEyes(small, inv, found);
}

// Detect the two eyes inside each face and store their centres (full-res coords)
// in `eyesPerFace_`, aligned with `faces_`. Eye boxes are jittery frame to
// frame, so the result is low-pass filtered against the previous detection
// (matched to the nearest previous face), which keeps the pig's snout and ears
// from twitching. Runs on the shared downscaled detection image.
void FaceFilter::detectEyes(const cv::Mat& small, double invScale,
                            const std::vector<cv::Rect>& facesSmall) {
    eyesPerFace_.assign(faces_.size(), FaceEyes{});
    std::vector<cv::Point2f> centres(faces_.size());
    for (size_t i = 0; i < faces_.size(); ++i)
        centres[i] = cv::Point2f(faces_[i].x + faces_[i].width * 0.5f,
                                 faces_[i].y + faces_[i].height * 0.5f);

    if (eyesLoaded_) {
        for (size_t i = 0; i < facesSmall.size(); ++i) {
            const cv::Rect& fs = facesSmall[i];
            // Eyes live in the upper-middle band of the face; searching just
            // there avoids nostrils/eyebrows and keeps detection cheap.
            cv::Rect band(fs.x, fs.y + (int)(0.18f * fs.height), fs.width,
                          (int)(0.42f * fs.height));
            band &= cv::Rect(0, 0, small.cols, small.rows);
            if (band.width < 12 || band.height < 8) continue;

            std::vector<cv::Rect> es;
            int eMin = std::max(6, fs.width / 8);
            eyes_.detectMultiScale(small(band), es, 1.1, 3, 0,
                                   cv::Size(eMin, eMin));

            // Keep the single largest candidate on each side of the face midline.
            const float mid = fs.width * 0.5f;
            cv::Rect bestL, bestR;
            for (const cv::Rect& e : es) {
                float ecx = (float)(e.x + band.x - fs.x) + e.width * 0.5f;
                if (ecx < mid) {
                    if (e.area() > bestL.area()) bestL = e;
                } else {
                    if (e.area() > bestR.area()) bestR = e;
                }
            }
            if (bestL.area() == 0 || bestR.area() == 0) continue;

            auto centreFull = [&](const cv::Rect& e) {
                return cv::Point2f(
                    (float)((band.x + e.x + e.width * 0.5) * invScale),
                    (float)((band.y + e.y + e.height * 0.5) * invScale));
            };
            FaceEyes fe;
            fe.has = true;
            fe.left = centreFull(bestL);   // image-left eye
            fe.right = centreFull(bestR);  // image-right eye
            eyesPerFace_[i] = fe;
        }
    }

    // Temporal smoothing: blend each face's eyes with the nearest previous
    // face's smoothed eyes so small detection jitter doesn't wobble the pig.
    const float kBlend = 0.5f; // weight of the new measurement
    for (size_t i = 0; i < faces_.size(); ++i) {
        if (!eyesPerFace_[i].has) continue;
        int best = -1;
        float bestD = 1e18f;
        float thr = 0.6f * std::max(faces_[i].width, faces_[i].height);
        for (size_t j = 0; j < prevCentres_.size(); ++j) {
            if (!prevEyes_[j].has) continue;
            float dx = prevCentres_[j].x - centres[i].x;
            float dy = prevCentres_[j].y - centres[i].y;
            float d = std::sqrt(dx * dx + dy * dy);
            if (d < bestD) { bestD = d; best = (int)j; }
        }
        if (best >= 0 && bestD < thr) {
            const FaceEyes& p = prevEyes_[best];
            FaceEyes& c = eyesPerFace_[i];
            c.left = p.left * (1.f - kBlend) + c.left * kBlend;
            c.right = p.right * (1.f - kBlend) + c.right * kBlend;
        }
    }

    prevCentres_ = centres;
    prevEyes_ = eyesPerFace_;
}

void FaceFilter::apply(cv::Mat& frame, Filter filter, double phase) {
    if (filter == Filter::None || frame.empty()) return;
    if (frame.type() != CV_8UC3) return; // filters assume BGR 8-bit
    if (!loaded_) {
        if (!warned_) {
            std::cerr << "filters: no face cascade loaded; facial filters "
                         "disabled. Install `opencv-data` or pass "
                         "--face-cascade PATH.\n";
            warned_ = true;
        }
        return;
    }

    if (frameCount_ % kDetectEvery == 0) {
        cv::Mat gray;
        cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);
        detectLuma(gray);
    }
    ++frameCount_;

    applyRegion(frame, {0, 0}, filter, phase);
}

void FaceFilter::updateDetection(const cv::Mat& luma) {
    if (!loaded_ || luma.empty()) {
        if (!loaded_ && !warned_) {
            std::cerr << "filters: no face cascade loaded; facial filters "
                         "disabled. Install `opencv-data` or pass "
                         "--face-cascade PATH.\n";
            warned_ = true;
        }
        return;
    }
    if (frameCount_ % kDetectEvery == 0) detectLuma(luma);
    ++frameCount_;
}

cv::Rect FaceFilter::dirtyRegion(Filter filter, int w, int h) const {
    if (filter == Filter::None || w <= 0 || h <= 0) return cv::Rect();

    // Union the per-face bounding boxes, each grown to cover the warp's
    // Gaussian falloff (~half a face width) and the tears that fall down the
    // cheeks below the eyes. One face -> a tight box; several -> a larger box,
    // still far cheaper than converting the whole frame.
    //
    // The pig-face ears rise well above the head and the snout/cheeks spread to
    // the sides, so that filter needs a noticeably larger margin than the warps.
    const bool pig = (filter == Filter::PigFace);
    cv::Rect uni;
    for (const cv::Rect& f : faces_) {
        if (f.width < 40 || f.height < 40) continue;
        int mx = pig ? std::max(10, f.width * 9 / 10) : std::max(8, f.width * 2 / 5);
        int mtop = pig ? std::max(10, f.height) : std::max(6, f.height * 3 / 10);
        int mbot = pig ? std::max(10, f.height * 2 / 5) : std::max(8, f.height / 2);
        cv::Rect r(f.x - mx, f.y - mtop, f.width + 2 * mx, f.height + mtop + mbot);
        uni = (uni.area() == 0) ? r : (uni | r);
    }
    uni &= cv::Rect(0, 0, w, h);
    if (uni.area() == 0) return cv::Rect();

    // Snap to an even grid so the crop lines up with NV12's 2x2 chroma plane.
    int x0 = uni.x & ~1, y0 = uni.y & ~1;
    int x1 = (uni.x + uni.width) & ~1, y1 = (uni.y + uni.height) & ~1;
    if (x1 - x0 < 4 || y1 - y0 < 4) return cv::Rect();
    return cv::Rect(x0, y0, x1 - x0, y1 - y0);
}

void FaceFilter::applyRegion(cv::Mat& roi, cv::Point origin, Filter filter,
                             double phase) {
    if (filter == Filter::None || roi.empty() || roi.type() != CV_8UC3) return;
    for (size_t i = 0; i < faces_.size(); ++i) {
        const cv::Rect& faceFrame = faces_[i];
        // Skip faces too small to reshape cleanly.
        if (faceFrame.width < 40 || faceFrame.height < 40) continue;
        // Face rect in roi-local coords. The reshaping helpers clip to roi's
        // bounds, so a face only partly inside the region is handled safely.
        cv::Rect face(faceFrame.x - origin.x, faceFrame.y - origin.y,
                      faceFrame.width, faceFrame.height);
        if (filter == Filter::BigSmile) {
            applySmile(roi, face);
        } else if (filter == Filter::Crying) {
            applyCry(roi, face, phase);
        } else if (filter == Filter::PigFace) {
            // Shift the eye landmarks into the same roi-local frame as the face.
            FaceEyes eyes = (i < eyesPerFace_.size()) ? eyesPerFace_[i] : FaceEyes{};
            if (eyes.has) {
                cv::Point2f o((float)origin.x, (float)origin.y);
                eyes.left -= o;
                eyes.right -= o;
            }
            applyPig(roi, face, eyes, phase);
        }
    }
}

float FaceFilter::mouthOpenness(const cv::Mat& frame, const cv::Rect& f) const {
    cv::Rect m((int)(f.x + 0.35f * f.width), (int)(f.y + 0.66f * f.height),
               (int)(0.30f * f.width), (int)(0.16f * f.height));
    m &= cv::Rect(0, 0, frame.cols, frame.rows);
    if (m.area() < 20) return 0.f;
    cv::Mat g;
    cv::cvtColor(frame(m), g, cv::COLOR_BGR2GRAY);
    cv::Scalar mean, stddev;
    cv::meanStdDev(g, mean, stddev);
    // A closed mouth is fairly flat; an open one pairs a dark cavity with bright
    // teeth, so its patch has high contrast. Map that spread onto 0..1.
    return clamp01((float)(stddev[0] / 55.0));
}

void FaceFilter::whitenTeeth(cv::Mat& frame, const cv::Rect& f, float open) const {
    cv::Rect m((int)(f.x + 0.33f * f.width), (int)(f.y + 0.70f * f.height),
               (int)(0.34f * f.width), (int)(0.14f * f.height));
    m &= cv::Rect(0, 0, frame.cols, frame.rows);
    if (m.area() < 20) return;

    float strength = 0.30f + 0.50f * open; // teeth pop more the wider you grin
    cv::Mat roi = frame(m);
    for (int y = 0; y < roi.rows; ++y) {
        cv::Vec3b* row = roi.ptr<cv::Vec3b>(y);
        for (int x = 0; x < roi.cols; ++x) {
            cv::Vec3b& px = row[x];
            // Rec.601 luma; only already-bright pixels (the teeth) get whitened.
            float luma = 0.114f * px[0] + 0.587f * px[1] + 0.299f * px[2];
            if (luma <= 135.f) continue;
            float t = strength * clamp01((luma - 135.f) / 110.f);
            for (int c = 0; c < 3; ++c)
                px[c] = cv::saturate_cast<uchar>(px[c] + t * (255.f - px[c]));
        }
    }
}

void FaceFilter::applySmile(cv::Mat& frame, const cv::Rect& f) {
    const float fx = f.x, fy = f.y, fw = f.width, fh = f.height;
    const float mcx = fx + 0.50f * fw, mcy = fy + 0.74f * fh;
    const float open = mouthOpenness(frame, f);

    const float outX = 0.10f * fw;                 // corners pull outward
    const float upY = 0.06f * fh;                  // ...and upward
    const float openY = (0.02f + 0.06f * open) * fh; // vertical mouth stretch

    std::vector<cv::Point2f> src, dst;
    std::vector<float> sig;
    // Left/right mouth corners -> up and out (the grin).
    src.push_back({fx + 0.32f * fw, mcy});
    dst.push_back({fx + 0.32f * fw - outX, mcy - upY});
    sig.push_back(0.16f * fw);
    src.push_back({fx + 0.68f * fw, mcy});
    dst.push_back({fx + 0.68f * fw + outX, mcy - upY});
    sig.push_back(0.16f * fw);
    // Upper lip up / lower lip down -> open the mouth so the teeth show.
    src.push_back({mcx, mcy - 0.03f * fh});
    dst.push_back({mcx, mcy - 0.03f * fh - openY});
    sig.push_back(0.13f * fw);
    src.push_back({mcx, mcy + 0.03f * fh});
    dst.push_back({mcx, mcy + 0.03f * fh + openY});
    sig.push_back(0.13f * fw);

    warpRegion(frame, src, dst, sig);
    whitenTeeth(frame, f, open);
}

void FaceFilter::drawTears(cv::Mat& frame, const cv::Rect& f, double phase) const {
    const float fw = f.width, fh = f.height;
    const float eyeY = f.y + 0.49f * fh;
    // Two tear tracks under each eye (inner + outer corner), each shedding a
    // little stream of droplets. Everything is phase-staggered so it reads as
    // heavy weeping -- lots of tears -- rather than a curtain of rain.
    struct Track { float ox, dir, fall, off; };
    const Track tracks[] = {
        {f.x + 0.32f * fw, -1.0f, 0.42f * fh, 0.00f}, // left, outer corner
        {f.x + 0.41f * fw, -0.35f, 0.38f * fh, 0.29f}, // left, inner corner
        {f.x + 0.59f * fw, +0.35f, 0.38f * fh, 0.61f}, // right, inner corner
        {f.x + 0.68f * fw, +1.0f, 0.42f * fh, 0.83f}, // right, outer corner
    };
    // Several droplets per track, spread across the cycle so a tear is welling,
    // rolling and drying on each cheek at once.
    const float dropOff[] = {0.0f, 0.34f, 0.67f};
    for (const Track& t : tracks) {
        for (float d : dropOff) {
            float p = (float)std::fmod(phase * kTearSpeed + t.off + d, 1.0);
            drawTear(frame, t.ox, eyeY, t.fall, fw, t.dir, p);
        }
    }
}

void FaceFilter::applyCry(cv::Mat& frame, const cv::Rect& f, double phase) {
    const float fx = f.x, fy = f.y, fw = f.width, fh = f.height;
    const float mcx = fx + 0.50f * fw, mcy = fy + 0.74f * fh;

    const float downY = 0.06f * fh; // corners sink
    const float inX = 0.03f * fw;   // ...and draw slightly inward

    std::vector<cv::Point2f> src, dst;
    std::vector<float> sig;
    // Mouth corners down + centre up -> a sad frown (inverse of the smile).
    src.push_back({fx + 0.34f * fw, mcy});
    dst.push_back({fx + 0.34f * fw + inX, mcy + downY});
    sig.push_back(0.15f * fw);
    src.push_back({fx + 0.66f * fw, mcy});
    dst.push_back({fx + 0.66f * fw - inX, mcy + downY});
    sig.push_back(0.15f * fw);
    src.push_back({mcx, mcy - 0.01f * fh});
    dst.push_back({mcx, mcy - 0.05f * fh});
    sig.push_back(0.13f * fw);
    // Inner brows down and together -> the pinched, crumpled crying brow.
    src.push_back({fx + 0.40f * fw, fy + 0.36f * fh});
    dst.push_back({fx + 0.43f * fw, fy + 0.42f * fh});
    sig.push_back(0.12f * fw);
    src.push_back({fx + 0.60f * fw, fy + 0.36f * fh});
    dst.push_back({fx + 0.57f * fw, fy + 0.42f * fh});
    sig.push_back(0.12f * fw);

    warpRegion(frame, src, dst, sig);
    drawTears(frame, f, phase);
}

void FaceFilter::applyPig(cv::Mat& frame, const cv::Rect& f, const FaceEyes& eyes,
                          double phase) const {
    // The pig is a set of real 3D meshes (ears + snout) rendered through a
    // perspective camera by pig3d, using a head pose estimated from the eye
    // landmarks and the face box. Keeping the graphics three-dimensional is what
    // makes them share the face's orientation and perspective -- the snout
    // protrudes and foreshortens, the ears swing around and occlude behind the
    // head as it turns -- instead of looking like flat stickers.
    cv::Point2f l = eyes.has ? eyes.left : cv::Point2f(-1.f, -1.f);
    cv::Point2f r = eyes.has ? eyes.right : cv::Point2f(-1.f, -1.f);
    pig3d::render(frame, f, eyes.has, l, r, phase);
}

void FaceFilter::drawPigPreview(cv::Mat& frame, const cv::Rect& face,
                                cv::Point2f leftEye, cv::Point2f rightEye,
                                double phase) const {
    if (frame.empty() || frame.type() != CV_8UC3) return;
    FaceEyes e;
    if (leftEye.x >= 0.f && rightEye.x >= 0.f) {
        e.has = true;
        e.left = leftEye;
        e.right = rightEye;
    }
    applyPig(frame, face, e, phase);
}

Filter nextFilter(Filter f) {
    switch (f) {
        case Filter::None:     return Filter::BigSmile;
        case Filter::BigSmile: return Filter::Crying;
        case Filter::Crying:   return Filter::PigFace;
        case Filter::PigFace:  return Filter::None;
    }
    return Filter::None;
}

const char* filterName(Filter f) {
    switch (f) {
        case Filter::None:     return "Filter Off";
        case Filter::BigSmile: return "Big Smile";
        case Filter::Crying:   return "Crying";
        case Filter::PigFace:  return "Pig Face";
    }
    return "";
}

} // namespace olc
