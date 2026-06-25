#include "task_queue.h"

namespace ai_metadata {

TaskQueue::TaskQueue() = default;

TaskQueue::~TaskQueue() {
    stop_processing();
}

void TaskQueue::push(const Task& task) {
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.push(task);
    stats_.total_submitted++;
    cv_.notify_one();
}

bool TaskQueue::pop(Task& task, uint32_t timeout_ms) {
    std::unique_lock<std::mutex> lock(mutex_);
    
    if (timeout_ms > 0) {
        if (!cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                         [this] { return !queue_.empty() || stop_requested_; })) {
            return false;
        }
    } else {
        cv_.wait(lock, [this] { return !queue_.empty() || stop_requested_; });
    }
    
    if (queue_.empty() || stop_requested_) {
        return false;
    }
    
    task = queue_.top();
    queue_.pop();
    
    return true;
}

bool TaskQueue::empty() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.empty();
}

size_t TaskQueue::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size();
}

void TaskQueue::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    while (!queue_.empty()) {
        queue_.pop();
    }
}

void TaskQueue::start_processing() {
    if (processing_) return;
    
    processing_ = true;
    stop_requested_ = false;
    
    processor_thread_ = std::thread([this]() {
        while (!stop_requested_) {
            Task task;
            if (pop(task, 100)) {
                if (callback_) {
                    callback_(task);
                }
                stats_.total_completed++;
                stats_.current_batch_index = task.batch_index + 1;
                
                if (progress_callback_) {
                    std::string msg = "Batch " + std::to_string(task.batch_index + 1) + 
                                      "/" + std::to_string(task.total_batches) + " completed";
                    progress_callback_(stats_.total_completed, stats_.total_batches, msg);
                }
            }
        }
    });
}

void TaskQueue::stop_processing() {
    stop_requested_ = true;
    cv_.notify_all();
    
    if (processor_thread_.joinable()) {
        processor_thread_.join();
    }
    
    processing_ = false;
}

void TaskQueue::prioritize(const std::string& task_id, uint32_t new_priority) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<Task> tasks;
    while (!queue_.empty()) {
        Task t = queue_.top();
        queue_.pop();
        if (t.id == task_id) {
            t.priority = new_priority;
        }
        tasks.push_back(t);
    }
    
    for (const auto& t : tasks) {
        queue_.push(t);
    }
}

void TaskQueue::remove(const std::string& task_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<Task> tasks;
    while (!queue_.empty()) {
        Task t = queue_.top();
        queue_.pop();
        if (t.id != task_id) {
            tasks.push_back(t);
        }
    }
    
    for (const auto& t : tasks) {
        queue_.push(t);
    }
}

TaskQueue::Statistics TaskQueue::get_statistics() const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto stats = stats_;
    stats.current_queue_size = queue_.size();
    return stats;
}

void TaskQueue::reset_statistics() {
    std::lock_guard<std::mutex> lock(mutex_);
    stats_ = Statistics{};
}

}
