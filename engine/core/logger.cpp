#include "logger.h"
#include <chrono>
#include <iomanip>
#include <sstream>

namespace engine::core {

namespace {
    std::string levelToString(LogLevel level) {
        switch (level) {
            case LogLevel::Trace:    return "TRACE";
            case LogLevel::Info:     return "INFO";
            case LogLevel::Warn:     return "WARN";
            case LogLevel::Error:    return "ERROR";
            case LogLevel::Critical: return "CRITICAL";
            default:                 return "UNKNOWN";
        }
    }

    std::string getTimestamp() {
        // Pobieranie aktualnego czasu systemowego z wysoką precyzją (milisekundy).
        // W silnikach graficznych precyzyjny czas jest kluczowy do analizy opóźnień (latency).
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
        
        std::stringstream ss;
        ss << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S")
           << '.' << std::setfill('0') << std::setw(3) << ms.count();
        return ss.str();
    }

    /**
     * @brief Formatuje linię logu.
     * Standaryzacja formatu ułatwia późniejsze parsowanie logów przez zewnętrzne narzędzia.
     */
    std::string formatLog(LogLevel level, std::string_view message) {
        return std::format("[{}] [{}] {}\n", getTimestamp(), levelToString(level), message);
    }
}

void ConsoleBackend::log(LogLevel level, std::string_view message) {
    // Rozdzielenie strumieni: błędy trafiają na stderr, zwykłe info na stdout.
    // Pozwala to systemowi operacyjnemu na odpowiednie przekierowanie potoków.
    if (level >= LogLevel::Error) {
        std::cerr << formatLog(level, message) << std::flush;
    } else {
        std::cout << formatLog(level, message) << std::flush;
    }
}

FileBackend::FileBackend(const std::string& filename) {
    // Otwieranie pliku w trybie 'app' (append) zapewnia, że nowe logi 
    // nie nadpisują poprzedniej sesji silnika.
    file_.open(filename, std::ios::app);
}

void FileBackend::log(LogLevel level, std::string_view message) {
    if (file_.is_open()) {
        file_ << formatLog(level, message) << std::flush;
    }
}

void MultiBackend::addBackend(std::shared_ptr<LogBackend> backend) {
    backends_.push_back(std::move(backend));
}

void MultiBackend::log(LogLevel level, std::string_view message) {
    for (auto& backend : backends_) {
        backend->log(level, message);
    }
}

Logger::Logger() {
    backend_ = std::make_unique<ConsoleBackend>();
}

Logger& Logger::getInstance() {
    static Logger instance;
    return instance;
}

void Logger::setLevel(LogLevel level) {
    minLevel_ = level;
}

void Logger::setBackend(std::unique_ptr<LogBackend> backend) {
    std::lock_guard<std::mutex> lock(mutex_);
    backend_ = std::move(backend);
}

void Logger::useConsoleBackend() {
    setBackend(std::make_unique<ConsoleBackend>());
}

void Logger::useFileBackend(const std::string& filename) {
    setBackend(std::make_unique<FileBackend>(filename));
}

void Logger::useBothBackends(const std::string& filename) {
    auto multi = std::make_unique<MultiBackend>();
    multi->addBackend(std::make_shared<ConsoleBackend>());
    multi->addBackend(std::make_shared<FileBackend>(filename));
    setBackend(std::move(multi));
}

} // namespace engine::core
