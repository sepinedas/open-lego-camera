// Visual check for the new UX features: thumbnail gallery button, pinch zoom
// factor label, shutter flash, and the gallery timestamp bar. Reuses ui.cpp /
// icons.cpp; the text + thumbnail drawing mirror app.cpp.
#include <SDL2/SDL.h>
#include <SDL2/SDL2_gfxPrimitives.h>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>

#include "../src/ui.hpp"
#include "../src/icons.hpp"
using namespace olc;

static void drawText(SDL_Renderer* r, int x, int topY, const std::string& s,
                     int scale, SDL_Color c, bool center) {
    int w = 8 * (int)s.size(), h = 8;
    SDL_Texture* prev = SDL_GetRenderTarget(r);
    SDL_Texture* t = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, w, h);
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    SDL_SetRenderTarget(r, t);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 0);
    SDL_RenderClear(r);
    stringRGBA(r, 0, 0, s.c_str(), c.r, c.g, c.b, c.a);
    SDL_SetRenderTarget(r, prev);
    SDL_Rect dst{center ? x - (w * scale) / 2 : x, topY, w * scale, h * scale};
    SDL_RenderCopy(r, t, nullptr, &dst);
    SDL_DestroyTexture(t);
}

static void fakePreview(SDL_Renderer* r, int w, int h, Uint8 tintR, Uint8 tintB) {
    for (int y = 0; y < h; ++y) {
        Uint8 v = (Uint8)(40 + 120 * y / h);
        SDL_SetRenderDrawColor(r, tintR ? tintR : v / 2, v, tintB ? tintB : (Uint8)(200 - v / 2), 255);
        SDL_RenderDrawLine(r, 0, y, w, y);
    }
}

static SDL_Texture* fakeThumb(SDL_Renderer* r) {
    cv::Mat m(128, 128, CV_8UC3);
    for (int y = 0; y < 128; ++y)
        for (int x = 0; x < 128; ++x)
            m.at<cv::Vec3b>(y, x) = cv::Vec3b((uchar)(x * 2), (uchar)(y * 2), 160);
    cv::circle(m, {64, 54}, 22, {40, 210, 240}, -1);
    SDL_Texture* t = SDL_CreateTexture(r, SDL_PIXELFORMAT_BGR24, SDL_TEXTUREACCESS_STATIC, 128, 128);
    SDL_UpdateTexture(t, nullptr, m.data, (int)m.step);
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    return t;
}

static void drawGalleryButton(SDL_Renderer* r, const Button& b, Uint8 alpha, SDL_Texture* thumb) {
    int s = b.r;
    roundedBoxRGBA(r, b.cx - s, b.cy - s, b.cx + s, b.cy + s, 7, 18, 18, 24, (Uint8)(alpha * 42 / 100));
    SDL_SetTextureAlphaMod(thumb, alpha);
    SDL_Rect dst{b.cx - s + 3, b.cy - s + 3, 2 * s - 6, 2 * s - 6};
    SDL_RenderCopy(r, thumb, nullptr, &dst);
    roundedRectangleRGBA(r, b.cx - s, b.cy - s, b.cx + s, b.cy + s, 7, 255, 255, 255, (Uint8)(alpha * 55 / 100));
}

static cv::Mat toMat(SDL_Surface* s) {
    cv::Mat m(s->h, s->w, CV_8UC4, s->pixels, s->pitch), bgr;
    cv::cvtColor(m, bgr, cv::COLOR_BGRA2BGR);
    return bgr.clone();
}

static cv::Mat cameraScene(int W, int H, SDL_Texture* thumb) {
    SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormat(0, W, H, 32, SDL_PIXELFORMAT_ARGB8888);
    SDL_Renderer* r = SDL_CreateSoftwareRenderer(surf);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    fakePreview(r, W, H, 0, 0);

    Menu menu; menu.wake();
    for (const auto& b : menu.layout(Mode::Camera, W, H, false)) {
        if (b.action == Action::OpenGallery) drawGalleryButton(r, b, 255, thumb);
        else Menu::drawButton(r, b, 255, false);
    }
    // zoom label
    const char* z = "2.0x";
    int scale = std::max(2, H / 160);
    int tw = 8 * 4 * scale, pad = 8 * scale / 2;
    roundedBoxRGBA(r, W / 2 - tw / 2 - pad, 12, W / 2 + tw / 2 + pad, 12 + 8 * scale + pad, 6, 0, 0, 0, 110);
    drawText(r, W / 2, 12 + pad / 2, z, scale, {255, 255, 255, 235}, true);
    // shutter flash (mid-animation)
    boxRGBA(r, 0, 0, W, H, 255, 255, 255, 110);

    cv::Mat out = toMat(surf);
    SDL_DestroyRenderer(r); SDL_FreeSurface(surf);
    return out;
}

static cv::Mat galleryScene(int W, int H) {
    SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormat(0, W, H, 32, SDL_PIXELFORMAT_ARGB8888);
    SDL_Renderer* r = SDL_CreateSoftwareRenderer(surf);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    fakePreview(r, W, H, 60, 90);

    std::string ts = "2026-07-23  14:35:12";
    int scale = std::max(2, H / 200);
    int barH = 8 * scale + 20;
    boxRGBA(r, 0, 0, W, barH, 0, 0, 0, 105);
    drawText(r, W / 2, (barH - 8 * scale) / 2, ts, scale, {255, 255, 255, 220}, true);

    Menu menu; menu.wake();
    for (const auto& b : menu.layout(Mode::Gallery, W, H, true))
        Menu::drawButton(r, b, 255, false);

    cv::Mat out = toMat(surf);
    SDL_DestroyRenderer(r); SDL_FreeSurface(surf);
    return out;
}

int main() {
    setenv("SDL_VIDEODRIVER", "dummy", 1);
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { SDL_Log("init: %s", SDL_GetError()); return 1; }
    const int W = 800, H = 480;

    SDL_Surface* tmp = SDL_CreateRGBSurfaceWithFormat(0, 8, 8, 32, SDL_PIXELFORMAT_ARGB8888);
    SDL_Renderer* rr = SDL_CreateSoftwareRenderer(tmp);
    SDL_Texture* thumb = fakeThumb(rr);

    cv::Mat cam = cameraScene(W, H, thumb);
    cv::Mat gal = galleryScene(W, H);
    auto label = [](cv::Mat& m, const std::string& t) {
        cv::putText(m, t, {16, 470}, cv::FONT_HERSHEY_SIMPLEX, 0.6, {255, 255, 255}, 2);
    };
    label(cam, "CAMERA: thumb button + 2.0x + shutter flash");
    label(gal, "GALLERY: timestamp bar");
    cv::Mat sheet; cv::hconcat(cam, gal, sheet);
    cv::imwrite("/home/user/open-lego-camera-cpp/build/features-mockup.png", sheet);

    SDL_DestroyTexture(thumb); SDL_DestroyRenderer(rr); SDL_FreeSurface(tmp);
    SDL_Quit();
    return 0;
}
