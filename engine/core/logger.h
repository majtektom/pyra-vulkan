#pragma once

#include <string>
#include <string_view>
#include <format>
#include <iostream>
#include <fstream>
#include <memory>
#include <mutex>
#include <vector>

namespace engine::core {

enum class LogLevel {
    Trace,
    Info,
    Warn,
    Error,
    Critical
};

class LogBackend {
public:
    virtual ~LogBackend() = default;
    virtual void log(LogLevel level, std::string_view message) = 0;
};

class ConsoleBackend : public LogBackend {
public:
    void log(LogLevel level, std::string_view message) override;
};

class FileBackend : public LogBackend {
public:
    explicit FileBackend(const std::string& filename);
    void log(LogLevel level, std::string_view message) override;
private:
    std::ofstream file_;
};

class MultiBackend : public LogBackend {
public:
    void addBackend(std::shared_ptr<LogBackend> backend);
    void log(LogLevel level, std::string_view message) override;
private:
    std::vector<std::shared_ptr<LogBackend>> backends_;
};

/**
 * @brief System logowania (Logger).
 * 
 * Logger jest niezbędnym narzędziem w rozwoju silnika 3D. Pozwala na:
 * - Śledzenie błędów inicjalizacji GPU (Vulkan).
 * - Monitorowanie wydajności i ładowania zasobów.
 * - Debugowanie logiki gry bez konieczności zatrzymywania programu (breakpoints).
 * 
 * Implementacja używa wzorca Backend (Strategia):
 * Logger decyduje CO zalogować, a klasy LogBackend decydują GDZIE (konsola, plik, sieć).
 */
class Logger {
public:
    static Logger& getInstance();

    void setLevel(LogLevel level);
    void setBackend(std::unique_ptr<LogBackend> backend);
    
    // Convenient way to set common backends
    void useConsoleBackend();
    void useFileBackend(const std::string& filename);
    void useBothBackends(const std::string& filename);

    template<typename... Args>
    void log(LogLevel level, std::string_view fmt, Args&&... args) {
        if (level >= minLevel_) {
            try {
                std::string message = std::vformat(fmt, std::make_format_args(args...));
                std::lock_guard<std::mutex> lock(mutex_);
                if (backend_) {
                    backend_->log(level, message);
                }
            } catch (const std::format_error& e) {
                // Fallback if formatting fails
                std::lock_guard<std::mutex> lock(mutex_);
                if (backend_) {
                    backend_->log(LogLevel::Error, std::string("Log format error: ") + e.what());
                    backend_->log(level, fmt);
                }
            }
        }
    }

    template<typename... Args> void trace(std::string_view fmt, Args&&... args) { log(LogLevel::Trace, fmt, std::forward<Args>(args)...); }
    template<typename... Args> void info(std::string_view fmt, Args&&... args) { log(LogLevel::Info, fmt, std::forward<Args>(args)...); }
    template<typename... Args> void warn(std::string_view fmt, Args&&... args) { log(LogLevel::Warn, fmt, std::forward<Args>(args)...); }
    template<typename... Args> void error(std::string_view fmt, Args&&... args) { log(LogLevel::Error, fmt, std::forward<Args>(args)...); }
    template<typename... Args> void critical(std::string_view fmt, Args&&... args) { log(LogLevel::Critical, fmt, std::forward<Args>(args)...); }

private:
    Logger();
    LogLevel minLevel_ = LogLevel::Info;
    std::unique_ptr<LogBackend> backend_;
    std::mutex mutex_;
};

} // namespace engine::core

#define LOG_TRACE(...)   ::engine::core::Logger::getInstance().trace(__VA_ARGS__)
#define LOG_INFO(...)    ::engine::core::Logger::getInstance().info(__VA_ARGS__)
#define LOG_WARN(...)    ::engine::core::Logger::getInstance().warn(__VA_ARGS__)
#define LOG_ERROR(...)   ::engine::core::Logger::getInstance().error(__VA_ARGS__)
#define LOG_CRITICAL(...) ::engine::core::Logger::getInstance().critical(__VA_ARGS__)
