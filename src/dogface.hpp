#pragma once

#include <opencv2/core.hpp>

namespace olc {

// Render smooth-shaded 3D dog-face assets onto `bgr` for the face at `face`
// (given in `bgr`-local pixel coords). The assets are real geometry -- folded
// floppy ears, a rounded muzzle, a glossy black nose, whiskers and a lolling
// tongue -- lit by a small software 3D pipeline (Phong shading over supersampled
// coverage), so they read as three-dimensional objects sitting on the face
// rather than flat 2D stickers pasted on top.
//
// The rig is anchored to a handful of landmark points derived from the detected
// face box (ear roots, muzzle centre, nose, mouth line). The Pi-Zero target
// deliberately avoids a heavyweight FaceMesh model -- see filters.hpp -- so we
// place the 3D assets on this lightweight approximate landmark frame.
//
//   tongue     0..1 estimate that the user is *sticking their tongue out*. The
//              dog tongue only lolls out to match -- an open mouth alone is not
//              enough, so a plain smile or an "aah" keeps the tongue in.
//   roll       head-roll angle in radians (from the eye line); the whole rig is
//              rotated by it so the ears/muzzle track a tilted head.
//   phase      free-running per-frame counter; drives a subtle idle ear sway.
void renderDogFace(cv::Mat& bgr, const cv::Rect& face, float tongue, float roll,
                   double phase);

} // namespace olc
