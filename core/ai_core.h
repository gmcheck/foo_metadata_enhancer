#pragma once

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
 * 管理缓存层、Worker进程和任务队列，提供同步和异步分析接口
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
    
    std::optional<std::map<std::string, std::string>> rollback_snapshot(const std::string& track_id);
    
    bool ensure_snapshot(
        const std::string& track_id,
        const std::map<std::string, std::string>& snapshot
    );
    
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
