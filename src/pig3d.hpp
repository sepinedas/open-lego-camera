#pragma once

#include <opencv2/core.hpp>

// A tiny software 3D renderer for the pig-face filter.
//
// Rather than pasting flat sprites over the face, this builds real 3D meshes for
// the pig's ears and snout, estimates the head's pose (roll / yaw / pitch) from
// the face landmarks, and renders the meshes through a perspective camera with a
// z-buffer, per-pixel (Gouraud) shading and supersampled anti-aliasing. Because
// the geometry is genuinely three-dimensional, the snout protrudes and
// foreshortens, and the ears swing around and occlude behind the head exactly as
// the head turns -- they share the face's orientation and perspective instead of
// looking like decals.
namespace olc::pig3d {

// Draw the 3D pig over `frame` (BGR, 8-bit) for one face.
//
//   face      face bounding box, image coords (roi-local when called per-region)
//   hasEyes   whether the eye landmarks are valid (else pose falls back to the
//             box: front-facing, upright)
//   leftEye   image-left eye centre (only read when hasEyes)
//   rightEye  image-right eye centre (only read when hasEyes)
//   phase     free-running frame counter; drives a subtle ear wiggle
void render(cv::Mat& frame, const cv::Rect& face, bool hasEyes,
            cv::Point2f leftEye, cv::Point2f rightEye, double phase);

} // namespace olc::pig3d
