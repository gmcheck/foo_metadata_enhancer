#pragma once

#include "../include/types.h"
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <atomic>
#include <thread>

namespace ai_metadata {

/**
 * @brief 任务队列类，管理分析任务的优先级队列
 * 
 * 支持优先级排序、任务处理和统计功能
 */
class TaskQueue {
public:
    /**
     * @brief 任务回调类型
     * @param task 要处理的任务
     */
    using TaskCallback = std::function<void(const Task&)>;
    
    /**
     * @brief 进度回调类型
     * @param completed 已完成任务数
     * @param total 总任务数
     * @param message 进度消息
     */
    using ProgressCallback = std::function<void(size_t completed, size_t total, const std::string& message)>;
    
    /**
     * @brief 构造函数
     */
    TaskQueue();
    
    /**
     * @brief 析构函数
     */
    ~TaskQueue();
    
    TaskQueue(const TaskQueue&) = delete;
    TaskQueue& operator=(const TaskQueue&) = delete;
    
    /**
     * @brief 将任务推入队列
     * @param task 要添加的任务
     */
    void push(const Task& task);
    
    /**
     * @brief 从队列中弹出任务
     * @param task 输出任务
     * @param timeout_ms 超时时间（毫秒），0表示不等待
     * @return 成功获取任务返回true
     */
    bool pop(Task& task, uint32_t timeout_ms = 0);
    
    /**
     * @brief 检查队列是否为空
     * @return 队列为空返回true
     */
    bool empty() const;
    
    /**
     * @brief 获取队列大小
     * @return 队列中的任务数
     */
    size_t size() const;
    
    /**
     * @brief 清空队列
     */
    void clear();
    
    /**
     * @brief 设置任务回调
     * @param callback 任务回调函数
     */
    void set_callback(TaskCallback callback) { callback_ = callback; }
    
    /**
     * @brief 设置进度回调
     * @param callback 进度回调函数
     */
    void set_progress_callback(ProgressCallback callback) { progress_callback_ = callback; }
    
    /**
     * @brief 开始处理队列中的任务
     */
    void start_processing();
    
    /**
     * @brief 停止处理任务
     */
    void stop_processing();
    
    /**
     * @brief 检查是否正在处理
     * @return 正在处理返回true
     */
    bool is_processing() const { return processing_; }
    
    /**
     * @brief 更改任务优先级
     * @param task_id 任务ID
     * @param new_priority 新优先级
     */
    void prioritize(const std::string& task_id, uint32_t new_priority);
    
    /**
     * @brief 从队列中移除任务
     * @param task_id 任务ID
     */
    void remove(const std::string& task_id);
    
    /**
     * @brief 队列统计结构体
     */
    struct Statistics {
        size_t total_submitted = 0;    ///< 总提交任务数
        size_t total_completed = 0;    ///< 总完成任务数
        size_t total_failed = 0;       ///< 总失败任务数
        size_t current_queue_size = 0; ///< 当前队列大小
        size_t total_batches = 0;      ///< 总批次数
        size_t current_batch_index = 0; ///< 当前批次索引
    };
    
    /**
     * @brief 获取统计信息
     * @return 统计结构体
     */
    Statistics get_statistics() const;
    
    /**
     * @brief 重置统计信息
     */
    void reset_statistics();
    
    /**
     * @brief 设置总批次数
     * @param total 总批次数
     */
    void set_total_batches(size_t total) { 
        std::lock_guard<std::mutex> lock(mutex_);
        stats_.total_batches = total; 
    }
    
private:
    /**
     * @brief 任务比较器，用于优先级队列排序
     */
    struct TaskComparator {
        /**
         * @brief 比较两个任务的优先级
         * @param a 第一个任务
         * @param b 第二个任务
         * @return a优先级低于b返回true
         */
        bool operator()(const Task& a, const Task& b) const {
            if (a.priority != b.priority) {
                return a.priority < b.priority;
            }
            return a.submit_time > b.submit_time;
        }
    };
    
    std::priority_queue<Task, std::vector<Task>, TaskComparator> queue_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    
    TaskCallback callback_;
    ProgressCallback progress_callback_;
    std::atomic<bool> processing_{false};
    std::atomic<bool> stop_requested_{false};
    std::thread processor_thread_;
    
    Statistics stats_;
};

}
