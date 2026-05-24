#include "platform.h"

#include <SDL3/SDL.h>
#if ENGINE3D_HAS_TTF
#include <SDL3_ttf/SDL_ttf.h>
#endif
#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace engine3d {

Platform::Platform(const std::string& title, uint32_t width, uint32_t height)
    : width_(width), height_(height) {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        throw std::runtime_error(std::string("SDL3 init failed: ") + SDL_GetError());
    }
#if ENGINE3D_HAS_TTF
    TTF_Init();
#endif

    window_ = SDL_CreateWindow(title.c_str(), static_cast<int>(width), static_cast<int>(height),
                                SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
    if (!window_) {
        SDL_Quit();
        throw std::runtime_error(std::string("SDL3 window creation failed: ") + SDL_GetError());
    }

    SDL_SetWindowRelativeMouseMode(window_, false);

    // Tworzenie niewidzialnego kursora (1x1 przezroczysta bitmapa)
    uint8_t blankData[4] = {0, 0, 0, 0};
    SDL_Surface* surface = SDL_CreateSurfaceFrom(1, 1, SDL_PIXELFORMAT_RGBA8888, blankData, 4);
    if (surface) {
        invisibleCursor_ = SDL_CreateColorCursor(surface, 0, 0);
        SDL_DestroySurface(surface);
    }

    SDL_ShowWindow(window_);
}

Platform::~Platform() {
    if (invisibleCursor_) {
        SDL_DestroyCursor(invisibleCursor_);
    }
    if (window_) {
        SDL_DestroyWindow(window_);
    }
#if ENGINE3D_HAS_TTF
    TTF_Quit();
#endif
    SDL_Quit();
}

void Platform::processEvents() {
    mouseDeltaX_ = 0;
    mouseDeltaY_ = 0;
    mouseWheel_ = 0;

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
        case SDL_EVENT_QUIT:
            shouldClose_ = true;
            break;
        case SDL_EVENT_WINDOW_RESIZED:
            width_ = static_cast<uint32_t>(event.window.data1);
            height_ = static_cast<uint32_t>(event.window.data2);
            resized_ = true;
            break;
        case SDL_EVENT_KEY_DOWN: {
            SDL_Scancode sc = event.key.scancode;
            if (sc < SDL_SCANCODE_COUNT && static_cast<int>(sc) < 256) {
                keys_[static_cast<int>(sc)] = true;
            }
            if (event.key.key == SDLK_ESCAPE) {
                shouldClose_ = true;
            }
            break;
        }
        case SDL_EVENT_KEY_UP: {
            SDL_Scancode sc = event.key.scancode;
            if (sc < SDL_SCANCODE_COUNT && static_cast<int>(sc) < 256) {
                keys_[static_cast<int>(sc)] = false;
            }
            break;
        }
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if (!pointerGrabbed_) {
                grabPointer(true);
            }
            if (event.button.button == SDL_BUTTON_LEFT)  mouseButtons_[0] = true;
            if (event.button.button == SDL_BUTTON_RIGHT) mouseButtons_[1] = true;
            break;
        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (event.button.button == SDL_BUTTON_LEFT)  mouseButtons_[0] = false;
            if (event.button.button == SDL_BUTTON_RIGHT) mouseButtons_[1] = false;
            break;
        case SDL_EVENT_MOUSE_MOTION:
            if (pointerGrabbed_) {
                mouseDeltaX_ += static_cast<int>(event.motion.xrel);
                mouseDeltaY_ += static_cast<int>(event.motion.yrel);
            }
            mouseX_ = static_cast<int>(event.motion.x);
            mouseY_ = static_cast<int>(event.motion.y);
            break;
        case SDL_EVENT_MOUSE_WHEEL:
            mouseWheel_ += static_cast<int>(event.wheel.y);
            break;
        case SDL_EVENT_WINDOW_FOCUS_LOST:
            if (pointerGrabbed_) {
                grabPointer(false);
            }
            break;
        case SDL_EVENT_WINDOW_FOCUS_GAINED:
            if (!pointerGrabbed_) {
                std::fill(std::begin(keys_), std::end(keys_), false);
            }
            break;
        }
    }
}

void Platform::grabPointer(bool grab) {
    if (grab == pointerGrabbed_) return;
    pointerGrabbed_ = grab;
    if (grab) {
        SDL_SetWindowRelativeMouseMode(window_, true);
        if (invisibleCursor_) {
            SDL_SetCursor(invisibleCursor_);
        }
    } else {
        SDL_SetWindowRelativeMouseMode(window_, false);
        SDL_SetCursor(SDL_GetDefaultCursor());
    }
}

bool Platform::shouldClose() const { return shouldClose_; }
bool Platform::wasResized() const { return resized_; }
void Platform::clearResizeFlag() { resized_ = false; }
uint32_t Platform::width() const { return width_; }
uint32_t Platform::height() const { return height_; }

SDL_Window* Platform::nativeWindow() const { return window_; }

bool Platform::pointerGrabbed() const { return pointerGrabbed_; }

InputSnapshot Platform::pollInput() {
    InputSnapshot snap{};
    std::memcpy(snap.keys, keys_, sizeof(keys_));
    snap.mouseDeltaX = mouseDeltaX_;
    snap.mouseDeltaY = mouseDeltaY_;
    std::memcpy(snap.mouseButtons, mouseButtons_, sizeof(mouseButtons_));
    snap.mouseX = mouseX_;
    snap.mouseY = mouseY_;
    snap.mouseWheel = mouseWheel_;
    return snap;
}

}  // namespace engine3d
