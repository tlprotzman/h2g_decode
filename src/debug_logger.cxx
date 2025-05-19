#include "debug_logger.h"

// Initialize static member
DebugLogger* DebugLogger::instance = nullptr;

// Get the singleton instance
DebugLogger* DebugLogger::getInstance() {
    if (instance == nullptr) {
        instance = new DebugLogger();
    }
    return instance;
}

// Set the debug level
void DebugLogger::setLevel(int level) {
    if (level >= DEBUG_OFF && level <= DEBUG_TRACE) {
        this->level = level;
    } else {
        std::cerr << "Invalid debug level: " << level << ". Using DEBUG_INFO level." << std::endl;
        this->level = DEBUG_INFO;
    }
}

// Get the current debug level
int DebugLogger::getLevel() const {
    return level;
}

// Enable/disable timestamps
void DebugLogger::setShowTimestamp(bool show) {
    showTimestamp = show;
}

// Set component prefix
void DebugLogger::setComponentPrefix(const std::string& prefix) {
    componentPrefix = prefix;
}

// Get component prefix
std::string DebugLogger::getComponentPrefix() const {
    return componentPrefix;
}

// Get current timestamp as string
std::string DebugLogger::getCurrentTimestamp() const {
    auto now = std::chrono::system_clock::now();
    auto nowTime = std::chrono::system_clock::to_time_t(now);
    auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    
    std::stringstream ss;
    ss << std::put_time(std::localtime(&nowTime), "%H:%M:%S");
    ss << '.' << std::setfill('0') << std::setw(3) << nowMs.count();
    
    return ss.str();
}

// Get level name as string
std::string DebugLogger::getLevelName(int level) {
    const char* names[] = {"OFF", "ERROR", "WARNING", "INFO", "DEBUG", "TRACE"};
    if (level >= DEBUG_OFF && level <= DEBUG_TRACE) {
        return names[level];
    }
    return "UNKNOWN";
}

// Log a message at the specified level
void DebugLogger::log(int messageLevel, const std::string& message) {
    if (messageLevel <= level) {
        std::stringstream logStream;
        
        // Add timestamp if enabled
        if (showTimestamp) {
            logStream << "[" << getCurrentTimestamp() << "] ";
        }
        
        // Add level prefix
        logStream << "[" << getLevelName(messageLevel) << "] ";
        
        // Add component prefix if set
        if (!componentPrefix.empty()) {
            logStream << "[" << componentPrefix << "] ";
        }
        
        // Add the message
        logStream << message;
        
        // Output to correct stream
        if (messageLevel == DEBUG_ERROR) {
            std::cerr << logStream.str() << std::endl;
        } else {
            std::cout << logStream.str() << std::endl;
        }
    }
}

// Global function to log a message
void log_message(int level, const std::string& message) {
    DebugLogger::getInstance()->log(level, message);
}

// Global function to log a message with a component prefix
void log_message(int level, const std::string& component, const std::string& message) {
    // Store the previous component prefix
    std::string previousPrefix = DebugLogger::getInstance()->getComponentPrefix();
    
    // Set the new component prefix
    DebugLogger::getInstance()->setComponentPrefix(component);
    
    // Log the message
    DebugLogger::getInstance()->log(level, message);
    
    // Restore the previous component prefix
    DebugLogger::getInstance()->setComponentPrefix(previousPrefix);
}