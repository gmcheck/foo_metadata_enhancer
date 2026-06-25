#pragma once

#include <string>
#include <vector>
#include <optional>
#include <cstdint>
#include <chrono>
#include <map>
#include <nlohmann/json.hpp>

namespace ai_metadata {

/**
 * @brief 音轨分析选项结构体
 */
struct TrackOptions {
    bool classify_genre = true;
    bool identify_edition = true;
    bool translate_metadata = true;
};

/**
 * @brief V8新增：刮削选项结构体 - 阶段一
 */
struct ScrapingOptions {
    // 查询辅助字段（用于提高匹配精度，不会被修改）
    std::string album;
    std::string year;
    
    // 基础字段补全/纠正
    bool scrape_title = true;
    bool scrape_artist = true;
    bool scrape_album = true;
    bool scrape_year = true;
    bool scrape_track_number = true;
    bool scrape_disc_number = true;
    
    // 人员字段补全
    bool scrape_composer = true;
    bool scrape_lyricist = true;
    bool scrape_conductor = false;
    bool scrape_performer = false;
    bool scrape_producer = false;
    bool scrape_engineer = false;
    bool scrape_orchestra = false;
    bool scrape_ensemble = false;
    
    // 元数据补全
    bool scrape_label = true;
    bool scrape_country = false;
    bool scrape_catalog_number = false;
    bool scrape_original_artist = false;
    bool scrape_original_album = false;
    bool scrape_original_year = false;
    
    // 标识符补全
    bool scrape_musicbrainz_id = true;
    bool scrape_isrc = false;
    
    // 数据源配置
    bool enable_musicbrainz = true;
    bool enable_discogs = true;
    bool enable_ai = true;
    
    // 置信度设置
    float auto_accept_threshold = 0.9f;
    float confirm_threshold = 0.7f;
};

/**
 * @brief V8新增：增强选项结构体 - 阶段二
 */
struct EnhancementOptions {
    // 翻译
    bool translate_title = true;
    bool translate_album = true;
    bool translate_artist = true;
    std::string target_language = "zh";
    
    // 分类识别
    bool classify_genre = true;
    bool identify_edition = true;
    
    // 其他增强
    bool scrape_mood = false;
    bool scrape_bpm = false;
};

/**
 * @brief V8新增：数据源类型枚举
 */
enum class DataSourceType {
    MUSICBRAINZ,
    DISCOGS,
    AI
};

/**
 * @brief V8新增：刮削字段结果结构体
 */
struct ScrapedField {
    std::string value;
    float confidence = 0.0f;
    DataSourceType source = DataSourceType::AI;
    std::string raw_data;
};

/**
 * @brief V8新增：音轨刮削结果结构体
 */
struct TrackScrapingResult {
    std::string track_id;
    bool success = false;
    std::map<std::string, ScrapedField> scraped_fields;
    std::string release_id;
    DataSourceType release_source = DataSourceType::MUSICBRAINZ;
    std::string error;
};

enum class FailureReason {
    None,
    Timeout,
    WorkerCrash,
    NetworkError,
    NoCandidates,
    AIDecisionFailed,
    Unknown
};

struct FailedTrackInfo {
    std::string track_id;
    FailureReason reason = FailureReason::Unknown;
    std::string error_message;
    int retry_count = 0;
};

struct EnhancementResult {
    std::string track_id;
    bool success = false;
    
    std::string title_zh;
    std::string album_zh;
    std::string artist_zh;
    float translation_confidence = 0.0f;
    
    std::string genre_value;
    float genre_confidence = 0.0f;
    
    std::string edition_value;
    float edition_confidence = 0.0f;
    
    std::string error;
};

/**
 * @brief V8新增：缺失字段信息结构体
 */
struct MissingFieldInfo {
    std::string track_id;
    std::vector<std::string> missing_fields;
};

/**
 * @brief 音轨输入数据结构体
 * 
 * 包含从foobar2000传入的音轨元数据信息
 */
struct TrackInput {
    std::string track_id;          ///< 音轨唯一标识符（TrackUID）
    std::string file_path;         ///< 文件路径
    std::string title;             ///< 标题
    std::string album;             ///< 专辑名
    std::string artist;            ///< 艺术家
    std::string album_artist;      ///< 专辑艺术家
    std::string musicbrainz_id;    ///< MusicBrainz ID
    std::string file_hash;         ///< 文件哈希
    uint32_t duration_sec = 0;     ///< 时长（秒）
    uint32_t track_number = 0;     ///< 音轨号
    uint32_t disc_number = 0;      ///< 光盘号
    uint32_t subsong_index = 0;    ///< 子音轨索引
    std::string year;              ///< 年份
    std::string genre;             ///< 现有流派
    std::string genre_existing;    ///< 现有流派（别名）
    std::string comment;           ///< 注释
    std::string label;             ///< 厂牌
    std::string language_hint;     ///< 语言提示
    std::string composer;          ///< 作曲家
    std::string lyricist;          ///< 作词家
    std::string conductor;         ///< 指挥
    std::string performer;         ///< 演奏者
    int year_int = 0;              ///< 年份（整数）
    TrackOptions options;          ///< 分析选项
};

/**
 * @brief 流派分析结果结构体
 */
struct GenreResult {
    std::string value;      ///< 流派值
    double confidence = 0.0; ///< 置信度（0.0-1.0）
    std::string source;     ///< 来源（ai/musicbrainz/user）
};

/**
 * @brief 版本识别结果结构体
 */
struct EditionResult {
    std::string value;      ///< 版本类型
    double confidence = 0.0; ///< 置信度（0.0-1.0）
};

/**
 * @brief 翻译结果结构体
 */
struct TranslationResult {
    std::string title_zh;   ///< 中文标题
    std::string album_zh;   ///< 中文专辑名
    std::string artist_zh;  ///< 中文艺术家名
};

/**
 * @brief AI分析结果结构体
 */
struct AIResult {
    GenreResult genre;              ///< 流派结果
    EditionResult edition;          ///< 版本结果
    TranslationResult translation;  ///< 翻译结果
    double translation_confidence = 0.0; ///< 翻译置信度
};

/**
 * @brief 原始元数据结构体
 */
struct OriginalMetadata {
    std::string title;        ///< 原始标题
    std::string album;        ///< 原始专辑名
    std::string artist;       ///< 原始艺术家
    std::string album_artist; ///< 原始专辑艺术家
    std::string year;         ///< 原始年份
};

/**
 * @brief 分析信息结构体
 */
struct AnalysisInfo {
    std::string model;              ///< 使用的模型名称
    std::string model_type;         ///< 模型类型（local/remote）
    uint32_t tokens_used = 0;       ///< 使用的令牌数
    uint32_t api_latency_ms = 0;    ///< API延迟（毫秒）
    bool cache_hit = false;         ///< 是否缓存命中
    uint32_t batch_size = 1;        ///< 批处理大小
};

/**
 * @brief 音轨分析结果结构体
 */
struct TrackAnalysisResult {
    std::string track_id;      ///< 音轨ID
    std::string timestamp;     ///< 时间戳
    bool success = false;      ///< 是否成功
    std::string error;         ///< 错误信息
    OriginalMetadata original; ///< 原始元数据
    AIResult ai;               ///< AI分析结果
    AnalysisInfo analysis_info; ///< 分析信息
};

/**
 * @brief 错误级别枚举
 */
enum class ErrorLevel {
    Info,     ///< 信息
    Warning,  ///< 警告
    Error,    ///< 错误
    Critical  ///< 严重错误
};

/**
 * @brief 错误信息结构体
 */
struct ErrorInfo {
    std::string code;           ///< 错误代码
    std::string message;        ///< 错误消息
    std::string detail;         ///< 错误详情
    bool retryable = false;     ///< 是否可重试
    bool can_retry = false;     ///< 是否可以重试
    ErrorLevel level = ErrorLevel::Error; ///< 错误级别
};

/**
 * @brief 批量响应结构体
 */
struct BatchResponse {
    std::string id;                           ///< 请求ID
    bool success = false;                     ///< 是否成功
    int count = 0;                            ///< 结果数量
    std::vector<nlohmann::json> results;      ///< 结果列表 (JSON格式)
    std::optional<ErrorInfo> error;           ///< 错误信息（可选）
};

/**
 * @brief 任务结构体
 */
struct Task {
    std::string id;                                      ///< 任务ID
    std::string method;                                  ///< 方法名
    std::vector<TrackInput> tracks;                      ///< 音轨列表
    uint32_t priority = 5;                               ///< 优先级（数字越小优先级越高）
    uint32_t timeout_ms = 30000;                         ///< 超时时间（毫秒）
    std::chrono::system_clock::time_point submit_time;   ///< 提交时间
    
    size_t batch_index = 0;                              ///< 批次索引（从0开始）
    size_t total_batches = 1;                            ///< 总批次数
    size_t track_offset = 0;                             ///< 在原始音轨列表中的偏移量
    std::string parent_task_id;                          ///< 父任务ID（用于分批任务关联）
};

/**
 * @brief 缓存统计结构体
 */
struct CacheStatistics {
    size_t total_entries = 0;     ///< 总条目数
    size_t total_hits = 1;        ///< 总命中数
    size_t total_misses = 0;      ///< 总未命中数
    double hit_rate = 0.0;        ///< 命中率
    size_t database_size_bytes = 0; ///< 数据库大小（字节）
    size_t api_calls_saved = 0;   ///< 节省的API调用数
    double db_size_mb = 0.0;      ///< 数据库大小（MB）
};

/**
 * @brief Worker信息结构体
 */
struct WorkerInfo {
    int id = 0;                   ///< Worker ID
    std::string status;           ///< 状态
    int queue_size = 0;           ///< 队列大小
    bool healthy = false;         ///< 是否健康
    uint32_t requests_processed = 0; ///< 已处理请求数
    uint32_t avg_latency_ms = 0;  ///< 平均延迟（毫秒）
};

}
