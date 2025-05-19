#pragma once

#include <string>
#include <iostream>
#include <chrono>
#include <iomanip>
#include <sstream>

// Debug levels
enum DebugLevel {
    DEBUG_OFF = 0,
    DEBUG_ERROR = 1,
    DEBUG_WARNING = 2,
    DEBUG_INFO = 3,
    DEBUG_DEBUG = 4,
    DEBUG_TRACE = 5
};

class DebugLogger {
private:
    // Private constructor for singleton pattern
    DebugLogger() : level(DEBUG_OFF), showTimestamp(true), componentPrefix("") {}
    
    // Singleton instance
    static DebugLogger* instance;
    
    // Current debug level
    int level;
    
    // Whether to show timestamps in log messages
    bool showTimestamp;
    
    // Component prefix for log messages
    std::string componentPrefix;
    
    // Get current timestamp as string
    std::string getCurrentTimestamp() const;
    
public:
    // Delete copy constructor and assignment operator
    DebugLogger(const DebugLogger&) = delete;
    DebugLogger& operator=(const DebugLogger&) = delete;
    
    // Get the singleton instance
    static DebugLogger* getInstance();
    
    // Set the debug level
    void setLevel(int level);
    
    // Get the current debug level
    int getLevel() const;
    
    // Enable/disable timestamps
    void setShowTimestamp(bool show);
    
    // Set component prefix
    void setComponentPrefix(const std::string& prefix);
    
    // Get component prefix
    std::string getComponentPrefix() const;
    
    // Log a message at the specified level
    void log(int messageLevel, const std::string& message);
    
    // Get level name as string
    static std::string getLevelName(int level);
};

// Global function to log a message - easier to use than the singleton directly
void log_message(int level, const std::string& message);

// Global function to log a message with a component prefix
void log_message(int level, const std::string& component, const std::string& message);