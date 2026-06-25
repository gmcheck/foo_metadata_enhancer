#pragma once

#include "../include/constants.h"
#include <string>
#include <mutex>
#include <atomic>

namespace ai_metadata {

#define LOG_DEBUG(msg) ai_metadata::Logger::instance().debug(msg, __FILE__, __FUNCTION__)
#define LOG_INFO(msg) ai_metadata::Logger::instance().info(msg, __FILE__, __FUNCTION__)
#define LOG_WARN(msg) ai_metadata::Logger::instance().warning(msg, __FILE__, __FUNCTION__)
#define LOG_ERROR(msg) ai_metadata::Logger::instance().error(msg, __FILE__, __FUNCTION__)

class Logger {
public:
    static Logger& instance();
    
    void set_log_level(constants::LogLevel level);
    
    constants::LogLevel get_log_level() const;
    
    void log(constants::LogLevel level, const std::string& message, 
             const char* file = nullptr, const char* func = nullptr);
    
    void debug(const std::string& message, const char* file = nullptr, const char* func = nullptr);
    
    void info(const std::string& message, const char* file = nullptr, const char* func = nullptr);
    
    void warning(const std::string& message, const char* file = nullptr, const char* func = nullptr);
    
    void error(const std::string& message, const char* file = nullptr, const char* func = nullptr);
    
private:
    Logger();
    
    ~Logger();
    
    void log_to_file(constants::LogLevel level, const std::string& message,
                     const char* file = nullptr, const char* func = nullptr);
    
    std::string get_log_file_path();
    
    std::string extract_filename(const char* file_path);
    
    std::atomic<constants::LogLevel> log_level_;
    std::mutex mutex_;
    std::string log_file_path_;
};

}
