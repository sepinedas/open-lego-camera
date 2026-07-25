#include "dogface.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace olc {

namespace {

// ---------------------------------------------------------------------------
// Tiny 3D vector maths. The scene lives in "view space": +x right, +y down (to
// match image coordinates), +z toward the camera. The face plane is at z == 0
// and every asset protrudes toward the viewer (+z), so normals can be shaded
// directly against a fixed view direction of (0,0,1).
// ---------------------------------------------------------------------------
struct V3 {
    float x = 0, y = 0, z = 0;
};
inline V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline V3 operator*(V3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V3 cross(V3 a, V3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline V3 norm(V3 a) {
    float l = std::sqrt(dot(a, a));
    return l > 1e-8f ? a * (1.f / l) : V3{0, 0, 1};
}
inline float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }

// Rotate `p` about an axis-aligned pivot line. rotX turns in the y-z plane
// (used for the ear fold and hang), rotZ turns in the x-y plane (ear splay).
inline V3 rotX(V3 p, V3 pivot, float a) {
    float c = std::cos(a), s = std::sin(a);
    float dy = p.y - pivot.y, dz = p.z - pivot.z;
    return {p.x, pivot.y + dy * c - dz * s, pivot.z + dy * s + dz * c};
}
inline V3 rotZ(V3 p, V3 pivot, float a) {
    float c = std::cos(a), s = std::sin(a);
    float dx = p.x - pivot.x, dy = p.y - pivot.y;
    return {pivot.x + dx * c - dy * s, pivot.y + dx * s + dy * c, p.z};
}

// A surface material. Colours are BGR in 0..1 to match OpenCV frames.
struct Material {
    V3 base;         // diffuse albedo (BGR)
    float ks;        // specular strength (0 = matte, 1 = glossy/wet)
    float shin;      // specular exponent (higher = tighter highlight)
    float rim;       // fresnel rim-light amount (soft edge glow)
    bool twoSided;   // light thin flaps (ears, tongue) from both faces
};

// A triangle handed to the rasteriser, carrying per-vertex position + normal so
// shading is smooth (Phong) across the surface rather than faceted.
struct Tri {
    V3 p[3];
    V3 n[3];
    const Material* mat;
};

// A mesh under construction: positions + triangle indices. Smooth per-vertex
// normals are computed from the connectivity when it is flushed into the scene,
// which is what makes curved surfaces (muzzle, nose, tongue) shade smoothly.
struct Mesh {
    std::vector<V3> pos;
    std::vector<int> idx;
    void tri(int a, int b, int c) {
        idx.push_back(a);
        idx.push_back(b);
        idx.push_back(c);
    }
    // Stitch a regular (rows x cols) parametric grid into triangles.
    void grid(int rows, int cols, int base) {
        for (int i = 0; i < rows - 1; ++i)
            for (int j = 0; j < cols - 1; ++j) {
                int a = base + i * cols + j, b = a + 1;
                int c = a + cols, d = c + 1;
                tri(a, b, c);
                tri(b, d, c);
            }
    }
};

// Flush `m` into the scene as shaded triangles: compute area-weighted smooth
// vertex normals, then emit one Tri per face pointing at `mat`.
void addMesh(std::vector<Tri>& scene, const Mesh& m, const Material& mat) {
    std::vector<V3> nrm(m.pos.size(), V3{0, 0, 0});
    for (size_t t = 0; t + 2 < m.idx.size(); t += 3) {
        int a = m.idx[t], b = m.idx[t + 1], c = m.idx[t + 2];
        V3 fn = cross(m.pos[b] - m.pos[a], m.pos[c] - m.pos[a]); // area-weighted
        nrm[a] = nrm[a] + fn;
        nrm[b] = nrm[b] + fn;
        nrm[c] = nrm[c] + fn;
    }
    for (V3& n : nrm) n = norm(n);
    for (size_t t = 0; t + 2 < m.idx.size(); t += 3) {
        int a = m.idx[t], b = m.idx[t + 1], c = m.idx[t + 2];
        scene.push_back({{m.pos[a], m.pos[b], m.pos[c]},
                         {nrm[a], nrm[b], nrm[c]},
                         &mat});
    }
}

// --- asset generators ------------------------------------------------------

// A closed ellipsoid centred at `c` with radii `r`. Used for the muzzle bulge,
// the nose and the little inner-ear beads -- all the smooth rounded volumes.
Mesh ellipsoid(V3 c, V3 r, int nu = 16, int nv = 20) {
    Mesh m;
    const float PI = 3.14159265358979f;
    for (int i = 0; i <= nu; ++i) {
        float u = PI * (float)i / nu;          // polar angle (0..PI)
        float su = std::sin(u), cu = std::cos(u);
        for (int j = 0; j <= nv; ++j) {
            float v = 2 * PI * (float)j / nv;  // azimuth
            m.pos.push_back({c.x + r.x * su * std::cos(v),
                             c.y + r.y * cu,
                             c.z + r.z * su * std::sin(v)});
        }
    }
    m.grid(nu + 1, nv + 1, 0);
    return m;
}

// One floppy, folded ear. Built in a canonical (left-side) local frame -- origin
// at the ear root, +y running *down* the ear, +x across its width -- as a curved
// leaf whose lower half folds forward over a crease (the "folded ear" look). The
// ear then drapes down and slightly out/forward from the side of the head. The
// dangle is animated as a bend that grows toward the tip (`swayX` side-to-side,
// `swayZ` forward-back), so the ear jiggles like soft tissue -- the root barely
// moves while the tip swings -- instead of swinging as one rigid flap. Mirrored
// for the right side.
Mesh ear(V3 root, float len, float halfW, float splay, float lean, float swayX,
         float swayZ, int side, V3* innerAnchor = nullptr) {
    Mesh m;
    const float PI = 3.14159265358979f;
    const int N = 16, M = 10;               // length x width tessellation
    const float foldS = 0.50f;              // crease sits ~half-way down the ear
    const float foldA = 0.95f;              // fold angle (radians), forward
    V3 pivot{0, len * foldS, 0};
    for (int i = 0; i <= N; ++i) {
        float s = (float)i / N;
        float L = len * s;
        // Rounded leaf silhouette: wide, rounded shoulders near the root,
        // tapering to a soft point at the tip.
        float lobe = std::pow(std::sin(PI * clamp01(0.12f + 0.88f * s)), 0.72f);
        float w = halfW * lobe;
        // Soft-tissue bend profile: ~0 at the root, growing to 1 at the tip so
        // the swing accumulates down the ear (the tip leads, the base lags).
        float bend = s * s;
        for (int j = 0; j <= M; ++j) {
            float t = -1.f + 2.f * (float)j / M;
            // Convex cross-section so the ear has a smooth rounded surface.
            float z = 0.26f * halfW * (1.f - t * t);
            V3 p{t * w, L, z};
            // Fold the lower part forward over the crease (creates the fold).
            if (s > foldS) p = rotX(p, pivot, foldA);
            // Animated dangle, accumulated toward the tip.
            p = rotZ(p, {0, 0, 0}, swayX * bend);   // side-to-side swing
            p = rotX(p, {0, 0, 0}, swayZ * bend);   // gentle forward-back flutter
            m.pos.push_back(p);
        }
    }
    m.grid(N + 1, M + 1, 0);
    // Rest pose: splay the ear outward and lean it a touch forward so it drapes
    // down the side of the head, then mirror for the right ear and translate on.
    for (V3& p : m.pos) {
        p = rotX(p, {0, 0, 0}, lean);        // lean slightly toward the camera
        p = rotZ(p, {0, 0, 0}, splay);       // splay outward from the head
        p.x *= (float)side;                  // mirror for the right ear
        p = p + root;
    }
    // Anchor for the inner-ear patch: the centroid of the folded-over tip rows,
    // nudged toward the camera so the pink bead sits on the ear's front face.
    if (innerAnchor) {
        V3 sum{0, 0, 0};
        int cnt = 0;
        for (int i = (int)(0.60f * N); i <= (int)(0.86f * N); ++i)
            for (int j = 0; j <= M; ++j) {
                sum = sum + m.pos[i * (M + 1) + j];
                ++cnt;
            }
        *innerAnchor = sum * (1.f / std::max(1, cnt));
        innerAnchor->z += 0.02f * len;
    }
    return m;
}

// The lolling tongue: a soft rounded strap that hangs from under the muzzle,
// juts forward and curls down, with a shallow central groove. Only added when
// the mouth is open. `open` (0..1) scales how far it flops out.
Mesh tongue(V3 top, float len, float halfW, float open) {
    Mesh m;
    const float PI = 3.14159265358979f;
    const int N = 14, M = 10;
    float reach = 0.55f + 0.45f * open;      // droops further the wider you open
    for (int i = 0; i <= N; ++i) {
        float s = (float)i / N;
        // Round the tip: hold width, then taper over the last third.
        float taper = s < 0.7f ? 1.f : std::sqrt(std::max(0.f, 1.f - std::pow((s - 0.7f) / 0.3f, 2.f)));
        float w = halfW * (0.85f + 0.15f * std::sin(PI * s)) * taper;
        float y = top.y + len * s * reach;
        float fwd = top.z + 0.55f * halfW * std::sin(PI * 0.85f * s); // juts out then dips
        for (int j = 0; j <= M; ++j) {
            float t = -1.f + 2.f * (float)j / M;
            float groove = -0.22f * halfW * std::exp(-(t * t) / 0.12f); // central dip
            float dome = 0.10f * halfW * (1.f - t * t);                 // rounded body
            m.pos.push_back({top.x + t * w, y, fwd + dome + groove});
        }
    }
    m.grid(N + 1, M + 1, 0);
    return m;
}

// One whisker: a slender tapered 3-sided tube swept along a shallow arc, so it
// is real (shaded) geometry catching the light rather than a drawn line.
Mesh whisker(V3 rootP, V3 dir, float len, float droop, float r0) {
    Mesh m;
    const int SEG = 6, SIDES = 3;
    const float PI = 3.14159265358979f;
    dir = norm(dir);
    V3 up{0, 0, 1};
    V3 sidev = norm(cross(dir, up));
    V3 upv = norm(cross(sidev, dir));
    for (int i = 0; i <= SEG; ++i) {
        float s = (float)i / SEG;
        float r = r0 * (1.f - s);            // taper to a point
        // Arc: run along `dir`, sagging downward (droop) toward the tip.
        V3 c = rootP + dir * (len * s);
        c.y += droop * s * s;
        for (int k = 0; k < SIDES; ++k) {
            float a = 2 * PI * k / SIDES;
            m.pos.push_back(c + sidev * (r * std::cos(a)) + upv * (r * std::sin(a)));
        }
    }
    for (int i = 0; i < SEG; ++i)
        for (int k = 0; k < SIDES; ++k) {
            int a = i * SIDES + k, b = i * SIDES + (k + 1) % SIDES;
            int c = a + SIDES, d = b + SIDES;
            m.tri(a, b, c);
            m.tri(b, d, c);
        }
    return m;
}

// --- shading + rasterisation ----------------------------------------------

// Fixed key light coming from the upper-front-left, and the view direction.
const V3 kLight = norm({-0.45f, -0.55f, 0.80f});
const V3 kView = {0, 0, 1};

// Phong shade one surface point. Two-sided materials (thin flaps) flip the
// normal toward the viewer so the ear/tongue back also catches light.
V3 shade(V3 n, const Material& m) {
    n = norm(n);
    if (m.twoSided && dot(n, kView) < 0) n = n * -1.f;
    float ndl = std::max(0.f, dot(n, kLight));
    V3 h = norm(kLight + kView);
    float spec = m.ks * std::pow(std::max(0.f, dot(n, h)), m.shin);
    float rim = m.rim * std::pow(1.f - std::max(0.f, dot(n, kView)), 3.f);
    float amb = 0.34f, diff = 0.72f;
    V3 col = m.base * (amb + diff * ndl);
    col = col + V3{1, 1, 1} * spec + m.base * rim;
    return {clamp01(col.x), clamp01(col.y), clamp01(col.z)};
}

inline float edge(float ax, float ay, float bx, float by, float px, float py) {
    return (px - ax) * (by - ay) - (py - ay) * (bx - ax);
}

// Software rasteriser with 2x supersampling. Every triangle is Phong-shaded per
// (sub)pixel and depth-tested against a z-buffer, then the coverage buffer is
// box-downsampled so silhouettes come out smoothly anti-aliased before the
// result is alpha-composited onto the frame.
void rasterize(cv::Mat& bgr, const std::vector<Tri>& scene) {
    const int W = bgr.cols, H = bgr.rows;
    // Orthographic projection: the assets already live in image-pixel space, so
    // x/y map straight to the screen. The three-dimensional read comes from the
    // Phong shading and the z-buffer occlusion (near assets hiding far ones),
    // not from perspective foreshortening -- which keeps the rig rock-steady
    // and cheap for the Pi Zero.
    auto project = [&](V3 p, float& sx, float& sy) {
        sx = p.x;
        sy = p.y;
    };

    // Screen bounds of the whole rig.
    float minx = 1e9f, miny = 1e9f, maxx = -1e9f, maxy = -1e9f;
    for (const Tri& t : scene)
        for (int k = 0; k < 3; ++k) {
            float sx, sy;
            project(t.p[k], sx, sy);
            minx = std::min(minx, sx);
            miny = std::min(miny, sy);
            maxx = std::max(maxx, sx);
            maxy = std::max(maxy, sy);
        }
    int x0 = std::max(0, (int)std::floor(minx) - 1);
    int y0 = std::max(0, (int)std::floor(miny) - 1);
    int x1 = std::min(W, (int)std::ceil(maxx) + 1);
    int y1 = std::min(H, (int)std::ceil(maxy) + 1);
    if (x1 - x0 < 2 || y1 - y0 < 2) return;

    const int SS = 2;
    const int bw = (x1 - x0) * SS, bh = (y1 - y0) * SS;
    std::vector<float> zb(bw * bh, -1e30f);
    std::vector<float> cb(bw * bh * 3, 0.f);
    std::vector<unsigned char> cov(bw * bh, 0);

    for (const Tri& t : scene) {
        float sx[3], sy[3];
        for (int k = 0; k < 3; ++k) project(t.p[k], sx[k], sy[k]);
        // To supersample buffer space.
        float bx[3], by[3];
        for (int k = 0; k < 3; ++k) {
            bx[k] = (sx[k] - x0) * SS;
            by[k] = (sy[k] - y0) * SS;
        }
        float area = edge(bx[0], by[0], bx[1], by[1], bx[2], by[2]);
        if (std::fabs(area) < 1e-6f) continue;
        float inv = 1.f / area;
        int tminx = std::max(0, (int)std::floor(std::min({bx[0], bx[1], bx[2]})));
        int tmaxx = std::min(bw - 1, (int)std::ceil(std::max({bx[0], bx[1], bx[2]})));
        int tminy = std::max(0, (int)std::floor(std::min({by[0], by[1], by[2]})));
        int tmaxy = std::min(bh - 1, (int)std::ceil(std::max({by[0], by[1], by[2]})));
        for (int py = tminy; py <= tmaxy; ++py) {
            float fy = py + 0.5f;
            for (int px = tminx; px <= tmaxx; ++px) {
                float fx = px + 0.5f;
                float w0 = edge(bx[1], by[1], bx[2], by[2], fx, fy) * inv;
                float w1 = edge(bx[2], by[2], bx[0], by[0], fx, fy) * inv;
                float w2 = edge(bx[0], by[0], bx[1], by[1], fx, fy) * inv;
                if (w0 < 0 || w1 < 0 || w2 < 0) continue; // outside triangle
                float z = w0 * t.p[0].z + w1 * t.p[1].z + w2 * t.p[2].z;
                int idx = py * bw + px;
                if (z <= zb[idx]) continue;            // behind something nearer
                V3 n{w0 * t.n[0].x + w1 * t.n[1].x + w2 * t.n[2].x,
                     w0 * t.n[0].y + w1 * t.n[1].y + w2 * t.n[2].y,
                     w0 * t.n[0].z + w1 * t.n[1].z + w2 * t.n[2].z};
                V3 c = shade(n, *t.mat);
                zb[idx] = z;
                cb[idx * 3 + 0] = c.x;
                cb[idx * 3 + 1] = c.y;
                cb[idx * 3 + 2] = c.z;
                cov[idx] = 1;
            }
        }
    }

    // Downsample the SSxSS blocks and alpha-composite onto the frame.
    const float invSS2 = 1.f / (SS * SS);
    for (int y = y0; y < y1; ++y) {
        cv::Vec3b* row = bgr.ptr<cv::Vec3b>(y);
        for (int x = x0; x < x1; ++x) {
            int cnt = 0;
            float b = 0, g = 0, r = 0;
            for (int sy2 = 0; sy2 < SS; ++sy2)
                for (int sx2 = 0; sx2 < SS; ++sx2) {
                    int bxp = (x - x0) * SS + sx2, byp = (y - y0) * SS + sy2;
                    int idx = byp * bw + bxp;
                    if (!cov[idx]) continue;
                    ++cnt;
                    b += cb[idx * 3 + 0];
                    g += cb[idx * 3 + 1];
                    r += cb[idx * 3 + 2];
                }
            if (!cnt) continue;
            float a = cnt * invSS2;           // fractional coverage -> soft edge
            float ib = b / cnt * 255.f, ig = g / cnt * 255.f, ir = r / cnt * 255.f;
            cv::Vec3b& px = row[x];
            px[0] = (unsigned char)(px[0] * (1.f - a) + ib * a + 0.5f);
            px[1] = (unsigned char)(px[1] * (1.f - a) + ig * a + 0.5f);
            px[2] = (unsigned char)(px[2] * (1.f - a) + ir * a + 0.5f);
        }
    }
}

} // namespace

void renderDogFace(cv::Mat& bgr, const cv::Rect& face, float tongueScore,
                   float roll, double phase) {
    if (bgr.empty() || bgr.type() != CV_8UC3) return;
    const float fx = face.x, fy = face.y, fw = face.width, fh = face.height;
    const float cx = fx + 0.5f * fw;
    const float U = fw;                       // one unit == face width

    // Approximate landmark anchors derived from the face box. The muzzle sits
    // low over the nose/mouth (roughly the lower third of the face); the ears
    // are rooted *outside* the face box so they frame the head instead of
    // covering the eyes.
    const float noseY = fy + 0.62f * fh;      // over the wearer's nose
    const float muzzleY = fy + 0.74f * fh;    // muzzle centred low, over the mouth
    const float mouthY = fy + 0.88f * fh;
    const float earY = fy - 0.05f * fh;        // ear roots just above the head

    // Materials (BGR albedo). A warm tan coat with darker ears, a pink inner
    // ear, a wet black nose, a glossy tongue and pale whiskers.
    static const Material furSnout{{0.44f, 0.64f, 0.84f}, 0.16f, 14.f, 0.16f, false};
    static const Material furEar{{0.24f, 0.38f, 0.55f}, 0.10f, 10.f, 0.12f, true};
    static const Material innerEar{{0.60f, 0.66f, 0.93f}, 0.14f, 12.f, 0.10f, true};
    static const Material noseMat{{0.07f, 0.07f, 0.09f}, 0.85f, 48.f, 0.22f, false};
    static const Material tongueMat{{0.48f, 0.46f, 0.95f}, 0.55f, 26.f, 0.14f, true};
    static const Material whiskerMat{{0.88f, 0.90f, 0.93f}, 0.20f, 18.f, 0.05f, false};

    std::vector<Tri> scene;
    scene.reserve(4096);

    // Muzzle: a smooth oval snout over the nose/mouth, a touch taller than wide.
    addMesh(scene, ellipsoid({cx, muzzleY, 0.11f * U},
                             {0.20f * U, 0.22f * U, 0.17f * U}, 18, 24),
            furSnout);

    // Nose: a glossy black bulb at the front-top of the muzzle, wider than tall.
    addMesh(scene, ellipsoid({cx, noseY, 0.29f * U},
                             {0.105f * U, 0.078f * U, 0.085f * U}, 14, 18),
            noseMat);

    // Whiskers: three per side, fanning out and drooping from the muzzle sides.
    for (int side = -1; side <= 1; side += 2) {
        V3 rootP{cx + side * 0.14f * U, muzzleY, 0.15f * U};
        const float ang[3] = {-0.16f, 0.10f, 0.36f};   // slight up / mid / down fan
        for (int k = 0; k < 3; ++k) {
            V3 dir{(float)side * std::cos(ang[k]), std::sin(ang[k]), 0.05f};
            addMesh(scene, whisker(rootP, dir, 0.32f * U, 0.05f * U, 0.013f * U),
                    whiskerMat);
        }
    }

    // Ears: folded floppy ears rooted just outside the top corners of the face
    // and splayed further outward, so they drape down *beside* the head rather
    // than over it. Each ear dangles with an organic multi-sine idle motion -- a
    // slow primary swing plus a faster, smaller flutter -- and the two ears run
    // on offset phases so they never swing in lockstep (natural soft tissue).
    const double t = phase * 0.10;
    for (int side = -1; side <= 1; side += 2) {
        double ph = t + (side > 0 ? 1.9 : 0.0);   // desync the two ears
        float swayX = 0.13f * (float)(std::sin(ph) + 0.32 * std::sin(2.3 * ph + 0.7));
        float swayZ = 0.06f * (float)std::sin(1.7 * ph + 0.5);
        V3 root{cx + side * 0.54f * U, earY, -0.03f * U};
        V3 innerAt;
        Mesh e = ear(root, 0.66f * U, 0.17f * U, 0.52f, 0.14f, swayX, swayZ, side,
                     &innerAt);
        addMesh(scene, e, furEar);
        // Inner-ear patch nestled on the folded-forward tip of the ear itself
        // (anchored to the ear geometry so it can never drift onto the cheek).
        addMesh(scene, ellipsoid(innerAt, {0.07f * U, 0.11f * U, 0.05f * U}, 12, 14),
                innerEar);
    }

    // Tongue: only lolls out when the wearer is actually sticking their tongue
    // out (see FaceFilter::tongueOut), not merely opening their mouth.
    if (tongueScore > 0.5f) {
        float out = clamp01((tongueScore - 0.5f) / 0.5f);
        addMesh(scene, tongue({cx, mouthY, 0.12f * U}, 0.34f * U, 0.11f * U, out),
                tongueMat);
    }

    // Track the head's roll: rotate the whole rig in the image plane about the
    // face centre so the ears and muzzle stay aligned with a tilted head.
    if (std::fabs(roll) > 1e-3f) {
        const V3 pv{cx, fy + 0.5f * fh, 0};
        const float cr = std::cos(roll), sr = std::sin(roll);
        for (Tri& tr : scene)
            for (int k = 0; k < 3; ++k) {
                float dx = tr.p[k].x - pv.x, dy = tr.p[k].y - pv.y;
                tr.p[k].x = pv.x + dx * cr - dy * sr;
                tr.p[k].y = pv.y + dx * sr + dy * cr;
                float nx = tr.n[k].x, ny = tr.n[k].y;   // rotate normals too
                tr.n[k].x = nx * cr - ny * sr;
                tr.n[k].y = nx * sr + ny * cr;
            }
    }

    rasterize(bgr, scene);
}

} // namespace olc
