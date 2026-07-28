#include "mp_landmarker.hpp"

#include <algorithm>
#include <iostream>

#include <opencv2/imgproc.hpp>

#include "mediapipe/framework/formats/image.h"
#include "mediapipe/framework/formats/image_frame.h"
#include "mediapipe/framework/formats/image_frame_opencv.h"
#include "mediapipe/tasks/cc/core/base_options.h"
#include "mediapipe/tasks/cc/vision/core/running_mode.h"
#include "mediapipe/tasks/cc/vision/face_landmarker/face_landmarker.h"
#include "mediapipe/tasks/cc/vision/face_landmarker/face_landmarker_result.h"

namespace olc {

namespace {

namespace mp = ::mediapipe;
using ::mediapipe::tasks::vision::face_landmarker::FaceLandmarker;
using ::mediapipe::tasks::vision::face_landmarker::FaceLandmarkerOptions;
using RunningMode = ::mediapipe::tasks::vision::core::RunningMode;

// Canonical MediaPipe Face Mesh vertex indices for the features the filters
// reshape. The mesh has 468 points; these are the stable, widely-documented
// landmarks for the mouth, eyes, inner brows and chin.
constexpr int kMouthLeft   = 61;
constexpr int kMouthRight  = 291;
constexpr int kMouthTop    = 13;  // upper inner lip centre
constexpr int kMouthBottom = 14;  // lower inner lip centre
// One eye is indexed by the 33/133/145 cluster, the other by 263/362/374. Which
// falls on the left of the *image* depends on head pose / mirroring, so we sort
// the two by x below rather than assuming.
constexpr int kEyeAOuter = 33,  kEyeAInner = 133, kEyeALower = 145;
constexpr int kEyeBOuter = 263, kEyeBInner = 362, kEyeBLower = 374;
constexpr int kBrowAInner = 55;   // inner brow tip above eye A
constexpr int kBrowBInner = 285;  // inner brow tip above eye B
constexpr int kChin = 152;

// Largest index we dereference; a partial result with fewer points is skipped.
constexpr int kMaxIndex = 374;

} // namespace

struct MpFaceLandmarker::Impl {
    std::unique_ptr<FaceLandmarker> landmarker;
};

MpFaceLandmarker::MpFaceLandmarker() : impl_(std::make_unique<Impl>()) {}
MpFaceLandmarker::~MpFaceLandmarker() = default;

std::unique_ptr<MpFaceLandmarker> MpFaceLandmarker::create(
        const std::string& modelPath, int maxFaces) {
    if (modelPath.empty()) return nullptr;

    auto options = std::make_unique<FaceLandmarkerOptions>();
    options->base_options.model_asset_path = modelPath;
    options->running_mode = RunningMode::VIDEO; // tracks between frames -> cheap
    options->num_faces = std::max(1, maxFaces);
    options->output_face_blendshapes = false;
    options->output_facial_transformation_matrixes = false;

    auto created = FaceLandmarker::Create(std::move(options));
    if (!created.ok()) {
        std::cerr << "filters: MediaPipe FaceLandmarker failed to load '"
                  << modelPath << "': " << created.status().message() << "\n";
        return nullptr;
    }

    std::unique_ptr<MpFaceLandmarker> self(new MpFaceLandmarker());
    self->impl_->landmarker = std::move(*created);
    std::cout << "filters: MediaPipe Face Mesh loaded (" << modelPath << ")\n";
    return self;
}

bool MpFaceLandmarker::detect(const cv::Mat& bgr, int64_t timestampMs,
                              float outW, float outH,
                              std::vector<FaceLandmarks>& out) {
    if (!impl_ || !impl_->landmarker || bgr.empty() || bgr.type() != CV_8UC3)
        return false;

    // MediaPipe wants an SRGB image; wrap an aligned frame and convert into it.
    auto frame = std::make_shared<mp::ImageFrame>(
        mp::ImageFormat::SRGB, bgr.cols, bgr.rows,
        mp::ImageFrame::kDefaultAlignmentBoundary);
    cv::Mat view = mp::formats::MatView(frame.get());
    cv::cvtColor(bgr, view, cv::COLOR_BGR2RGB);
    mp::Image image(std::move(frame));

    auto res = impl_->landmarker->DetectForVideo(image, timestampMs);
    if (!res.ok()) {
        std::cerr << "filters: MediaPipe detect error: "
                  << res.status().message() << "\n";
        return false;
    }

    const float W = outW, H = outH;
    for (const auto& face : res->face_landmarks) {
        const auto& lm = face.landmarks;
        if ((int)lm.size() <= kMaxIndex) continue; // not a full mesh

        auto pt = [&](int i) { return cv::Point2f(lm[i].x * W, lm[i].y * H); };

        FaceLandmarks L;
        L.mouthLeft   = pt(kMouthLeft);
        L.mouthRight  = pt(kMouthRight);
        L.mouthTop    = pt(kMouthTop);
        L.mouthBottom = pt(kMouthBottom);
        L.mouthCenter = (L.mouthTop + L.mouthBottom) * 0.5f;
        L.chin        = pt(kChin);

        // Assign the two eye clusters to image-left / image-right by x, so the
        // filter is correct regardless of head pose or a mirrored preview.
        cv::Point2f aC = (pt(kEyeAOuter) + pt(kEyeAInner)) * 0.5f;
        cv::Point2f bC = (pt(kEyeBOuter) + pt(kEyeBInner)) * 0.5f;
        const bool aLeft = aC.x <= bC.x;
        const cv::Point2f lC = aLeft ? aC : bC;
        const cv::Point2f rC = aLeft ? bC : aC;
        L.leftEye       = lC;
        L.rightEye      = rC;
        L.leftEyeOuter  = aLeft ? pt(kEyeAOuter) : pt(kEyeBOuter);
        L.rightEyeOuter = aLeft ? pt(kEyeBOuter) : pt(kEyeAOuter);
        L.leftTear      = aLeft ? pt(kEyeALower) : pt(kEyeBLower);
        L.rightTear     = aLeft ? pt(kEyeBLower) : pt(kEyeALower);
        L.browLeftInner  = aLeft ? pt(kBrowAInner) : pt(kBrowBInner);
        L.browRightInner = aLeft ? pt(kBrowBInner) : pt(kBrowAInner);

        // Tight face box from the mesh extent.
        float minx = 1e9f, miny = 1e9f, maxx = -1e9f, maxy = -1e9f;
        for (const auto& p : lm) {
            const float px = p.x * W, py = p.y * H;
            minx = std::min(minx, px); miny = std::min(miny, py);
            maxx = std::max(maxx, px); maxy = std::max(maxy, py);
        }
        cv::Rect box((int)minx, (int)miny, (int)(maxx - minx), (int)(maxy - miny));
        L.box = box & cv::Rect(0, 0, (int)outW, (int)outH);

        out.push_back(L);
    }
    return true;
}

} // namespace olc
