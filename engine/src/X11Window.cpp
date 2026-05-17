#include "X11Window.hpp"

#include <X11/Xlib.h>
#include <X11/keysym.h>

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
    XSelectInput(display, window, ExposureMask | StructureNotifyMask | KeyPressMask);
    XStoreName(display, window, title.c_str());

    Atom wmDeleteMessage = XInternAtom(display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(display, window, &wmDeleteMessage, 1);

    XMapWindow(display, window);
    XFlush(display);

    display_ = display;
    window_ = window;
    wmDeleteMessage_ = static_cast<unsigned long>(wmDeleteMessage);
}

// Zwalnia okno i polaczenie z serwerem X.
X11Window::~X11Window() {
    auto* display = static_cast<Display*>(display_);
    if (display != nullptr && window_ != 0) {
        XDestroyWindow(display, static_cast<Window>(window_));
    }
    if (display != nullptr) {
        XCloseDisplay(display);
    }
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
            const KeySym key = XLookupKeysym(&event.xkey, 0);
            if (key == XK_Escape) {
                shouldClose_ = true;
                return;
            }
        }
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