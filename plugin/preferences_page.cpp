#include "preferences_page.h"
#include <CommCtrl.h>
#include "menu_handler.h"
#include "resource.h"
#include "../core/logger.h"
#include "../core/feedback.h"
#include "../core/ai_core.h"
#include "../core/worker_manager.h"
#include "../include/constants.h"
#include <foobar2000/SDK/foobar2000.h>
#include <shlobj.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <thread>
#include <tuple>
#include <nlohmann/json.hpp>

namespace ai_metadata {

// 隐藏窗口执行命令并捕获输出（避免 _popen 弹出 cmd 窗口）
static bool run_command_hidden_pref(const std::string& cmd, std::string& output, int* exit_code = nullptr) {
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = nullptr;

    HANDLE hReadPipe = nullptr;
    HANDLE hWritePipe = nullptr;
    if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0)) return false;
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
        nullptr, nullptr,
        TRUE,
        CREATE_NO_WINDOW,
        nullptr, nullptr,
        &si, &pi
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

    WaitForSingleObject(pi.hProcess, 30000); // 30s 超时

    if (exit_code != nullptr) {
        DWORD code = 0;
        if (GetExitCodeProcess(pi.hProcess, &code)) {
            *exit_code = static_cast<int>(code);
        } else {
            *exit_code = -1;
        }
    }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return true;
}

static const UINT WM_FILL_COMBO_BOXES = WM_USER + 100;
static const int FILL_COMBO_TIMER_ID = 1;
static const int FILL_COMBO_TIMER_DELAY = constants::UI_TIMER_DELAY_MS;

static void log_format_impl(std::ostringstream& oss) {}

template<typename T, typename... Args>
static void log_format_impl(std::ostringstream& oss, T first, Args... rest) {
    oss << first;
    log_format_impl(oss, rest...);
}

template<typename... Args>
static void log_format(Args... args) {
    std::ostringstream oss;
    log_format_impl(oss, args...);
    Logger::instance().debug(oss.str(), "preferences_page.cpp", "log_format");
}

static const GUID guid_preferences_root = 
    { 0x7a8b9c0d, 0x1e2f, 0x3a4b, { 0x5c, 0x6d, 0x7e, 0x8f, 0x9a, 0x0b, 0x1c, 0x2d } };
static const GUID guid_preferences_general = 
    { 0x7a8b9c0e, 0x1e2f, 0x3a4b, { 0x5c, 0x6d, 0x7e, 0x8f, 0x9a, 0x0b, 0x1c, 0x2d } };
static const GUID guid_preferences_data_sources = 
    { 0x7a8b9c0f, 0x1e2f, 0x3a4b, { 0x5c, 0x6d, 0x7e, 0x8f, 0x9a, 0x0b, 0x1c, 0x2d } };
static const GUID guid_preferences_advanced =
    { 0x7a8b9c10, 0x1e2f, 0x3a4b, { 0x5c, 0x6d, 0x7e, 0x8f, 0x9a, 0x0b, 0x1c, 0x2d } };
static const GUID guid_preferences_processing =
    { 0x7a8b9c12, 0x1e2f, 0x3a4b, { 0x5c, 0x6d, 0x7e, 0x8f, 0x9a, 0x0b, 0x1c, 0x2d } };
static const GUID guid_preferences_cache_logs =
    { 0x7a8b9c13, 0x1e2f, 0x3a4b, { 0x5c, 0x6d, 0x7e, 0x8f, 0x9a, 0x0b, 0x1c, 0x2d } };
static const GUID guid_preferences_prompts =
    { 0x7a8b9c11, 0x1e2f, 0x3a4b, { 0x5c, 0x6d, 0x7e, 0x8f, 0x9a, 0x0b, 0x1c, 0x2d } };

const GUID AIPreferencePageRoot::g_guid = guid_preferences_root;
const GUID AIPreferencePageGeneral::g_guid = guid_preferences_general;
const GUID AIPreferencePageDataSources::g_guid = guid_preferences_data_sources;
const GUID AIPreferencePageProcessing::g_guid = guid_preferences_processing;
const GUID AIPreferencePageCacheLogs::g_guid = guid_preferences_cache_logs;
const GUID AIPreferencePagePrompts::g_guid = guid_preferences_prompts;

SettingsManager& SettingsManager::instance() {
    static SettingsManager instance;
    return instance;
}

SettingsManager::SettingsManager()
    : m_initialized(false) {
}

void SettingsManager::ensure_initialized() const {
    if (m_initialized) return;
    
    const_cast<SettingsManager*>(this)->do_ensure_initialized();
}

void SettingsManager::do_ensure_initialized() {
    m_config_path = core_api::get_profile_path();
    
    if (m_config_path.find("file://") == 0) {
        m_config_path = m_config_path.substr(7);
    }
    
    m_config_path += "\\foo_metadata_enhancer";
    CreateDirectoryA(m_config_path.c_str(), NULL);
    m_config_path += "\\settings.json";
    
    load_from_config_yaml();
    load();
    Logger::instance().set_log_level(m_settings.log_level);
    set_auto_install_packages(m_settings.auto_install_packages);
    save();
    
    m_initialized = true;
}

std::string SettingsManager::get_config_path() const {
    return m_config_path;
}

std::string SettingsManager::get_api_key() const {
    // V1: API Key 在 Python SQLite providers 表；此方法仅兼容旧调用。
    return "";
}

std::vector<ModelInfo> SettingsManager::get_models_for_provider(AIProvider provider) const {
    // V1: 模型列表不再由 C++ settings 维护
    (void)provider;
    return {};
}

static std::string trim_string(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\"`");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\"`");
    return s.substr(start, end - start + 1);
}

static std::string get_dll_directory() {
    char dll_path[MAX_PATH] = {0};
    HMODULE hModule = GetModuleHandleA("foo_metadata_enhancer.dll");
    if (hModule) {
        GetModuleFileNameA(hModule, dll_path, MAX_PATH);
        std::string dll_dir(dll_path);
        size_t pos = dll_dir.find_last_of("\\/");
        if (pos != std::string::npos) {
            return dll_dir.substr(0, pos);
        }
    }
    return ".";
}

/// 转义多行字符串为合法 JSON 字符串字面量（含外围双引号）
static std::string json_escape_string(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    out.push_back('"');
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out.push_back(c);
                }
        }
    }
    out.push_back('"');
    return out;
}

/// UTF-8 string -> UTF-16 wstring
static std::wstring utf8_to_wstring(const std::string& s) {
    if (s.empty()) return std::wstring();
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (len <= 0) return std::wstring();
    std::wstring ws(len - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &ws[0], len);
    return ws;
}

/// UTF-16 wstring -> UTF-8 string
static std::string wstring_to_utf8(const std::wstring& ws) {
    if (ws.empty()) return std::string();
    int len = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return std::string();
    std::string s(len - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), -1, &s[0], len, nullptr, nullptr);
    return s;
}

/// SetDlgItemText 支持 UTF-8 输入（内部转 UTF-16 调用 W 版本）
static void SetDlgItemTextUTF8(HWND wnd, int id, const std::string& utf8_str) {
    SetDlgItemTextW(wnd, id, utf8_to_wstring(utf8_str).c_str());
}

/// GetDlgItemText 返回 UTF-8 字符串（内部调用 W 版本再转 UTF-8）
static std::string GetDlgItemTextUTF8(HWND wnd, int id) {
    HWND ctrl = GetDlgItem(wnd, id);
    if (!ctrl) return std::string();
    int len = GetWindowTextLengthW(ctrl);
    if (len <= 0) return std::string();
    std::wstring ws(len + 1, L'\0');
    int actual = GetDlgItemTextW(wnd, id, &ws[0], len + 1);
    ws.resize(actual);
    return wstring_to_utf8(ws);
}

void SettingsManager::load_from_config_yaml() {
    std::string yaml_path;
    std::ifstream file;
    
    std::string dll_dir = get_dll_directory();
    
    yaml_path = dll_dir + "\\foo_metadata_enhancer\\worker\\config.yaml";
    file.open(yaml_path);
    
    if (!file.is_open()) {
        yaml_path = dll_dir + "\\..\\foo_metadata_enhancer\\worker\\config.yaml";
        file.open(yaml_path);
    }
    
    if (!file.is_open()) {
        yaml_path = dll_dir + "\\config.yaml";
        file.open(yaml_path);
    }
    
    if (!file.is_open()) {
        yaml_path = dll_dir + "\\worker\\config.yaml";
        file.open(yaml_path);
    }
    
    if (!file.is_open()) {
        Logger::instance().warning("load_from_config_yaml: Failed to open config.yaml");
        return;
    }
    
    std::string line;
    std::string current_provider;
    bool in_models = false;
    bool in_provider = false;
    bool in_providers_section = false;
    bool in_cache_section = false;
    bool in_python_section = false;
    bool in_worker_section = false;
    bool in_logging_section = false;
    bool in_data_sources_section = false;
    bool in_musicbrainz_section = false;
    bool in_discogs_section = false;
    bool in_ai_section = false;
    
    while (std::getline(file, line)) {
        if (line.find("python:") != std::string::npos) {
            in_python_section = true;
            in_providers_section = false;
            in_cache_section = false;
            in_worker_section = false;
            in_logging_section = false;
            in_provider = false;
            in_models = false;
            continue;
        }
        
        if (line.find("providers:") != std::string::npos) {
            in_providers_section = true;
            in_cache_section = false;
            in_python_section = false;
            in_worker_section = false;
            in_logging_section = false;
            continue;
        }
        
        if (line.find("cache:") != std::string::npos) {
            in_cache_section = true;
            in_providers_section = false;
            in_provider = false;
            in_models = false;
            in_python_section = false;
            in_worker_section = false;
            in_logging_section = false;
            continue;
        }
        
        if (line.find("worker:") != std::string::npos) {
            in_worker_section = true;
            in_providers_section = false;
            in_provider = false;
            in_models = false;
            in_cache_section = false;
            in_python_section = false;
            in_logging_section = false;
            continue;
        }
        
        if (line.find("logging:") != std::string::npos) {
            in_logging_section = true;
            in_providers_section = false;
            in_provider = false;
            in_models = false;
            in_cache_section = false;
            in_python_section = false;
            in_worker_section = false;
            continue;
        }
        
        if (line.find("data_sources:") != std::string::npos) {
            in_data_sources_section = true;
            in_providers_section = false;
            in_provider = false;
            in_models = false;
            in_cache_section = false;
            in_python_section = false;
            in_worker_section = false;
            in_logging_section = false;
            continue;
        }
        
        if (in_python_section) {
            if (line.find("python_path:") != std::string::npos) {
                size_t pos = line.find(':');
                if (pos != std::string::npos) {
                    m_settings.python_path = trim_string(line.substr(pos + 1));
                    if (!m_settings.python_path.empty()) {
                        set_python_path(m_settings.python_path);
                    }
                }
            }
            else if (line.find("auto_install_packages:") != std::string::npos) {
                size_t pos = line.find(':');
                if (pos != std::string::npos) {
                    std::string val = trim_string(line.substr(pos + 1));
                    m_settings.auto_install_packages = (val == "true");
                }
            }
            continue;
        }
        
        if (in_cache_section) {
            if (line.find("enabled:") != std::string::npos) {
                size_t pos = line.find(':');
                if (pos != std::string::npos) {
                    std::string val = trim_string(line.substr(pos + 1));
                    m_settings.cache_enabled = (val == "true");
                }
            }
            else if (line.find("expiration_days:") != std::string::npos) {
                size_t pos = line.find(':');
                if (pos != std::string::npos) {
                    m_settings.cache_expiration_days = std::stoi(trim_string(line.substr(pos + 1)));
                }
            }
            else if (line.find("max_size_mb:") != std::string::npos) {
                size_t pos = line.find(':');
                if (pos != std::string::npos) {
                    m_settings.max_cache_size_mb = std::stoi(trim_string(line.substr(pos + 1)));
                }
            }
            else if (line.find("auto_cleanup:") != std::string::npos) {
                size_t pos = line.find(':');
                if (pos != std::string::npos) {
                    std::string val = trim_string(line.substr(pos + 1));
                    m_settings.auto_cleanup = (val == "true");
                }
            }
            continue;
        }
        
        if (in_worker_section) {
            if (line.find("auto_restart:") != std::string::npos) {
                size_t pos = line.find(':');
                if (pos != std::string::npos) {
                    std::string val = trim_string(line.substr(pos + 1));
                    m_settings.auto_restart = (val == "true");
                }
            }
            else if (line.find("ai_batch_size:") != std::string::npos) {
                size_t pos = line.find(':');
                if (pos != std::string::npos) {
                    m_settings.ai_batch_size = std::stoi(trim_string(line.substr(pos + 1)));
                }
            }
            else if (line.find("taskqueue_batch_size:") != std::string::npos) {
                size_t pos = line.find(':');
                if (pos != std::string::npos) {
                    m_settings.taskqueue_batch_size = std::stoi(trim_string(line.substr(pos + 1)));
                }
            }
            continue;
        }
        
        if (in_logging_section) {
            if (line.find("level:") != std::string::npos) {
                size_t pos = line.find(':');
                if (pos != std::string::npos) {
                    std::string level = trim_string(line.substr(pos + 1));
                    if (level == "DEBUG") m_settings.log_level = ai_metadata::constants::LogLevel::Debug;
                    else if (level == "INFO") m_settings.log_level = ai_metadata::constants::LogLevel::Info;
                    else if (level == "WARNING") m_settings.log_level = ai_metadata::constants::LogLevel::Warning;
                    else if (level == "ERROR") m_settings.log_level = ai_metadata::constants::LogLevel::Error;
                }
            }
            else if (line.find("max_file_size:") != std::string::npos) {
                size_t pos = line.find(':');
                if (pos != std::string::npos) {
                    int size_bytes = std::stoi(trim_string(line.substr(pos + 1)));
                    m_settings.max_log_file_size_mb = size_bytes / (1024 * 1024);
                }
            }
            continue;
        }
        
        if (in_data_sources_section) {
            if (line.find("musicbrainz:") != std::string::npos) {
                in_musicbrainz_section = true;
                in_discogs_section = false;
                in_ai_section = false;
                continue;
            }
            else if (line.find("discogs:") != std::string::npos) {
                in_discogs_section = true;
                in_musicbrainz_section = false;
                in_ai_section = false;
                continue;
            }
            else if (line.find("ai:") != std::string::npos) {
                in_ai_section = true;
                in_musicbrainz_section = false;
                in_discogs_section = false;
                continue;
            }
            
            if (in_musicbrainz_section) {
                size_t first_non_ws = line.find_first_not_of(" \t");
                if (first_non_ws != std::string::npos && line[first_non_ws] == '#') {
                    continue;
                }
                
                if (line.find("enabled:") != std::string::npos) {
                    size_t pos = line.find(':');
                    if (pos != std::string::npos) {
                        std::string val = trim_string(line.substr(pos + 1));
                        m_settings.enable_musicbrainz = (val == "true");
                    }
                }
                else if (line.find("timeout:") != std::string::npos && first_non_ws == 4) {
                    size_t pos = line.find(':');
                    if (pos != std::string::npos) {
                        try {
                            m_settings.mb_timeout = std::stoi(trim_string(line.substr(pos + 1)));
                        } catch (...) {}
                    }
                }
                else if (line.find("retries:") != std::string::npos && first_non_ws == 4) {
                    size_t pos = line.find(':');
                    if (pos != std::string::npos) {
                        try {
                            m_settings.mb_retries = std::stoi(trim_string(line.substr(pos + 1)));
                        } catch (...) {}
                    }
                }
                else if (line.find("page_size:") != std::string::npos && first_non_ws == 4) {
                    size_t pos = line.find(':');
                    if (pos != std::string::npos) {
                        try {
                            m_settings.mb_page_size = std::stoi(trim_string(line.substr(pos + 1)));
                        } catch (...) {}
                    }
                }
                else if (line.find("max_pages:") != std::string::npos && first_non_ws == 4) {
                    size_t pos = line.find(':');
                    if (pos != std::string::npos) {
                        try {
                            m_settings.mb_max_pages = std::stoi(trim_string(line.substr(pos + 1)));
                        } catch (...) {}
                    }
                }
                else if (line.find("score_threshold:") != std::string::npos && first_non_ws == 4) {
                    size_t pos = line.find(':');
                    if (pos != std::string::npos) {
                        try {
                            m_settings.mb_score_threshold = std::stoi(trim_string(line.substr(pos + 1)));
                        } catch (...) {}
                    }
                }
                else if (line.find("score_margin:") != std::string::npos && first_non_ws == 4) {
                    size_t pos = line.find(':');
                    if (pos != std::string::npos) {
                        try {
                            m_settings.mb_score_margin = std::stoi(trim_string(line.substr(pos + 1)));
                        } catch (...) {}
                    }
                }
                else if (line.find("rate_limit_rpm:") != std::string::npos && first_non_ws == 4) {
                    size_t pos = line.find(':');
                    if (pos != std::string::npos) {
                        try {
                            m_settings.mb_rate_limit = std::stoi(trim_string(line.substr(pos + 1)));
                        } catch (...) {}
                    }
                }
            }
            else if (in_discogs_section) {
                if (line.find("enabled:") != std::string::npos) {
                    size_t pos = line.find(':');
                    if (pos != std::string::npos) {
                        std::string val = trim_string(line.substr(pos + 1));
                        m_settings.enable_discogs = (val == "true");
                    }
                }
                else if (line.find("consumer_key:") != std::string::npos) {
                    size_t pos = line.find(':');
                    if (pos != std::string::npos) {
                        m_settings.discogs_consumer_key = trim_string(line.substr(pos + 1));
                    }
                }
                else if (line.find("consumer_secret:") != std::string::npos) {
                    size_t pos = line.find(':');
                    if (pos != std::string::npos) {
                        m_settings.discogs_consumer_secret = trim_string(line.substr(pos + 1));
                    }
                }
            }
            else if (in_ai_section) {
                if (line.find("enabled:") != std::string::npos) {
                    size_t pos = line.find(':');
                    if (pos != std::string::npos) {
                        std::string val = trim_string(line.substr(pos + 1));
                        m_settings.enable_ai = (val == "true");
                    }
                }
            }
            continue;
        }
        
        if (!in_providers_section) continue;
        
        if (line.find("default:") != std::string::npos && line.find("providers") == std::string::npos) {
            size_t pos = line.find(':');
            if (pos != std::string::npos) {
                std::string default_provider = trim_string(line.substr(pos + 1));
                m_settings.provider = string_to_provider(default_provider);
            }
        }
        else if (line.find("openrouter:") != std::string::npos ||
                 line.find("zhipu:") != std::string::npos ||
                 line.find("gemini:") != std::string::npos ||
                 line.find("ollama:") != std::string::npos ||
                 line.find("deepseek:") != std::string::npos ||
                 line.find("custom:") != std::string::npos) {
            size_t pos = line.find(':');
            if (pos != std::string::npos) {
                current_provider = trim_string(line.substr(0, pos));
                in_provider = true;
                in_models = false;
            }
        }
        else if (in_provider && line.find("api_key:") != std::string::npos) {
            size_t pos = line.find(':');
            if (pos != std::string::npos) {
                AIProvider p = string_to_provider(current_provider);
                m_settings.provider_configs[p].api_key = trim_string(line.substr(pos + 1));
            }
        }
        else if (in_provider && line.find("base_url:") != std::string::npos) {
            size_t pos = line.find(':');
            if (pos != std::string::npos) {
                AIProvider p = string_to_provider(current_provider);
                m_settings.provider_configs[p].base_url = trim_string(line.substr(pos + 1));
            }
        }
        else if (in_provider && line.find("models:") != std::string::npos) {
            in_models = true;
        }
        else if (in_models && line.find("- name:") != std::string::npos) {
            size_t pos = line.find("name:");
            if (pos != std::string::npos) {
                std::string model_name = trim_string(line.substr(pos + 5));
                AIProvider p = string_to_provider(current_provider);
                ModelInfo mi;
                mi.name = model_name;
                mi.priority = 999;
                m_settings.provider_configs[p].models.push_back(mi);
            }
        }
        else if (in_models && line.find("priority:") != std::string::npos) {
            size_t pos = line.find(':');
            if (pos != std::string::npos && !m_settings.provider_configs[string_to_provider(current_provider)].models.empty()) {
                AIProvider p = string_to_provider(current_provider);
                m_settings.provider_configs[p].models.back().priority = std::stoi(trim_string(line.substr(pos + 1)));
            }
        }
        else if (in_provider && current_provider == "custom" && line.find("api_format:") != std::string::npos) {
            size_t pos = line.find(':');
            if (pos != std::string::npos) {
                AIProvider p = string_to_provider(current_provider);
                m_settings.provider_configs[p].api_format = trim_string(line.substr(pos + 1));
            }
        }
        else if (line.find("ai_batch_size:") != std::string::npos) {
            size_t pos = line.find(':');
            if (pos != std::string::npos) {
                m_settings.ai_batch_size = std::stoi(trim_string(line.substr(pos + 1)));
            }
        }
        else if (line.find("taskqueue_batch_size:") != std::string::npos) {
            size_t pos = line.find(':');
            if (pos != std::string::npos) {
                m_settings.taskqueue_batch_size = std::stoi(trim_string(line.substr(pos + 1)));
            }
        }
        else if (in_cache_section && line.find("enabled:") != std::string::npos) {
            size_t pos = line.find(':');
            if (pos != std::string::npos) {
                std::string val = trim_string(line.substr(pos + 1));
                m_settings.cache_enabled = (val == "true");
            }
        }
        else if (in_cache_section && line.find("expiration_days:") != std::string::npos) {
            size_t pos = line.find(':');
            if (pos != std::string::npos) {
                m_settings.cache_expiration_days = std::stoi(trim_string(line.substr(pos + 1)));
            }
        }
        else if (in_cache_section && line.find("max_size_mb:") != std::string::npos) {
            size_t pos = line.find(':');
            if (pos != std::string::npos) {
                m_settings.max_cache_size_mb = std::stoi(trim_string(line.substr(pos + 1)));
            }
        }
        else if (in_cache_section && line.find("auto_cleanup:") != std::string::npos) {
            size_t pos = line.find(':');
            if (pos != std::string::npos) {
                std::string val = trim_string(line.substr(pos + 1));
                m_settings.auto_cleanup = (val == "true");
            }
        }
    }
    
    for (auto& [p, config] : m_settings.provider_configs) {
        std::sort(config.models.begin(), config.models.end(), 
            [](const ModelInfo& a, const ModelInfo& b) { return a.priority < b.priority; });
        if (!config.models.empty() && config.selected_model.empty()) {
            config.selected_model = config.models[0].name;
        }
    }
}

void SettingsManager::load() {
    std::ifstream file(m_config_path);
    if (!file.is_open()) {
        return;
    }
    
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();
    
    auto get_value = [&content](const std::string& key) -> std::string {
        std::string search = "\"" + key + "\":";
        size_t pos = content.find(search);
        if (pos == std::string::npos) return "";
        pos += search.length();
        while (pos < content.length() && (content[pos] == ' ' || content[pos] == '\t')) pos++;
        if (pos >= content.length()) return "";
        if (content[pos] == '"') {
            pos++;
            size_t end = content.find('"', pos);
            if (end != std::string::npos) {
                return content.substr(pos, end - pos);
            }
        } else {
            size_t end = content.find_first_of(",}\n", pos);
            if (end != std::string::npos) {
                return content.substr(pos, end - pos);
            }
        }
        return "";
    };
    
    std::string provider_str = get_value("provider");
    if (!provider_str.empty()) {
        m_settings.provider = string_to_provider(provider_str);
    }
    
    m_settings.use_env_key = (get_value("use_env_key") == "true");
    
    std::string python_path = get_value("python_path");
    if (!python_path.empty()) {
        m_settings.python_path = python_path;
        set_python_path(python_path);
    }
    
    std::string auto_install = get_value("auto_install_packages");
    if (!auto_install.empty()) {
        m_settings.auto_install_packages = (auto_install == "true");
    }
    
    std::string cache_enabled = get_value("cache_enabled");
    if (!cache_enabled.empty()) {
        m_settings.cache_enabled = (cache_enabled == "true");
    }
    
    std::string auto_cleanup = get_value("auto_cleanup");
    if (!auto_cleanup.empty()) {
        m_settings.auto_cleanup = (auto_cleanup == "true");
    }
    
    std::string auto_restart = get_value("auto_restart");
    if (!auto_restart.empty()) {
        m_settings.auto_restart = (auto_restart == "true");
    }
    
    std::string show_progress = get_value("show_progress_dialog");
    if (!show_progress.empty()) {
        m_settings.show_progress_dialog = (show_progress == "true");
    }
    
    std::string cache_expiration = get_value("cache_expiration_days");
    if (!cache_expiration.empty()) {
        m_settings.cache_expiration_days = std::stoi(cache_expiration);
    }
    
    std::string max_cache_size = get_value("max_cache_size_mb");
    if (!max_cache_size.empty()) {
        m_settings.max_cache_size_mb = std::stoi(max_cache_size);
    }
    
    std::string ai_batch_size = get_value("ai_batch_size");
    if (!ai_batch_size.empty()) {
        m_settings.ai_batch_size = std::stoi(ai_batch_size);
    }
    
    std::string taskqueue_batch_size = get_value("taskqueue_batch_size");
    if (!taskqueue_batch_size.empty()) {
        m_settings.taskqueue_batch_size = std::stoi(taskqueue_batch_size);
    }
    
    std::string concurrency = get_value("concurrency");
    if (!concurrency.empty()) {
        m_settings.concurrency = std::stoi(concurrency);
    }
    
    std::string mb_timeout = get_value("mb_timeout");
    if (!mb_timeout.empty()) {
        m_settings.mb_timeout = std::stoi(mb_timeout);
    }
    
    std::string mb_retries = get_value("mb_retries");
    if (!mb_retries.empty()) {
        m_settings.mb_retries = std::stoi(mb_retries);
    }
    
    std::string mb_page_size = get_value("mb_page_size");
    if (!mb_page_size.empty()) {
        m_settings.mb_page_size = std::stoi(mb_page_size);
    }
    
    std::string mb_max_pages = get_value("mb_max_pages");
    if (!mb_max_pages.empty()) {
        m_settings.mb_max_pages = std::stoi(mb_max_pages);
    }
    
    std::string mb_score_threshold = get_value("mb_score_threshold");
    if (!mb_score_threshold.empty()) {
        m_settings.mb_score_threshold = std::stoi(mb_score_threshold);
    }
    
    std::string mb_score_margin = get_value("mb_score_margin");
    if (!mb_score_margin.empty()) {
        m_settings.mb_score_margin = std::stoi(mb_score_margin);
    }
    
    std::string mb_rate_limit = get_value("mb_rate_limit");
    if (!mb_rate_limit.empty()) {
        m_settings.mb_rate_limit = std::stoi(mb_rate_limit);
    }
    
    std::string discogs_key = get_value("discogs_consumer_key");
    if (!discogs_key.empty()) {
        m_settings.discogs_consumer_key = discogs_key;
    }
    
    std::string discogs_secret = get_value("discogs_consumer_secret");
    if (!discogs_secret.empty()) {
        m_settings.discogs_consumer_secret = discogs_secret;
    }
    
    std::string log_level = get_value("log_level");
    if (!log_level.empty()) {
        m_settings.log_level = static_cast<ai_metadata::constants::LogLevel>(std::stoi(log_level));
    }
    
    std::string max_log_size = get_value("max_log_file_size_mb");
    if (!max_log_size.empty()) {
        m_settings.max_log_file_size_mb = std::stoi(max_log_size);
    }
    
    size_t pc_pos = content.find("\"provider_configs\":");
    if (pc_pos != std::string::npos) {
        size_t pc_start = content.find('{', pc_pos);
        if (pc_start != std::string::npos) {
            int pc_brace = 1;
            size_t pc_end = pc_start + 1;
            while (pc_end < content.length() && pc_brace > 0) {
                if (content[pc_end] == '{') pc_brace++;
                else if (content[pc_end] == '}') pc_brace--;
                pc_end++;
            }
            std::string pc_str = content.substr(pc_start, pc_end - pc_start);
            
            const char* prov_names[] = {"openrouter", "zhipu", "gemini", "ollama", "deepseek", "custom"};
            AIProvider prov_types[] = {AIProvider::OpenRouter, AIProvider::Zhipu, AIProvider::Gemini, AIProvider::Ollama, AIProvider::DeepSeek, AIProvider::Custom};

            for (int i = 0; i < 6; i++) {
                std::string search_name = std::string("\"") + prov_names[i] + "\":";
                size_t prov_pos = pc_str.find(search_name);
                if (prov_pos == std::string::npos) continue;
                
                size_t prov_start = pc_str.find('{', prov_pos);
                if (prov_start == std::string::npos) continue;
                
                int prov_brace = 1;
                size_t prov_end = prov_start + 1;
                while (prov_end < pc_str.length() && prov_brace > 0) {
                    if (pc_str[prov_end] == '{') prov_brace++;
                    else if (pc_str[prov_end] == '}') prov_brace--;
                    prov_end++;
                }
                std::string prov_str = pc_str.substr(prov_start, prov_end - prov_start);
                
                size_t sm_pos = prov_str.find("\"selected_model\":");
                if (sm_pos != std::string::npos) {
                    size_t val_start = prov_str.find('"', sm_pos + 17);
                    if (val_start != std::string::npos) {
                        size_t val_end = prov_str.find('"', val_start + 1);
                        if (val_end != std::string::npos) {
                            std::string sel_model = prov_str.substr(val_start + 1, val_end - val_start - 1);
                            m_settings.provider_configs[prov_types[i]].selected_model = sel_model;
                        }
                    }
                }
                
                size_t ak_pos = prov_str.find("\"api_key\":");
                if (ak_pos != std::string::npos) {
                    size_t val_start = prov_str.find('"', ak_pos + 10);
                    if (val_start != std::string::npos) {
                        size_t val_end = prov_str.find('"', val_start + 1);
                        if (val_end != std::string::npos) {
                            std::string api_key = prov_str.substr(val_start + 1, val_end - val_start - 1);
                            m_settings.provider_configs[prov_types[i]].api_key = api_key;
                        }
                    }
                }

                // 解析 base_url
                size_t bu_pos = prov_str.find("\"base_url\":");
                if (bu_pos != std::string::npos) {
                    size_t val_start = prov_str.find('"', bu_pos + 11);
                    if (val_start != std::string::npos) {
                        size_t val_end = prov_str.find('"', val_start + 1);
                        if (val_end != std::string::npos) {
                            m_settings.provider_configs[prov_types[i]].base_url = prov_str.substr(val_start + 1, val_end - val_start - 1);
                        }
                    }
                }

                // 解析 api_format
                size_t af_pos = prov_str.find("\"api_format\":");
                if (af_pos != std::string::npos) {
                    size_t val_start = prov_str.find('"', af_pos + 13);
                    if (val_start != std::string::npos) {
                        size_t val_end = prov_str.find('"', val_start + 1);
                        if (val_end != std::string::npos) {
                            m_settings.provider_configs[prov_types[i]].api_format = prov_str.substr(val_start + 1, val_end - val_start - 1);
                        }
                    }
                }
            }
        }
    }

    // 解析 prompts.user_prefs（Layer 3）
    {
        std::string ts = get_value("translation_style");
        if (!ts.empty()) m_settings.prompt_prefs.translation_style = ts;

        std::string gl = get_value("genre_language");
        if (!gl.empty()) m_settings.prompt_prefs.genre_language = gl;

        std::string ko = get_value("keep_original_when_uncertain");
        if (!ko.empty()) m_settings.prompt_prefs.keep_original_when_uncertain = (ko == "true");

        std::string mc = get_value("min_translation_confidence");
        if (!mc.empty()) {
            try { m_settings.prompt_prefs.min_translation_confidence = std::stod(mc); }
            catch (...) {}
        }

        std::string pp = get_value("translation_platform_priority");
        if (!pp.empty()) m_settings.prompt_prefs.translation_platform_priority = pp;

        // 多行字符串字段需要处理转义：找到 "custom_translation_hints": 后的字符串字面量
        auto extract_escaped_string = [&content](const std::string& key) -> std::string {
            std::string search = "\"" + key + "\":";
            size_t pos = content.find(search);
            if (pos == std::string::npos) return "";
            pos += search.length();
            while (pos < content.length() && (content[pos] == ' ' || content[pos] == '\t' || content[pos] == '\n' || content[pos] == '\r')) pos++;
            if (pos >= content.length() || content[pos] != '"') return "";
            pos++;
            std::string out;
            while (pos < content.length()) {
                char c = content[pos];
                if (c == '\\' && pos + 1 < content.length()) {
                    char next = content[pos + 1];
                    switch (next) {
                        case '"':  out.push_back('"');  break;
                        case '\\': out.push_back('\\'); break;
                        case 'n':  out.push_back('\n'); break;
                        case 'r':  out.push_back('\r'); break;
                        case 't':  out.push_back('\t'); break;
                        case 'b':  out.push_back('\b'); break;
                        case 'f':  out.push_back('\f'); break;
                        default:   out.push_back(next);  break;
                    }
                    pos += 2;
                } else if (c == '"') {
                    break;
                } else {
                    out.push_back(c);
                    pos++;
                }
            }
            return out;
        };

        std::string hints = extract_escaped_string("custom_translation_hints");
        if (!hints.empty()) m_settings.prompt_prefs.custom_translation_hints = hints;

        std::string instr = extract_escaped_string("custom_instructions");
        if (!instr.empty()) m_settings.prompt_prefs.custom_instructions = instr;
    }
}

void SettingsManager::save() {
    std::string dir = m_config_path.substr(0, m_config_path.find_last_of("\\/"));
    CreateDirectoryA(dir.c_str(), NULL);
    
    std::ofstream file(m_config_path);
    if (file.is_open()) {
        file << "{\n";
        // V1: providers 列表不再写入 settings.json（权威源为 Python SQLite）
        file << "  \"python_path\": \"" << m_settings.python_path << "\",\n";
        file << "  \"auto_install_packages\": " << (m_settings.auto_install_packages ? "true" : "false") << ",\n";
        
        file << "  \"cache_enabled\": " << (m_settings.cache_enabled ? "true" : "false") << ",\n";
        file << "  \"cache_expiration_days\": " << m_settings.cache_expiration_days << ",\n";
        file << "  \"max_cache_size_mb\": " << m_settings.max_cache_size_mb << ",\n";
        file << "  \"auto_cleanup\": " << (m_settings.auto_cleanup ? "true" : "false") << ",\n";
        file << "  \"auto_restart\": " << (m_settings.auto_restart ? "true" : "false") << ",\n";
        file << "  \"ai_batch_size\": " << m_settings.ai_batch_size << ",\n";
        file << "  \"taskqueue_batch_size\": " << m_settings.taskqueue_batch_size << ",\n";
        file << "  \"concurrency\": " << m_settings.concurrency << ",\n";
        file << "  \"show_progress_dialog\": " << (m_settings.show_progress_dialog ? "true" : "false") << ",\n";
        file << "  \"mb_timeout\": " << m_settings.mb_timeout << ",\n";
        file << "  \"mb_retries\": " << m_settings.mb_retries << ",\n";
        file << "  \"mb_page_size\": " << m_settings.mb_page_size << ",\n";
        file << "  \"mb_max_pages\": " << m_settings.mb_max_pages << ",\n";
        file << "  \"mb_score_threshold\": " << m_settings.mb_score_threshold << ",\n";
        file << "  \"mb_score_margin\": " << m_settings.mb_score_margin << ",\n";
        file << "  \"mb_rate_limit\": " << m_settings.mb_rate_limit << ",\n";
        file << "  \"discogs_consumer_key\": \"" << m_settings.discogs_consumer_key << "\",\n";
        file << "  \"discogs_consumer_secret\": \"" << m_settings.discogs_consumer_secret << "\",\n";
        file << "  \"log_level\": " << static_cast<int>(m_settings.log_level) << ",\n";
        file << "  \"max_log_file_size_mb\": " << m_settings.max_log_file_size_mb << ",\n";

        // prompts.user_prefs（Layer 3）
        file << "  \"prompts\": {\n";
        file << "    \"user_prefs\": {\n";
        file << "      \"translation_style\": \"" << m_settings.prompt_prefs.translation_style << "\",\n";
        file << "      \"genre_language\": \"" << m_settings.prompt_prefs.genre_language << "\",\n";
        file << "      \"keep_original_when_uncertain\": " << (m_settings.prompt_prefs.keep_original_when_uncertain ? "true" : "false") << ",\n";
        file << "      \"min_translation_confidence\": " << m_settings.prompt_prefs.min_translation_confidence << ",\n";
        file << "      \"translation_platform_priority\": \"" << m_settings.prompt_prefs.translation_platform_priority << "\",\n";
        // 多行字符串以 JSON 字符串形式转义换行
        file << "      \"custom_translation_hints\": " << json_escape_string(m_settings.prompt_prefs.custom_translation_hints) << ",\n";
        file << "      \"custom_instructions\": " << json_escape_string(m_settings.prompt_prefs.custom_instructions) << "\n";
        file << "    }\n";
        file << "  }\n";
        file << "}\n";
    }
}

void SettingsManager::reset() {
    m_initialized = false;
    m_settings = PluginSettings();
}

std::string SettingsManager::get_python_path() const {
    const auto& s = settings();
    if (!s.python_path.empty()) {
        return s.python_path;
    }
    return auto_detect_python_path();
}

std::string SettingsManager::auto_detect_python_path() const {
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
        std::string test_cmd = std::string(path) + " --version 2>&1";
        std::string result;
        int exit_code = -1;
        // 使用 CREATE_NO_WINDOW 避免弹出 cmd 窗口
        if (!run_command_hidden_pref(test_cmd, result, &exit_code)) {
            continue;
        }
        if (exit_code == 0 && result.find("Python") != std::string::npos) {
            log_format("[AI Metadata] auto_detect_python_path: Found Python at '", path, "' - ", result.c_str());

            if (std::string(path).find("\\") != std::string::npos ||
                std::string(path).find("/") != std::string::npos) {
                return path;
            }

            // 用 where 解析完整路径（同样隐藏窗口）
            std::string where_cmd = std::string("where ") + path + " 2>nul";
            std::string where_out;
            int where_exit = -1;
            if (run_command_hidden_pref(where_cmd, where_out, &where_exit) && where_exit == 0) {
                // 取第一行
                size_t nl = where_out.find_first_of("\r\n");
                std::string full_path = (nl == std::string::npos) ? where_out : where_out.substr(0, nl);
                if (!full_path.empty()) {
                    return full_path;
                }
            }
            return path;
        }
    }

    log_format("[AI Metadata] auto_detect_python_path: Python not found");
    return "";
}

const char* AIPreferencePageRoot::get_name() {
    return "AI Metadata";
}

GUID AIPreferencePageRoot::get_guid() {
    return g_guid;
}

GUID AIPreferencePageRoot::get_parent_guid() {
    return guid_tools;
}

preferences_page_instance::ptr AIPreferencePageRoot::instantiate(HWND parent, preferences_page_callback::ptr callback) {
    return new service_impl_t<AIPreferencePageInstance>(parent, callback, IDD_PREFERENCES);
}

const char* AIPreferencePageGeneral::get_name() {
    return "General";
}

GUID AIPreferencePageGeneral::get_guid() {
    return g_guid;
}

GUID AIPreferencePageGeneral::get_parent_guid() {
    return guid_preferences_root;
}

preferences_page_instance::ptr AIPreferencePageGeneral::instantiate(HWND parent, preferences_page_callback::ptr callback) {
    return new service_impl_t<AIPreferencePageInstance>(parent, callback, IDD_PREF_GENERAL);
}

const char* AIPreferencePageDataSources::get_name() {
    return "Data Sources";
}

GUID AIPreferencePageDataSources::get_guid() {
    return g_guid;
}

GUID AIPreferencePageDataSources::get_parent_guid() {
    return guid_preferences_root;
}

preferences_page_instance::ptr AIPreferencePageDataSources::instantiate(HWND parent, preferences_page_callback::ptr callback) {
    return new service_impl_t<AIPreferencePageInstance>(parent, callback, IDD_PREF_DATA_SOURCES);
}

const char* AIPreferencePageProcessing::get_name() {
    return "Processing";
}

GUID AIPreferencePageProcessing::get_guid() {
    return g_guid;
}

GUID AIPreferencePageProcessing::get_parent_guid() {
    return guid_preferences_root;
}

preferences_page_instance::ptr AIPreferencePageProcessing::instantiate(HWND parent, preferences_page_callback::ptr callback) {
    return new service_impl_t<AIPreferencePageInstance>(parent, callback, IDD_PREF_PROCESSING);
}

const char* AIPreferencePageCacheLogs::get_name() {
    return "Cache & Logs";
}

GUID AIPreferencePageCacheLogs::get_guid() {
    return g_guid;
}

GUID AIPreferencePageCacheLogs::get_parent_guid() {
    return guid_preferences_root;
}

preferences_page_instance::ptr AIPreferencePageCacheLogs::instantiate(HWND parent, preferences_page_callback::ptr callback) {
    return new service_impl_t<AIPreferencePageInstance>(parent, callback, IDD_PREF_CACHE_LOGS);
}

const char* AIPreferencePagePrompts::get_name() {
    return "Translation";
}

GUID AIPreferencePagePrompts::get_guid() {
    return g_guid;
}

GUID AIPreferencePagePrompts::get_parent_guid() {
    return guid_preferences_root;
}

preferences_page_instance::ptr AIPreferencePagePrompts::instantiate(HWND parent, preferences_page_callback::ptr callback) {
    Logger::instance().info("[PrefPage] AIPreferencePagePrompts::instantiate called, IDD_PREF_PROMPTS=" + std::to_string(IDD_PREF_PROMPTS));
    return new service_impl_t<AIPreferencePageInstance>(parent, callback, IDD_PREF_PROMPTS);
}

AIPreferencePageInstance::AIPreferencePageInstance(HWND parent, preferences_page_callback::ptr callback, int dialog_id)
    : m_callback(callback), m_modified(false), m_dialog_id(dialog_id) {
    Logger::instance().info("[PrefPage] Constructor ENTER, dialog_id=" + std::to_string(dialog_id) + ", parent=" + std::to_string(reinterpret_cast<uintptr_t>(parent)));
    m_settings = SettingsManager::instance().settings();

    // 先验证资源是否存在
    HRSRC hRes = FindResource(core_api::get_my_instance(), MAKEINTRESOURCE(dialog_id), RT_DIALOG);
    Logger::instance().info("[PrefPage] FindResource: dialog_id=" + std::to_string(dialog_id) + ", hRes=" + std::to_string(reinterpret_cast<uintptr_t>(hRes)) + ", err=" + std::to_string(GetLastError()));

    SetLastError(0);
    m_wnd = CreateDialogParam(
        core_api::get_my_instance(),
        MAKEINTRESOURCE(dialog_id),
        parent,
        dialog_proc,
        reinterpret_cast<LPARAM>(this)
    );
    DWORD err = GetLastError();
    Logger::instance().info("[PrefPage] Constructor EXIT, dialog_id=" + std::to_string(dialog_id) + ", m_wnd=" + std::to_string(reinterpret_cast<uintptr_t>(m_wnd)) + ", err=" + std::to_string(err));
}

AIPreferencePageInstance::~AIPreferencePageInstance() {
    if (m_wnd) {
        DestroyWindow(m_wnd);
    }
}

HWND AIPreferencePageInstance::get_wnd() {
    return m_wnd;
}

t_uint32 AIPreferencePageInstance::get_state() {
    t_uint32 state = preferences_state::resettable;
    if (m_modified) {
        state |= preferences_state::changed;
    }
    return state;
}

void AIPreferencePageInstance::apply() {
    auto old_settings = SettingsManager::instance().settings();
    auto old_log_level = old_settings.log_level;

    save_settings();
    SettingsManager::instance().settings() = m_settings;
    SettingsManager::instance().save();

    AICore* ai_core = get_ai_core_instance();
    if (ai_core) {
        ai_core->set_config("expiration_days", std::to_string(m_settings.cache_expiration_days));
        ai_core->set_config("max_cache_size_mb", std::to_string(m_settings.max_cache_size_mb));
        ai_core->set_config("auto_cleanup", m_settings.auto_cleanup ? "true" : "false");
        ai_core->set_taskqueue_batch_size(m_settings.taskqueue_batch_size);
        ai_core->set_ai_batch_size(m_settings.ai_batch_size);
        // 同步 cache_enabled 到 AICore，否则 UI 改了但 AICore 仍用旧值会导致缓存继续写入
        ai_core->set_cache_enabled(m_settings.cache_enabled);

        // 日志级别变更时，动态通知Python worker（无需重启）
        if (m_settings.log_level != old_log_level) {
            std::string level_name = constants::log_level_to_string(m_settings.log_level);
            ai_core->update_worker_log_level(level_name);
        }

        // V1: Provider 配置由 SQLite + providers IPC 即时生效，无需因 settings.json 重启 Worker
    }

    m_modified = false;
    m_callback->on_state_changed();
}

void AIPreferencePageInstance::reset() {
    m_settings = PluginSettings();
    update_controls();
    on_changed();
}

INT_PTR CALLBACK AIPreferencePageInstance::dialog_proc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) {
    AIPreferencePageInstance* self = nullptr;

    if (msg == WM_INITDIALOG) {
        self = reinterpret_cast<AIPreferencePageInstance*>(lp);
        SetWindowLongPtr(wnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->m_wnd = wnd;
        Logger::instance().info("[PrefPage] WM_INITDIALOG: wnd=" + std::to_string(reinterpret_cast<uintptr_t>(wnd)) + ", dialog_id=" + std::to_string(self->m_dialog_id));
        self->on_init_dialog();
        SetTimer(wnd, FILL_COMBO_TIMER_ID, FILL_COMBO_TIMER_DELAY, NULL);
        return TRUE;
    }
    
    self = reinterpret_cast<AIPreferencePageInstance*>(GetWindowLongPtr(wnd, GWLP_USERDATA));
    if (!self) return FALSE;
    
    switch (msg) {
        case WM_TIMER:
            if (wp == FILL_COMBO_TIMER_ID) {
                KillTimer(wnd, FILL_COMBO_TIMER_ID);
                self->fill_combo_boxes();
            }
            break;
            
        case WM_SHOWWINDOW:
            // 页面显示时确保 combo 框已填充（防御性：on_init_dialog 和 timer 可能因
            // 消息时序问题未执行）
            if (self) {
                HWND tcombo = GetDlgItem(wnd, IDC_TRANSLATION_STYLE);
                if (tcombo && SendMessageW(tcombo, CB_GETCOUNT, 0, 0) == 0) {
                    self->fill_combo_boxes();
                }
            }
            return TRUE;
            
        case WM_FILL_COMBO_BOXES:
            self->fill_combo_boxes();
            return TRUE;
            
        case WM_COMMAND:
            switch (LOWORD(wp)) {
                case IDC_CACHE_EXPIRATION:
                case IDC_CACHE_SIZE:
                case IDC_BATCH_SIZE_PREF:
                case IDC_TASKQUEUE_BATCH_SIZE:
                case IDC_CONCURRENCY_PREF:
                case IDC_MB_TIMEOUT:
                case IDC_MB_RETRIES:
                case IDC_MB_PAGE_SIZE:
                case IDC_MB_MAX_PAGES:
                case IDC_MB_SCORE_THRESHOLD:
                case IDC_MB_SCORE_MARGIN:
                case IDC_MB_RATE_LIMIT:
                case IDC_LOG_SIZE:
                case IDC_DISCOGS_KEY:
                case IDC_DISCOGS_SECRET:
                    if (HIWORD(wp) == EN_CHANGE) {
                        self->on_changed();
                    }
                    break;
                    
                case IDC_ENABLE_CACHE:
                case IDC_AUTO_CLEANUP:
                case IDC_AUTO_RESTART:
                case IDC_SHOW_PROGRESS_PREF:
                case IDC_AUTO_INSTALL_PACKAGES:
                case IDC_ENABLE_MUSICBRAINZ:
                case IDC_ENABLE_DISCOGS:
                case IDC_ENABLE_AI:
                    self->on_changed();
                    break;
                    
                case IDC_LOG_LEVEL:
                    if (HIWORD(wp) == CBN_SELCHANGE) {
                        self->on_changed();
                    }
                    break;
                    
                case IDC_TEST_API_BTN:
                    self->on_test_api();
                    break;

                case IDC_PROVIDER_ADD_BTN:
                    self->on_provider_add();
                    break;
                case IDC_PROVIDER_EDIT_BTN:
                    self->on_provider_edit();
                    break;
                case IDC_PROVIDER_DELETE_BTN:
                    self->on_provider_delete();
                    break;
                case IDC_PROVIDER_SET_CURRENT_BTN:
                    self->on_provider_set_current();
                    break;
                case IDC_PROVIDER_RESTORE_BTN:
                    self->on_provider_restore_presets();
                    break;

                case IDC_CUSTOM_MODEL_CONFIG_BTN:
                    // legacy custom dialog removed from General page
                    break;
                    
                case IDC_OPEN_LOG_BTN:
                    self->on_open_log_folder();
                    break;
                    
                case IDC_CLEAR_CACHE_BTN_PREF:
                    self->on_clear_cache();
                    break;
                    
                case IDC_RESTART_WORKERS_BTN:
                    self->on_restart_workers();
                    break;
                    
                case IDC_PYTHON_BROWSE:
                    self->on_browse_python();
                    break;

                case IDC_PYTHON_DETECT:
                    self->on_detect_python();
                    break;

                case IDC_TRANSLATION_STYLE:
                    if (HIWORD(wp) == CBN_SELCHANGE) {
                        self->on_changed();
                    }
                    break;

                case IDC_KEEP_ORIGINAL:
                    self->on_changed();
                    break;

                case IDC_MIN_CONFIDENCE:
                case IDC_PLATFORM_PRIORITY:
                case IDC_CUSTOM_HINTS:
                case IDC_CUSTOM_INSTRUCTIONS:
                    if (HIWORD(wp) == EN_CHANGE) {
                        self->on_changed();
                    }
                    break;

                case IDC_EXPORT_TEMPLATES_BTN:
                    self->on_export_templates();
                    break;
            }
            return TRUE;
    }
    
    return FALSE;
}

void AIPreferencePageInstance::on_init_dialog() {
    // 诊断日志：记录 dialog id 和关键控件句柄
    {
        char diag[256];
        std::snprintf(diag, sizeof(diag),
            "[PrefPage] on_init_dialog: dialog_id=%d wnd=%p IDC_TRANSLATION_STYLE=%p IDC_KEEP_ORIGINAL=%p",
            m_dialog_id, (void*)m_wnd,
            (void*)GetDlgItem(m_wnd, IDC_TRANSLATION_STYLE),
            (void*)GetDlgItem(m_wnd, IDC_KEEP_ORIGINAL));
        Logger::instance().info(diag);
    }
    // 同步填充 combo 框（确保控件在显示时已有数据）
    fill_combo_boxes();
    update_controls();
}

void AIPreferencePageInstance::fill_combo_boxes() {
    // V1: Provider 列表由 refresh_provider_list 填充
    if (GetDlgItem(m_wnd, IDC_PROVIDER_LIST)) {
        refresh_provider_list();
    }

    // 诊断日志：记录 fill_combo_boxes 调用及关键控件句柄
    {
        char diag[256];
        std::snprintf(diag, sizeof(diag),
            "[PrefPage] fill_combo_boxes: wnd=%p IDC_PROVIDER_LIST=%p IDC_TRANSLATION_STYLE=%p",
            (void*)m_wnd, (void*)GetDlgItem(m_wnd, IDC_PROVIDER_LIST),
            (void*)GetDlgItem(m_wnd, IDC_TRANSLATION_STYLE));
        Logger::instance().info(diag);
    }

    HWND combo = GetDlgItem(m_wnd, IDC_LOG_LEVEL);
    if (combo) {
        SendMessageW(combo, CB_RESETCONTENT, 0, 0);
        SendMessageW(combo, CB_INSERTSTRING, 0, (LPARAM)L"DEBUG");
        SendMessageW(combo, CB_INSERTSTRING, 1, (LPARAM)L"INFO");
        SendMessageW(combo, CB_INSERTSTRING, 2, (LPARAM)L"WARNING");
        SendMessageW(combo, CB_INSERTSTRING, 3, (LPARAM)L"ERROR");
        SendMessageW(combo, CB_SETCURSEL, static_cast<int>(m_settings.log_level), 0);
        Logger::instance().set_log_level(m_settings.log_level);
    }

    // Prompt 偏好页面组合框
    combo = GetDlgItem(m_wnd, IDC_TRANSLATION_STYLE);
    if (combo) {
        SendMessageW(combo, CB_RESETCONTENT, 0, 0);
        SendMessageW(combo, CB_INSERTSTRING, 0, (LPARAM)L"Official (recommended)");
        SendMessageW(combo, CB_INSERTSTRING, 1, (LPARAM)L"Literal");
        SendMessageW(combo, CB_INSERTSTRING, 2, (LPARAM)L"Semantic");
        int style_idx = 0;
        const std::string& ts = m_settings.prompt_prefs.translation_style;
        if (ts == "literal") style_idx = 1;
        else if (ts == "semantic") style_idx = 2;
        SendMessageW(combo, CB_SETCURSEL, style_idx, 0);
    }

    // Note: IDC_GENRE_LANGUAGE removed - genre is now sourced from MusicBrainz
    // (Scrape) in English, so the language selection is no longer applicable.

    // combo 填充完毕后重新同步控件值（WM_INITDIALOG 时 update_controls 在 combo 填充前调用，
    // 会导致 translation style 等 combo 显示空白）
    update_controls();
}

void AIPreferencePageInstance::update_controls() {
    // V1 providers UI: list is refreshed via refresh_provider_list

    if (GetDlgItem(m_wnd, IDC_PYTHON_PATH)) {
        SetDlgItemTextA(m_wnd, IDC_PYTHON_PATH, m_settings.python_path.c_str());
    }
    if (GetDlgItem(m_wnd, IDC_AUTO_INSTALL_PACKAGES)) {
        CheckDlgButton(m_wnd, IDC_AUTO_INSTALL_PACKAGES, m_settings.auto_install_packages ? BST_CHECKED : BST_UNCHECKED);
    }
    
    if (GetDlgItem(m_wnd, IDC_PYTHON_STATUS)) {
        std::string status_text = "Status: ";
        if (!m_settings.python_path.empty()) {
            status_text += m_settings.python_path;
        } else {
            status_text += "Not configured (click Detect)";
        }
        SetDlgItemTextA(m_wnd, IDC_PYTHON_STATUS, status_text.c_str());
    }
    
    if (GetDlgItem(m_wnd, IDC_ENABLE_CACHE)) {
        CheckDlgButton(m_wnd, IDC_ENABLE_CACHE, m_settings.cache_enabled ? BST_CHECKED : BST_UNCHECKED);
    }
    if (GetDlgItem(m_wnd, IDC_CACHE_EXPIRATION)) {
        SetDlgItemInt(m_wnd, IDC_CACHE_EXPIRATION, m_settings.cache_expiration_days, FALSE);
    }
    if (GetDlgItem(m_wnd, IDC_CACHE_SIZE)) {
        SetDlgItemInt(m_wnd, IDC_CACHE_SIZE, m_settings.max_cache_size_mb, FALSE);
    }
    if (GetDlgItem(m_wnd, IDC_AUTO_CLEANUP)) {
        CheckDlgButton(m_wnd, IDC_AUTO_CLEANUP, m_settings.auto_cleanup ? BST_CHECKED : BST_UNCHECKED);
    }
    
    if (GetDlgItem(m_wnd, IDC_AUTO_RESTART)) {
        CheckDlgButton(m_wnd, IDC_AUTO_RESTART, m_settings.auto_restart ? BST_CHECKED : BST_UNCHECKED);
    }
    
    if (GetDlgItem(m_wnd, IDC_BATCH_SIZE_PREF)) {
        SetDlgItemInt(m_wnd, IDC_BATCH_SIZE_PREF, m_settings.ai_batch_size, FALSE);
    }
    if (GetDlgItem(m_wnd, IDC_TASKQUEUE_BATCH_SIZE)) {
        SetDlgItemInt(m_wnd, IDC_TASKQUEUE_BATCH_SIZE, m_settings.taskqueue_batch_size, FALSE);
    }
    if (GetDlgItem(m_wnd, IDC_CONCURRENCY_PREF)) {
        SetDlgItemInt(m_wnd, IDC_CONCURRENCY_PREF, m_settings.concurrency, FALSE);
    }
    if (GetDlgItem(m_wnd, IDC_SHOW_PROGRESS_PREF)) {
        CheckDlgButton(m_wnd, IDC_SHOW_PROGRESS_PREF, m_settings.show_progress_dialog ? BST_CHECKED : BST_UNCHECKED);
    }
    
    if (GetDlgItem(m_wnd, IDC_MB_TIMEOUT)) {
        SetDlgItemInt(m_wnd, IDC_MB_TIMEOUT, m_settings.mb_timeout, FALSE);
    }
    if (GetDlgItem(m_wnd, IDC_MB_RETRIES)) {
        SetDlgItemInt(m_wnd, IDC_MB_RETRIES, m_settings.mb_retries, FALSE);
    }
    if (GetDlgItem(m_wnd, IDC_MB_PAGE_SIZE)) {
        SetDlgItemInt(m_wnd, IDC_MB_PAGE_SIZE, m_settings.mb_page_size, FALSE);
    }
    if (GetDlgItem(m_wnd, IDC_MB_MAX_PAGES)) {
        SetDlgItemInt(m_wnd, IDC_MB_MAX_PAGES, m_settings.mb_max_pages, FALSE);
    }
    if (GetDlgItem(m_wnd, IDC_MB_SCORE_THRESHOLD)) {
        SetDlgItemInt(m_wnd, IDC_MB_SCORE_THRESHOLD, m_settings.mb_score_threshold, FALSE);
    }
    if (GetDlgItem(m_wnd, IDC_MB_SCORE_MARGIN)) {
        SetDlgItemInt(m_wnd, IDC_MB_SCORE_MARGIN, m_settings.mb_score_margin, FALSE);
    }
    if (GetDlgItem(m_wnd, IDC_MB_RATE_LIMIT)) {
        SetDlgItemInt(m_wnd, IDC_MB_RATE_LIMIT, m_settings.mb_rate_limit, FALSE);
    }
    
    if (GetDlgItem(m_wnd, IDC_ENABLE_MUSICBRAINZ)) {
        CheckDlgButton(m_wnd, IDC_ENABLE_MUSICBRAINZ, m_settings.enable_musicbrainz ? BST_CHECKED : BST_UNCHECKED);
    }
    if (GetDlgItem(m_wnd, IDC_ENABLE_DISCOGS)) {
        CheckDlgButton(m_wnd, IDC_ENABLE_DISCOGS, m_settings.enable_discogs ? BST_CHECKED : BST_UNCHECKED);
    }
    if (GetDlgItem(m_wnd, IDC_DISCOGS_KEY)) {
        SetDlgItemTextA(m_wnd, IDC_DISCOGS_KEY, m_settings.discogs_consumer_key.c_str());
    }
    if (GetDlgItem(m_wnd, IDC_DISCOGS_SECRET)) {
        SetDlgItemTextA(m_wnd, IDC_DISCOGS_SECRET, m_settings.discogs_consumer_secret.c_str());
    }
    if (GetDlgItem(m_wnd, IDC_ENABLE_AI)) {
        CheckDlgButton(m_wnd, IDC_ENABLE_AI, m_settings.enable_ai ? BST_CHECKED : BST_UNCHECKED);
    }
    
    if (GetDlgItem(m_wnd, IDC_LOG_SIZE)) {
        SetDlgItemInt(m_wnd, IDC_LOG_SIZE, m_settings.max_log_file_size_mb, FALSE);
    }

    // Prompt 偏好页面控件
    if (GetDlgItem(m_wnd, IDC_KEEP_ORIGINAL)) {
        CheckDlgButton(m_wnd, IDC_KEEP_ORIGINAL, m_settings.prompt_prefs.keep_original_when_uncertain ? BST_CHECKED : BST_UNCHECKED);
    }
    if (GetDlgItem(m_wnd, IDC_MIN_CONFIDENCE)) {
        // 用 0-100 整数表示 0.0-1.0
        int conf = static_cast<int>(m_settings.prompt_prefs.min_translation_confidence * 100);
        SetDlgItemInt(m_wnd, IDC_MIN_CONFIDENCE, conf, FALSE);
    }
    if (GetDlgItem(m_wnd, IDC_PLATFORM_PRIORITY)) {
        SetDlgItemTextA(m_wnd, IDC_PLATFORM_PRIORITY, m_settings.prompt_prefs.translation_platform_priority.c_str());
    }
    if (GetDlgItem(m_wnd, IDC_CUSTOM_HINTS)) {
        SetDlgItemTextUTF8(m_wnd, IDC_CUSTOM_HINTS, m_settings.prompt_prefs.custom_translation_hints);
    }
    if (GetDlgItem(m_wnd, IDC_CUSTOM_INSTRUCTIONS)) {
        SetDlgItemTextUTF8(m_wnd, IDC_CUSTOM_INSTRUCTIONS, m_settings.prompt_prefs.custom_instructions);
    }
}




void AIPreferencePageInstance::save_settings() {
    char buffer[256];
    
    // V1: Provider 配置不经 settings.json，由 providers IPC 直接写 SQLite
    
    if (GetDlgItem(m_wnd, IDC_PYTHON_PATH)) {
        GetDlgItemTextA(m_wnd, IDC_PYTHON_PATH, buffer, sizeof(buffer));
        m_settings.python_path = buffer;
    }
    if (GetDlgItem(m_wnd, IDC_AUTO_INSTALL_PACKAGES)) {
        m_settings.auto_install_packages = IsDlgButtonChecked(m_wnd, IDC_AUTO_INSTALL_PACKAGES) == BST_CHECKED;
    }
    
    set_python_path(m_settings.python_path);
    set_auto_install_packages(m_settings.auto_install_packages);
    
    if (GetDlgItem(m_wnd, IDC_ENABLE_CACHE)) {
        m_settings.cache_enabled = IsDlgButtonChecked(m_wnd, IDC_ENABLE_CACHE) == BST_CHECKED;
    }
    if (GetDlgItem(m_wnd, IDC_CACHE_EXPIRATION)) {
        m_settings.cache_expiration_days = GetDlgItemInt(m_wnd, IDC_CACHE_EXPIRATION, NULL, FALSE);
    }
    if (GetDlgItem(m_wnd, IDC_CACHE_SIZE)) {
        m_settings.max_cache_size_mb = GetDlgItemInt(m_wnd, IDC_CACHE_SIZE, NULL, FALSE);
    }
    if (GetDlgItem(m_wnd, IDC_AUTO_CLEANUP)) {
        m_settings.auto_cleanup = IsDlgButtonChecked(m_wnd, IDC_AUTO_CLEANUP) == BST_CHECKED;
    }
    
    if (GetDlgItem(m_wnd, IDC_AUTO_RESTART)) {
        m_settings.auto_restart = IsDlgButtonChecked(m_wnd, IDC_AUTO_RESTART) == BST_CHECKED;
    }
    
    if (GetDlgItem(m_wnd, IDC_BATCH_SIZE_PREF)) {
        m_settings.ai_batch_size = GetDlgItemInt(m_wnd, IDC_BATCH_SIZE_PREF, NULL, FALSE);
    }
    if (GetDlgItem(m_wnd, IDC_TASKQUEUE_BATCH_SIZE)) {
        m_settings.taskqueue_batch_size = GetDlgItemInt(m_wnd, IDC_TASKQUEUE_BATCH_SIZE, NULL, FALSE);
    }
    if (GetDlgItem(m_wnd, IDC_CONCURRENCY_PREF)) {
        m_settings.concurrency = GetDlgItemInt(m_wnd, IDC_CONCURRENCY_PREF, NULL, FALSE);
    }
    if (GetDlgItem(m_wnd, IDC_SHOW_PROGRESS_PREF)) {
        m_settings.show_progress_dialog = IsDlgButtonChecked(m_wnd, IDC_SHOW_PROGRESS_PREF) == BST_CHECKED;
    }
    
    if (GetDlgItem(m_wnd, IDC_MB_TIMEOUT)) {
        m_settings.mb_timeout = GetDlgItemInt(m_wnd, IDC_MB_TIMEOUT, NULL, FALSE);
    }
    if (GetDlgItem(m_wnd, IDC_MB_RETRIES)) {
        m_settings.mb_retries = GetDlgItemInt(m_wnd, IDC_MB_RETRIES, NULL, FALSE);
    }
    if (GetDlgItem(m_wnd, IDC_MB_PAGE_SIZE)) {
        m_settings.mb_page_size = GetDlgItemInt(m_wnd, IDC_MB_PAGE_SIZE, NULL, FALSE);
    }
    if (GetDlgItem(m_wnd, IDC_MB_MAX_PAGES)) {
        m_settings.mb_max_pages = GetDlgItemInt(m_wnd, IDC_MB_MAX_PAGES, NULL, FALSE);
    }
    if (GetDlgItem(m_wnd, IDC_MB_SCORE_THRESHOLD)) {
        m_settings.mb_score_threshold = GetDlgItemInt(m_wnd, IDC_MB_SCORE_THRESHOLD, NULL, FALSE);
    }
    if (GetDlgItem(m_wnd, IDC_MB_SCORE_MARGIN)) {
        m_settings.mb_score_margin = GetDlgItemInt(m_wnd, IDC_MB_SCORE_MARGIN, NULL, FALSE);
    }
    if (GetDlgItem(m_wnd, IDC_MB_RATE_LIMIT)) {
        m_settings.mb_rate_limit = GetDlgItemInt(m_wnd, IDC_MB_RATE_LIMIT, NULL, FALSE);
    }
    
    if (GetDlgItem(m_wnd, IDC_ENABLE_MUSICBRAINZ)) {
        m_settings.enable_musicbrainz = IsDlgButtonChecked(m_wnd, IDC_ENABLE_MUSICBRAINZ) == BST_CHECKED;
    }
    if (GetDlgItem(m_wnd, IDC_ENABLE_DISCOGS)) {
        m_settings.enable_discogs = IsDlgButtonChecked(m_wnd, IDC_ENABLE_DISCOGS) == BST_CHECKED;
    }
    if (GetDlgItem(m_wnd, IDC_DISCOGS_KEY)) {
        char buffer[256] = {0};
        GetDlgItemTextA(m_wnd, IDC_DISCOGS_KEY, buffer, 256);
        m_settings.discogs_consumer_key = buffer;
    }
    if (GetDlgItem(m_wnd, IDC_DISCOGS_SECRET)) {
        char buffer[256] = {0};
        GetDlgItemTextA(m_wnd, IDC_DISCOGS_SECRET, buffer, 256);
        m_settings.discogs_consumer_secret = buffer;
    }
    if (GetDlgItem(m_wnd, IDC_ENABLE_AI)) {
        m_settings.enable_ai = IsDlgButtonChecked(m_wnd, IDC_ENABLE_AI) == BST_CHECKED;
    }
    
    HWND combo = GetDlgItem(m_wnd, IDC_LOG_LEVEL);
    if (combo) {
        m_settings.log_level = static_cast<ai_metadata::constants::LogLevel>(ComboBox_GetCurSel(combo));
        Logger::instance().set_log_level(m_settings.log_level);
    }
    if (GetDlgItem(m_wnd, IDC_LOG_SIZE)) {
        m_settings.max_log_file_size_mb = GetDlgItemInt(m_wnd, IDC_LOG_SIZE, NULL, FALSE);
    }

    // Prompt 偏好页面控件
    combo = GetDlgItem(m_wnd, IDC_TRANSLATION_STYLE);
    if (combo) {
        int sel = ComboBox_GetCurSel(combo);
        switch (sel) {
            case 1:  m_settings.prompt_prefs.translation_style = "literal";  break;
            case 2:  m_settings.prompt_prefs.translation_style = "semantic"; break;
            default: m_settings.prompt_prefs.translation_style = "official"; break;
        }
    }
    // Note: IDC_GENRE_LANGUAGE removed - genre is now sourced from MusicBrainz (Scrape).
    if (GetDlgItem(m_wnd, IDC_KEEP_ORIGINAL)) {
        m_settings.prompt_prefs.keep_original_when_uncertain = IsDlgButtonChecked(m_wnd, IDC_KEEP_ORIGINAL) == BST_CHECKED;
    }
    if (GetDlgItem(m_wnd, IDC_MIN_CONFIDENCE)) {
        int conf = GetDlgItemInt(m_wnd, IDC_MIN_CONFIDENCE, NULL, FALSE);
        m_settings.prompt_prefs.min_translation_confidence = conf / 100.0;
    }
    if (GetDlgItem(m_wnd, IDC_PLATFORM_PRIORITY)) {
        char buffer[512] = {0};
        GetDlgItemTextA(m_wnd, IDC_PLATFORM_PRIORITY, buffer, sizeof(buffer));
        m_settings.prompt_prefs.translation_platform_priority = buffer;
    }
    if (GetDlgItem(m_wnd, IDC_CUSTOM_HINTS)) {
        m_settings.prompt_prefs.custom_translation_hints = GetDlgItemTextUTF8(m_wnd, IDC_CUSTOM_HINTS);
    }
    if (GetDlgItem(m_wnd, IDC_CUSTOM_INSTRUCTIONS)) {
        m_settings.prompt_prefs.custom_instructions = GetDlgItemTextUTF8(m_wnd, IDC_CUSTOM_INSTRUCTIONS);
    }
}

void AIPreferencePageInstance::on_test_api() {
    if (m_test_in_progress) {
        return;
    }

    ensure_ai_core_ready();
    AICore* ai_core = get_ai_core_instance();
    if (!ai_core || !ai_core->is_initialized()) {
        SetDlgItemTextA(m_wnd, IDC_STATUS_TEXT, "[ERROR] AI Core not initialized");
        Feedback::error("AI Core not initialized",
                         "AI Core instance is null or failed to start.",
                         ErrorCategory::PythonWorker);
        return;
    }

    std::string pid = selected_provider_id();
    if (pid.empty()) {
        if (m_current_provider_id.empty()) {
            SetDlgItemTextA(m_wnd, IDC_STATUS_TEXT, "[ERROR] No provider selected");
            Feedback::warn("Please select a provider in the list first.", "AI Metadata");
            return;
        }
        pid = m_current_provider_id;
    }

    m_test_in_progress = true;
    EnableWindow(GetDlgItem(m_wnd, IDC_TEST_API_BTN), FALSE);
    SetDlgItemTextA(m_wnd, IDC_STATUS_TEXT, "Testing provider connection (timeout: 30s)...");

    nlohmann::json params;
    params["id"] = pid;
    std::string params_json = params.dump();
    HWND wnd = m_wnd;

    std::thread([wnd, ai_core, params_json, pid, this]() {
        std::string result_json = ai_core->providers_test(params_json, 30000);
        fb2k::inMainThread([wnd, result_json, pid, this]() {
            if (!IsWindow(wnd)) {
                m_test_in_progress = false;
                return;
            }
            m_test_in_progress = false;
            EnableWindow(GetDlgItem(wnd, IDC_TEST_API_BTN), TRUE);
            try {
                // 契约: { success, status("success"|"failed"), data{}, error(""), error_category("") }
                nlohmann::json result = nlohmann::json::parse(result_json);
                const std::string status = result.value("status", result.value("success", false) ? "success" : "failed");
                const bool ok = (status == "success") || result.value("success", false);
                const auto& data = result.contains("data") && result["data"].is_object()
                    ? result["data"]
                    : nlohmann::json::object();
                const std::string model = data.value("model", "");
                const std::string provider_name = data.value("provider", "");
                const std::string err = result.value("error", "");
                const std::string cat = result.value("error_category", "");

                if (ok) {
                    std::string msg = "API connection successful!";
                    if (!provider_name.empty()) msg += "\nProvider: " + provider_name;
                    if (!model.empty()) msg += "\nModel: " + model;
                    SetDlgItemTextA(wnd, IDC_STATUS_TEXT, "API test: SUCCESS");
                    Feedback::success(msg);
                } else {
                    std::string detail = err.empty() ? "Unknown error" : err;
                    if (!cat.empty()) detail += " [" + cat + "]";
                    SetDlgItemTextA(wnd, IDC_STATUS_TEXT, "API test: FAILED");
                    Feedback::warn(std::string("API connection failed!\n") + detail, "API Test Failed");
                }
            } catch (const std::exception& e) {
                SetDlgItemTextA(wnd, IDC_STATUS_TEXT, "API test: ERROR");
                Feedback::warn(std::string("Failed to parse test result: ") + e.what(), "API Test Error");
            }
        });
    }).detach();
}

void AIPreferencePageInstance::on_open_log_folder() {
    // 复用 Logger 统一确定的日志路径（components/foo_metadata_enhancer/logs/core.log）
    std::string log_file = Logger::instance().get_log_file_path();
    if (log_file.empty()) {
        SetDlgItemTextA(m_wnd, IDC_WORKER_STATUS_TEXT, "Cannot determine log file path");
        return;
    }

    // Use explorer.exe /select to reliably open Explorer and select the log file
    int len = MultiByteToWideChar(CP_UTF8, 0, log_file.c_str(), -1, NULL, 0);
    std::wstring wlog_file(len, 0);
    MultiByteToWideChar(CP_UTF8, 0, log_file.c_str(), -1, &wlog_file[0], len);

    std::wstring params = L"/select,\"" + wlog_file + L"\"";
    HINSTANCE result = ShellExecuteW(NULL, NULL, L"explorer.exe", params.c_str(), NULL, SW_SHOWNORMAL);
    
    if ((INT_PTR)result <= 32) {
        // Fallback: try opening the folder directly
        std::string log_dir;
        size_t slash = log_file.find_last_of("\\/");
        if (slash != std::string::npos) log_dir = log_file.substr(0, slash);
        if (!log_dir.empty()) {
            len = MultiByteToWideChar(CP_UTF8, 0, log_dir.c_str(), -1, NULL, 0);
            std::wstring wlog_dir(len, 0);
            MultiByteToWideChar(CP_UTF8, 0, log_dir.c_str(), -1, &wlog_dir[0], len);
            ShellExecuteW(NULL, L"open", wlog_dir.c_str(), NULL, NULL, SW_SHOWNORMAL);
        }
    }
}

void AIPreferencePageInstance::on_clear_cache() {
    int result = MessageBoxW(
        m_wnd,
        L"Are you sure you want to clear all cached AI results?",
        L"Clear Cache",
        MB_YESNO | MB_ICONQUESTION
    );

    if (result == IDYES) {
        AICore* ai_core = get_ai_core_instance();
        if (!ai_core) {
            SetDlgItemTextA(m_wnd, IDC_STATUS_TEXT, "[ERROR] AI Core not initialized");
            Feedback::error("AI Core not initialized",
                             "AI Core instance is null. This usually indicates the plugin failed to start.",
                             ErrorCategory::PythonWorker);
            return;
        }
        ai_core->clear_cache();
        SetDlgItemTextA(m_wnd, IDC_STATUS_TEXT, "Cache cleared successfully");
        Feedback::success("Cache cleared successfully");
    }
}

void AIPreferencePageInstance::on_restart_workers() {
    Logger::instance().info("on_restart_workers: ENTER", __FILE__, __FUNCTION__);
    AICore* ai_core = get_ai_core_instance();
    if (!ai_core) {
        Logger::instance().error("on_restart_workers: ai_core is null", __FILE__, __FUNCTION__);
        SetDlgItemTextA(m_wnd, IDC_WORKER_STATUS_TEXT, "Core not initialized");
        return;
    }
    Logger::instance().info("on_restart_workers: ai_core=" + std::to_string((intptr_t)ai_core) +
        ", is_initialized=" + std::string(ai_core->is_initialized() ? "true" : "false"), __FILE__, __FUNCTION__);

    // AICore 是懒加载。未初始化时 worker_manager_ 为空，restart_all_workers() 会返回 false。
    // 用户可能刚配置了 Python 路径，主动尝试 initialize（破除"先跑 scrape 才能 init"的死循环）。
    if (!ai_core->is_initialized()) {
        Logger::instance().info("on_restart_workers: AICore not initialized, calling ensure_ai_core_ready", __FILE__, __FUNCTION__);
        SetDlgItemTextA(m_wnd, IDC_WORKER_STATUS_TEXT, "Starting...");
        ensure_ai_core_ready();
        if (!ai_core->is_initialized()) {
            Logger::instance().error("on_restart_workers: ensure_ai_core_ready failed", __FILE__, __FUNCTION__);
            SetDlgItemTextA(m_wnd, IDC_WORKER_STATUS_TEXT, "Status: Failed (check Python path)");
            Feedback::warn("Failed to start worker. Please check Python path in settings and retry.",
                           "AI Metadata");
            return;
        }
        // 初始化成功，刷新 provider 列表
        SetDlgItemTextA(m_wnd, IDC_WORKER_STATUS_TEXT, "Status: Running");
        refresh_provider_list();
        return;
    }

    SetDlgItemTextA(m_wnd, IDC_WORKER_STATUS_TEXT, "Restarting...");

    bool success = ai_core->restart_all_workers();
    Logger::instance().info("on_restart_workers: restart_all_workers returned " + std::string(success ? "true" : "false"), __FILE__, __FUNCTION__);

    if (success) {
        // Check worker health after restart (restart is synchronous: stop + start)
        bool healthy = ai_core->is_worker_healthy();
        Logger::instance().info("on_restart_workers: is_worker_healthy=" + std::string(healthy ? "true" : "false"), __FILE__, __FUNCTION__);
        if (healthy) {
            SetDlgItemTextA(m_wnd, IDC_WORKER_STATUS_TEXT, "Status: Running");
        } else {
            SetDlgItemTextA(m_wnd, IDC_WORKER_STATUS_TEXT, "Status: Starting...");
        }
    } else {
        SetDlgItemTextA(m_wnd, IDC_WORKER_STATUS_TEXT, "Status: Failed");
    }
}

void AIPreferencePageInstance::on_browse_python() {
    char buffer[MAX_PATH] = {0};

    OPENFILENAMEA ofn = {0};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = m_wnd;
    ofn.lpstrFilter = "Python Executable\0python.exe\0All Files\0*.*\0";
    ofn.lpstrFile = buffer;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = "Select Python Executable";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

    if (GetOpenFileNameA(&ofn)) {
        SetDlgItemTextA(m_wnd, IDC_PYTHON_PATH, buffer);
        m_settings.python_path = buffer;
        on_changed();

        std::string status_text = "Status: " + std::string(buffer);
        SetDlgItemTextA(m_wnd, IDC_PYTHON_STATUS, status_text.c_str());
    }
}

void AIPreferencePageInstance::on_detect_python() {
    SetDlgItemTextA(m_wnd, IDC_PYTHON_STATUS, "Detecting...");

    // 强制重新检测（不走 get_python_path 缓存，直接调 auto_detect）
    std::string detected = SettingsManager::instance().auto_detect_python_path();

    if (detected.empty()) {
        SetDlgItemTextA(m_wnd, IDC_PYTHON_STATUS, "Status: Python not found");
        Feedback::warn(
            "Python was not found on this system.\n\n"
            "Please install Python 3.8+ or manually specify the python.exe path using Browse.",
            "AI Metadata");
        return;
    }

    SetDlgItemTextA(m_wnd, IDC_PYTHON_PATH, detected.c_str());
    m_settings.python_path = detected;
    on_changed();

    std::string status_text = "Status: " + detected;
    SetDlgItemTextA(m_wnd, IDC_PYTHON_STATUS, status_text.c_str());
}

void AIPreferencePageInstance::on_changed() {
    if (!m_modified) {
        m_modified = true;
        m_callback->on_state_changed();
    }
}

// ============================================================================
// Custom Model Configuration Dialog
// ============================================================================

/**
 * @brief 自定义模型配置对话框的数据结构
 */

void AIPreferencePageInstance::on_export_templates() {
    // 目标目录：<profile>/foo_metadata_enhancer/prompts/
    std::string profile_path = core_api::get_profile_path();
    if (profile_path.find("file://") == 0) {
        profile_path = profile_path.substr(7);
    }
    std::string prompts_dir = profile_path + "\\foo_metadata_enhancer\\prompts";
    CreateDirectoryA(prompts_dir.c_str(), NULL);

    // 源目录：<dll_dir>/foo_metadata_enhancer/worker/prompts/templates/
    // 或 <dll_dir>/../foo_metadata_enhancer/worker/prompts/templates/
    std::string dll_dir = get_dll_directory();
    std::vector<std::string> src_candidates = {
        dll_dir + "\\foo_metadata_enhancer\\worker\\prompts\\templates",
        dll_dir + "\\..\\foo_metadata_enhancer\\worker\\prompts\\templates",
        dll_dir + "\\worker\\prompts\\templates"
    };

    std::string src_dir;
    for (const auto& candidate : src_candidates) {
        DWORD attr = GetFileAttributesA(candidate.c_str());
        if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) {
            src_dir = candidate;
            break;
        }
    }

    if (src_dir.empty()) {
        MessageBoxA(m_wnd,
            "Could not find templates directory.\n"
            "Expected: worker/prompts/templates/",
            "Export Failed",
            MB_OK | MB_ICONERROR);
        return;
    }

    const char* filenames[] = {
        "genre_categories.md",
        "edition_types.md",
        "source_priority.md",
        "translation_platforms.md"
    };

    int copied = 0;
    int skipped = 0;
    for (const char* fname : filenames) {
        std::string src = src_dir + "\\" + fname;
        std::string dst = prompts_dir + "\\" + fname;

        if (GetFileAttributesA(dst.c_str()) != INVALID_FILE_ATTRIBUTES) {
            skipped++;
            continue;
        }

        if (CopyFileA(src.c_str(), dst.c_str(), TRUE)) {
            copied++;
        } else {
            Logger::instance().warning(std::string("on_export_templates: Failed to copy ") + fname);
        }
    }

    std::string msg;
    if (copied > 0) {
        msg = "Exported " + std::to_string(copied) + " template file(s) to:\n" + prompts_dir;
        if (skipped > 0) {
            msg += "\n\n" + std::to_string(skipped) + " file(s) skipped (already exist).";
        }
        msg += "\n\nEdit those .md files to customize domain knowledge.\n"
               "Changes take effect on next AI call (hot reload).";
        MessageBoxA(m_wnd, msg.c_str(), "Export Successful", MB_OK | MB_ICONINFORMATION);

        // 打开 prompts 目录
        int len = MultiByteToWideChar(CP_UTF8, 0, prompts_dir.c_str(), -1, NULL, 0);
        std::wstring wprompts(len, 0);
        MultiByteToWideChar(CP_UTF8, 0, prompts_dir.c_str(), -1, &wprompts[0], len);
        ShellExecuteW(NULL, L"open", wprompts.c_str(), NULL, NULL, SW_SHOWNORMAL);
    } else if (skipped > 0) {
        msg = "All template files already exist in:\n" + prompts_dir +
              "\n\nEdit them directly to customize.";
        MessageBoxA(m_wnd, msg.c_str(), "Nothing to Export", MB_OK | MB_ICONINFORMATION);
    } else {
        MessageBoxA(m_wnd, "Failed to export templates. Check logs.", "Export Failed", MB_OK | MB_ICONERROR);
    }
}


// ========================= Providers V1 UI =========================

void AIPreferencePageInstance::ensure_ai_core_ready() {
    AICore* ai_core = get_ai_core_instance();
    if (!ai_core) return;
    if (ai_core->is_initialized()) return;

    std::string profile_path = core_api::get_profile_path();
    if (profile_path.find("file://") == 0) {
        profile_path = profile_path.substr(7);
    }
    std::string sub_dir = profile_path + "\\foo_metadata_enhancer";
    CreateDirectoryA(sub_dir.c_str(), NULL);
    ai_core->set_cache_path(sub_dir + "\\" + constants::cache_db_name());
    if (!ai_core->initialize()) {
        Logger::instance().error("ensure_ai_core_ready: initialize failed", __FILE__, __FUNCTION__);
    }
}

std::string AIPreferencePageInstance::selected_provider_id() const {
    HWND list = GetDlgItem(m_wnd, IDC_PROVIDER_LIST);
    if (!list) return {};
    int sel = ListView_GetNextItem(list, -1, LVNI_SELECTED);
    if (sel < 0 || sel >= (int)m_provider_ids.size()) return {};
    return m_provider_ids[sel];
}

void AIPreferencePageInstance::refresh_provider_list() {
    HWND list = GetDlgItem(m_wnd, IDC_PROVIDER_LIST);
    if (!list) {
        LOG_ERROR("refresh_provider_list: IDC_PROVIDER_LIST not found");
        return;
    }
    LOG_INFO("refresh_provider_list: start, m_wnd=" + std::to_string(reinterpret_cast<uintptr_t>(m_wnd)));

    if (ListView_GetColumnWidth(list, 0) <= 0) {
        ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
        // UNICODE 构建下 SysListView32 内部为 W 版本，直接使用 LVCOLUMNW + LVM_INSERTCOLUMNW
        // 避免 A 版本消息在某些 Windows 版本上转换失败导致列不可见。
        LVCOLUMNW col{};
        col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
        col.pszText = const_cast<wchar_t*>(L"Name");
        col.cx = 90;
        col.iSubItem = 0;
        SendMessageW(list, LVM_INSERTCOLUMNW, 0, reinterpret_cast<LPARAM>(&col));
        col.pszText = const_cast<wchar_t*>(L"Protocol");
        col.cx = 70;
        col.iSubItem = 1;
        SendMessageW(list, LVM_INSERTCOLUMNW, 1, reinterpret_cast<LPARAM>(&col));
        col.pszText = const_cast<wchar_t*>(L"Model");
        col.cx = 70;
        col.iSubItem = 2;
        SendMessageW(list, LVM_INSERTCOLUMNW, 2, reinterpret_cast<LPARAM>(&col));
        Logger::instance().info("refresh_provider_list: columns inserted");
    }

    // 不在此处调用 ensure_ai_core_ready()。
    // 原因：refresh_provider_list 在 on_init_dialog 时被调用，若强制 initialize() 会同步阻塞
    // fb2k 主线程去 CreateProcess，worker 启动失败时还会反复重试，导致 fb2k 卡死。
    // 改为只检查 is_initialized()，未初始化时显示提示，由用户主动点击 'Restart Workers' 启动。
    AICore* ai_core = get_ai_core_instance();
    ListView_DeleteAllItems(list);
    m_provider_ids.clear();

    if (!ai_core || !ai_core->is_initialized()) {
        SetDlgItemTextA(m_wnd, IDC_PROVIDER_CURRENT_TEXT, "(worker not ready)");
        SetDlgItemTextA(m_wnd, IDC_STATUS_TEXT, "Worker not ready. Click 'Restart Workers' to start.");
        LOG_WARN("refresh_provider_list: ai_core not ready");
        return;
    }

    std::string resp = ai_core->providers_list(false, 15000);
    LOG_INFO("refresh_provider_list: resp preview (first 200 bytes): " +
             (resp.size() > 200 ? resp.substr(0, 200) : resp));
    try {
        auto j = nlohmann::json::parse(resp);
        const bool ok = j.value("success", false) || j.value("status", "") == "success";
        if (!ok) {
            std::string err = j.value("error", "list failed");
            SetDlgItemTextA(m_wnd, IDC_STATUS_TEXT, ("Providers list failed: " + err).c_str());
            LOG_ERROR("refresh_provider_list: list failed, err=" + err);
            return;
        }
        auto data = j.value("data", nlohmann::json::object());
        m_current_provider_id = data.value("current_provider_id", "");
        auto providers = data.value("providers", nlohmann::json::array());
        int row = 0;
        std::string current_label = "(none)";
        for (auto& p : providers) {
            std::string id = p.value("id", "");
            std::string name = p.value("name", "");
            std::string protocol = p.value("protocol", "");
            std::string model = p.value("model", "");
            m_provider_ids.push_back(id);
            LOG_INFO(std::string("refresh_provider_list: row=") + std::to_string(row) +
                     " name=" + name + " protocol=" + protocol + " model=" + model);

            // UNICODE 构建下直接使用 W 版本 API + wchar_t 字符串，
            // 避免 A 版本消息转换失败导致行/subitem 不可见。
            std::wstring wname = utf8_to_wstring(name);
            std::wstring wprotocol = utf8_to_wstring(protocol);
            std::wstring wmodel = utf8_to_wstring(model);

            LVITEMW item{};
            item.mask = LVIF_TEXT;
            item.iItem = row;
            item.iSubItem = 0;
            item.pszText = const_cast<wchar_t*>(wname.c_str());
            int inserted = static_cast<int>(SendMessageW(list, LVM_INSERTITEMW, 0, reinterpret_cast<LPARAM>(&item)));
            if (inserted < 0) {
                LOG_ERROR("refresh_provider_list: LVM_INSERTITEMW failed for row=" + std::to_string(row));
                continue;
            }
            row = inserted;

            LVITEMW sub{};
            sub.mask = LVIF_TEXT;
            sub.iItem = row;
            sub.iSubItem = 1;
            sub.pszText = const_cast<wchar_t*>(wprotocol.c_str());
            SendMessageW(list, LVM_SETITEMTEXTW, static_cast<WPARAM>(row), reinterpret_cast<LPARAM>(&sub));
            sub.iSubItem = 2;
            sub.pszText = const_cast<wchar_t*>(wmodel.c_str());
            SendMessageW(list, LVM_SETITEMTEXTW, static_cast<WPARAM>(row), reinterpret_cast<LPARAM>(&sub));

            if (!m_current_provider_id.empty() && id == m_current_provider_id) {
                current_label = name + " / " + model;
                ListView_SetItemState(list, row, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
            }
            row++;
        }
        SetDlgItemTextA(m_wnd, IDC_PROVIDER_CURRENT_TEXT, current_label.c_str());
        char status[128];
        std::snprintf(status, sizeof(status), "Providers: %d", row);
        SetDlgItemTextA(m_wnd, IDC_STATUS_TEXT, status);
        int actual_count = ListView_GetItemCount(list);
        BOOL visible = IsWindowVisible(list);
        RECT rc{};
        GetWindowRect(list, &rc);
        HWND parent_wnd = GetParent(list);
        BOOL parent_visible = parent_wnd ? IsWindowVisible(parent_wnd) : FALSE;
        BOOL wnd_visible = m_wnd ? IsWindowVisible(m_wnd) : FALSE;
        LOG_INFO(std::string("refresh_provider_list: done, rows=") + std::to_string(row) +
                 " actual_item_count=" + std::to_string(actual_count) +
                 " list_visible=" + (visible ? "1" : "0") +
                 " parent_visible=" + (parent_visible ? "1" : "0") +
                 " mwnd_visible=" + (wnd_visible ? "1" : "0") +
                 " rect=(" + std::to_string(rc.left) + "," + std::to_string(rc.top) +
                 "," + std::to_string(rc.right) + "," + std::to_string(rc.bottom) + ")");
        // 防御性：如果 ListView 不可见但父窗口可见，显式显示 ListView
        if (!visible && parent_visible) {
            ShowWindow(list, SW_SHOW);
            LOG_INFO("refresh_provider_list: explicitly showed ListView (was invisible but parent visible)");
        }
    } catch (const std::exception& e) {
        SetDlgItemTextA(m_wnd, IDC_STATUS_TEXT, (std::string("Providers list parse error: ") + e.what()).c_str());
        LOG_ERROR(std::string("refresh_provider_list parse error: ") + e.what());
    }
}

struct ProviderEditDlgData {
    std::string* name;
    std::string* protocol;
    std::string* base_url;
    std::string* api_key;
    std::string* model;
    bool is_new;
};

static INT_PTR CALLBACK ProviderEditDlgProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) {
    ProviderEditDlgData* data = reinterpret_cast<ProviderEditDlgData*>(GetWindowLongPtr(wnd, GWLP_USERDATA));
    switch (msg) {
    case WM_INITDIALOG: {
        data = reinterpret_cast<ProviderEditDlgData*>(lp);
        if (!data) {
            EndDialog(wnd, IDCANCEL);
            return TRUE;
        }
        SetWindowLongPtr(wnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(data));
        // UNICODE 构建下使用宽字符标题，避免 A/W 混用崩溃
        SetWindowTextW(wnd, data->is_new ? L"Add Provider" : L"Edit Provider");

        HWND combo = GetDlgItem(wnd, IDC_PROV_EDIT_PROTOCOL);
        if (combo) {
            // 注意：CB_ADDSTRING 在某些环境下被 hook 拦截（返回 0 但实际未添加），
            // 改用 CB_INSERTSTRING（wParam=索引，lParam=字符串），经测试可正常工作。
            SendMessageW(combo, CB_RESETCONTENT, 0, 0);
            SendMessageW(combo, CB_INSERTSTRING, 0, reinterpret_cast<LPARAM>(L"openai_chat"));
            SendMessageW(combo, CB_INSERTSTRING, 1, reinterpret_cast<LPARAM>(L"anthropic_messages"));
            int sel = 0;
            if (data->protocol && *data->protocol == "anthropic_messages") sel = 1;
            SendMessageW(combo, CB_SETCURSEL, sel, 0);
            int count = static_cast<int>(SendMessageW(combo, CB_GETCOUNT, 0, 0));
            LOG_INFO(std::string("ProviderEditDlg: protocol combo filled, count=") + std::to_string(count));
        } else {
            LOG_ERROR("ProviderEditDlg WM_INITDIALOG: IDC_PROV_EDIT_PROTOCOL not found");
        }
        if (data->name) SetDlgItemTextA(wnd, IDC_PROV_EDIT_NAME, data->name->c_str());
        if (data->base_url) SetDlgItemTextA(wnd, IDC_PROV_EDIT_URL, data->base_url->c_str());
        if (data->api_key) SetDlgItemTextA(wnd, IDC_PROV_EDIT_KEY, data->api_key->c_str());
        if (data->model) SetDlgItemTextA(wnd, IDC_PROV_EDIT_MODEL, data->model->c_str());
        return TRUE;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == IDOK) {
            if (!data) {
                EndDialog(wnd, IDCANCEL);
                return TRUE;
            }
            char buf[1024];
            GetDlgItemTextA(wnd, IDC_PROV_EDIT_NAME, buf, sizeof(buf));
            if (data->name) *data->name = buf;
            GetDlgItemTextA(wnd, IDC_PROV_EDIT_URL, buf, sizeof(buf));
            if (data->base_url) *data->base_url = buf;
            GetDlgItemTextA(wnd, IDC_PROV_EDIT_KEY, buf, sizeof(buf));
            if (data->api_key) *data->api_key = buf;
            GetDlgItemTextA(wnd, IDC_PROV_EDIT_MODEL, buf, sizeof(buf));
            if (data->model) *data->model = buf;
            HWND combo = GetDlgItem(wnd, IDC_PROV_EDIT_PROTOCOL);
            int sel = combo ? static_cast<int>(SendMessageW(combo, CB_GETCURSEL, 0, 0)) : 0;
            if (data->protocol) {
                *data->protocol = (sel == 1) ? "anthropic_messages" : "openai_chat";
            }
            if (data->name && data->name->empty()) {
                MessageBoxW(wnd, L"Name is required.", L"Provider", MB_OK | MB_ICONWARNING);
                return TRUE;
            }
            EndDialog(wnd, IDOK);
            return TRUE;
        }
        if (LOWORD(wp) == IDCANCEL) {
            EndDialog(wnd, IDCANCEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

bool AIPreferencePageInstance::show_provider_edit_dialog(
    std::string& name,
    std::string& protocol,
    std::string& base_url,
    std::string& api_key,
    std::string& model,
    bool is_new
) {
    ProviderEditDlgData data{&name, &protocol, &base_url, &api_key, &model, is_new};
    HINSTANCE inst = core_api::get_my_instance();
    // 与项目其它对话框一致：UNICODE 下走 DialogBoxParamW
    // 父窗口优先主窗口，避免 Preferences 子页句柄导致模态对话框异常
    HWND parent = core_api::get_main_window();
    if (!parent || !IsWindow(parent)) parent = m_wnd;

    SetLastError(ERROR_SUCCESS);
    INT_PTR ret = DialogBoxParam(
        inst,
        MAKEINTRESOURCE(IDD_PROVIDER_EDIT),
        parent,
        ProviderEditDlgProc,
        reinterpret_cast<LPARAM>(&data)
    );
    if (ret == -1) {
        DWORD err = GetLastError();
        LOG_ERROR(std::string("show_provider_edit_dialog: DialogBoxParam failed, GetLastError=") +
                  std::to_string(err));
        Feedback::warn(
            std::string("Failed to open provider editor (error ") + std::to_string(err) + ").",
            "Provider"
        );
        return false;
    }
    return ret == IDOK;
}

void AIPreferencePageInstance::on_provider_add() {
    ensure_ai_core_ready();
    AICore* ai_core = get_ai_core_instance();
    if (!ai_core || !ai_core->is_initialized()) {
        Feedback::warn(
            "Worker is not running. Provider configuration is stored on the worker side, "
            "so the worker must be started first.\n\n"
            "Please click 'Restart Workers' button to start the worker, "
            "then retry adding a provider.",
            "AI Metadata");
        return;
    }
    std::string name = "New Provider";
    std::string protocol = "openai_chat";
    std::string base_url;
    std::string api_key;
    std::string model;
    if (!show_provider_edit_dialog(name, protocol, base_url, api_key, model, true)) return;

    try {
        nlohmann::json body;
        body["name"] = name;
        body["protocol"] = protocol.empty() ? "openai_chat" : protocol;
        body["base_url"] = base_url;
        body["api_key"] = api_key;
        body["model"] = model;
        body["enabled"] = true;
        body["is_preset"] = false;

        LOG_INFO(std::string("on_provider_add: creating name=") + name +
                 " protocol=" + protocol + " model=" + model);
        auto resp = nlohmann::json::parse(ai_core->providers_create(body.dump(), 15000), nullptr, false);
        const bool ok = resp.is_object() &&
            (resp.value("success", false) || resp.value("status", "") == "success");
        if (!ok) {
            std::string err = resp.is_object() ? resp.value("error", "create failed") : "create failed";
            Feedback::warn(err, "Add Provider");
            return;
        }
        refresh_provider_list();
        SetDlgItemTextA(m_wnd, IDC_STATUS_TEXT, "Provider created");
    } catch (const std::exception& e) {
        LOG_ERROR(std::string("on_provider_add exception: ") + e.what());
        Feedback::warn(std::string("Add provider failed: ") + e.what(), "Add Provider");
    }
}

void AIPreferencePageInstance::on_provider_edit() {
    ensure_ai_core_ready();
    AICore* ai_core = get_ai_core_instance();
    if (!ai_core || !ai_core->is_initialized()) {
        Feedback::warn("Worker not ready.", "AI Metadata");
        return;
    }
    std::string pid = selected_provider_id();
    if (pid.empty()) {
        Feedback::warn("Select a provider first.", "AI Metadata");
        return;
    }
    auto get_resp = nlohmann::json::parse(ai_core->providers_get(pid, true, 15000), nullptr, false);
    if (!get_resp.is_object() ||
        (!get_resp.value("success", false) && get_resp.value("status", "") != "success")) {
        Feedback::warn("Failed to load provider.", "Edit Provider");
        return;
    }
    auto prov = get_resp.value("data", nlohmann::json::object()).value("provider", nlohmann::json::object());
    std::string name = prov.value("name", "");
    std::string protocol = prov.value("protocol", "openai_chat");
    std::string base_url = prov.value("base_url", "");
    std::string api_key = prov.value("api_key", "");
    std::string model = prov.value("model", "");
    if (!show_provider_edit_dialog(name, protocol, base_url, api_key, model, false)) return;

    nlohmann::json body;
    body["id"] = pid;
    body["name"] = name;
    body["protocol"] = protocol;
    body["base_url"] = base_url;
    body["api_key"] = api_key;
    body["model"] = model;
    auto resp = nlohmann::json::parse(ai_core->providers_update(body.dump(), 15000), nullptr, false);
    const bool ok = resp.is_object() &&
        (resp.value("success", false) || resp.value("status", "") == "success");
    if (!ok) {
        std::string err = resp.is_object() ? resp.value("error", "update failed") : "update failed";
        Feedback::warn(err, "Edit Provider");
        return;
    }
    refresh_provider_list();
    SetDlgItemTextA(m_wnd, IDC_STATUS_TEXT, "Provider updated");
}

void AIPreferencePageInstance::on_provider_delete() {
    ensure_ai_core_ready();
    AICore* ai_core = get_ai_core_instance();
    if (!ai_core || !ai_core->is_initialized()) return;
    std::string pid = selected_provider_id();
    if (pid.empty()) {
        Feedback::warn("Select a provider first.", "AI Metadata");
        return;
    }
    if (MessageBoxA(m_wnd, "Delete selected provider?", "Delete Provider", MB_YESNO | MB_ICONQUESTION) != IDYES) {
        return;
    }
    auto resp = nlohmann::json::parse(ai_core->providers_delete(pid, 15000), nullptr, false);
    if (!resp.is_object() || !resp.value("success", false)) {
        std::string err = resp.is_object() ? resp.value("error", "delete failed") : "delete failed";
        Feedback::warn(err, "Delete Provider");
        return;
    }
    refresh_provider_list();
    SetDlgItemTextA(m_wnd, IDC_STATUS_TEXT, "Provider deleted");
}

void AIPreferencePageInstance::on_provider_set_current() {
    ensure_ai_core_ready();
    AICore* ai_core = get_ai_core_instance();
    if (!ai_core || !ai_core->is_initialized()) return;
    std::string pid = selected_provider_id();
    if (pid.empty()) {
        Feedback::warn("Select a provider first.", "AI Metadata");
        return;
    }
    auto resp = nlohmann::json::parse(ai_core->providers_set_current(pid, 15000), nullptr, false);
    if (!resp.is_object() || !resp.value("success", false)) {
        std::string err = resp.is_object() ? resp.value("error", "set_current failed") : "set_current failed";
        Feedback::warn(err, "Set Current");
        return;
    }
    refresh_provider_list();
    SetDlgItemTextA(m_wnd, IDC_STATUS_TEXT, "Current provider updated");
}

void AIPreferencePageInstance::on_provider_restore_presets() {
    ensure_ai_core_ready();
    AICore* ai_core = get_ai_core_instance();
    if (!ai_core || !ai_core->is_initialized()) return;
    if (MessageBoxA(m_wnd,
                    "Restore seed presets (Zhipu/DeepSeek/OpenRouter)?\nExisting names are kept unless overwritten.",
                    "Restore Presets",
                    MB_YESNO | MB_ICONQUESTION) != IDYES) {
        return;
    }
    auto resp = nlohmann::json::parse(ai_core->providers_restore_presets(false, 15000), nullptr, false);
    if (!resp.is_object() || !resp.value("success", false)) {
        std::string err = resp.is_object() ? resp.value("error", "restore failed") : "restore failed";
        Feedback::warn(err, "Restore Presets");
        return;
    }
    refresh_provider_list();
    SetDlgItemTextA(m_wnd, IDC_STATUS_TEXT, "Presets restored");
}

static preferences_page_factory_t<AIPreferencePageRoot> g_preferences_page_root_factory;
static preferences_page_factory_t<AIPreferencePageGeneral> g_preferences_page_general_factory;
static preferences_page_factory_t<AIPreferencePageDataSources> g_preferences_page_data_sources_factory;
static preferences_page_factory_t<AIPreferencePageProcessing> g_preferences_page_processing_factory;
static preferences_page_factory_t<AIPreferencePageCacheLogs> g_preferences_page_cache_logs_factory;
static preferences_page_factory_t<AIPreferencePagePrompts> g_preferences_page_prompts_factory;

}
