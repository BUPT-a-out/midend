#pragma once

#include <atomic>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <ostream>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>

namespace midend {

enum class LogLevel : int {
    DEBUG = 0,
    INFO = 1,
    WARNING = 2,
    ERROR = 3,
    NONE = 4
};

// Thread-local indentation manager
class IndentationManager {
private:
    static thread_local int currentIndent_;
    static constexpr int INDENT_SIZE = 2;

public:
    static int getCurrentIndent() { return currentIndent_; }
    static void increaseIndent() { currentIndent_ += INDENT_SIZE; }
    static void decreaseIndent() { 
        currentIndent_ = std::max(0, currentIndent_ - INDENT_SIZE); 
    }
    static std::string getIndentString() {
        return std::string(currentIndent_, ' ');
    }
};

// RAII indent guard
class IndentGuard {
public:
    IndentGuard() {
        IndentationManager::increaseIndent();
    }
    
    ~IndentGuard() {
        IndentationManager::decreaseIndent();
    }
    
    IndentGuard(const IndentGuard&) = delete;
    IndentGuard& operator=(const IndentGuard&) = delete;
    IndentGuard(IndentGuard&&) = default;
    IndentGuard& operator=(IndentGuard&&) = default;
};

// Thread-safe configuration singleton
class LoggerConfig {
private:
    static std::unique_ptr<LoggerConfig> instance_;
    static std::once_flag initFlag_;
    
    mutable std::unordered_map<std::string, std::atomic<LogLevel>> moduleLogLevels_;
    std::atomic<LogLevel> globalLogLevel_;
    std::atomic<bool> enableTimestamps_;
    std::atomic<bool> enableThreadIds_;
    std::atomic<bool> enableColors_;
    std::ostream* outputStream_;
    mutable std::mutex outputMutex_;
    
    LoggerConfig();

public:
    static LoggerConfig& getInstance();
    
    // Configuration methods
    void setGlobalLogLevel(LogLevel level);
    void setModuleLogLevel(const std::string& moduleName, LogLevel level);
    LogLevel getEffectiveLogLevel(const std::string& moduleName) const;
    
    // Output configuration
    void setOutputStream(std::ostream& stream);
    std::ostream& getOutputStream() const;
    std::mutex& getOutputMutex() const;
    
    // Feature toggles
    void enableTimestamps(bool enable);
    void enableThreadIds(bool enable);
    void enableColors(bool enable);
    bool timestampsEnabled() const;
    bool threadIdsEnabled() const;
    bool colorsEnabled() const;
    
    // Runtime configuration from environment
    void loadFromEnvironment();
    
    // Get color codes for terminal output
    std::string getColorCode(LogLevel level) const;
    std::string getResetCode() const;
};

// Forward declarations
class Logger;

// Stream-based logger that buffers until destruction
class LogStream {
private:
    std::ostringstream buffer_;
    LogLevel level_;
    std::string moduleName_;
    bool shouldLog_;
    bool isFirstLine_;
    mutable bool moved_;

public:
    LogStream(LogLevel level, const std::string& moduleName, bool shouldLog);
    ~LogStream();
    
    // Stream operators for any type
    template<typename T>
    LogStream& operator<<(const T& value) {
        if (shouldLog_ && !moved_) {
            buffer_ << value;
        }
        return *this;
    }
    
    // Special handling for manipulators
    LogStream& operator<<(std::ostream& (*manip)(std::ostream&));
    
    // Non-copyable but movable
    LogStream(const LogStream&) = delete;
    LogStream& operator=(const LogStream&) = delete;
    LogStream(LogStream&& other) noexcept;
    LogStream& operator=(LogStream&& other) noexcept;

private:
    void flushToOutput();
    std::string formatLogPrefix() const;
    void processMultilineContent(const std::string& content, std::ostream& out) const;
};

// Main logger class with module context
class Logger {
private:
    std::string moduleName_;
    
public:
    explicit Logger(const std::string& moduleName = "General") 
        : moduleName_(moduleName) {}
    
    // Create log streams for different levels
    LogStream debug() const {
        return createLogStream(LogLevel::DEBUG);
    }
    
    LogStream info() const {
        return createLogStream(LogLevel::INFO);
    }
    
    LogStream warning() const {
        return createLogStream(LogLevel::WARNING);
    }
    
    LogStream error() const {
        return createLogStream(LogLevel::ERROR);
    }
    
    // Conditional logging
    LogStream debugIf(bool condition) const {
        return createLogStream(LogLevel::DEBUG, condition);
    }
    
    LogStream infoIf(bool condition) const {
        return createLogStream(LogLevel::INFO, condition);
    }
    
    // Check if logging is enabled for a level
    bool isDebugEnabled() const {
        return shouldLog(LogLevel::DEBUG);
    }
    
    bool isInfoEnabled() const {
        return shouldLog(LogLevel::INFO);
    }
    
    // Create indent guard
    IndentGuard indent() const {
        return IndentGuard{};
    }
    
    // Get module name
    const std::string& getModuleName() const { return moduleName_; }

private:
    LogStream createLogStream(LogLevel level, bool condition = true) const {
        bool shouldLogThis = condition && shouldLog(level);
        return LogStream(level, moduleName_, shouldLogThis);
    }
    
    bool shouldLog(LogLevel level) const {
        return level >= LoggerConfig::getInstance().getEffectiveLogLevel(moduleName_);
    }
};

// Global logger instance for general use
inline Logger& getGlobalLogger() {
    static Logger globalLogger("Global");
    return globalLogger;
}

} // namespace midend