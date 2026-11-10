#include "core/profiler.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "core/logger.h"

namespace engine3d {

Profiler& Profiler::get() {
    static Profiler p;
    return p;
}

Profiler::Profiler() {
    const char* env = std::getenv("ENGINE_PROFILE");
    enabled_ = env && std::strcmp(env, "0") != 0;
    windowStart_ = std::chrono::steady_clock::now();
}

Profiler::Section& Profiler::section(const char* name) {
    for (auto& s : sections_) {
        if (s.name == name || std::strcmp(s.name, name) == 0) return s;
    }
    sections_.push_back({name, 0.0});
    return sections_.back();
}

void Profiler::beginFrame() {
    if (!enabled_) return;
    frameStart_ = lapStart_ = std::chrono::steady_clock::now();
}

void Profiler::lap(const char* name) {
    if (!enabled_) return;
    const auto now = std::chrono::steady_clock::now();
    section(name).totalMs += std::chrono::duration<double, std::milli>(now - lapStart_).count();
    lapStart_ = now;
}

void Profiler::add(const char* name, double ms) {
    if (!enabled_) return;
    section(name).totalMs += ms;
}

void Profiler::count(const char* name, double n) {
    if (!enabled_) return;
    Section& s = section(name);
    s.counter = true;
    s.totalMs += n;
}

void Profiler::endFrame() {
    if (!enabled_) return;
    const auto now = std::chrono::steady_clock::now();
    frameTotalMs_ += std::chrono::duration<double, std::milli>(now - frameStart_).count();
    ++frames_;
    if (std::chrono::duration<double>(now - windowStart_).count() < 3.0 || frames_ == 0) return;

    const double f = static_cast<double>(frames_);
    char buf[128];
    std::snprintf(buf, sizeof(buf), "klatka %.1f ms (%.0f fps)", frameTotalMs_ / f, 1000.0 * f / std::max(1.0, frameTotalMs_));
    std::string report = buf;
    for (auto& s : sections_) {
        std::snprintf(buf, sizeof(buf), s.counter ? " | %s %.0fx" : " | %s %.2f", s.name, s.totalMs / f);
        report += buf;
        s.totalMs = 0.0;
    }
    lastReport_ = report;
    LOG_INFO("[profil] {}", report);
    frameTotalMs_ = 0.0;
    frames_ = 0;
    windowStart_ = now;
}

} // namespace engine3d
