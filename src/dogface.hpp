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
//   mouthOpen  0..1 estimate of how open the user's own mouth is. The tongue
//              only lolls out when the mouth is open (>~0.4), matching the
//              WhatsApp/Snapchat dog filter behaviour.
//   phase      free-running per-frame counter; drives a subtle idle ear sway.
void renderDogFace(cv::Mat& bgr, const cv::Rect& face, float mouthOpen,
                   double phase);

} // namespace olc
