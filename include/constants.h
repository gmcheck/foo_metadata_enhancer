#pragma once

#include <cstdint>
#include <string>

namespace ai_metadata {
namespace constants {

// IPC通信参数
constexpr uint32_t IPC_MAX_MESSAGE_SIZE = 10 * 1024 * 1024;  ///< IPC最大消息大小（10MB），用于Worker进程通信
constexpr uint32_t IPC_VERSION = 1;                          ///< IPC协议版本号

// 批次处理参数
constexpr uint32_t DEFAULT_BATCH_SIZE = 50;                  ///< 默认批次大小，Stage1刮削时每批处理的曲目数
constexpr uint32_t DEFAULT_AI_BATCH_SIZE = 10;               ///< AI批次大小，Stage2增强时每批处理的曲目数

// 超时参数
constexpr uint32_t BASE_TIMEOUT_MS = 120000;                 ///< 基础超时时间（2分钟），批次处理的固定超时
constexpr uint32_t PER_TRACK_TIMEOUT_MS = 60000;             ///< 每曲目额外超时（1分钟），动态计算：BASE + count * PER_TRACK
constexpr uint32_t CHECK_INTERVAL_MS = 500;                  ///< 响应检查间隔（毫秒），轮询Worker响应的频率
constexpr uint32_t WORKER_CHECK_INTERVAL_MS = 1000;          ///< Worker健康检查间隔（毫秒），检测超时请求的频率

// Worker进程参数
constexpr uint32_t DEFAULT_GLOBAL_TIMEOUT_MS = 48 * 60 * 60 * 1000;  ///< 全局超时（48小时），Worker进程最大运行时间
constexpr uint32_t DEFAULT_MAX_SILENCE_TIME_MS = 300000;     ///< 最大静默时间（5分钟），无响应后强制重启Worker

// 数据库参数
constexpr uint32_t SQLITE_BUSY_TIMEOUT_MS = 5000;            ///< SQLite忙等待超时（5秒），数据库锁定时的重试时间
constexpr uint32_t PROCESS_WAIT_TIMEOUT_MS = 5000;           ///< 进程等待超时（5秒），等待子进程结束的时间

// 存储限制
constexpr uint32_t DEFAULT_MAX_CACHE_SIZE_MB = 500;          ///< 默认缓存数据库最大大小（MB）
constexpr uint32_t DEFAULT_MAX_LOG_FILE_SIZE_MB = 10;        ///< 默认日志文件最大大小（MB）

// UI参数
constexpr uint32_t UI_TIMER_DELAY_MS = 100;                  ///< UI定时器延迟（毫秒），用于偏好设置页面的下拉框填充

/**
 * @brief 日志级别枚举
 */
enum class LogLevel {
    Debug = 0,   ///< 调试级别
    Info = 1,    ///< 信息级别
    Warning = 2, ///< 警告级别
    Error = 3    ///< 错误级别
};

/**
 * @brief 将日志级别转换为字符串
 * @param level 日志级别枚举值
 * @return 日志级别字符串
 */
inline const char* log_level_to_string(LogLevel level) {
    switch (level) {
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info: return "INFO";
        case LogLevel::Warning: return "WARNING";
        case LogLevel::Error: return "ERROR";
        default: return "INFO";
    }
}

/**
 * @brief 将字符串转换为日志级别
 * @param s 日志级别字符串
 * @return 日志级别枚举值
 */
inline LogLevel string_to_log_level(const std::string& s) {
    if (s == "DEBUG") return LogLevel::Debug;
    if (s == "INFO") return LogLevel::Info;
    if (s == "WARNING") return LogLevel::Warning;
    if (s == "ERROR") return LogLevel::Error;
    return LogLevel::Info;
}

/**
 * @brief 获取Worker崩溃错误代码
 * @return 错误代码字符串
 */
inline const char* error_worker_crash() { return "WORKER_CRASH"; }

/**
 * @brief 获取超时错误代码
 * @return 错误代码字符串
 */
inline const char* error_timeout() { return "TIMEOUT"; }

/**
 * @brief 获取AI超时错误代码
 * @return 错误代码字符串
 */
inline const char* error_ai_timeout() { return "AI_TIMEOUT"; }

/**
 * @brief 获取无效请求错误代码
 * @return 错误代码字符串
 */
inline const char* error_invalid_request() { return "INVALID_REQUEST"; }

/**
 * @brief 获取API错误代码
 * @return 错误代码字符串
 */
inline const char* error_api_error() { return "API_ERROR"; }

/**
 * @brief 获取解析错误代码
 * @return 错误代码字符串
 */
inline const char* error_parse_error() { return "PARSE_ERROR"; }

/**
 * @brief 获取插件名称
 * @return 插件名称字符串
 */
inline const char* plugin_name() { return "foo_ai_metadata"; }

/**
 * @brief 获取插件版本
 * @return 插件版本字符串
 */
inline const char* plugin_version() { return "1.0.0"; }

/**
 * @brief 获取关闭方法名
 * @return 方法名字符串
 */
inline const char* method_shutdown() { return "shutdown"; }

/**
 * @brief 获取缓存数据库名称
 * @return 数据库文件名字符串
 */
inline const char* cache_db_name() { return "cache.db"; }

/**
 * @brief 获取配置文件名称
 * @return 配置文件名字符串
 */
inline const char* config_file_name() { return "config.yaml"; }

/**
 * @brief 获取AI流派标签名
 * @return 标签名字符串
 */
inline const char* TAG_AI_GENRE() { return "AI_GENRE"; }

/**
 * @brief 获取AI版本标签名
 * @return 标签名字符串
 */
inline const char* TAG_AI_EDITION() { return "AI_EDITION"; }

/**
 * @brief 获取AI中文标题标签名
 * @return 标签名字符串
 */
inline const char* TAG_AI_TITLE_ZH() { return "AI_TITLE_ZH"; }

/**
 * @brief 获取AI中文专辑名标签名
 * @return 标签名字符串
 */
inline const char* TAG_AI_ALBUM_ZH() { return "AI_ALBUM_ZH"; }

/**
 * @brief 获取AI中文艺术家标签名
 * @return 标签名字符串
 */
inline const char* TAG_AI_ARTIST_ZH() { return "AI_ARTIST_ZH"; }

/**
 * @brief 获取原始流派备份标签名
 * @return 标签名字符串
 */
inline const char* TAG_ORIGINAL_GENRE() { return "AI_ORIGINAL_GENRE"; }

/**
 * @brief 获取原始版本备份标签名
 * @return 标签名字符串
 */
inline const char* TAG_ORIGINAL_EDITION() { return "AI_ORIGINAL_EDITION"; }

/**
 * @brief 获取原始标题备份标签名
 * @return 标签名字符串
 */
inline const char* TAG_ORIGINAL_TITLE() { return "AI_ORIGINAL_TITLE"; }

/**
 * @brief 获取原始专辑备份标签名
 * @return 标签名字符串
 */
inline const char* TAG_ORIGINAL_ALBUM() { return "AI_ORIGINAL_ALBUM"; }

/**
 * @brief 获取原始艺术家备份标签名
 * @return 标签名字符串
 */
inline const char* TAG_ORIGINAL_ARTIST() { return "AI_ORIGINAL_ARTIST"; }

/**
 * @brief 获取标签不存在的标记值
 * @return 标记值字符串
 */
inline const char* TAG_NOT_EXIST_MARKER() { return "__NOT_EXIST__"; }

/**
 * @brief 获取流派标签名（标准标签）
 * @return 标签名字符串
 */
inline const char* TAG_GENRE() { return "GENRE"; }

/**
 * @brief 获取版本标签名（标准标签）
 * @return 标签名字符串
 */
inline const char* TAG_EDITION() { return "EDITION"; }

/**
 * @brief 获取标题标签名（标准标签）
 * @return 标签名字符串
 */
inline const char* TAG_TITLE() { return "TITLE"; }

/**
 * @brief 获取专辑标签名（标准标签）
 * @return 标签名字符串
 */
inline const char* TAG_ALBUM() { return "ALBUM"; }

/**
 * @brief 获取艺术家标签名（标准标签）
 * @return 标签名字符串
 */
inline const char* TAG_ARTIST() { return "ARTIST"; }

}
}
