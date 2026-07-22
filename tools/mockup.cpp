// Offscreen UI mockup: renders each menu mode to a PNG using the real ui.cpp /
// icons.cpp code, so the icons can be eyeballed without a Pi or a display.
// Not part of the app build; see the comment at the bottom for the compile line.
#include <SDL2/SDL.h>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>

#include "../src/ui.hpp"
#include "../src/icons.hpp"

using namespace olc;

static cv::Mat surfaceToMat(SDL_Surface* s) {
    cv::Mat m(s->h, s->w, CV_8UC4, s->pixels, s->pitch);
    cv::Mat bgr;
    cv::cvtColor(m, bgr, cv::COLOR_BGRA2BGR); // ARGB8888 little-endian -> BGRA bytes
    return bgr.clone();
}

// Fake a camera preview so the translucency is visible.
static void drawFakePreview(SDL_Renderer* r, int w, int h) {
    for (int y = 0; y < h; ++y) {
        Uint8 v = (Uint8)(40 + 120 * y / h);
        SDL_SetRenderDrawColor(r, v / 2, v, (Uint8)(200 - v / 2), 255);
        SDL_RenderDrawLine(r, 0, y, w, y);
    }
    SDL_SetRenderDrawColor(r, 230, 180, 60, 255);
    SDL_Rect box{w / 6, h / 5, w / 5, h / 4};
    SDL_RenderFillRect(r, &box);
}

static cv::Mat renderMode(Mode mode, int w, int h, bool hasVideo, bool recording) {
    SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
    SDL_Renderer* r = SDL_CreateSoftwareRenderer(surf);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);

    drawFakePreview(r, w, h);
    if (mode == Mode::ConfirmDelete)
        SDL_SetRenderDrawColor(r, 0, 0, 0, 120), SDL_RenderFillRect(r, nullptr);

    Menu menu;
    menu.wake();
    for (const auto& b : menu.layout(mode, w, h, hasVideo))
        Menu::drawButton(r, b, mode == Mode::ConfirmDelete ? 255 : menu.alpha(), recording);

    cv::Mat out = surfaceToMat(surf);
    SDL_DestroyRenderer(r);
    SDL_FreeSurface(surf);
    return out;
}

int main() {
    setenv("SDL_VIDEODRIVER", "dummy", 1);
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { SDL_Log("init: %s", SDL_GetError()); return 1; }
    const int W = 800, H = 480; // a common Pi touchscreen resolution

    cv::Mat cam = renderMode(Mode::Camera, W, H, false, false);
    cv::Mat rec = renderMode(Mode::Camera, W, H, false, true);
    cv::Mat gal = renderMode(Mode::Gallery, W, H, true, false);
    cv::Mat con = renderMode(Mode::ConfirmDelete, W, H, false, false);

    auto label = [](cv::Mat& m, const std::string& t) {
        cv::putText(m, t, {16, 34}, cv::FONT_HERSHEY_SIMPLEX, 0.7, {255, 255, 255}, 2);
    };
    label(cam, "CAMERA");
    label(rec, "RECORDING");
    label(gal, "GALLERY (video)");
    label(con, "CONFIRM DELETE");

    cv::Mat top, bot, sheet;
    cv::hconcat(cam, rec, top);
    cv::hconcat(gal, con, bot);
    cv::vconcat(top, bot, sheet);
    cv::imwrite("/home/user/open-lego-camera-cpp/build/ui-mockup.png", sheet);
    SDL_Quit();
    return 0;
}
