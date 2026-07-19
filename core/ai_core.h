#pragma once

// =============================================================================
// foo_metadata_enhancer - 三功能边界定义（V8.2）
// =============================================================================
// 本头文件通过 AICore 暴露三个同步入口，对应三个互不重叠的功能层：
//
//   1. Scrape    (stage1_scrape_sync)
//      - 职责：从外部数据源获取本地没有的数据（事实获取）
//      - 数据源：MusicBrainz / Discogs / AI 降级
//      - 产物：title / artist / album / year / genre / composer / ... / musicbrainz_id
//      - V8.2 变更：genre 改由本层从 MusicBrainz recording 详情获取
//
//   2. Enhancer  (stage2_enhance_sync)
//      - 职责：基于已有元数据生成新价值（不获取新事实）
//      - 当前能力：中文翻译（title_zh / album_zh / artist_zh）
//      - V8.2 变更：移除 edition 识别（AI 推断不可靠）；genre 不再由本层产出
//
//   3. Normalize (normalize_sync)
//      - 职责：已有 Tag → 标准 Tag（一致性归一化）
//      - 当前能力：歌手名规范化（alias → canonical）
//      - 未来扩展：Genre 映射等
//      - 注意：本接口不写 SQLite、不修改 Tag；用户确认后由调用方写入
//
// 回滚：每种操作对应独立的 OperationType 快照（见 backup_manager.h），
//       回滚时仅恢复该操作影响的字段（见 menu_handler.cpp get_operation_fields）。
// =============================================================================

#include "../include/types.h"
#include "../include/constants.h"
#include "cache_layer.h"
#include "worker_manager.h"
#include "task_queue.h"
#include "logger.h"
#include "../include/backup_manager.h"
#include <memory>
#include <string>
#include <functional>
#include <map>
#include <mutex>
#include <atomic>
#include <future>

namespace ai_metadata {

/**
 * @brief AI核心类，负责协调音轨分析流程
 *
 * 管理缓存层、Worker进程和任务队列，提供同步和异步分析接口。
 * 暴露 Scrape / Enhancer / Normalize 三个互不重叠的同步入口（见文件头注释）。
 */
class AICore {
public:
    using ProgressCallback = std::function<void(int current, int total, const std::string& message)>;
    
    using AbortCallback = std::function<bool()>;
    
    AICore();
    
    ~AICore();
    
    bool initialize();
    
    void shutdown();
    
    std::vector<TrackScrapingResult> stage1_scrape_sync(
        const std::vector<TrackInput>& tracks,
        const ScrapingOptions& options,
        ProgressCallback on_progress = nullptr,
        AbortCallback on_abort = nullptr
    );
    
    std::vector<EnhancementResult> stage2_enhance_sync(
        const std::vector<TrackInput>& tracks,
        const EnhancementOptions& options,
        ProgressCallback on_progress = nullptr,
        AbortCallback on_abort = nullptr
    );

    /**
     * @brief Normalize 同步接口：元数据实体归一化
     *
     * 流程：
     *   1. 从 tracks 提取 field 字段值，去重得 alias 列表
     *   2. 先查 SQLite normalize_alias 表：命中的直接记为"已知映射"
     *   3. 未命中的收集 examples（最多 N 首/张），批量发送给 AI
     *   4. 解析 AI 响应为 NormalizeResult
     *   5. Python 端根据最终 groups 为每个 track 构造 track_updates（目标 values），
     *      C++ 端直接用 track_updates 写入，不再做 alias_to_canonical 匹配
     *
     * 注意：本方法不写 SQLite，不修改 Tag。用户确认后由调用方写入。
     *
     * @param tracks 选中音轨列表
     * @param options Normalize 选项（field / max_examples 等）
     * @param track_field_values 每个 track 当前 field 的所有 values（multi-value 支持），
     *        与 tracks 一一对应，传给 Python 用于构造 track_updates
     * @param on_progress 进度回调（可选）
     * @param on_abort 中止回调（可选）
     * @return NormalizeResult，包含 groups / uncertain / track_updates
     */
    std::optional<NormalizeResult> normalize_sync(
        const std::vector<TrackInput>& tracks,
        const NormalizeOptions& options,
        const std::vector<std::vector<std::string>>& track_field_values,
        ProgressCallback on_progress = nullptr,
        AbortCallback on_abort = nullptr
    );

    std::optional<std::map<std::string, std::string>> rollback_snapshot(const std::string& track_id);

    bool ensure_snapshot(
        const std::string& track_id,
        const std::map<std::string, std::string>& snapshot
    );

    // ============= 多操作类型回滚接口 =============
    // 同一 track 可有多种操作（scrape/translate/normalize）的初始快照
    // 每种操作的回滚数据独立保存，回滚时按类型恢复到该操作执行前的状态

    bool ensure_operation_snapshot(
        const std::string& track_id,
        OperationType op_type,
        const std::map<std::string, std::string>& snapshot
    );

    std::optional<std::map<std::string, std::string>> rollback_operation(
        const std::string& track_id,
        OperationType op_type
    );

    // 批量回滚指定操作类型：返回每个 track 的初始快照
    std::map<std::string, std::map<std::string, std::string>> batch_rollback_operations(
        const std::vector<std::string>& track_ids,
        OperationType op_type
    );

    // 批量查询：返回每个 track 拥有的可回滚操作类型（供 UI 显示哪些可回滚）
    std::map<std::string, std::vector<OperationType>> get_operations_for_tracks(
        const std::vector<std::string>& track_ids
    );

    // 注：batch_upsert_aliases 已移除
    // normalize_alias 表已迁移到 Python 端 NormalizeStore 管理，
    // C++ 通过 save_normalize_aliases IPC 方法通知 Python 写入（见 menu_handler.cpp）。

    /**
     * @brief 保存 normalize alias 映射到 Python 端 SQLite 知识库
     *
     * 用户在 Normalize 预览对话框确认后调用。通过 IPC 通知 Python worker 把
     * 选中的 alias → canonical 映射写入 normalize_alias 表（Python 端管理）。
     * 后续 normalize 调用会通过 NormalizeStore.get_aliases 命中这些条目，
     * 跳过 AI 调用。
     *
     * @param field 目标字段（artist/album_artist/album/genre/...）
     * @param entries 待写入的 alias 条目列表，每条包含：
     *                alias_name / canonical_name / source / confidence / confirmed / reason
     * @return 是否成功（IPC 发送成功且 Python 返回 success=true）
     */
    struct NormalizeAliasEntry {
        std::string alias_name;
        std::string canonical_name;
        std::string source = "ai";
        double confidence = 1.0;
        bool confirmed = true;
        std::string reason;
    };
    bool save_normalize_aliases(const std::string& field,
                                const std::vector<NormalizeAliasEntry>& entries);

    bool save_snapshot(
        const std::string& track_id,
        const std::map<std::string, std::string>& snapshot
    );
    
    std::map<std::string, std::string> get_snapshot(const std::string& track_id);
    
    bool has_snapshot(const std::string& track_id);
    
    void save_stage1_cache(
        const std::string& cache_key,
        const TrackScrapingResult& result,
        const TrackInput& input
    );
    
    void save_stage2_cache(
        const std::string& cache_key,
        const EnhancementResult& result,
        const TrackInput& input,
        const EnhancementOptions& options
    );
    
    std::string generate_stage1_cache_key(const TrackInput& input);
    std::string generate_stage2_cache_key(const TrackInput& input, const EnhancementOptions& options);
    
    /**
     * @brief 检查是否已初始化
     * @return 已初始化返回true
     */
    bool is_initialized() const { return initialized_; }
    
    /**
     * @brief 检查是否正在处理
     * @return 正在处理返回true
     */
    bool is_processing() const { return processing_; }
    
    /**
     * @brief 停止当前处理
     */
    void stop_processing() { 
        Logger::instance().debug("stop_processing: setting processing_ to false", __FILE__, __FUNCTION__);
        processing_ = false; 
    }
    
    /**
     * @brief 请求中断当前任务
     * @param task_id 任务ID
     */
    void request_abort(const std::string& task_id);
    
    /**
     * @brief 清除中断标志
     * @param task_id 任务ID
     */
    void clear_abort(const std::string& task_id);
    
    /**
     * @brief 检查是否请求了中断
     * @param task_id 任务ID
     * @return 已请求中断返回true
     */
    bool is_abort_requested(const std::string& task_id);
    
    /**
     * @brief 设置中断目录
     * @param path 中断标志文件目录
     */
    void set_abort_dir(const std::string& path) { abort_dir_ = path; }
    
    /**
     * @brief 设置Worker路径
     * @param path Worker脚本路径
     */
    void set_worker_path(const std::string& path) { worker_path_ = path; }
    
    /**
     * @brief 设置缓存路径
     * @param path 缓存数据库路径
     */
    void set_cache_path(const std::string& path) { cache_path_ = path; }
    
    /**
     * @brief 设置批处理大小
     * @param size 每批处理的音轨数
     */
    void set_taskqueue_batch_size(uint32_t size) { batch_size_ = size; }
    
    void set_ai_batch_size(uint32_t size) { ai_batch_size_ = size; }
    
    /**
     * @brief 动态更新Worker的日志级别（无需重启worker）
     * @param level_name 日志级别名称 (DEBUG/INFO/WARNING/ERROR)
     */
    void update_worker_log_level(const std::string& level_name);
    
    /**
     * @brief 获取缓存统计信息
     * @return 缓存统计结构体
     */
    CacheStatistics get_cache_statistics();
    
    /**
     * @brief 清除所有缓存
     */
    void clear_cache();
    
    /**
     * @brief 根据track_id列表清除缓存
     * @param track_ids 要清除的track_id列表
     * @return 清除的条目数量
     */
    int clear_cache_by_track_ids(const std::vector<std::string>& track_ids);
    
    /**
     * @brief 获取配置值
     * @param key 配置键
     * @return 配置值字符串
     */
    std::string get_config(const std::string& key);
    
    /**
     * @brief 设置配置值
     * @param key 配置键
     * @param value 配置值
     */
    void set_config(const std::string& key, const std::string& value);
    
    /**
     * @brief 重启所有Worker进程
     * @return 重启成功返回true
     */
    bool restart_all_workers();
    
    /**
     * @brief 检查Worker是否健康
     * @return 健康返回true
     */
    bool is_worker_healthy() const;
    
    /**
     * @brief 获取Worker信息列表
     * @return Worker信息列表
     */
    std::vector<WorkerInfo> get_worker_info() const;
    
    /**
     * @brief 测试API连接
     * @param provider AI提供商名称
     * @param model 模型名称
     * @param timeout_ms 超时时间（毫秒）
     * @return 测试结果（JSON格式字符串）
     */
    std::string test_api_connection(
        const std::string& provider,
        const std::string& model,
        uint32_t timeout_ms = 30000
    );

private:
    std::string generate_request_id();
    
    std::unique_ptr<CacheLayer> cache_;
    std::unique_ptr<WorkerManager> worker_manager_;
    std::unique_ptr<BackupManager> backup_manager_;
    std::unique_ptr<TaskQueue> task_queue_;
    
    std::string worker_path_;
    std::string cache_path_;
    std::string abort_dir_;
    
    static constexpr uint32_t TIMEOUT_MS = constants::DEFAULT_GLOBAL_TIMEOUT_MS;
    
    uint32_t batch_size_ = constants::DEFAULT_BATCH_SIZE;
    uint32_t ai_batch_size_ = constants::DEFAULT_AI_BATCH_SIZE;
    
    std::atomic<bool> initialized_{false};
    std::atomic<bool> processing_{false};
    
    std::mutex mutex_;
    
    std::vector<TrackScrapingResult> process_batch(
        const std::vector<TrackInput>& tracks,
        const ScrapingOptions& options,
        const std::string& task_id,
        AbortCallback on_abort
    );
};

}
