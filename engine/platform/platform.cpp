#define VK_USE_PLATFORM_XLIB_KHR

#include "platform.h"

#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace engine3d {
namespace {

constexpr int kMouseLeft = 0;
constexpr int kMouseMiddle = 2;
constexpr int kMouseRight = 3;
constexpr int kMouseScrollUp = 4;
constexpr int kMouseScrollDown = 5;

} // namespace

Platform::Platform(const std::string& title, uint32_t width, uint32_t height)
    : width_(width), height_(height) {
    display_ = XOpenDisplay(nullptr);
    if (!display_) {
        throw std::runtime_error("Failed to open X display");
    }

    int screen = DefaultScreen(display_);
    Window root = RootWindow(display_, screen);

    XSetWindowAttributes attribs{};
    attribs.event_mask = ExposureMask | KeyPressMask | KeyReleaseMask |
                         ButtonPressMask | ButtonReleaseMask |
                         PointerMotionMask | StructureNotifyMask |
                         FocusChangeMask;

    window_ = XCreateWindow(display_, root, 0, 0, width, height, 0,
                            CopyFromParent, InputOutput, CopyFromParent,
                            CWEventMask, &attribs);

    XStoreName(display_, window_, title.c_str());
    XMapWindow(display_, window_);

    wmDeleteMessage_ = XInternAtom(display_, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(display_, window_, &wmDeleteMessage_, 1);

    {
        Pixmap blank = XCreateBitmapFromData(display_, window_, "", 1, 1);
        XColor dummy;
        invisibleCursor_ = XCreatePixmapCursor(display_, blank, blank, &dummy, &dummy, 0, 0);
        XFreePixmap(display_, blank);
    }
}

Platform::~Platform() {
    if (display_ && window_) {
        XUnmapWindow(display_, window_);
        XDestroyWindow(display_, window_);
    }
    if (invisibleCursor_) {
        XFreeCursor(display_, invisibleCursor_);
    }
    if (display_) {
        XCloseDisplay(display_);
    }
}

void Platform::processEvents() {
    if (pointerGrabbed_) {
        int cx = static_cast<int>(width_) / 2;
        int cy = static_cast<int>(height_) / 2;
        mouseX_ = cx;
        mouseY_ = cy;
        XWarpPointer(display_, None, window_, 0, 0, 0, 0, cx, cy);
    }

    mouseDeltaX_ = 0;
    mouseDeltaY_ = 0;

    while (XPending(display_)) {
        XEvent event;
        XNextEvent(display_, &event);

        switch (event.type) {
        case ClientMessage:
            if (static_cast<unsigned long>(event.xclient.data.l[0]) == wmDeleteMessage_) {
                shouldClose_ = true;
            }
            break;
        case DestroyNotify:
            shouldClose_ = true;
            break;
        case ConfigureNotify:
            if (static_cast<uint32_t>(event.xconfigure.width) != width_ ||
                static_cast<uint32_t>(event.xconfigure.height) != height_) {
                width_ = event.xconfigure.width;
                height_ = event.xconfigure.height;
                resized_ = true;
            }
            break;
        case KeyPress: {
            int kc = static_cast<int>(event.xkey.keycode);
            if (kc >= 0 && kc < 256) keys_[kc] = true;
            KeySym ks = XLookupKeysym(&event.xkey, 0);
            if (ks == XK_Escape) {
                shouldClose_ = true;
            }
            break;
        }
        case KeyRelease: {
            int kc = static_cast<int>(event.xkey.keycode);
            if (kc >= 0 && kc < 256) keys_[kc] = false;
            break;
        }
        case ButtonPress:
            if (!pointerGrabbed_) {
                grabPointer(true);
            }
            if (event.xbutton.button == Button1) mouseButtons_[kMouseLeft] = true;
            else if (event.xbutton.button == Button3) mouseButtons_[kMouseRight] = true;
            break;
        case ButtonRelease:
            if (event.xbutton.button == Button1) mouseButtons_[kMouseLeft] = false;
            else if (event.xbutton.button == Button3) mouseButtons_[kMouseRight] = false;
            break;
        case FocusOut:
            if (pointerGrabbed_) {
                grabPointer(false);
            }
            break;
        case FocusIn:
            if (!pointerGrabbed_) {
                std::fill(std::begin(keys_), std::end(keys_), false);
            }
            break;
        case MotionNotify:
            mouseDeltaX_ += event.xmotion.x - mouseX_;
            mouseDeltaY_ += event.xmotion.y - mouseY_;
            mouseX_ = event.xmotion.x;
            mouseY_ = event.xmotion.y;
            break;
        }
    }
}

void Platform::grabPointer(bool grab) {
    if (grab == pointerGrabbed_) return;
    pointerGrabbed_ = grab;
    if (grab) {
        int cx = static_cast<int>(width_) / 2;
        int cy = static_cast<int>(height_) / 2;
        mouseX_ = cx;
        mouseY_ = cy;
        XGrabPointer(display_, window_, True, ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
                     GrabModeAsync, GrabModeAsync, window_, invisibleCursor_, CurrentTime);
    } else {
        XUngrabPointer(display_, CurrentTime);
        XUndefineCursor(display_, window_);
    }
}

bool Platform::shouldClose() const { return shouldClose_; }
bool Platform::wasResized() const { return resized_; }
void Platform::clearResizeFlag() { resized_ = false; }
uint32_t Platform::width() const { return width_; }
uint32_t Platform::height() const { return height_; }

void* Platform::nativeDisplay() const { return display_; }
unsigned long Platform::nativeWindow() const { return window_; }

bool Platform::pointerGrabbed() const { return pointerGrabbed_; }

InputSnapshot Platform::pollInput() {
    InputSnapshot snap{};
    std::memcpy(snap.keys, keys_, sizeof(keys_));
    snap.mouseDeltaX = mouseDeltaX_;
    snap.mouseDeltaY = mouseDeltaY_;
    std::memcpy(snap.mouseButtons, mouseButtons_, sizeof(mouseButtons_));
    snap.mouseX = mouseX_;
    snap.mouseY = mouseY_;
    return snap;
}

}  // namespace engine3d
