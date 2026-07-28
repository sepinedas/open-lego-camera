#include "pig3d.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include <opencv2/imgproc.hpp>

namespace olc::pig3d {

namespace {

using cv::Vec3f;
using cv::Matx33f;

constexpr float kPi = 3.14159265358979f;
constexpr int kSS = 2;         // supersampling factor for anti-aliasing
constexpr float kCamZ = 7.0f;  // camera distance in model units (eye-widths)

float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

Vec3f norm(const Vec3f& v) {
    float n = std::sqrt(v.dot(v));
    return n > 1e-8f ? v * (1.f / n) : v;
}

// --- Mesh ------------------------------------------------------------------

// A triangle mesh with per-vertex position/normal/colour and a single material.
// `doubleSided` ears are lit from whichever face points at the camera; the
// closed snout is back-face culled instead.
struct Mesh {
    std::vector<Vec3f> pos;    // model space
    std::vector<Vec3f> nrm;    // model-space vertex normals (accumulated)
    std::vector<Vec3f> col;    // BGR 0..255 base colour
    std::vector<cv::Vec3i> tri;
    bool doubleSided = false;
    float ambient = 0.35f;
    float spec = 0.2f;
    float shin = 14.f;

    int add(const Vec3f& p, const Vec3f& c) {
        pos.push_back(p);
        col.push_back(c);
        nrm.emplace_back(0.f, 0.f, 0.f);
        return (int)pos.size() - 1;
    }
    void face(int a, int b, int c) { tri.emplace_back(a, b, c); }

    // Smooth vertex normals by accumulating adjacent face normals.
    void computeNormals() {
        for (auto& n : nrm) n = Vec3f(0.f, 0.f, 0.f);
        for (const auto& t : tri) {
            Vec3f fn = (pos[t[1]] - pos[t[0]]).cross(pos[t[2]] - pos[t[0]]);
            nrm[t[0]] += fn;
            nrm[t[1]] += fn;
            nrm[t[2]] += fn;
        }
        for (auto& n : nrm) n = norm(n);
    }
};

// Rotate a point about an axis-aligned pivot by a rotation matrix.
Vec3f rotAbout(const Matx33f& R, const Vec3f& p, const Vec3f& pivot) {
    return R * (p - pivot) + pivot;
}

Matx33f rotZ(float a) {
    float c = std::cos(a), s = std::sin(a);
    return Matx33f(c, -s, 0, s, c, 0, 0, 0, 1);
}

// --- Mesh builders ---------------------------------------------------------

// An orthonormal (u, v) basis spanning the plane perpendicular to `axis`.
void basis(const Vec3f& axis, Vec3f& u, Vec3f& v) {
    Vec3f up(0.f, 1.f, 0.f);
    if (std::fabs(axis.dot(up)) > 0.9f) up = Vec3f(1.f, 0.f, 0.f);
    u = norm(up.cross(axis)); // roughly head-right
    v = norm(axis.cross(u));  // roughly head-up/down
}

// The snout: an elliptical tube protruding forward (-Z) from the face, capped by
// a domed front pad. Wider than tall, flaring slightly toward the front.
Mesh buildSnout() {
    Mesh m;
    m.ambient = 0.42f;
    m.spec = 0.26f;
    m.shin = 18.f;
    const Vec3f pink(168, 152, 236);
    const Vec3f base(0.f, 0.32f, -0.28f);       // sits low on the face, forward
    const Vec3f axis = norm(Vec3f(0.f, 0.12f, -1.f)); // mostly forward, a touch down
    const float len = 0.50f;
    const int nSeg = 26, nRing = 4;
    Vec3f u, v;
    basis(axis, u, v);

    // Rings from the face out to the front.
    std::vector<std::vector<int>> ring(nRing);
    for (int r = 0; r < nRing; ++r) {
        float t = (float)r / (nRing - 1);
        Vec3f c = base + axis * (t * len);
        float rx = 0.42f * (1.f + 0.14f * t); // flare forward
        float ry = 0.35f * (1.f + 0.12f * t);
        for (int i = 0; i < nSeg; ++i) {
            float a = 2.f * kPi * i / nSeg;
            Vec3f p = c + u * (rx * std::cos(a)) + v * (ry * std::sin(a));
            ring[r].push_back(m.add(p, pink));
        }
    }
    for (int r = 0; r + 1 < nRing; ++r)
        for (int i = 0; i < nSeg; ++i) {
            int i2 = (i + 1) % nSeg;
            m.face(ring[r][i], ring[r][i2], ring[r + 1][i2]);
            m.face(ring[r][i], ring[r + 1][i2], ring[r + 1][i]);
        }

    // Front pad: a slightly brighter, domed disc closing the tube. `axis` points
    // toward the camera (its z is negative), so adding it bulges the pad OUT.
    const Vec3f pad(184, 168, 243);
    Vec3f fc = base + axis * len;
    float frx = 0.42f * 1.14f, fry = 0.35f * 1.12f;
    int centre = m.add(fc + axis * 0.07f, pad); // dome bulging toward camera
    std::vector<int> fringe;
    for (int i = 0; i < nSeg; ++i) {
        float a = 2.f * kPi * i / nSeg;
        Vec3f p = fc + u * (frx * std::cos(a)) + v * (fry * std::sin(a));
        fringe.push_back(m.add(p, pad));
    }
    for (int i = 0; i < nSeg; ++i)
        m.face(centre, fringe[(i + 1) % nSeg], fringe[i]);

    m.computeNormals();
    return m;
}

// Two nostrils: small dark domes recessed into the snout's front pad.
Mesh buildNostrils() {
    Mesh m;
    m.doubleSided = true; // tiny discs; skip culling so winding never hides them
    m.ambient = 0.34f;
    m.spec = 0.05f;
    m.shin = 20.f;
    const Vec3f dark(40, 32, 70);
    const Vec3f base(0.f, 0.32f, -0.28f);
    const Vec3f axis = norm(Vec3f(0.f, 0.12f, -1.f));
    const float len = 0.50f;
    Vec3f u, v; // u ~ head-right (image), v ~ head-down (image)
    basis(axis, u, v);
    Vec3f fc = base + axis * len;

    const int nSeg = 16;
    for (float side : {-1.f, 1.f}) {
        // Two prominent holes near the centre of the domed pad, sitting just in
        // front of it so they win the z-test; slanted outward as a pig's are.
        // (+axis moves toward the camera; +v is downward.)
        Vec3f c = fc + u * (0.16f * side) + v * 0.015f + axis * 0.09f;
        float rx = 0.10f, ry = 0.15f;
        float ca = std::cos(side * 0.28f), sa = std::sin(side * 0.28f);
        int centre = m.add(c + axis * 0.02f, dark);
        std::vector<int> fr;
        for (int i = 0; i < nSeg; ++i) {
            float a = 2.f * kPi * i / nSeg;
            float ex = rx * std::cos(a), ey = ry * std::sin(a);
            float rxr = ex * ca - ey * sa, ryr = ex * sa + ey * ca; // tilt
            fr.push_back(m.add(c + u * rxr + v * ryr, dark));
        }
        for (int i = 0; i < nSeg; ++i)
            m.face(centre, fr[i], fr[(i + 1) % nSeg]);
    }
    m.computeNormals();
    return m;
}

// One ear: a broad, gently cupped triangular flap seated high on the top-side of
// the head (clear of the eyes), rising up-and-out from a wide base and drooping
// forward at the tip like a real pig's ear. Double-sided; the lower-inner part is
// tinted a deeper pink so the ear reads as having an inner hollow.
Mesh buildEar(float side, float wiggle) {
    Mesh m;
    m.doubleSided = true;
    m.ambient = 0.36f;
    m.spec = 0.16f;
    m.shin = 12.f;
    const Vec3f pink(170, 150, 238);
    const Vec3f inner(120, 95, 200);

    // Seat the ear high on the top-side of the head, well above and outside the
    // eyes (model eyes are at (+-0.5, -0.35)), so it never covers them.
    const Vec3f baseC(side * 0.74f, -0.82f, 0.02f);
    // The ear is a broad triangular flap. `length` runs up and outward from the
    // base; `front` is roughly where its front face looks (forward, out, up).
    Vec3f length = norm(Vec3f(side * 0.42f, -1.0f, -0.05f)); // up and out
    Vec3f front = Vec3f(side * 0.45f, -0.20f, -0.86f);
    Vec3f width = norm(front.cross(length)); // across the ear, in its plane
    if (width[0] * side < 0.f) width = -width;
    Vec3f N = norm(length.cross(width)); // true plane normal
    if (N[2] > 0.f) N = -N;              // face the camera
    // Floppy forward-and-down droop of the tip -> a real ear, not a stiff horn.
    const Vec3f droopDir = norm(Vec3f(side * 0.05f, 0.42f, -1.0f));
    const float earLen = 1.02f, curv = 0.12f, droop = 0.34f;

    const int nS = 13, nT = 9;
    std::vector<std::vector<int>> g(nT, std::vector<int>(nS));
    for (int ti = 0; ti < nT; ++ti) {
        float t = (float)ti / (nT - 1);
        // Wide, rounded base tapering to a soft point: broad triangle, not a spike.
        float halfW = 0.62f * std::pow(1.f - t, 0.85f);
        // Round the very base corners in a touch.
        if (t < 0.12f) halfW *= 0.75f + 0.25f * (t / 0.12f);
        for (int si = 0; si < nS; ++si) {
            float s = 2.f * si / (nS - 1) - 1.f; // -1..1 across width
            Vec3f p = baseC + length * (t * earLen) + width * (s * halfW);
            // Gentle cup so the flap catches light without curling like a cone.
            float cup = curv * (1.f - s * s) * (1.f - 0.5f * t);
            p += N * cup;
            // Progressive forward droop, strongest at the tip.
            p += droopDir * (droop * t * t);
            // Deeper pink toward the lower-central inner hollow.
            float inF = clampf((1.f - std::fabs(s)) * 1.1f - 0.15f, 0.f, 1.f) *
                        clampf(1.3f * (1.f - t), 0.f, 1.f);
            Vec3f c = pink * (1.f - inF) + inner * inF;
            g[ti][si] = m.add(p, c);
        }
    }
    for (int ti = 0; ti + 1 < nT; ++ti)
        for (int si = 0; si + 1 < nS; ++si) {
            m.face(g[ti][si], g[ti][si + 1], g[ti + 1][si + 1]);
            m.face(g[ti][si], g[ti + 1][si + 1], g[ti + 1][si]);
        }

    // Idle wiggle: rock the whole ear about its base in the image plane.
    if (wiggle != 0.f) {
        Matx33f Rw = rotZ(side * wiggle);
        for (auto& p : m.pos) p = rotAbout(Rw, p, baseC);
    }
    m.computeNormals();
    return m;
}

// --- Pose + rasteriser -----------------------------------------------------

struct Pose {
    Matx33f R;         // model -> world rotation
    cv::Point2f anchor; // image point the model origin projects to
    float scale;        // model unit -> pixels at the head's depth
};

// Estimate head pose from the eye landmarks and face box. The eye line fixes
// roll and the in-plane scale; where the eyes sit inside the box gives rough
// yaw/pitch. Model eyes sit at (+-0.5, -0.35, -0.2), so the model origin is the
// head centre and the whole rig rotates about it.
Pose estimatePose(const cv::Rect& f, bool hasEyes, cv::Point2f le,
                  cv::Point2f re) {
    float roll = 0.f, yaw = 0.f, pitch = 0.f, eyeDist;
    cv::Point2f eyeMid;
    if (hasEyes) {
        cv::Point2f d = re - le;
        eyeDist = std::sqrt(d.x * d.x + d.y * d.y);
        eyeMid = (le + re) * 0.5f;
        roll = std::atan2(d.y, d.x);
        float bcx = f.x + f.width * 0.5f;
        float nx = (eyeMid.x - bcx) / (0.5f * f.width);
        float ny = (eyeMid.y - (f.y + 0.45f * f.height)) / (0.5f * f.height);
        yaw = clampf(nx * 2.2f, -0.9f, 0.9f);   // + => head turned toward image-right
        pitch = clampf(-ny * 1.1f, -0.5f, 0.5f); // + => chin up
    } else {
        eyeDist = 0.42f * f.width;
        eyeMid = cv::Point2f(f.x + 0.5f * f.width, f.y + 0.42f * f.height);
    }

    // R = Rz(roll) * Ry(yaw) * Rx(pitch), applied to model points.
    float cr = std::cos(roll), sr = std::sin(roll);
    float cy = std::cos(yaw), sy = std::sin(yaw);
    float cp = std::cos(pitch), sp = std::sin(pitch);
    Matx33f Rz(cr, -sr, 0, sr, cr, 0, 0, 0, 1);
    Matx33f Ry(cy, 0, sy, 0, 1, 0, -sy, 0, cy);
    Matx33f Rx(1, 0, 0, 0, cp, -sp, 0, sp, cp);

    Pose ps;
    ps.R = Rz * Ry * Rx;

    // Calibrate scale so the model eye separation matches the observed one, and
    // anchor so the model eye-midpoint lands on the observed eye-midpoint. This
    // undoes the yaw/perspective foreshortening baked into the observation.
    Vec3f eyeMidModel(0.f, -0.35f, -0.2f);
    Vec3f wMid = ps.R * eyeMidModel;
    float perspMid = kCamZ / (kCamZ + wMid[2]);
    Vec3f eyeAxis = ps.R * Vec3f(1.f, 0.f, 0.f); // model eye separation vector
    float fe = clampf(std::sqrt(eyeAxis[0] * eyeAxis[0] + eyeAxis[1] * eyeAxis[1]),
                      0.3f, 1.f);
    ps.scale = eyeDist / (perspMid * fe);
    ps.anchor = eyeMid - cv::Point2f(ps.scale * perspMid * wMid[0],
                                     ps.scale * perspMid * wMid[1]);
    return ps;
}

// Project a model point to screen (in supersampled pixels) plus its camera
// depth. Perspective divide about the camera at (0,0,-kCamZ) looking +Z.
struct Proj { cv::Point2f s; float depth; };
Proj project(const Pose& ps, const Vec3f& world, float ssScale, cv::Point2f ssOrg) {
    float depth = kCamZ + world[2];
    float persp = kCamZ / std::max(0.5f, depth);
    cv::Point2f img = ps.anchor + cv::Point2f(ps.scale * persp * world[0],
                                              ps.scale * persp * world[1]);
    Proj p;
    p.s = (img - ssOrg) * ssScale;
    p.depth = depth;
    return p;
}

// Light rig (camera space): key light from the upper-left front.
const Vec3f kLight = norm(Vec3f(-0.4f, -0.55f, -0.8f));
const Vec3f kHalf = norm(kLight + Vec3f(0.f, 0.f, -1.f)); // view = -Z

// Rasterise one mesh into the supersampled layer with a shared z-buffer.
// `layer` is BGR float, `cover` marks written pixels, `zbuf` resolves occlusion.
void raster(const Mesh& m, const Pose& ps, float ssScale, cv::Point2f ssOrg,
            cv::Mat& layer, cv::Mat& cover, cv::Mat& zbuf) {
    const int W = layer.cols, H = layer.rows;
    std::vector<Vec3f> world(m.pos.size()), wnrm(m.nrm.size());
    std::vector<Proj> pr(m.pos.size());
    for (size_t i = 0; i < m.pos.size(); ++i) {
        world[i] = ps.R * m.pos[i];
        wnrm[i] = ps.R * m.nrm[i];
        pr[i] = project(ps, world[i], ssScale, ssOrg);
    }

    for (const auto& t : m.tri) {
        const Proj &A = pr[t[0]], &B = pr[t[1]], &C = pr[t[2]];
        // Signed area in screen space (y-down): >0 is a front face.
        float area = (B.s.x - A.s.x) * (C.s.y - A.s.y) -
                     (C.s.x - A.s.x) * (B.s.y - A.s.y);
        if (std::fabs(area) < 1e-4f) continue;
        bool back = area < 0.f;
        if (back && !m.doubleSided) continue; // cull for the closed snout

        int minx = (int)std::floor(std::min({A.s.x, B.s.x, C.s.x}));
        int maxx = (int)std::ceil(std::max({A.s.x, B.s.x, C.s.x}));
        int miny = (int)std::floor(std::min({A.s.y, B.s.y, C.s.y}));
        int maxy = (int)std::ceil(std::max({A.s.y, B.s.y, C.s.y}));
        minx = std::max(0, minx); miny = std::max(0, miny);
        maxx = std::min(W - 1, maxx); maxy = std::min(H - 1, maxy);
        float invArea = 1.f / area;

        for (int y = miny; y <= maxy; ++y) {
            float* zb = zbuf.ptr<float>(y);
            cv::Vec3f* lp = layer.ptr<cv::Vec3f>(y);
            float* cp = cover.ptr<float>(y);
            for (int x = minx; x <= maxx; ++x) {
                float px = x + 0.5f, py = y + 0.5f;
                // Barycentric weights.
                float w0 = ((B.s.x - px) * (C.s.y - py) -
                            (C.s.x - px) * (B.s.y - py)) * invArea;
                float w1 = ((C.s.x - px) * (A.s.y - py) -
                            (A.s.x - px) * (C.s.y - py)) * invArea;
                float w2 = 1.f - w0 - w1;
                if (w0 < 0.f || w1 < 0.f || w2 < 0.f) continue;

                float depth = w0 * A.depth + w1 * B.depth + w2 * C.depth;
                if (depth >= zb[x]) continue; // farther than what's there

                Vec3f n = norm(w0 * wnrm[t[0]] + w1 * wnrm[t[1]] + w2 * wnrm[t[2]]);
                if (n[2] > 0.f) n = -n; // face the camera (both sides / robustness)
                Vec3f base = w0 * m.col[t[0]] + w1 * m.col[t[1]] + w2 * m.col[t[2]];

                float diff = std::max(0.f, n.dot(kLight));
                float shade = m.ambient + (1.f - m.ambient) * diff;
                float s = m.spec * std::pow(std::max(0.f, n.dot(kHalf)), m.shin);
                Vec3f c = base * shade + Vec3f(255, 255, 255) * s;

                zb[x] = depth;
                lp[x] = cv::Vec3f(std::min(255.f, c[0]), std::min(255.f, c[1]),
                                  std::min(255.f, c[2]));
                cp[x] = 1.f;
            }
        }
    }
}

} // namespace

void render(cv::Mat& frame, const cv::Rect& face, bool hasEyes,
            cv::Point2f leftEye, cv::Point2f rightEye, double phase) {
    if (frame.empty() || frame.type() != CV_8UC3) return;
    if (face.width < 40 || face.height < 40) return;

    Pose ps = estimatePose(face, hasEyes, leftEye, rightEye);
    if (ps.scale < 8.f) return;

    float wig = 0.05f * std::sin((float)phase * 0.11f);
    std::vector<Mesh> meshes;
    meshes.push_back(buildEar(-1.f, wig));
    meshes.push_back(buildEar(+1.f, wig));
    meshes.push_back(buildSnout());
    meshes.push_back(buildNostrils());

    // Image-space bounding box of every projected vertex -> the region we touch.
    float minx = 1e9f, miny = 1e9f, maxx = -1e9f, maxy = -1e9f;
    for (const Mesh& m : meshes)
        for (const Vec3f& p : m.pos) {
            Proj pr = project(ps, ps.R * p, 1.f, cv::Point2f(0, 0));
            minx = std::min(minx, pr.s.x); maxx = std::max(maxx, pr.s.x);
            miny = std::min(miny, pr.s.y); maxy = std::max(maxy, pr.s.y);
        }
    cv::Rect roi((int)std::floor(minx) - 2, (int)std::floor(miny) - 2,
                 (int)std::ceil(maxx - minx) + 4, (int)std::ceil(maxy - miny) + 4);
    roi &= cv::Rect(0, 0, frame.cols, frame.rows);
    if (roi.width < 2 || roi.height < 2) return;

    // Supersampled layer, coverage mask and z-buffer over the ROI only.
    cv::Point2f org(roi.x, roi.y);
    int LW = roi.width * kSS, LH = roi.height * kSS;
    cv::Mat layer(LH, LW, CV_32FC3, cv::Scalar(0, 0, 0));
    cv::Mat cover(LH, LW, CV_32F, cv::Scalar(0));
    cv::Mat zbuf(LH, LW, CV_32F, cv::Scalar(1e9f));
    for (const Mesh& m : meshes)
        raster(m, ps, (float)kSS, org, layer, cover, zbuf);

    // Downsample (box filter) to resolve the supersampled edges, then composite
    // the pig over the frame ROI with the resulting per-pixel coverage.
    cv::Mat colDown, covDown;
    cv::resize(layer, colDown, cv::Size(roi.width, roi.height), 0, 0, cv::INTER_AREA);
    cv::resize(cover, covDown, cv::Size(roi.width, roi.height), 0, 0, cv::INTER_AREA);

    cv::Mat dst = frame(roi);
    for (int y = 0; y < roi.height; ++y) {
        cv::Vec3b* d = dst.ptr<cv::Vec3b>(y);
        const cv::Vec3f* c = colDown.ptr<cv::Vec3f>(y);
        const float* a = covDown.ptr<float>(y);
        for (int x = 0; x < roi.width; ++x) {
            float cov = a[x];
            if (cov <= 0.003f) continue;
            // colDown is the coverage-weighted colour sum from INTER_AREA, so the
            // lit colour is colDown/cover; composite that over the frame by cov.
            for (int k = 0; k < 3; ++k) {
                float src = c[x][k] / cov;
                d[x][k] = cv::saturate_cast<uchar>(src * cov + d[x][k] * (1.f - cov));
            }
        }
    }
}

} // namespace olc::pig3d
