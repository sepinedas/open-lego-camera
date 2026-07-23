#pragma once

// Shared enums and small value types used across the UI and app modules.

namespace olc {

// The high-level screen the app is currently on.
enum class Mode {
    Camera,        // live preview + capture controls
    Gallery,       // browse captured photos/videos
    Playback,      // playing a video from the gallery
    ConfirmDelete, // icon-only yes/no before deleting
};

// Every tappable control maps to exactly one action.
enum class Action {
    None,
    Shutter,     // take a photo
    Record,      // start/stop video (with audio when available)
    ZoomIn,
    ZoomOut,
    OpenGallery, // camera -> gallery
    CycleFilter, // step through the face filters (none -> smile -> cry -> ...)
    Back,        // gallery -> camera
    Prev,        // previous item in gallery
    Next,        // next item in gallery
    Play,        // play the selected video
    Delete,      // ask to delete the selected item
    ConfirmYes,  // confirm deletion
    ConfirmNo,   // cancel deletion
    Quit,
};

// A live, WhatsApp-style face filter, drawn on top of the preview and baked into
// captured photos/videos. Faces are found with a Haar cascade (see filters.cpp).
enum class Filter {
    None,   // no overlay
    Smile,  // a big grin that bares big teeth when the mouth opens
    Cry,    // a sad face with animated tears running down the cheeks
};

// The next filter in the cycle used by the on-screen filter button.
inline Filter nextFilter(Filter f) {
    switch (f) {
        case Filter::None:  return Filter::Smile;
        case Filter::Smile: return Filter::Cry;
        case Filter::Cry:   return Filter::None;
    }
    return Filter::None;
}

} // namespace olc
