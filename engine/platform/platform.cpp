#include "platform/platform.h"

#include <SDL3/SDL.h>
#if ENGINE3D_HAS_TTF
#include <SDL3_ttf/SDL_ttf.h>
#endif
#include <algorithm>
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
    input_.mouseDeltaX = 0;
    input_.mouseDeltaY = 0;
    input_.mouseWheel = 0;
    input_.mouseWheelF = 0.0f;

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
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        case SDL_EVENT_WINDOW_RESTORED:
            // Rozmiar w pikselach (HiDPI) lub powrót z minimalizacji — swapchain do przebudowy.
            resized_ = true;
            break;
        case SDL_EVENT_WINDOW_ENTER_FULLSCREEN:
            fullscreen_ = true;
            break;
        case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN:
            fullscreen_ = false;
            break;
        case SDL_EVENT_KEY_DOWN: {
            SDL_Scancode sc = event.key.scancode;
            const int keyIndex = static_cast<int>(sc);
            if (keyIndex >= 0 && keyIndex < 256) {
                input_.keys[keyIndex] = true;
            }
            break;
        }
        case SDL_EVENT_KEY_UP: {
            SDL_Scancode sc = event.key.scancode;
            const int keyIndex = static_cast<int>(sc);
            if (keyIndex >= 0 && keyIndex < 256) {
                input_.keys[keyIndex] = false;
            }
            break;
        }
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if (!pointerGrabbed_) {
                grabPointer(true);
            }
            if (event.button.button == SDL_BUTTON_LEFT)  input_.mouseButtons[0] = true;
            if (event.button.button == SDL_BUTTON_RIGHT) input_.mouseButtons[1] = true;
            if (event.button.button == SDL_BUTTON_MIDDLE) input_.mouseButtons[2] = true;
            break;
        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (event.button.button == SDL_BUTTON_LEFT)  input_.mouseButtons[0] = false;
            if (event.button.button == SDL_BUTTON_RIGHT) input_.mouseButtons[1] = false;
            if (event.button.button == SDL_BUTTON_MIDDLE) input_.mouseButtons[2] = false;
            break;
        case SDL_EVENT_MOUSE_MOTION:
            if (pointerGrabbed_) {
                input_.mouseDeltaX += static_cast<int>(event.motion.xrel);
                input_.mouseDeltaY += static_cast<int>(event.motion.yrel);
            }
            input_.mouseX = static_cast<int>(event.motion.x);
            input_.mouseY = static_cast<int>(event.motion.y);
            break;
        case SDL_EVENT_MOUSE_WHEEL:
        {
            // SDL3 raportuje kółko jako float - precyzyjne myszy i touchpady dają ułamki,
            // które przy rzutowaniu na int ginęły. Akumulujemy resztę między zdarzeniami.
            float wy = event.wheel.y;
            if (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) wy = -wy;
            input_.mouseWheelF += wy;
            wheelRemainder_ += wy;
            const int steps = static_cast<int>(wheelRemainder_);
            input_.mouseWheel += steps;
            wheelRemainder_ -= static_cast<float>(steps);
        }
            break;
        case SDL_EVENT_WINDOW_FOCUS_LOST:
            if (pointerGrabbed_) {
                grabPointer(false);
            }
            break;
        case SDL_EVENT_WINDOW_FOCUS_GAINED:
            if (!pointerGrabbed_) {
                std::fill(std::begin(input_.keys), std::end(input_.keys), false);
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
bool Platform::isMinimized() const {
    return window_ && (SDL_GetWindowFlags(window_) & SDL_WINDOW_MINIMIZED) != 0;
}
void Platform::clearResizeFlag() { resized_ = false; }
uint32_t Platform::width() const { return width_; }
uint32_t Platform::height() const { return height_; }

SDL_Window* Platform::nativeWindow() const { return window_; }

void Platform::setFullscreen(bool fullscreen) {
    if (fullscreen_ == fullscreen) return;
    fullscreen_ = fullscreen;
    SDL_SetWindowFullscreen(window_, fullscreen);
}

bool Platform::isFullscreen() const { return fullscreen_; }

void Platform::setWindowSize(uint32_t width, uint32_t height) {
    if (!window_ || width == 0 || height == 0) return;
    SDL_SetWindowSize(window_, static_cast<int>(width), static_cast<int>(height));
    SDL_SetWindowPosition(window_, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    SDL_SyncWindow(window_);
    resized_ = true;
}

bool Platform::usableDisplaySize(uint32_t& width, uint32_t& height) const {
    if (!window_) return false;
    const SDL_DisplayID display = SDL_GetDisplayForWindow(window_);
    SDL_Rect r{};
    if (display == 0 || !SDL_GetDisplayUsableBounds(display, &r)) return false;
    width = static_cast<uint32_t>(r.w);
    height = static_cast<uint32_t>(r.h);
    return true;
}

InputSnapshot Platform::pollInput() {
    return input_;
}

}  // namespace engine3d
