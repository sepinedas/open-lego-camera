#include "filters.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>

#include <opencv2/imgproc.hpp>

namespace olc {

namespace {

// Detection is the expensive part, so the mesh runs on a downscaled image and
// only every few frames; between detections the last landmarks are reused. On a
// hand-held selfie camera the face barely moves frame-to-frame, so this is
// visually seamless while keeping the Pi Zero comfortable.
constexpr int kDetectEvery = 3;
constexpr double kMeshWidth = 256.0;   // downscale target for MediaPipe inference
constexpr double kTearSpeed = 0.019;   // tear cycle progress per frame (fall speed)

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

bool FaceFilter::setLandmarker(const std::string& modelPath, int maxFaces) {
    auto mp = MpFaceLandmarker::create(modelPath, maxFaces);
    if (!mp) return false;
    mp_ = std::move(mp);
    warned_ = false;
    return true;
}

bool FaceFilter::ensureReady() {
    if (mp_) return true;
    if (!warned_) {
        std::cerr << "filters: no Face Mesh model loaded; facial filters "
                     "disabled. Install the MediaPipe .deb and a "
                     "face_landmarker.task model, or pass --face-landmarker.\n";
        warned_ = true;
    }
    return false;
}

void FaceFilter::detectColor(const cv::Mat& bgr) {
    if (!mp_ || bgr.empty() || bgr.type() != CV_8UC3) return;
    // Downscale so MediaPipe's front-end stays cheap on the Pi; the mesh returns
    // normalised coordinates, which we project back onto the full frame.
    double scale = kMeshWidth / std::max(1, bgr.cols);
    if (scale > 1.0) scale = 1.0;
    cv::Mat small;
    if (scale < 1.0)
        cv::resize(bgr, small, cv::Size(), scale, scale, cv::INTER_AREA);
    else
        small = bgr;

    std::vector<FaceLandmarks> got;
    if (mp_->detect(small, videoTs_, (float)bgr.cols, (float)bgr.rows, got))
        landmarks_.swap(got);
    videoTs_ += 33; // ~30fps stamp; only needs to strictly increase
}

void FaceFilter::apply(cv::Mat& frame, Filter filter, double phase) {
    if (filter == Filter::None || frame.empty()) return;
    if (frame.type() != CV_8UC3) return; // filters assume BGR 8-bit
    if (!ensureReady()) return;

    if (frameCount_ % kDetectEvery == 0) detectColor(frame);
    ++frameCount_;

    applyRegion(frame, {0, 0}, filter, phase);
}

void FaceFilter::updateDetectionNV12(const cv::Mat& nv12) {
    if (nv12.empty() || !ensureReady()) return;
    if (frameCount_ % kDetectEvery == 0) {
        // Convert to BGR only on the frames we actually sample.
        cv::Mat bgr;
        cv::cvtColor(nv12, bgr, cv::COLOR_YUV2BGR_NV12);
        detectColor(bgr);
    }
    ++frameCount_;
}

cv::Rect FaceFilter::dirtyRegion(Filter filter, int w, int h) const {
    if (filter == Filter::None || w <= 0 || h <= 0) return cv::Rect();

    // Union the per-face bounding boxes, each grown to cover the warp's
    // Gaussian falloff (~half a face width) and the tears that fall down the
    // cheeks below the eyes. One face -> a tight box; several -> a larger box,
    // still far cheaper than converting the whole frame.
    cv::Rect uni;
    for (const FaceLandmarks& L : landmarks_) {
        const cv::Rect& f = L.box;
        if (f.width < 40 || f.height < 40) continue;
        int mx = std::max(8, f.width * 2 / 5);
        int mtop = std::max(6, f.height * 3 / 10);
        int mbot = std::max(8, f.height / 2);
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
    for (const FaceLandmarks& L : landmarks_) {
        // Skip faces too small to reshape cleanly.
        if (L.box.width < 40 || L.box.height < 40) continue;
        // Shift the landmarks into roi-local coords. The reshaping helpers clip
        // to roi's bounds, so a face only partly inside the region is safe.
        FaceLandmarks face = L.translated(cv::Point2f(-origin.x, -origin.y));
        if (filter == Filter::BigSmile) applySmile(roi, face);
        else if (filter == Filter::Crying) applyCry(roi, face, phase);
    }
}

// A box centred on the mouth, sized to the real mouth width, in which the
// openness/teeth heuristics sample. Precise from the mesh, approximate from the
// cascade box -- either way it tracks the mouth rather than a fixed face slice.
static cv::Rect mouthPatch(const FaceLandmarks& L, const cv::Mat& frame,
                           float wScale, float hScale) {
    float mw = std::max(8.f, L.mouthWidth());
    int w = std::max(4, (int)(mw * wScale));
    int h = std::max(4, (int)(mw * hScale));
    cv::Rect m((int)(L.mouthCenter.x - w * 0.5f),
               (int)(L.mouthCenter.y - h * 0.5f), w, h);
    return m & cv::Rect(0, 0, frame.cols, frame.rows);
}

float FaceFilter::mouthOpenness(const cv::Mat& frame, const FaceLandmarks& L) const {
    cv::Rect m = mouthPatch(L, frame, 0.85f, 0.45f);
    if (m.area() < 20) return 0.f;
    cv::Mat g;
    cv::cvtColor(frame(m), g, cv::COLOR_BGR2GRAY);
    cv::Scalar mean, stddev;
    cv::meanStdDev(g, mean, stddev);
    // A closed mouth is fairly flat; an open one pairs a dark cavity with bright
    // teeth, so its patch has high contrast. Map that spread onto 0..1.
    return clamp01((float)(stddev[0] / 55.0));
}

void FaceFilter::whitenTeeth(cv::Mat& frame, const FaceLandmarks& L, float open) const {
    cv::Rect m = mouthPatch(L, frame, 0.90f, 0.40f);
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

void FaceFilter::applySmile(cv::Mat& frame, const FaceLandmarks& L) {
    const float mw = std::max(8.f, L.mouthWidth());
    const float open = mouthOpenness(frame, L);
    const cv::Point2f axis = L.mouthAxis();  // along the mouth (left->right)
    const cv::Point2f up = L.upAxis();       // toward the cheeks/brow

    // Anchor the grin to the real mouth, scaled by its actual width. The mesh
    // pins the corners exactly, so push a generous grin.
    const float outD = 0.36f * mw;                   // corners slide outward
    const float upD = 0.23f * mw;                    // ...and lift up the cheeks
    const float openD = (0.06f + 0.17f * open) * mw; // vertical mouth stretch

    std::vector<cv::Point2f> src, dst;
    std::vector<float> sig;
    // Mouth corners -> up and out along the mouth's own frame (so the grin stays
    // aligned even when the head is tilted).
    src.push_back(L.mouthLeft);
    dst.push_back(L.mouthLeft - outD * axis + upD * up);
    sig.push_back(0.45f * mw);
    src.push_back(L.mouthRight);
    dst.push_back(L.mouthRight + outD * axis + upD * up);
    sig.push_back(0.45f * mw);
    // Upper lip up / lower lip down -> open the mouth so the teeth show.
    src.push_back(L.mouthTop);
    dst.push_back(L.mouthTop + openD * up);
    sig.push_back(0.38f * mw);
    src.push_back(L.mouthBottom);
    dst.push_back(L.mouthBottom - openD * up);
    sig.push_back(0.38f * mw);

    warpRegion(frame, src, dst, sig);
    whitenTeeth(frame, L, open);
}

void FaceFilter::drawTears(cv::Mat& frame, const FaceLandmarks& L, double phase) const {
    const float fw = std::max(20.f, (float)L.box.width);
    // Two tear columns under each eye (outer + inner), welling from the real
    // lower lids and rolling down to the chin. Everything is phase-staggered so
    // it reads as heavy weeping -- lots of tears -- rather than a curtain of rain.
    const cv::Point2f inL = L.leftEye - L.leftEyeOuter;   // outer->inner (left)
    const cv::Point2f inR = L.rightEye - L.rightEyeOuter; // outer->inner (right)
    struct Col { cv::Point2f o; float dir, off; };
    const Col cols[] = {
        {L.leftTear,                -1.00f, 0.00f}, // left, outer corner
        {L.leftTear + inL * 1.3f,   -0.35f, 0.29f}, // left, inner corner
        {L.rightTear + inR * 1.3f,  +0.35f, 0.61f}, // right, inner corner
        {L.rightTear,               +1.00f, 0.83f}, // right, outer corner
    };
    // Several droplets per column, spread across the cycle so a tear is welling,
    // rolling and drying on each cheek at once.
    const float dropOff[] = {0.0f, 0.34f, 0.67f};
    for (const Col& c : cols) {
        // Fall from the lid down to (roughly) the jaw of this particular face.
        float fall = std::max(30.f, L.chin.y - c.o.y);
        for (float d : dropOff) {
            float p = (float)std::fmod(phase * kTearSpeed + c.off + d, 1.0);
            drawTear(frame, c.o.x, c.o.y, fall, fw, c.dir, p);
        }
    }
}

void FaceFilter::applyCry(cv::Mat& frame, const FaceLandmarks& L, double phase) {
    const float mw = std::max(8.f, L.mouthWidth());
    const float fh = std::max(8.f, (float)L.box.height);
    const float fw = std::max(8.f, (float)L.box.width);
    const cv::Point2f axis = L.mouthAxis();
    const cv::Point2f up = L.upAxis();
    const cv::Point2f down = -up;

    const float downD = 0.069f * fh;    // corners sink toward the chin
    const float inD = 0.10f * mw;       // ...and draw slightly inward
    const float upC = 0.04f * fh;       // philtrum lifts -> deepens the frown
    const float browDownD = 0.069f * fh; // inner brows sink
    const float browInD = 0.115f * mw;   // ...and pinch together

    std::vector<cv::Point2f> src, dst;
    std::vector<float> sig;
    // Mouth corners down + inward, centre up -> a sad frown (inverse of the grin).
    src.push_back(L.mouthLeft);
    dst.push_back(L.mouthLeft + inD * axis + downD * down);
    sig.push_back(0.42f * mw);
    src.push_back(L.mouthRight);
    dst.push_back(L.mouthRight - inD * axis + downD * down);
    sig.push_back(0.42f * mw);
    src.push_back(L.mouthCenter);
    dst.push_back(L.mouthCenter + upC * up);
    sig.push_back(0.36f * mw);
    // Inner brows down and together -> the pinched, crumpled crying brow.
    src.push_back(L.browLeftInner);
    dst.push_back(L.browLeftInner + browInD * axis + browDownD * down);
    sig.push_back(0.12f * fw);
    src.push_back(L.browRightInner);
    dst.push_back(L.browRightInner - browInD * axis + browDownD * down);
    sig.push_back(0.12f * fw);

    warpRegion(frame, src, dst, sig);
    drawTears(frame, L, phase);
}

Filter nextFilter(Filter f) {
    switch (f) {
        case Filter::None:     return Filter::BigSmile;
        case Filter::BigSmile: return Filter::Crying;
        case Filter::Crying:   return Filter::None;
    }
    return Filter::None;
}

const char* filterName(Filter f) {
    switch (f) {
        case Filter::None:     return "Filter Off";
        case Filter::BigSmile: return "Big Smile";
        case Filter::Crying:   return "Crying";
    }
    return "";
}

} // namespace olc
