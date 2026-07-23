#pragma once

#include <SDL2/SDL.h>

#include "types.hpp"

namespace olc {

// Draws the icon for `action` centred at (cx, cy) inside a circle of radius r.
// `alpha` is the current menu-fade opacity (0..255) and modulates every icon so
// the whole menu fades as one. `recording` swaps the Record icon between the
// idle dot and the stop square. All icons are pure vector shapes, no text.
void drawIcon(SDL_Renderer* ren, Action action, int cx, int cy, int r,
              Uint8 alpha, bool recording);

// Draws the face-filter button glyph: a little face whose expression reflects
// the active `filter` (neutral, a big grin, or a tearful frown). Centred at
// (cx, cy) inside a circle of radius r; `alpha` modulates it with the menu fade.
void drawFilterIcon(SDL_Renderer* ren, Filter filter, int cx, int cy, int r,
                    Uint8 alpha);

} // namespace olc
