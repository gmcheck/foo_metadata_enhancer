#include "worker_manager.h"
#include "logger.h"
#include "../include/constants.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <fstream>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <ctime>

namespace ai_metadata {

static std::string g_python_path;
static bool g_auto_install_packages = true;

void set_python_path(const std::string& path) {
    g_python_path = path;
}

void set_auto_install_packages(bool auto_install) {
    g_auto_install_packages = auto_install;
}

static bool run_command_hidden(const std::string& cmd, std::string& output) {
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = nullptr;
    
    HANDLE hReadPipe = nullptr;
    HANDLE hWritePipe = nullptr;
    
    if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0)) {
        return false;
    }
    SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);
    
    STARTUPINFOA si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.hStdOutput = hWritePipe;
    si.hStdError = hWritePipe;
    si.dwFlags = STARTF_USESTDHANDLES;
    
    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));
    
    std::string cmd_line = cmd;
    
    BOOL result = CreateProcessA(
        nullptr,
        const_cast<char*>(cmd_line.c_str()),
        nullptr,
        nullptr,
        TRUE,
        CREATE_NO_WINDOW,
        nullptr,
        nullptr,
        &si,
        &pi
    );
    
    CloseHandle(hWritePipe);
    
    if (!result) {
        CloseHandle(hReadPipe);
        return false;
    }
    
    char buffer[128];
    DWORD bytes_read;
    
    while (ReadFile(hReadPipe, buffer, sizeof(buffer) - 1, &bytes_read, nullptr) && bytes_read > 0) {
        buffer[bytes_read] = '\0';
        output += buffer;
    }
    
    CloseHandle(hReadPipe);
    
    WaitForSingleObject(pi.hProcess, constants::PROCESS_WAIT_TIMEOUT_MS);
    
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    
    return true;
}

static std::string get_python_path_internal() {
    if (!g_python_path.empty()) {
        return g_python_path;
    }
    
    const char* python_paths[] = {
        "python",
        "python3",
        "py",
        "C:\\Python311\\python.exe",
        "C:\\Python310\\python.exe",
        "C:\\Python39\\python.exe",
        "C:\\Python38\\python.exe",
        "D:\\programs\\miniconda3\\python.exe",
        "C:\\ProgramData\\miniconda3\\python.exe",
        "C:\\Users\\Lenovo\\miniconda3\\python.exe",
        "/usr/bin/python3",
        "/usr/local/bin/python3",
        "/opt/homebrew/bin/python3"
    };
    
    for (const char* path : python_paths) {
        std::string test_cmd = std::string(path) + " --version";
        std::string result;
        
        if (run_command_hidden(test_cmd, result)) {
            if (result.find("Python") != std::string::npos) {
                Logger::instance().debug(std::string("Found Python at '") + path + "' - " + result, __FILE__, __FUNCTION__);
                
                if (std::string(path).find("\\") != std::string::npos || 
                    std::string(path).find("/") != std::string::npos) {
                    return path;
                }
                
                std::string full_path;
                std::string where_cmd = std::string("where ") + path;
                if (run_command_hidden(where_cmd, full_path)) {
                    size_t len = full_path.length();
                    while (len > 0 && (full_path[len-1] == '\n' || full_path[len-1] == '\r')) {
                        full_path[--len] = '\0';
                    }
                    if (!full_path.empty()) {
                        return full_path;
                    }
                }
                return path;
            }
        }
    }
    
    LOG_ERROR("Python not found");
    return "";
}

WorkerManager::WorkerManager(const std::string& worker_path)
    : worker_path_(worker_path) {
}

WorkerManager::~WorkerManager() {
    shutdown();
}

bool WorkerManager::initialize() {
    std::lock_guard<std::mutex> lock(worker_mutex_);
    
    LOG_INFO("initialize: Starting worker");
    
    Logger::instance().debug("initialize: Checking Python dependencies", __FILE__, __FUNCTION__);
    if (!check_and_install_dependencies()) {
        LOG_WARN("initialize: Some dependencies may not be installed correctly");
    }
    
    worker_ = std::make_unique<WorkerProcess>();
    worker_->id = 0;
    
    if (!start_worker()) {
        LOG_ERROR("initialize: Failed to start worker");
        return false;
    }
    
    LOG_INFO("initialize: Worker started successfully");
    
    read_loop_thread_ = std::thread(&WorkerManager::worker_read_loop, this);
    
    return true;
}

void WorkerManager::shutdown() {
    shutdown_requested_ = true;
    
    if (read_loop_thread_.joinable()) {
        read_loop_thread_.join();
    }
    
    if (worker_) {
        stop_worker();
    }
}

bool WorkerManager::start_worker() {
    auto& worker = worker_;
    
    Logger::instance().debug("start_worker: Creating pipes for worker", __FILE__, __FUNCTION__);
    
    HANDLE hStdinRead = nullptr;
    HANDLE hStdinWrite = nullptr;
    HANDLE hStdoutRead = nullptr;
    HANDLE hStdoutWrite = nullptr;
    HANDLE hStderrRead = nullptr;
    HANDLE hStderrWrite = nullptr;
    
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = nullptr;
    
    if (!CreatePipe(&hStdinRead, &hStdinWrite, &sa, 0)) {
        LOG_ERROR("start_worker: Failed to create stdin pipe");
        return false;
    }
    SetHandleInformation(hStdinWrite, HANDLE_FLAG_INHERIT, 0);
    
    if (!CreatePipe(&hStdoutRead, &hStdoutWrite, &sa, 0)) {
        LOG_ERROR("start_worker: Failed to create stdout pipe");
        CloseHandle(hStdinRead);
        CloseHandle(hStdinWrite);
        return false;
    }
    SetHandleInformation(hStdoutRead, HANDLE_FLAG_INHERIT, 0);
    
    if (!CreatePipe(&hStderrRead, &hStderrWrite, &sa, 0)) {
        LOG_ERROR("start_worker: Failed to create stderr pipe");
        CloseHandle(hStdinRead);
        CloseHandle(hStdinWrite);
        CloseHandle(hStdoutRead);
        CloseHandle(hStdoutWrite);
        return false;
    }
    SetHandleInformation(hStderrRead, HANDLE_FLAG_INHERIT, 0);
    
    STARTUPINFOA si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.hStdInput = hStdinRead;
    si.hStdOutput = hStdoutWrite;
    si.hStdError = hStderrWrite;
    si.dwFlags = STARTF_USESTDHANDLES;
    
    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));
    
    std::string python_exe = get_python_path_internal();
    if (python_exe.empty()) {
        LOG_ERROR("start_worker: Python not found. Please configure python_path in settings.");
        CloseHandle(hStdinRead);
        CloseHandle(hStdinWrite);
        CloseHandle(hStdoutRead);
        CloseHandle(hStdoutWrite);
        CloseHandle(hStderrRead);
        CloseHandle(hStderrWrite);
        return false;
    }
    
    std::string cmd = "\"" + python_exe + "\" -u \"" + worker_path_ + "\"";
    Logger::instance().debug("start_worker: Command = " + cmd, __FILE__, __FUNCTION__);
    
    BOOL result = CreateProcessA(
        nullptr,
        const_cast<char*>(cmd.c_str()),
        nullptr,
        nullptr,
        TRUE,
        CREATE_NO_WINDOW,
        nullptr,
        nullptr,
        &si,
        &pi
    );
    
    CloseHandle(hStdinRead);
    CloseHandle(hStdoutWrite);
    CloseHandle(hStderrWrite);
    
    if (!result) {
        DWORD error = GetLastError();
        char error_msg[256];
        FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM, NULL, error, 0, error_msg, sizeof(error_msg), NULL);
        std::stringstream ss;
        ss << "start_worker: CreateProcess failed, error = " << error << ": " << error_msg;
        LOG_ERROR(ss.str());
        CloseHandle(hStdinWrite);
        CloseHandle(hStdoutRead);
        CloseHandle(hStderrRead);
        return false;
    }
    
    worker->hProcess = pi.hProcess;
    worker->hThread = pi.hThread;
    worker->hStdinWrite = hStdinWrite;
    worker->hStdoutRead = hStdoutRead;
    worker->hStderrRead = hStderrRead;
    worker->is_running = true;
    
    Logger::instance().debug("start_worker: Worker process created", __FILE__, __FUNCTION__);
    
    return true;
}

void WorkerManager::stop_worker() {
    std::lock_guard<std::mutex> lock(worker_mutex_);
    
    if (!worker_ || !worker_->is_running) {
        return;
    }
    
    worker_->is_running = false;
    
    if (worker_->hProcess) {
        TerminateProcess(worker_->hProcess, 0);
        WaitForSingleObject(worker_->hProcess, 1000);
        CloseHandle(worker_->hProcess);
        worker_->hProcess = nullptr;
    }
    
    if (worker_->hThread) {
        CloseHandle(worker_->hThread);
        worker_->hThread = nullptr;
    }
    
    if (worker_->hStdinWrite) {
        CloseHandle(worker_->hStdinWrite);
        worker_->hStdinWrite = nullptr;
    }
    
    if (worker_->hStdoutRead) {
        CloseHandle(worker_->hStdoutRead);
        worker_->hStdoutRead = nullptr;
    }
    
    if (worker_->hStderrRead) {
        CloseHandle(worker_->hStderrRead);
        worker_->hStderrRead = nullptr;
    }
}

void WorkerManager::worker_read_loop() {
    Logger::instance().debug("worker_read_loop: Starting read loop", __FILE__, __FUNCTION__);
    
    char stderr_buffer[4096];
    DWORD stderr_read = 0;
    DWORD last_check_time = GetTickCount();
    const DWORD check_interval_ms = constants::WORKER_CHECK_INTERVAL_MS;
    
    while (!shutdown_requested_) {
        bool worker_running = false;
        {
            std::lock_guard<std::mutex> lock(worker_mutex_);
            worker_running = worker_ && worker_->is_running;
        }
        
        if (!worker_running) {
            Sleep(10);
            continue;
        }
        
        DWORD current_time = GetTickCount();
        
        if (current_time - last_check_time >= check_interval_ms) {
            last_check_time = current_time;
            
            DWORD oldest_send_time = 0;
            std::string oldest_request_id;
            size_t pending_count = 0;
            {
                std::lock_guard<std::mutex> lock(pending_mutex_);
                pending_count = pending_requests_.size();
                for (const auto& pair : pending_requests_) {
                    if (oldest_send_time == 0 || pair.second.send_time < oldest_send_time) {
                        oldest_send_time = pair.second.send_time;
                        oldest_request_id = pair.first;
                    }
                }
            }
            
            if (pending_count > 0 && oldest_send_time > 0) {
                DWORD elapsed = current_time - oldest_send_time;
                
                if (elapsed > max_silence_time_ms_) {
                    LOG_ERROR("worker_read_loop: Request timeout detected, oldest_request_id=" + 
                        oldest_request_id + ", elapsed=" + std::to_string(elapsed / 1000) + 
                        "s, max_allowed=" + std::to_string(max_silence_time_ms_ / 1000) + 
                        "s, pending_count=" + std::to_string(pending_count) + ", forcing restart");
                    force_restart_worker();
                    continue;
                }
            }
        }
        
        DWORD available = 0;
        {
            std::lock_guard<std::mutex> lock(worker_mutex_);
            if (!worker_ || !worker_->is_running) {
                continue;
            }
            if (!PeekNamedPipe(worker_->hStdoutRead, nullptr, 0, nullptr, &available, nullptr)) {
                LOG_ERROR("worker_read_loop: PeekNamedPipe failed");
                worker_->is_running = false;
                continue;
            }
        }
        
        if (available > 0) {
            Logger::instance().debug("worker_read_loop: " + std::to_string(available) + " bytes available", __FILE__, __FUNCTION__);
            Logger::instance().debug("worker_read_loop: STAGE 3 - C++ receiving response from Python", __FILE__, __FUNCTION__);
            
            std::string response;
            {
                std::lock_guard<std::mutex> lock(worker_mutex_);
                response = read_message(worker_->hStdoutRead);
            }
            
            if (!response.empty()) {
                Logger::instance().debug("worker_read_loop: Received response, size=" + std::to_string(response.size()), __FILE__, __FUNCTION__);
                process_worker_response(response);
                {
                    std::lock_guard<std::mutex> lock(worker_mutex_);
                    if (worker_) {
                        worker_->queue_size = (std::max)(0, worker_->queue_size.load() - 1);
                    }
                }
            } else {
                LOG_WARN("worker_read_loop: Empty response, worker may have died");
                {
                    std::lock_guard<std::mutex> lock(worker_mutex_);
                    if (worker_) {
                        worker_->is_running = false;
                    }
                }
            }
        }
        
        {
            std::lock_guard<std::mutex> lock(worker_mutex_);
            if (worker_ && worker_->hStderrRead && 
                PeekNamedPipe(worker_->hStderrRead, nullptr, 0, nullptr, &available, nullptr) && available > 0) {
                if (ReadFile(worker_->hStderrRead, stderr_buffer, sizeof(stderr_buffer) - 1, &stderr_read, nullptr) && stderr_read > 0) {
                    stderr_buffer[stderr_read] = '\0';
                    std::string stderr_str(stderr_buffer);
                    while (!stderr_str.empty() && (stderr_str.back() == '\n' || stderr_str.back() == '\r')) {
                        stderr_str.pop_back();
                    }
                    if (!stderr_str.empty()) {
                        Logger::instance().debug("worker_read_loop: [Python stderr] " + stderr_str, __FILE__, __FUNCTION__);
                    }
                }
            }
        }
        
        Sleep(10);
    }
    
    Logger::instance().debug("worker_read_loop: Read loop stopped", __FILE__, __FUNCTION__);
}

void WorkerManager::process_worker_response(const std::string& response) {
    Logger::instance().debug("process_worker_response: Response size = " + std::to_string(response.size()), __FILE__, __FUNCTION__);
    Logger::instance().debug("process_worker_response: Response preview = " + response.substr(0, (std::min)(size_t(500), response.size())), __FILE__, __FUNCTION__);
    
    try {
        auto json = nlohmann::json::parse(response);
        std::string request_id = json.value("id", "");
        
        Logger::instance().debug("process_worker_response: Request ID = " + request_id + 
            ", Success = " + std::to_string(json.value("success", false)) + 
            ", Count = " + std::to_string(json.value("count", 0)), __FILE__, __FUNCTION__);
        
        if (!request_id.empty()) {
            ResultCallback callback;
            ErrorCallback error_callback;
            {
                std::lock_guard<std::mutex> lock(pending_mutex_);
                auto it = pending_requests_.find(request_id);
                if (it != pending_requests_.end()) {
                    callback = it->second.on_result;
                    error_callback = it->second.on_error;
                    pending_requests_.erase(it);
                    Logger::instance().debug("process_worker_response: Found pending request callback", __FILE__, __FUNCTION__);
                } else {
                    LOG_WARN("process_worker_response: No pending request found for ID = " + request_id);
                }
            }
            
            if (callback) {
                BatchResponse batch_response;
                batch_response.id = request_id;
                batch_response.success = json.value("success", false);
                batch_response.count = json.value("count", 0);
                
                if (json.contains("results") && json["results"].is_array()) {
                    Logger::instance().debug("process_worker_response: Processing " + std::to_string(json["results"].size()) + " results", __FILE__, __FUNCTION__);
                    for (const auto& item : json["results"]) {
                        batch_response.results.push_back(item);
                    }
                }
                
                if (json.contains("error")) {
                    ErrorInfo err;
                    err.code = json["error"].value("code", "");
                    err.message = json["error"].value("message", "");
                    err.retryable = json["error"].value("retryable", false);
                    batch_response.error = err;
                    LOG_ERROR("process_worker_response: Error code = " + err.code + ", message = " + err.message);
                }
                
                Logger::instance().debug("process_worker_response: Calling callback", __FILE__, __FUNCTION__);
                callback(request_id, batch_response);
                Logger::instance().debug("process_worker_response: Callback completed", __FILE__, __FUNCTION__);
            }
        }
    } catch (const std::exception& e) {
        LOG_ERROR("process_worker_response: Exception processing response: " + std::string(e.what()));
    }
}

bool WorkerManager::write_message(HANDLE hPipe, const std::string& message) {
    if (!hPipe || hPipe == INVALID_HANDLE_VALUE) {
        LOG_ERROR("write_message: Invalid pipe handle");
        return false;
    }
    
    uint32_t len = static_cast<uint32_t>(message.size());
    uint32_t net_len = htonl(len);
    
    Logger::instance().debug("write_message: Writing header (4 bytes): " + std::to_string(net_len) + 
        ", payload (" + std::to_string(len) + " bytes)", __FILE__, __FUNCTION__);
    Logger::instance().debug("write_message: Payload preview: " + message.substr(0, (std::min)(size_t(500), message.size())), __FILE__, __FUNCTION__);
    
    DWORD written = 0;
    if (!WriteFile(hPipe, &net_len, 4, &written, nullptr) || written != 4) {
        LOG_ERROR("write_message: Failed to write header");
        return false;
    }
    
    Logger::instance().debug("write_message: Header written successfully", __FILE__, __FUNCTION__);
    
    if (!WriteFile(hPipe, message.c_str(), len, &written, nullptr) || written != len) {
        LOG_ERROR("write_message: Failed to write payload");
        return false;
    }
    
    Logger::instance().debug("write_message: Payload written successfully (" + std::to_string(written) + " bytes), buffer flushed", __FILE__, __FUNCTION__);
    
    FlushFileBuffers(hPipe);
    
    return true;
}

std::string WorkerManager::read_message(HANDLE hPipe) {
    Logger::instance().debug("read_message: Starting to read message", __FILE__, __FUNCTION__);
    
    if (!hPipe || hPipe == INVALID_HANDLE_VALUE) {
        LOG_ERROR("read_message: Invalid pipe handle");
        return "";
    }
    
    DWORD available = 0;
    DWORD start_time = GetTickCount();
    
    while (available < 4) {
        if (!PeekNamedPipe(hPipe, nullptr, 0, nullptr, &available, nullptr)) {
            LOG_ERROR("read_message: PeekNamedPipe failed");
            return "";
        }
        
        if (GetTickCount() - start_time > timeout_ms_) {
            LOG_WARN("read_message: Timeout waiting for header");
            return "";
        }
        
        if (available < 4) {
            Sleep(10);
            continue;
        }
    }
    
    Logger::instance().debug("read_message: Header available, reading 4 bytes", __FILE__, __FUNCTION__);
    
    uint32_t net_len = 0;
    DWORD read = 0;
    if (!ReadFile(hPipe, &net_len, 4, &read, nullptr) || read != 4) {
        LOG_ERROR("read_message: Failed to read header");
        return "";
    }
    
    uint32_t len = ntohl(net_len);
    
    Logger::instance().debug("read_message: Decoded length: " + std::to_string(len) + " bytes", __FILE__, __FUNCTION__);
    
    if (len > constants::IPC_MAX_MESSAGE_SIZE) {
        LOG_ERROR("read_message: Message too large: " + std::to_string(len));
        return "";
    }
    
    std::string buffer(len, '\0');
    start_time = GetTickCount();
    
    size_t total_read = 0;
    while (total_read < len) {
        if (!PeekNamedPipe(hPipe, nullptr, 0, nullptr, &available, nullptr)) {
            LOG_ERROR("read_message: PeekNamedPipe failed during payload read");
            return "";
        }
        
        if (available == 0) {
            if (GetTickCount() - start_time > timeout_ms_) {
                LOG_WARN("read_message: Timeout waiting for payload");
                return "";
            }
            Sleep(10);
            continue;
        }
        
        DWORD to_read = static_cast<DWORD>((std::min)(static_cast<size_t>(available), len - total_read));
        if (!ReadFile(hPipe, buffer.data() + total_read, to_read, &read, nullptr)) {
            LOG_ERROR("read_message: Failed to read payload chunk");
            return "";
        }
        total_read += read;
    }
    
    Logger::instance().debug("read_message: Successfully read " + std::to_string(total_read) + " bytes", __FILE__, __FUNCTION__);
    Logger::instance().debug("read_message: Payload preview: " + buffer.substr(0, (std::min)(size_t(500), buffer.size())), __FILE__, __FUNCTION__);
    
    return buffer;
}

bool WorkerManager::send_request(const std::string& request_id, const std::string& json_request,
                                  ResultCallback on_result, ErrorCallback on_error) {
    Logger::instance().debug("send_request: STAGE 1 - C++ sending request to Python", __FILE__, __FUNCTION__);
    Logger::instance().debug("send_request: Request ID = " + request_id + ", Request JSON = " + json_request.substr(0, (std::min)(size_t(500), json_request.size())), __FILE__, __FUNCTION__);
    
    {
        std::lock_guard<std::mutex> lock(worker_mutex_);
        if (!worker_ || !worker_->is_running) {
            LOG_ERROR("send_request: Worker not available");
            if (on_error) {
                ErrorInfo err;
                err.code = constants::error_worker_crash();
                err.message = "Worker not available";
                err.retryable = true;
                on_error(request_id, err);
            }
            return false;
        }
    }
    
    {
        std::lock_guard<std::mutex> pending_lock(pending_mutex_);
        PendingRequest req;
        req.id = request_id;
        req.send_time = GetTickCount();
        req.on_result = on_result;
        req.on_error = on_error;
        pending_requests_[request_id] = req;
    }
    
    Logger::instance().debug("send_request: Writing to worker stdin", __FILE__, __FUNCTION__);
    
    {
        std::lock_guard<std::mutex> lock(worker_mutex_);
        if (!write_message(worker_->hStdinWrite, json_request)) {
            LOG_ERROR("send_request: Failed to write to worker stdin");
            std::lock_guard<std::mutex> pending_lock(pending_mutex_);
            pending_requests_.erase(request_id);
            
            if (on_error) {
                ErrorInfo err;
                err.code = constants::error_worker_crash();
                err.message = "Failed to send request to worker";
                err.retryable = true;
                on_error(request_id, err);
            }
            return false;
        }
        worker_->queue_size++;
    }
    
    Logger::instance().debug("send_request: Request sent successfully", __FILE__, __FUNCTION__);
    return true;
}

std::vector<WorkerInfo> WorkerManager::get_worker_info() const {
    std::lock_guard<std::mutex> lock(worker_mutex_);
    
    std::vector<WorkerInfo> info;
    if (worker_) {
        WorkerInfo wi;
        wi.id = worker_->id;
        wi.status = worker_->is_running ? "running" : "stopped";
        wi.queue_size = worker_->queue_size;
        info.push_back(wi);
    }
    
    return info;
}

void WorkerManager::restart_worker() {
    std::lock_guard<std::mutex> lock(worker_mutex_);
    
    if (worker_) {
        stop_worker();
        start_worker();
    }
}

void WorkerManager::force_restart_worker() {
    LOG_WARN("force_restart_worker: Forcing worker restart due to timeout or hang");
    
    std::map<std::string, PendingRequest> pending_copy;
    {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        pending_copy = pending_requests_;
        pending_requests_.clear();
    }
    
    for (auto& pair : pending_copy) {
        const std::string& request_id = pair.first;
        PendingRequest& req = pair.second;
        
        if (req.on_error) {
            ErrorInfo err;
            err.code = constants::error_timeout();
            err.message = "Worker timeout or hung, request cancelled";
            err.retryable = true;
            req.on_error(request_id, err);
        }
    }
    
    {
        std::lock_guard<std::mutex> lock(worker_mutex_);
        
        if (worker_) {
            if (worker_->hProcess) {
                TerminateProcess(worker_->hProcess, 1);
                WaitForSingleObject(worker_->hProcess, 1000);
                CloseHandle(worker_->hProcess);
                worker_->hProcess = nullptr;
            }
            
            if (worker_->hThread) {
                CloseHandle(worker_->hThread);
                worker_->hThread = nullptr;
            }
            
            if (worker_->hStdinWrite) {
                CloseHandle(worker_->hStdinWrite);
                worker_->hStdinWrite = nullptr;
            }
            
            if (worker_->hStdoutRead) {
                CloseHandle(worker_->hStdoutRead);
                worker_->hStdoutRead = nullptr;
            }
            
            if (worker_->hStderrRead) {
                CloseHandle(worker_->hStderrRead);
                worker_->hStderrRead = nullptr;
            }
            
            worker_->is_running = false;
            worker_->queue_size = 0;
            
            if (!start_worker()) {
                LOG_ERROR("force_restart_worker: Failed to restart worker");
            } else {
                LOG_INFO("force_restart_worker: Worker restarted successfully");
            }
        }
    }
}

bool WorkerManager::is_healthy() const {
    std::lock_guard<std::mutex> lock(worker_mutex_);
    
    return worker_ && worker_->is_running;
}

bool WorkerManager::check_and_install_dependencies() {
    if (!g_auto_install_packages) {
        Logger::instance().debug("check_and_install_dependencies: Auto-install disabled", __FILE__, __FUNCTION__);
        return true;
    }
    
    std::string python_exe = get_python_path_internal();
    if (python_exe.empty()) {
        LOG_ERROR("check_and_install_dependencies: Python not found");
        return false;
    }
    
    std::vector<std::string> required_packages = {
        "musicbrainzngs",
        "requests",
        "nltk",
        "pyyaml"
    };
    
    bool all_installed = true;
    for (const auto& package : required_packages) {
        if (!is_package_installed(package)) {
            Logger::instance().debug("check_and_install_dependencies: Installing " + package, __FILE__, __FUNCTION__);
            
            std::string install_cmd = "\"" + python_exe + "\" -m pip install " + package + " --quiet";
            FILE* pipe = _popen(install_cmd.c_str(), "r");
            if (pipe) {
                char buffer[256];
                while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
                    Logger::instance().debug("check_and_install_dependencies: pip output: " + std::string(buffer), __FILE__, __FUNCTION__);
                }
                int result = _pclose(pipe);
                if (result != 0) {
                    LOG_ERROR("check_and_install_dependencies: Failed to install " + package);
                    all_installed = false;
                } else {
                    Logger::instance().debug("check_and_install_dependencies: Successfully installed " + package, __FILE__, __FUNCTION__);
                }
            } else {
                LOG_ERROR("check_and_install_dependencies: Failed to run pip for " + package);
                all_installed = false;
            }
        }
    }
    
    return all_installed;
}

bool WorkerManager::is_package_installed(const std::string& package_name) {
    std::string python_exe = get_python_path_internal();
    if (python_exe.empty()) {
        return false;
    }
    
    std::string check_cmd = "\"" + python_exe + "\" -c \"import " + package_name + "\" 2>&1";
    FILE* pipe = _popen(check_cmd.c_str(), "r");
    if (pipe) {
        int result = _pclose(pipe);
        return result == 0;
    }
    return false;
}

}
