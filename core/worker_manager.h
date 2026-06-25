#pragma once

#include "../include/types.h"
#include "../include/constants.h"
#include <string>
#include <vector>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <atomic>
#include <map>

#ifdef _WIN32
#include <windows.h>
#endif

namespace ai_metadata {

/**
 * @brief 设置 Python 可执行文件路径
 * @param path Python 路径
 */
void set_python_path(const std::string& path);

/**
 * @brief 设置是否自动安装 Python 包
 * @param auto_install 是否自动安装
 */
void set_auto_install_packages(bool auto_install);

/**
 * @brief Worker进程结构体，存储进程相关信息
 */
struct WorkerProcess {
    int id = 0;                          ///< Worker ID
    HANDLE hProcess = nullptr;           ///< 进程句柄
    HANDLE hThread = nullptr;            ///< 线程句柄
    HANDLE hStdinWrite = nullptr;        ///< 标准输入写入句柄
    HANDLE hStdoutRead = nullptr;        ///< 标准输出读取句柄
    HANDLE hStderrRead = nullptr;        ///< 标准错误读取句柄
    std::atomic<bool> is_running{false}; ///< 是否正在运行
    std::atomic<int> queue_size{0};      ///< 队列大小
};

/**
 * @brief Worker管理器类，负责管理Python Worker进程
 * 
 * 启动、监控和通信Python Worker进程，处理请求和响应
 */
class WorkerManager {
public:
    /**
     * @brief 结果回调类型
     * @param id 请求ID
     * @param response 批量响应
     */
    using ResultCallback = std::function<void(const std::string& id, const BatchResponse& response)>;
    
    /**
     * @brief 错误回调类型
     * @param id 请求ID
     * @param error 错误信息
     */
    using ErrorCallback = std::function<void(const std::string& id, const ErrorInfo& error)>;
    
    /**
     * @brief 构造函数
     * @param worker_path Worker脚本路径
     */
    WorkerManager(const std::string& worker_path);
    
    /**
     * @brief 析构函数，停止Worker进程
     */
    ~WorkerManager();
    
    WorkerManager(const WorkerManager&) = delete;
    WorkerManager& operator=(const WorkerManager&) = delete;
    
    /**
     * @brief 初始化Worker管理器
     * @return 初始化成功返回true
     */
    bool initialize();
    
    /**
     * @brief 关闭Worker管理器
     */
    void shutdown();
    
    /**
     * @brief 发送请求到Worker
     * @param request_id 请求ID
     * @param json_request JSON格式的请求
     * @param on_result 结果回调
     * @param on_error 错误回调
     * @return 发送成功返回true
     */
    bool send_request(const std::string& request_id, const std::string& json_request, 
                     ResultCallback on_result, ErrorCallback on_error);
    
    /**
     * @brief 设置超时时间
     * @param timeout_ms 超时时间（毫秒）
     */
    void set_timeout_ms(uint32_t timeout_ms) { timeout_ms_ = timeout_ms; }
    
    /**
     * @brief 获取超时时间
     * @return 超时时间（毫秒）
     */
    uint32_t get_timeout_ms() const { return timeout_ms_; }
    
    /**
     * @brief 获取Worker信息列表
     * @return Worker信息列表
     */
    std::vector<WorkerInfo> get_worker_info() const;
    
    /**
     * @brief 重启Worker进程
     */
    void restart_worker();
    
    /**
     * @brief 强制重启Worker进程（超时或无响应时调用）
     */
    void force_restart_worker();
    
    /**
     * @brief 检查Worker是否健康
     * @return 健康返回true
     */
    bool is_healthy() const;
    
    /**
     * @brief 设置最大静默时间（无响应超时）
     * @param ms 毫秒
     */
    void set_max_silence_time_ms(uint32_t ms) { max_silence_time_ms_ = ms; }
    
    /**
     * @brief 获取最大静默时间
     * @return 毫秒
     */
    uint32_t get_max_silence_time_ms() const { return max_silence_time_ms_; }
    
    /**
     * @brief 检查并安装Python依赖
     * @return 成功返回true
     */
    bool check_and_install_dependencies();
    
    /**
     * @brief 检查Python包是否已安装
     * @param package_name 包名
     * @return 已安装返回true
     */
    bool is_package_installed(const std::string& package_name);
    
private:
    /**
     * @brief 启动Worker进程
     * @return 启动成功返回true
     */
    bool start_worker();
    
    /**
     * @brief 停止Worker进程
     */
    void stop_worker();
    
    /**
     * @brief 向管道写入消息
     * @param hPipe 管道句柄
     * @param message 消息内容
     * @return 写入成功返回true
     */
    bool write_message(HANDLE hPipe, const std::string& message);
    
    /**
     * @brief 从管道读取消息
     * @param hPipe 管道句柄
     * @return 读取的消息内容
     */
    std::string read_message(HANDLE hPipe);
    
    /**
     * @brief Worker读取循环线程函数
     */
    void worker_read_loop();
    
    /**
     * @brief 处理Worker响应
     * @param response 响应内容
     */
    void process_worker_response(const std::string& response);
    
    std::string worker_path_;
    uint32_t timeout_ms_ = constants::DEFAULT_GLOBAL_TIMEOUT_MS;
    uint32_t max_silence_time_ms_ = constants::DEFAULT_MAX_SILENCE_TIME_MS;
    
    std::unique_ptr<WorkerProcess> worker_;
    mutable std::mutex worker_mutex_;
    
    std::thread read_loop_thread_;
    
    /**
     * @brief 待处理请求结构体
     */
    struct PendingRequest {
        std::string id;           ///< 请求ID
        uint32_t timeout_ms;      ///< 超时时间
        DWORD send_time;          ///< 发送时间戳 (GetTickCount)
        ResultCallback on_result; ///< 结果回调
        ErrorCallback on_error;   ///< 错误回调
    };
    
    std::map<std::string, PendingRequest> pending_requests_;
    std::mutex pending_mutex_;
    
    std::atomic<bool> shutdown_requested_{false};
};

}
