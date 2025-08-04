#include "Support/Logger.h"

#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace midend {

// Static member definitions
thread_local int IndentationManager::currentIndent_ = 0;
std::unique_ptr<LoggerConfig> LoggerConfig::instance_;
std::once_flag LoggerConfig::initFlag_;

// LoggerConfig implementation
LoggerConfig::LoggerConfig()
    : globalLogLevel_(LogLevel::INFO),
      enableTimestamps_(false),
      enableThreadIds_(false),
      enableColors_(true),
      outputStream_(&std::cerr) {}

LoggerConfig& LoggerConfig::getInstance() {
    std::call_once(initFlag_, []() {
        instance_ = std::unique_ptr<LoggerConfig>(new LoggerConfig());
        instance_->loadFromEnvironment();
    });
    return *instance_;
}

void LoggerConfig::setGlobalLogLevel(LogLevel level) {
    globalLogLevel_ = level;
}

void LoggerConfig::setModuleLogLevel(const std::string& moduleName,
                                     LogLevel level) {
    moduleLogLevels_[moduleName] = level;
}

LogLevel LoggerConfig::getEffectiveLogLevel(
    const std::string& moduleName) const {
    auto it = moduleLogLevels_.find(moduleName);
    if (it != moduleLogLevels_.end()) {
        return it->second.load();
    }
    return globalLogLevel_.load();
}

void LoggerConfig::setOutputStream(std::ostream& stream) {
    std::lock_guard<std::mutex> lock(outputMutex_);
    outputStream_ = &stream;
}

std::ostream& LoggerConfig::getOutputStream() const { return *outputStream_; }

std::mutex& LoggerConfig::getOutputMutex() const { return outputMutex_; }

void LoggerConfig::enableTimestamps(bool enable) { enableTimestamps_ = enable; }

void LoggerConfig::enableThreadIds(bool enable) { enableThreadIds_ = enable; }

void LoggerConfig::enableColors(bool enable) { enableColors_ = enable; }

bool LoggerConfig::timestampsEnabled() const {
    return enableTimestamps_.load();
}

bool LoggerConfig::threadIdsEnabled() const { return enableThreadIds_.load(); }

bool LoggerConfig::colorsEnabled() const { return enableColors_.load(); }

void LoggerConfig::loadFromEnvironment() {
    // MIDEND_LOG_LEVEL=DEBUG|INFO|WARNING|ERROR|NONE
    if (const char* level = std::getenv("MIDEND_LOG_LEVEL")) {
        std::string levelStr(level);
        if (levelStr == "DEBUG")
            setGlobalLogLevel(LogLevel::DEBUG);
        else if (levelStr == "INFO")
            setGlobalLogLevel(LogLevel::INFO);
        else if (levelStr == "WARNING")
            setGlobalLogLevel(LogLevel::WARNING);
        else if (levelStr == "ERROR")
            setGlobalLogLevel(LogLevel::ERROR);
        else if (levelStr == "NONE")
            setGlobalLogLevel(LogLevel::NONE);
    }

    // MIDEND_LOG_MODULES=GVNPass:DEBUG,InlinePass:INFO
    if (const char* modules = std::getenv("MIDEND_LOG_MODULES")) {
        std::string modulesStr(modules);
        size_t pos = 0;
        while (pos < modulesStr.length()) {
            size_t colonPos = modulesStr.find(':', pos);
            if (colonPos == std::string::npos) break;

            std::string moduleName = modulesStr.substr(pos, colonPos - pos);

            size_t commaPos = modulesStr.find(',', colonPos);
            if (commaPos == std::string::npos) commaPos = modulesStr.length();

            std::string levelStr =
                modulesStr.substr(colonPos + 1, commaPos - colonPos - 1);

            LogLevel level = LogLevel::INFO;
            if (levelStr == "DEBUG")
                level = LogLevel::DEBUG;
            else if (levelStr == "INFO")
                level = LogLevel::INFO;
            else if (levelStr == "WARNING")
                level = LogLevel::WARNING;
            else if (levelStr == "ERROR")
                level = LogLevel::ERROR;
            else if (levelStr == "NONE")
                level = LogLevel::NONE;

            setModuleLogLevel(moduleName, level);

            pos = commaPos + 1;
        }
    }

    // MIDEND_LOG_TIMESTAMPS=true/false
    if (const char* timestamps = std::getenv("MIDEND_LOG_TIMESTAMPS")) {
        enableTimestamps(std::string(timestamps) == "true");
    }

    // MIDEND_LOG_THREADS=true/false
    if (const char* threads = std::getenv("MIDEND_LOG_THREADS")) {
        enableThreadIds(std::string(threads) == "true");
    }

    // MIDEND_LOG_COLORS=true/false
    if (const char* colors = std::getenv("MIDEND_LOG_COLORS")) {
        enableColors(std::string(colors) == "true");
    }
}

std::string LoggerConfig::getColorCode(LogLevel level) const {
    if (!colorsEnabled()) return "";

    switch (level) {
        case LogLevel::DEBUG:
            return "\033[90m";  // Gray
        case LogLevel::INFO:
            return "\033[0m";  // Default
        case LogLevel::WARNING:
            return "\033[33m";  // Yellow
        case LogLevel::ERROR:
            return "\033[31m";  // Red
        default:
            return "";
    }
}

std::string LoggerConfig::getResetCode() const {
    if (!colorsEnabled()) return "";
    return "\033[0m";
}

// LogStream implementation
LogStream::LogStream(LogLevel level, const std::string& moduleName,
                     bool shouldLog)
    : level_(level),
      moduleName_(moduleName),
      shouldLog_(shouldLog),
      isFirstLine_(true),
      moved_(false) {}

LogStream::~LogStream() {
    if (shouldLog_ && !moved_) {
        flushToOutput();
    }
}

LogStream::LogStream(LogStream&& other) noexcept
    : buffer_(std::move(other.buffer_)),
      level_(other.level_),
      moduleName_(std::move(other.moduleName_)),
      shouldLog_(other.shouldLog_),
      isFirstLine_(other.isFirstLine_),
      moved_(other.moved_) {
    other.moved_ = true;
}

LogStream& LogStream::operator=(LogStream&& other) noexcept {
    if (this != &other) {
        if (shouldLog_ && !moved_) {
            flushToOutput();
        }

        buffer_ = std::move(other.buffer_);
        level_ = other.level_;
        moduleName_ = std::move(other.moduleName_);
        shouldLog_ = other.shouldLog_;
        isFirstLine_ = other.isFirstLine_;
        moved_ = other.moved_;

        other.moved_ = true;
    }
    return *this;
}

LogStream& LogStream::operator<<(std::ostream& (*manip)(std::ostream&)) {
    if (shouldLog_ && !moved_) {
        buffer_ << manip;
    }
    return *this;
}

void LogStream::flushToOutput() {
    auto& config = LoggerConfig::getInstance();
    std::lock_guard<std::mutex> lock(config.getOutputMutex());

    std::string content = buffer_.str();
    if (content.empty()) return;

    std::string prefix = formatLogPrefix();
    std::string colorCode = config.getColorCode(level_);
    std::string resetCode = config.getResetCode();

    auto& out = config.getOutputStream();

    if (!colorCode.empty()) {
        out << colorCode;
    }

    out << prefix;
    processMultilineContent(content, out);

    if (!resetCode.empty()) {
        out << resetCode;
    }

    out << std::endl;
}

std::string LogStream::formatLogPrefix() const {
    auto& config = LoggerConfig::getInstance();
    std::ostringstream prefix;

    // Timestamp
    if (config.timestampsEnabled()) {
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        prefix << "[" << std::put_time(std::localtime(&time_t), "%H:%M:%S")
               << "] ";
    }

    // Thread ID
    if (config.threadIdsEnabled()) {
        prefix << "[T" << std::this_thread::get_id() << "] ";
    }

    // Log level
    const char* levelStr;
    switch (level_) {
        case LogLevel::DEBUG:
            levelStr = "DEBUG";
            break;
        case LogLevel::INFO:
            levelStr = "INFO ";
            break;
        case LogLevel::WARNING:
            levelStr = "WARN ";
            break;
        case LogLevel::ERROR:
            levelStr = "ERROR";
            break;
        default:
            levelStr = "?????";
            break;
    }
    prefix << "[" << levelStr << "] ";

    // Module name
    if (!moduleName_.empty() && moduleName_ != "General") {
        prefix << "[" << moduleName_ << "] ";
    }

    // Indentation
    prefix << IndentationManager::getIndentString();

    return prefix.str();
}

void LogStream::processMultilineContent(const std::string& content,
                                        std::ostream& out) const {
    std::istringstream iss(content);
    std::string line;
    bool firstLine = true;

    std::string indentStr = IndentationManager::getIndentString();

    while (std::getline(iss, line)) {
        if (!firstLine) {
            out << "\n";
            // For subsequent lines, add proper indentation
            // Calculate prefix length without color codes
            auto& config = LoggerConfig::getInstance();
            std::string prefixPadding;

            if (config.timestampsEnabled()) {
                prefixPadding += std::string(21, ' ');  // Timestamp space
            }
            if (config.threadIdsEnabled()) {
                prefixPadding += std::string(15, ' ');  // Thread ID space
            }
            prefixPadding += std::string(8, ' ');  // Log level space

            if (!moduleName_.empty() && moduleName_ != "General") {
                prefixPadding +=
                    "[" + std::string(moduleName_.length(), ' ') + "] ";
            }

            out << prefixPadding << indentStr;
        }
        out << line;
        firstLine = false;
    }
}

}  // namespace midend