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
 *
 * V8.2: classify_genre / identify_edition 已移除。
 * - genre 由 Scrape 从 MusicBrainz 抓取
 * - edition 已废弃（AI 推断不可靠）
 */
struct TrackOptions {
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
 *
 * V8.2: classify_genre / identify_edition 已移除。
 * - genre 由 Scrape 从 MusicBrainz 抓取
 * - edition 已废弃
 * Enhance 仅做翻译（基于已有元数据生成新价值）。
 */
struct EnhancementOptions {
    // 翻译
    bool translate_title = true;
    bool translate_album = true;
    bool translate_artist = true;
    std::string target_language = "zh";

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
    bool cache_hit = false;         ///< 是否缓存命中（缓存命中不消耗 AI 资源）
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

    std::string error;
    uint32_t tokens_used = 0;       ///< AI 调用消耗的令牌数（仅远程 API 调用累计）
    bool cache_hit = false;         ///< 是否缓存命中（缓存命中不消耗 tokens）
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
 * @brief 版本识别结果结构体（V8.2 已废弃）
 *
 * 保留此结构体仅用于向后兼容旧数据/JSON。Enhance 不再产出 edition。
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
 *
 * V8.2: edition 字段已移除（已废弃）。
 * genre 字段保留作为兼容（实际 genre 由 Scrape 抓取，不在此结果中产出）。
 */
struct AIResult {
    GenreResult genre;              ///< 流派结果（兼容字段，Enhance 不再产出）
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

// ==================== Normalize 类型 ====================

/**
 * @brief Normalize 候选项：一个 alias 及其代表歌曲/专辑上下文
 */
struct NormalizeCandidate {
    std::string alias;                                  ///< 原始写法
    std::vector<std::map<std::string, std::string>> examples; ///< 上下文（title/album 等），辅助 AI 判断
};

/**
 * @brief Normalize 选项
 */
struct NormalizeOptions {
    std::string field = "artist";       ///< 目标字段（artist/album_artist/album/genre/label/composer/publisher）
    int max_examples_per_alias = 3;     ///< 每个 alias 最多带几首歌
    int max_albums_per_alias = 2;       ///< 每个 alias 最多带几张专辑
    float auto_apply_threshold = 1.0f;  ///< 置信度阈值，>=此值自动应用（1.0=总需确认）
};

/**
 * @brief Normalize 分组：一组被判为同一实体的 alias
 */
struct NormalizeGroup {
    std::string canonical_name;         ///< 推荐标准写法
    float confidence = 0.0f;            ///< AI 置信度
    std::vector<std::string> aliases;   ///< 同一实体的所有写法
    std::string reason;                 ///< AI 给出的归并理由
};

/**
 * @brief Normalize 不确定项
 */
struct NormalizeUncertain {
    std::string alias;                  ///< 不确定的 alias
    std::string reason;                 ///< 不确定的原因
};

/**
 * @brief Normalize 单个 track 的写入指令
 *
 * 由 Python 端根据最终 groups 和每个 track 的当前 field values 构造，
 * C++ 端直接用 new_values 写入 tag，无需再做 alias_to_canonical 匹配。
 * 这样避免 C++ 端因 Unicode 表示差异（NFC/NFD 韩文、尾部全角空格等）
 * 导致 alias_to_canonical.find(val) 漏匹配。
 */
struct NormalizeTrackUpdate {
    int track_index = -1;                       ///< 在原始 m_tracks 中的索引
    std::string track_id;                       ///< 音轨 ID
    bool matched = false;                       ///< 是否有任何 value 被替换
    std::vector<std::string> original_values;   ///< 原始 values
    std::vector<std::string> new_values;        ///< 目标 values（已替换 canonical，去重）
    std::string canonical_name;                 ///< 命中的 canonical（如有多个取第一个）
};

/**
 * @brief Normalize 结果
 */
struct NormalizeResult {
    std::vector<NormalizeGroup> groups;                 ///< 已分组建议
    std::vector<NormalizeUncertain> uncertain;          ///< 无法判定的 alias
    std::vector<NormalizeTrackUpdate> track_updates;    ///< 每个 track 的写入指令

    // 统计信息（由 Python 端回填，用于 CompletionStats 展示）
    int cache_hits = 0;       ///< SQLite normalize_alias 表命中的 alias 数
    int api_calls = 0;        ///< AI 调用次数（0=全缓存命中）
    int tokens_used = 0;      ///< AI token 用量（0=全缓存命中或未统计）
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
 * @brief 错误来源分类
 *
 * 用于错误反馈 UI 按来源展示不同图标、文案和修复建议按钮。
 * 设计原则：每类错误对应一组明确的用户引导动作。
 */
enum class ErrorCategory {
    Unknown,            ///< 未分类（兜底）
    Config,             ///< 配置错误（缺 API Key / Python 路径错误 / Provider 未选）
    Network,            ///< 网络错误（连接超时 / DNS 失败 / SSL 错误）
    Auth,               ///< 鉴权失败（API Key 无效 / 401 / 403）
    RateLimit,          ///< 速率限制（429 / 配额耗尽）
    ApiError,           ///< 上游 API 业务错误（模型不存在 / 请求格式错误 / 5xx）
    PythonWorker,       ///< Python worker 异常（启动失败 / crash / IPC 断开）
    AiInference,        ///< AI 推理错误（响应解析失败 / JSON 格式错误 / 字段缺失）
    DataSource,         ///< 外部数据源错误（MusicBrainz / Discogs 查询失败）
    FileSystem,         ///< 文件系统错误（无法读 tag / 写 tag / 数据库锁定）
    UserCancelled,      ///< 用户主动取消（非错误，用于流程跳转）
    NoData,              ///< 无可处理数据（无选中曲目 / 无快照 / 字段全空）
};

/**
 * @brief 错误信息结构体
 */
struct ErrorInfo {
    std::string code;           ///< 错误代码
    std::string message;        ///< 错误消息（用户可见的一行摘要）
    std::string detail;         ///< 错误详情（技术细节，多行）
    bool retryable = false;     ///< 是否可重试
    bool can_retry = false;     ///< 是否可以重试
    ErrorLevel level = ErrorLevel::Error; ///< 错误级别
    ErrorCategory category = ErrorCategory::Unknown; ///< 错误来源分类
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
