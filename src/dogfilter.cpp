#include "dogfilter.hpp"

#include <algorithm>
#include <cfloat>
#include <cmath>

#include <opencv2/calib3d.hpp>

namespace olc {

namespace {

// Rotation from Euler angles (degrees), applied X then Y then Z.
cv::Matx33d euler(double xd, double yd, double zd) {
    double x = xd * CV_PI / 180, y = yd * CV_PI / 180, z = zd * CV_PI / 180;
    cv::Matx33d Rx(1, 0, 0, 0, std::cos(x), -std::sin(x), 0, std::sin(x), std::cos(x));
    cv::Matx33d Ry(std::cos(y), 0, std::sin(y), 0, 1, 0, -std::sin(y), 0, std::cos(y));
    cv::Matx33d Rz(std::cos(z), -std::sin(z), 0, std::sin(z), std::cos(z), 0, 0, 0, 1);
    return Rz * Ry * Rx;
}

// BGR colours.
const cv::Vec3b kEar{55, 80, 120};      // brown (outer ear)
const cv::Vec3b kEarFold{42, 62, 98};   // darker brown (folded-over tip)
const cv::Vec3b kMuzzle{175, 205, 230}; // light tan
const cv::Vec3b kNose{35, 32, 38};      // near-black
const cv::Vec3b kTongue{110, 110, 235}; // pink-red
const cv::Vec3b kWhisker{232, 236, 240};// off-white

// Canonical 3D face model (arbitrary but self-consistent units) for the six
// pose landmarks. +Y up, +Z toward the front of the face.
const std::vector<cv::Point3f>& faceModel() {
    static const std::vector<cv::Point3f> m = {
        {0.0f, 0.0f, 0.0f},          // 30 nose tip
        {0.0f, -330.0f, -65.0f},     // 8  chin
        {-225.0f, 170.0f, -135.0f},  // 36 left eye, outer corner
        {225.0f, 170.0f, -135.0f},   // 45 right eye, outer corner
        {-150.0f, -150.0f, -125.0f}, // 48 left mouth corner
        {150.0f, -150.0f, -125.0f}   // 54 right mouth corner
    };
    return m;
}
std::vector<cv::Point2f> faceImagePts(const std::vector<cv::Point2f>& lm) {
    return {lm[30], lm[8], lm[36], lm[45], lm[48], lm[54]};
}
cv::Matx33d intrinsics(cv::Size img) {
    double f = std::max(img.width, img.height);
    return cv::Matx33d(f, 0, img.width / 2.0, 0, f, img.height / 2.0, 0, 0, 1);
}

} // namespace

DogFilter::DogFilter() {
    // Face-local frame (same units/orientation as the solvePnP model below):
    // +X right, +Y up, +Z out of the face toward the camera. Eyes sit near
    // y=+170, x=+/-225; the face is ~450 wide.

    // Muzzle: a rounded snout protruding forward over the nose/mouth.
    parts_.push_back({makeEllipsoid(105, 90, 118, kMuzzle),
                      euler(0, 0, 0), cv::Vec3d(0, -75, 100)});

    // Nose: dark blob on the front tip of the muzzle.
    parts_.push_back({makeEllipsoid(56, 47, 47, kNose),
                      euler(0, 0, 0), cv::Vec3d(0, -45, 205)});

    // Folded ears: an upright base segment near the top of the head, plus a tip
    // segment that folds forward and down over it (the classic folded-ear look).
    // Right (+X) then left (-X).
    parts_.push_back({makeEllipsoid(58, 92, 40, kEar),
                      euler(6, 0, 26), cv::Vec3d(248, 300, -10)});     // base
    parts_.push_back({makeEllipsoid(54, 86, 34, kEarFold),
                      euler(64, 0, 18), cv::Vec3d(250, 250, 78)});     // folded tip
    parts_.push_back({makeEllipsoid(58, 92, 40, kEar),
                      euler(6, 0, -26), cv::Vec3d(-248, 300, -10)});
    parts_.push_back({makeEllipsoid(54, 86, 34, kEarFold),
                      euler(64, 0, -18), cv::Vec3d(-250, 250, 78)});

    // Whiskers: thin off-white spindles fanning out from each side of the muzzle.
    Mesh whisker = makeEllipsoid(98, 4.5f, 4.5f, kWhisker, 6, 8);
    for (int side : {-1, 1}) {
        for (int k = -1; k <= 1; ++k) {
            double fan = k * 15.0;                          // up/down spread
            cv::Matx33d rot = euler(0, -side * 24.0, side * fan); // tip forward
            cv::Vec3d off(side * 96.0, -66.0 - k * 4.0, 150.0);
            parts_.push_back({whisker, rot, off});
        }
    }

    // Tongue: pink, flattened, hanging below the muzzle; extends when mouth opens.
    tongueBase_ = cv::Vec3d(0, -205, 150);
    parts_.push_back({makeEllipsoid(46, 108, 20, kTongue),
                      euler(18, 0, 0), tongueBase_});
    tongueIdx_ = parts_.size() - 1;
}

double DogFilter::mouthOpen(const std::vector<cv::Point2f>& lm) {
    if (lm.size() < 68) return 0.0;
    double gap = cv::norm(lm[66] - lm[62]);       // inner top<->bottom lip
    double eye = cv::norm(lm[45] - lm[36]);        // outer eye span (scale ref)
    if (eye < 1e-3) return 0.0;
    double r = gap / eye;                          // ~0 closed, ~0.4 wide open
    return std::max(0.0, std::min(1.0, (r - 0.06) / 0.30));
}

bool DogFilter::estimatePose(const std::vector<cv::Point2f>& lm, cv::Size img,
                             cv::Matx33d& R, cv::Vec3d& t, cv::Matx33d& K) {
    if (lm.size() < 68) return false;
    K = intrinsics(img);
    cv::Mat dist = cv::Mat::zeros(4, 1, CV_64F);
    cv::Mat rvec, tvec;
    if (!cv::solvePnP(faceModel(), faceImagePts(lm), cv::Mat(K), dist, rvec, tvec,
                      false, cv::SOLVEPNP_EPNP))
        return false;
    cv::Mat Rm;
    cv::Rodrigues(rvec, Rm);
    R = cv::Matx33d((double*)Rm.ptr<double>());
    t = cv::Vec3d(tvec.ptr<double>()[0], tvec.ptr<double>()[1],
                  tvec.ptr<double>()[2]);
    return true;
}

void DogFilter::render(cv::Mat& frame, const cv::Matx33d& R, const cv::Vec3d& t,
                       const cv::Matx33d& K, double open) {
    // Animate the tongue: drop and lengthen it as the mouth opens.
    open = std::max(0.0, std::min(1.0, open));
    parts_[tongueIdx_].offset =
        tongueBase_ + cv::Vec3d(0, -95.0 * open, 25.0 * open);

    // One shared depth buffer so the assets occlude each other correctly.
    if (zbuf_.size() != frame.size())
        zbuf_.create(frame.size(), CV_32F);
    zbuf_.setTo(FLT_MAX);

    for (const auto& p : parts_) {
        cv::Matx33d Rtot = R * p.rot;
        cv::Vec3d ttot = R * p.offset + t;
        renderMesh(frame, zbuf_, p.mesh, Rtot, ttot, K, light_);
    }
}

// Blend the pose toward the previous frame's, except on a large jump (which
// means we just re-acquired the face somewhere new).
void DogFilter::smoothPose(cv::Vec3d& rvec, cv::Vec3d& tvec) {
    if (!hasPose_) {
        prevRvec_ = rvec;
        prevTvec_ = tvec;
        hasPose_ = true;
        return;
    }
    double tref = std::max(1.0, cv::norm(prevTvec_));
    bool jump = cv::norm(tvec - prevTvec_) > 0.4 * tref ||
                cv::norm(rvec - prevRvec_) > 0.6; // ~34 degrees
    if (!jump) {
        const double aR = 0.35, aT = 0.4; // new-sample weight (lower = steadier)
        rvec = aR * rvec + (1.0 - aR) * prevRvec_;
        tvec = aT * tvec + (1.0 - aT) * prevTvec_;
    }
    prevRvec_ = rvec;
    prevTvec_ = tvec;
}

bool DogFilter::apply(cv::Mat& frame, const std::vector<cv::Point2f>& landmarks) {
    if (landmarks.size() < 68) return false;
    const auto& lm = landmarks;
    cv::Matx33d K = intrinsics(frame.size());
    cv::Mat dist = cv::Mat::zeros(4, 1, CV_64F);
    const auto& obj = faceModel();
    std::vector<cv::Point2f> imgPts = faceImagePts(lm);

    // Seeding solvePnP with the previous pose keeps each solve near the last
    // one. Without it, the six-point problem is two-fold ambiguous and flips
    // between frames -- which is what makes the model "fly around" and jump in
    // size. On the first frame we use EPnP (stable without a guess).
    // Use the previous pose as a guess while it's still trustworthy; after a run
    // of rejected solves (e.g. the face moved during a tracking loss) fall back
    // to a guess-free EPnP re-init so we don't stay stuck on a stale pose.
    bool useGuess = hasPose_ && badStreak_ < 5;
    cv::Mat rvec, tvec;
    bool ok;
    if (useGuess) {
        rvec = (cv::Mat_<double>(3, 1) << prevRvec_[0], prevRvec_[1], prevRvec_[2]);
        tvec = (cv::Mat_<double>(3, 1) << prevTvec_[0], prevTvec_[1], prevTvec_[2]);
        ok = cv::solvePnP(obj, imgPts, cv::Mat(K), dist, rvec, tvec, true,
                          cv::SOLVEPNP_ITERATIVE);
    } else {
        ok = cv::solvePnP(obj, imgPts, cv::Mat(K), dist, rvec, tvec, false,
                          cv::SOLVEPNP_EPNP);
    }

    cv::Vec3d rv, tv;
    bool bad = !ok;
    if (ok) {
        rv = cv::Vec3d(rvec.ptr<double>()[0], rvec.ptr<double>()[1], rvec.ptr<double>()[2]);
        tv = cv::Vec3d(tvec.ptr<double>()[0], tvec.ptr<double>()[1], tvec.ptr<double>()[2]);
        // Reject implausible solves (behind camera / non-finite / high
        // reprojection error) instead of letting them fling the model around.
        std::vector<cv::Point2f> proj;
        cv::projectPoints(obj, rvec, tvec, cv::Mat(K), dist, proj);
        double err = 0;
        for (size_t i = 0; i < proj.size(); ++i) err += cv::norm(proj[i] - imgPts[i]);
        err /= proj.size();
        double eyeSpan = std::max(1.0, cv::norm(imgPts[2] - imgPts[3]));
        if (!std::isfinite(tv[0]) || !std::isfinite(tv[2]) || tv[2] <= 1e-3 ||
            err > 0.35 * eyeSpan)
            bad = true;
    }

    if (bad) {
        ++badStreak_;
        if (!hasPose_) return false; // nothing good to fall back to
        rv = prevRvec_;              // hold the last good pose (don't fly)
        tv = prevTvec_;
    } else {
        badStreak_ = 0;
    }

    smoothPose(rv, tv); // temporal smoothing on top

    cv::Mat rm;
    cv::Rodrigues(cv::Mat(rv), rm);
    cv::Matx33d R((double*)rm.ptr<double>());
    render(frame, R, tv, K, mouthOpen(lm));
    return true;
}

} // namespace olc
