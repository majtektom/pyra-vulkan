#include "x11_window.h"

#include <X11/Xlib.h>
#include <X11/keysym.h>

#include <cstring>
#include <stdexcept>

namespace engine3d {

// Tworzy zwykle okno X11 i ustawia zdarzenia potrzebne rendererowi.
X11Window::X11Window(const std::string& title, uint32_t width, uint32_t height)
    : width_(width),
      height_(height) {
    auto* display = XOpenDisplay(nullptr);
    if (display == nullptr) {
        throw std::runtime_error("failed to open X display");
    }

    const int screen = DefaultScreen(display);
    const Window root = RootWindow(display, screen);

    const Window window = XCreateSimpleWindow(
        display,
        root,
        0,
        0,
        width,
        height,
        0,
        BlackPixel(display, screen),
        WhitePixel(display, screen)
    );
    if (window == 0) {
        XCloseDisplay(display);
        throw std::runtime_error("failed to create X11 window");
    }

// Rejestrujemy zdarzenia potrzebne do zamykania, resize i obslugi Escape.
    XSelectInput(display, window,
        ExposureMask | StructureNotifyMask |
        KeyPressMask | KeyReleaseMask |
        ButtonPressMask | ButtonReleaseMask |
        PointerMotionMask);
    XStoreName(display, window, title.c_str());

    Atom wmDeleteMessage = XInternAtom(display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(display, window, &wmDeleteMessage, 1);

    XMapWindow(display, window);
    XFlush(display);

    display_ = display;
    window_ = window;
    wmDeleteMessage_ = static_cast<unsigned long>(wmDeleteMessage);

    std::memset(keys_.data(), 0, keys_.size());
}

// Zwalnia okno i polaczenie z serwerem X.
X11Window::~X11Window() {
    if (pointerGrabbed_) {
        grabPointer(false);
    }
    auto* display = static_cast<Display*>(display_);
    if (display != nullptr && window_ != 0) {
        XDestroyWindow(display, static_cast<Window>(window_));
    }
    if (display != nullptr) {
        XCloseDisplay(display);
    }
}

void X11Window::grabPointer(bool grab) {
    auto* display = static_cast<Display*>(display_);
    if (display == nullptr) return;

    if (grab && !pointerGrabbed_) {
        // Tworzymy niewidzialny kursor: przezroczysty piksel 1x1
        XColor dummyColor{};
        Pixmap cursorPixmap = XCreatePixmap(display,
            static_cast<Window>(window_), 1, 1, 1);
        Cursor invisibleCursor = XCreatePixmapCursor(
            display, cursorPixmap, cursorPixmap, &dummyColor, &dummyColor, 0, 0);
        XFreePixmap(display, cursorPixmap);

        const int grabStatus = XGrabPointer(display,
            static_cast<Window>(window_), True,
            ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
            GrabModeAsync, GrabModeAsync,
            static_cast<Window>(window_), invisibleCursor, CurrentTime);
        if (grabStatus == GrabSuccess) {
            pointerGrabbed_ = true;
            // Resetujemy pozycje do srodka okna
            const int cx = static_cast<int>(width_) / 2;
            const int cy = static_cast<int>(height_) / 2;
            XWarpPointer(display, None, static_cast<Window>(window_),
                0, 0, 0, 0, cx, cy);
            mouseX_ = cx;
            mouseY_ = cy;
            mouseDeltaX_ = 0;
            mouseDeltaY_ = 0;
        } else {
            // Grab sie nie udal - nie bedziemy mieli poprawnego inputu myszy
        }
    } else if (!grab && pointerGrabbed_) {
        XUngrabPointer(display, CurrentTime);
        XFlush(display);
        pointerGrabbed_ = false;
    }
}

bool X11Window::pointerGrabbed() const {
    return pointerGrabbed_;
}

InputSnapshot X11Window::pollInput() {
    InputSnapshot snapshot;
    std::copy(std::begin(keys_), std::end(keys_), std::begin(snapshot.keys));
    snapshot.mouseDeltaX = mouseDeltaX_;
    snapshot.mouseDeltaY = mouseDeltaY_;
    std::copy(std::begin(mouseButtons_), std::end(mouseButtons_), std::begin(snapshot.mouseButtons));
    snapshot.mouseX = mouseX_;
    snapshot.mouseY = mouseY_;
    mouseDeltaX_ = 0;
    mouseDeltaY_ = 0;
    return snapshot;
}

// Odczytuje wszystkie oczekujace zdarzenia i zapisuje je w prostym stanie klasy.
void X11Window::processEvents() {
    auto* display = static_cast<Display*>(display_);
    if (display == nullptr) {
        return;
    }

    while (XPending(display) > 0) {
        XEvent event{};
        XNextEvent(display, &event);

        if (event.type == ClientMessage &&
            static_cast<unsigned long>(event.xclient.data.l[0]) == wmDeleteMessage_) {
            shouldClose_ = true;
            return;
        }

        if (event.type == ConfigureNotify) {
            const auto newWidth = static_cast<uint32_t>(event.xconfigure.width);
            const auto newHeight = static_cast<uint32_t>(event.xconfigure.height);
            if (newWidth != width_ || newHeight != height_) {
                width_ = newWidth;
                height_ = newHeight;
                resized_ = true;
            }
        }

        if (event.type == KeyPress) {
            const uint8_t keycode = static_cast<uint8_t>(event.xkey.keycode & 0xFF);
            keys_[keycode] = true;
            const KeySym key = XLookupKeysym(&event.xkey, 0);
            if (key == XK_Escape) {
                shouldClose_ = true;
                return;
            }
        }

        if (event.type == KeyRelease) {
            if (XPending(display) > 0) {
                XEvent nextEvent{};
                XPeekEvent(display, &nextEvent);
                if (nextEvent.type == KeyPress &&
                    nextEvent.xkey.keycode == event.xkey.keycode &&
                    nextEvent.xkey.time == event.xkey.time) {
                    XNextEvent(display, &nextEvent);
                    continue;
                }
            }
            const uint8_t keycode = static_cast<uint8_t>(event.xkey.keycode & 0xFF);
            keys_[keycode] = false;
        }

        if (event.type == ButtonPress) {
            const int button = event.xbutton.button;
            if (button >= 1 && button <= 5) {
                mouseButtons_[button - 1] = true;
            }
        }

        if (event.type == ButtonRelease) {
            const int button = event.xbutton.button;
            if (button >= 1 && button <= 5) {
                mouseButtons_[button - 1] = false;
            }
        }

        if (event.type == MotionNotify) {
            const int newX = event.xmotion.x;
            const int newY = event.xmotion.y;

            if (pointerGrabbed_) {
                // Kumulujemy deltę wzgledem poprzedniej pozycji myszy.
                // XWarpPointer wywolywany tylko raz po petli, nie per-event.
                mouseDeltaX_ += newX - mouseX_;
                mouseDeltaY_ += newY - mouseY_;
                mouseX_ = newX;
                mouseY_ = newY;
            } else {
                // Normalny tryb okienkowy: delta z poprzedniej pozycji
                if (mouseX_ != 0 || mouseY_ != 0) {
                    mouseDeltaX_ += newX - mouseX_;
                    mouseDeltaY_ += newY - mouseY_;
                }
                mouseX_ = newX;
                mouseY_ = newY;
            }
        }
    }

    // W trybie FPS warpujemy kursor do srodka tylko raz na klatke,
    // po przetworzeniu wszystkich zdarzen. Unikamy dziesiatek kosztownych
    // synchronicznych round-tripow X11 per klatke.
    if (pointerGrabbed_) {
        const int cx = static_cast<int>(width_) / 2;
        const int cy = static_cast<int>(height_) / 2;
        XWarpPointer(display, None, static_cast<Window>(window_),
            0, 0, 0, 0, cx, cy);
        mouseX_ = cx;
        mouseY_ = cy;
    }
}

bool X11Window::shouldClose() const {
    return shouldClose_;
}

bool X11Window::wasResized() const {
    return resized_;
}

void X11Window::clearResizeFlag() {
    resized_ = false;
}

uint32_t X11Window::width() const {
    return width_;
}

uint32_t X11Window::height() const {
    return height_;
}

void* X11Window::nativeDisplay() const {
    return display_;
}

unsigned long X11Window::nativeWindow() const {
    return window_;
}

}  // namespace engine3d
