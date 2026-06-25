#include "logger.h"
#include <fstream>
#include <chrono>
#include <iomanip>
#include <ctime>
#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#endif

namespace ai_metadata {

Logger& Logger::instance() {
    static Logger instance;
    return instance;
}

Logger::Logger() : log_level_(ai_metadata::constants::LogLevel::Info) {
    log_file_path_ = "";
}

Logger::~Logger() = default;

void Logger::set_log_level(ai_metadata::constants::LogLevel level) {
    log_level_ = level;
}

ai_metadata::constants::LogLevel Logger::get_log_level() const {
    return log_level_;
}

void Logger::log(ai_metadata::constants::LogLevel level, const std::string& message,
                 const char* file, const char* func) {
    if (level < log_level_) {
        return;
    }
    
    log_to_file(level, message, file, func);
}

void Logger::debug(const std::string& message, const char* file, const char* func) {
    log(ai_metadata::constants::LogLevel::Debug, message, file, func);
}

void Logger::info(const std::string& message, const char* file, const char* func) {
    log(ai_metadata::constants::LogLevel::Info, message, file, func);
}

void Logger::warning(const std::string& message, const char* file, const char* func) {
    log(ai_metadata::constants::LogLevel::Warning, message, file, func);
}

void Logger::error(const std::string& message, const char* file, const char* func) {
    log(ai_metadata::constants::LogLevel::Error, message, file, func);
}

std::string Logger::extract_filename(const char* file_path) {
    if (!file_path) return "";
    std::string path(file_path);
    size_t pos = path.find_last_of("\\/");
    if (pos != std::string::npos) {
        return path.substr(pos + 1);
    }
    return path;
}

void Logger::log_to_file(ai_metadata::constants::LogLevel level, const std::string& message,
                         const char* file, const char* func) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (log_file_path_.empty()) {
        log_file_path_ = get_log_file_path();
    }
    
    std::ofstream log_file(log_file_path_, std::ios::app);
    if (!log_file.is_open()) {
        return;
    }
    
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    struct tm timeinfo;
#ifdef _WIN32
    localtime_s(&timeinfo, &time);
#else
    localtime_r(&time, &timeinfo);
#endif
    
    char time_str[64];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", &timeinfo);
    
    log_file << time_str << " [" << constants::log_level_to_string(level) << "] [C++]";
    
    if (file && func) {
        std::string filename = extract_filename(file);
        log_file << " [" << filename << "::" << func << "]";
    }
    
    std::string cleaned_message = message;
    std::replace(cleaned_message.begin(), cleaned_message.end(), '\n', ' ');
    std::replace(cleaned_message.begin(), cleaned_message.end(), '\r', ' ');
    
    std::string::size_type pos;
    while ((pos = cleaned_message.find("  ")) != std::string::npos) {
        cleaned_message.replace(pos, 2, " ");
    }
    
    log_file << " " << cleaned_message << std::endl;
}

std::string Logger::get_log_file_path() {
    std::string log_path;
    
#ifdef _WIN32
    char dll_path[MAX_PATH] = {0};
    HMODULE hModule = GetModuleHandleA("foo_metadata_enhancer.dll");
    if (hModule) {
        GetModuleFileNameA(hModule, dll_path, MAX_PATH);
        std::string dll_dir(dll_path);
        size_t pos = dll_dir.find_last_of("\\/");
        if (pos != std::string::npos) {
            std::string logs_dir = dll_dir.substr(0, pos) + "\\foo_metadata_enhancer\\logs";
            CreateDirectoryA((dll_dir.substr(0, pos) + "\\foo_metadata_enhancer").c_str(), NULL);
            CreateDirectoryA(logs_dir.c_str(), NULL);
            log_path = logs_dir + "\\core.log";
        }
    }
#endif
    
    return log_path;
}

}
