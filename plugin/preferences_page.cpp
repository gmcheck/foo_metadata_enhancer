#include "preferences_page.h"
#include "menu_handler.h"
#include "resource.h"
#include "../core/logger.h"
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
#include <nlohmann/json.hpp>

namespace ai_metadata {

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

const GUID AIPreferencePageRoot::g_guid = guid_preferences_root;
const GUID AIPreferencePageGeneral::g_guid = guid_preferences_general;
const GUID AIPreferencePageDataSources::g_guid = guid_preferences_data_sources;
const GUID AIPreferencePageAdvanced::g_guid = guid_preferences_advanced;

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
    console::print("[AI Metadata] [preferences_page.cpp::do_ensure_initialized] Starting initialization");
    
    m_config_path = core_api::get_profile_path();
    
    if (m_config_path.find("file://") == 0) {
        m_config_path = m_config_path.substr(7);
    }
    
    m_config_path += "\\foo_ai_metadata";
    CreateDirectoryA(m_config_path.c_str(), NULL);
    m_config_path += "\\settings.json";
    
    log_format("[AI Metadata] [preferences_page.cpp::do_ensure_initialized] Config path = ", m_config_path.c_str());
    
    log_format("[AI Metadata] [preferences_page.cpp::do_ensure_initialized] Calling load_from_config_yaml()");
    load_from_config_yaml();
    
    log_format("[AI Metadata] [preferences_page.cpp::do_ensure_initialized] Calling load()");
    load();
    
    log_format("[AI Metadata] [preferences_page.cpp::do_ensure_initialized] Setting auto_install_packages = ", m_settings.auto_install_packages ? "true" : "false");
    set_auto_install_packages(m_settings.auto_install_packages);
    
    log_format("[AI Metadata] [preferences_page.cpp::do_ensure_initialized] Calling save()");
    save();
    
    console::print("[AI Metadata] [preferences_page.cpp::do_ensure_initialized] Initialization complete");
    log_format("[AI Metadata] [preferences_page.cpp::do_ensure_initialized] Provider = ", static_cast<int>(m_settings.provider));
    
    for (const auto& [p, config] : m_settings.provider_configs) {
        log_format("[AI Metadata] [preferences_page.cpp::do_ensure_initialized] Provider ", static_cast<int>(p), " has ", static_cast<int>(config.models.size()), " models, selected_model = ", config.selected_model.c_str());
    }
    
    m_initialized = true;
}

std::string SettingsManager::get_config_path() const {
    return m_config_path;
}

std::string SettingsManager::get_api_key() const {
    const auto& s = settings();
    if (s.use_env_key) {
        const char* env_key = std::getenv("OPENROUTER_API_KEY");
        if (env_key) return env_key;
    }
    auto it = s.provider_configs.find(s.provider);
    if (it != s.provider_configs.end()) {
        return it->second.api_key;
    }
    return "";
}

std::vector<ModelInfo> SettingsManager::get_models_for_provider(AIProvider provider) const {
    const auto& s = settings();
    auto it = s.provider_configs.find(provider);
    if (it != s.provider_configs.end()) {
        return it->second.models;
    }
    return {};
}

static std::string trim_string(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\"");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\"");
    return s.substr(start, end - start + 1);
}

static std::string get_dll_directory() {
    char dll_path[MAX_PATH] = {0};
    HMODULE hModule = GetModuleHandleA("foo_ai_metadata.dll");
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

void SettingsManager::load_from_config_yaml() {
    console::print("[AI Metadata] load_from_config_yaml: Starting");
    
    std::string yaml_path;
    
    std::ifstream file;
    
    std::string dll_dir = get_dll_directory();
    log_format("[AI Metadata] load_from_config_yaml: DLL dir = ", dll_dir.c_str());
    
    yaml_path = dll_dir + "\\foo_ai_metadata\\worker\\config.yaml";
    log_format("[AI Metadata] load_from_config_yaml: Trying path 1 = ", yaml_path.c_str());
    file.open(yaml_path);
    
    if (!file.is_open()) {
        yaml_path = dll_dir + "\\..\\foo_ai_metadata\\worker\\config.yaml";
        log_format("[AI Metadata] load_from_config_yaml: Trying path 2 = ", yaml_path.c_str());
        file.open(yaml_path);
    }
    
    if (!file.is_open()) {
        yaml_path = dll_dir + "\\config.yaml";
        log_format("[AI Metadata] load_from_config_yaml: Trying path 3 = ", yaml_path.c_str());
        file.open(yaml_path);
    }
    
    if (!file.is_open()) {
        yaml_path = dll_dir + "\\worker\\config.yaml";
        log_format("[AI Metadata] load_from_config_yaml: Trying path 4 = ", yaml_path.c_str());
        file.open(yaml_path);
    }
    
    if (!file.is_open()) {
        console::print("[AI Metadata] load_from_config_yaml: Failed to open config.yaml");
        return;
    }
    
    console::print("[AI Metadata] load_from_config_yaml: Successfully opened config.yaml");
    
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
    int line_num = 0;
    
    while (std::getline(file, line)) {
        line_num++;
        
        if (line.find("python:") != std::string::npos) {
            in_python_section = true;
            in_providers_section = false;
            in_cache_section = false;
            in_worker_section = false;
            in_logging_section = false;
            in_provider = false;
            in_models = false;
            log_format("[AI Metadata] load_from_config_yaml: Line ", line_num, " - Found python section");
            continue;
        }
        
        if (line.find("providers:") != std::string::npos) {
            in_providers_section = true;
            in_cache_section = false;
            in_python_section = false;
            in_worker_section = false;
            in_logging_section = false;
            log_format("[AI Metadata] load_from_config_yaml: Line ", line_num, " - Found providers section");
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
            log_format("[AI Metadata] load_from_config_yaml: Line ", line_num, " - Found cache section");
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
            log_format("[AI Metadata] load_from_config_yaml: Line ", line_num, " - Found worker section");
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
            log_format("[AI Metadata] load_from_config_yaml: Line ", line_num, " - Found logging section");
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
            log_format("[AI Metadata] load_from_config_yaml: Line ", line_num, " - Found data_sources section");
            continue;
        }
        
        if (in_python_section) {
            if (line.find("python_path:") != std::string::npos) {
                size_t pos = line.find(':');
                if (pos != std::string::npos) {
                    m_settings.python_path = trim_string(line.substr(pos + 1));
                    log_format("[AI Metadata] load_from_config_yaml: Line ", line_num, " - python_path = ", m_settings.python_path.c_str());
                    if (!m_settings.python_path.empty()) {
                        set_python_path(m_settings.python_path);
                        log_format("[AI Metadata] load_from_config_yaml: Set global python_path to ", m_settings.python_path.c_str());
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
                        log_format("[AI Metadata] load_from_config_yaml: enable_musicbrainz = ", m_settings.enable_musicbrainz ? "true" : "false");
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
                        log_format("[AI Metadata] load_from_config_yaml: enable_discogs = ", m_settings.enable_discogs ? "true" : "false");
                    }
                }
            }
            else if (in_ai_section) {
                if (line.find("enabled:") != std::string::npos) {
                    size_t pos = line.find(':');
                    if (pos != std::string::npos) {
                        std::string val = trim_string(line.substr(pos + 1));
                        m_settings.enable_ai = (val == "true");
                        log_format("[AI Metadata] load_from_config_yaml: enable_ai = ", m_settings.enable_ai ? "true" : "false");
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
                log_format("[AI Metadata] load_from_config_yaml: Line ", line_num, " - Default provider = ", default_provider.c_str());
            }
        }
        else if (line.find("openrouter:") != std::string::npos || 
                 line.find("zhipu:") != std::string::npos ||
                 line.find("gemini:") != std::string::npos ||
                 line.find("ollama:") != std::string::npos) {
            size_t pos = line.find(':');
            if (pos != std::string::npos) {
                current_provider = trim_string(line.substr(0, pos));
                in_provider = true;
                in_models = false;
                log_format("[AI Metadata] load_from_config_yaml: Line ", line_num, " - Found provider: ", current_provider.c_str());
            }
        }
        else if (in_provider && line.find("api_key:") != std::string::npos) {
            size_t pos = line.find(':');
            if (pos != std::string::npos) {
                AIProvider p = string_to_provider(current_provider);
                m_settings.provider_configs[p].api_key = trim_string(line.substr(pos + 1));
                log_format("[AI Metadata] load_from_config_yaml: Line ", line_num, " - API key loaded for ", current_provider.c_str());
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
            log_format("[AI Metadata] load_from_config_yaml: Line ", line_num, " - Found models section for ", current_provider.c_str());
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
                log_format("[AI Metadata] load_from_config_yaml: Line ", line_num, " - Added model: ", model_name.c_str(), " for ", current_provider.c_str());
            }
        }
        else if (in_models && line.find("priority:") != std::string::npos) {
            size_t pos = line.find(':');
            if (pos != std::string::npos && !m_settings.provider_configs[string_to_provider(current_provider)].models.empty()) {
                AIProvider p = string_to_provider(current_provider);
                m_settings.provider_configs[p].models.back().priority = std::stoi(trim_string(line.substr(pos + 1)));
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
    log_format("[AI Metadata] [preferences_page.cpp::load] Loading from ", m_config_path.c_str());
    std::ifstream file(m_config_path);
    if (!file.is_open()) {
        log_format("[AI Metadata] [preferences_page.cpp::load] File not found or cannot open");
        return;
    }
    
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();
    log_format("[AI Metadata] [preferences_page.cpp::load] File content length = ", static_cast<int>(content.length()));
    
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
    
    std::string log_level = get_value("log_level");
    if (!log_level.empty()) {
        m_settings.log_level = static_cast<ai_metadata::constants::LogLevel>(std::stoi(log_level));
    }
    
    std::string max_log_size = get_value("max_log_file_size_mb");
    if (!max_log_size.empty()) {
        m_settings.max_log_file_size_mb = std::stoi(max_log_size);
    }
    
    size_t pc_pos = content.find("\"provider_configs\":");
    log_format("[AI Metadata] [preferences_page.cpp::load] provider_configs position = ", static_cast<int>(pc_pos));
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
            log_format("[AI Metadata] [preferences_page.cpp::load] provider_configs content length = ", static_cast<int>(pc_str.length()));
            
            const char* prov_names[] = {"openrouter", "zhipu", "gemini", "ollama"};
            AIProvider prov_types[] = {AIProvider::OpenRouter, AIProvider::Zhipu, AIProvider::Gemini, AIProvider::Ollama};
            
            for (int i = 0; i < 4; i++) {
                std::string search_name = std::string("\"") + prov_names[i] + "\":";
                size_t prov_pos = pc_str.find(search_name);
                log_format("[AI Metadata] [preferences_page.cpp::load] Searching for ", prov_names[i], ", position = ", static_cast<int>(prov_pos));
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
                log_format("[AI Metadata] [preferences_page.cpp::load] ", prov_names[i], " config: ", prov_str.c_str());
                
                size_t sm_pos = prov_str.find("\"selected_model\":");
                log_format("[AI Metadata] [preferences_page.cpp::load] ", prov_names[i], " selected_model position = ", static_cast<int>(sm_pos));
                if (sm_pos != std::string::npos) {
                    size_t val_start = prov_str.find('"', sm_pos + 17);
                    if (val_start != std::string::npos) {
                        size_t val_end = prov_str.find('"', val_start + 1);
                        if (val_end != std::string::npos) {
                            std::string sel_model = prov_str.substr(val_start + 1, val_end - val_start - 1);
                            m_settings.provider_configs[prov_types[i]].selected_model = sel_model;
                            log_format("[AI Metadata] load: ", prov_names[i], " selected_model = ", sel_model.c_str());
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
            }
        }
    }
}

void SettingsManager::save() {
    std::string dir = m_config_path.substr(0, m_config_path.find_last_of("\\/"));
    CreateDirectoryA(dir.c_str(), NULL);
    
    std::ofstream file(m_config_path);
    if (file.is_open()) {
        file << "{\n";
        file << "  \"provider\": \"" << provider_to_string(m_settings.provider) << "\",\n";
        file << "  \"use_env_key\": " << (m_settings.use_env_key ? "true" : "false") << ",\n";
        file << "  \"python_path\": \"" << m_settings.python_path << "\",\n";
        file << "  \"auto_install_packages\": " << (m_settings.auto_install_packages ? "true" : "false") << ",\n";
        
        file << "  \"provider_configs\": {\n";
        bool first_provider = true;
        for (const auto& [p, config] : m_settings.provider_configs) {
            if (!first_provider) file << ",\n";
            first_provider = false;
            file << "    \"" << provider_to_string(p) << "\": {\n";
            file << "      \"api_key\": \"" << config.api_key << "\",\n";
            file << "      \"selected_model\": \"" << config.selected_model << "\"\n";
            file << "    }";
        }
        file << "\n  },\n";
        
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
        file << "  \"log_level\": " << static_cast<int>(m_settings.log_level) << ",\n";
        file << "  \"max_log_file_size_mb\": " << m_settings.max_log_file_size_mb << "\n";
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
        FILE* pipe = _popen(test_cmd.c_str(), "r");
        if (pipe) {
            char buffer[128];
            std::string result;
            while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
                result += buffer;
            }
            _pclose(pipe);
            
            if (result.find("Python") != std::string::npos) {
                log_format("[AI Metadata] auto_detect_python_path: Found Python at '", path, "' - ", result.c_str());
                
                if (std::string(path).find("\\") != std::string::npos || 
                    std::string(path).find("/") != std::string::npos) {
                    return path;
                }
                
                char full_path[MAX_PATH] = {0};
                std::string where_cmd = std::string("where ") + path + " 2>nul";
                FILE* where_pipe = _popen(where_cmd.c_str(), "r");
                if (where_pipe) {
                    if (fgets(full_path, sizeof(full_path), where_pipe) != nullptr) {
                        _pclose(where_pipe);
                        size_t len = strlen(full_path);
                        if (len > 0 && (full_path[len-1] == '\n' || full_path[len-1] == '\r')) {
                            full_path[len-1] = '\0';
                        }
                        if (len > 1 && (full_path[len-2] == '\n' || full_path[len-2] == '\r')) {
                            full_path[len-2] = '\0';
                        }
                        return std::string(full_path);
                    }
                    _pclose(where_pipe);
                }
                return path;
            }
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

const char* AIPreferencePageAdvanced::get_name() {
    return "Advanced";
}

GUID AIPreferencePageAdvanced::get_guid() {
    return g_guid;
}

GUID AIPreferencePageAdvanced::get_parent_guid() {
    return guid_preferences_root;
}

preferences_page_instance::ptr AIPreferencePageAdvanced::instantiate(HWND parent, preferences_page_callback::ptr callback) {
    return new service_impl_t<AIPreferencePageInstance>(parent, callback, IDD_PREF_ADVANCED);
}

AIPreferencePageInstance::AIPreferencePageInstance(HWND parent, preferences_page_callback::ptr callback, int dialog_id)
    : m_callback(callback), m_modified(false), m_dialog_id(dialog_id) {
    m_settings = SettingsManager::instance().settings();
    m_wnd = CreateDialogParam(
        core_api::get_my_instance(),
        MAKEINTRESOURCE(dialog_id),
        parent,
        dialog_proc,
        reinterpret_cast<LPARAM>(this)
    );
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
                console::print("[AI Metadata] dialog_proc: WM_TIMER - filling combo boxes");
                self->fill_combo_boxes();
            }
            break;
            
        case WM_SHOWWINDOW:
            return TRUE;
            
        case WM_FILL_COMBO_BOXES:
            console::print("[AI Metadata] dialog_proc: WM_FILL_COMBO_BOXES - filling combo boxes");
            self->fill_combo_boxes();
            return TRUE;
            
        case WM_COMMAND:
            switch (LOWORD(wp)) {
                case IDC_API_KEY:
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
                    if (HIWORD(wp) == EN_CHANGE) {
                        self->on_changed();
                    }
                    break;
                    
                case IDC_USE_ENV_KEY:
                case IDC_ENABLE_CACHE:
                case IDC_AUTO_CLEANUP:
                case IDC_AUTO_RESTART:
                case IDC_SHOW_PROGRESS_PREF:
                case IDC_AUTO_INSTALL_PACKAGES:
                case IDC_MENU_CACHE_STATS:
                case IDC_MENU_ROLLBACK:
                case IDC_MENU_CLEAR_CACHE:
                case IDC_ENABLE_MUSICBRAINZ:
                case IDC_ENABLE_DISCOGS:
                case IDC_ENABLE_AI:
                    self->on_changed();
                    break;
                    
                case IDC_PROVIDER:
                    if (HIWORD(wp) == CBN_SELCHANGE) {
                        self->on_provider_changed();
                    }
                    break;
                    
                case IDC_MODEL:
                case IDC_LOG_LEVEL:
                    if (HIWORD(wp) == CBN_SELCHANGE) {
                        self->on_changed();
                    }
                    break;
                    
                case IDC_TEST_API_BTN:
                    self->on_test_api();
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
            }
            return TRUE;
    }
    
    return FALSE;
}

void AIPreferencePageInstance::on_init_dialog() {
    console::print("[AI Metadata] on_init_dialog: Starting");
    update_controls();
}

void AIPreferencePageInstance::fill_combo_boxes() {
    console::print("[AI Metadata] fill_combo_boxes: Starting");
    
    HWND combo = GetDlgItem(m_wnd, IDC_PROVIDER);
    if (combo) {
        console::print("[AI Metadata] fill_combo_boxes: Adding provider items");
        log_format("[AI Metadata] fill_combo_boxes: IDC_PROVIDER handle = ", (intptr_t)combo);
        
        SendMessageW(combo, CB_RESETCONTENT, 0, 0);
        int count_after_reset = (int)SendMessageW(combo, CB_GETCOUNT, 0, 0);
        log_format("[AI Metadata] fill_combo_boxes: After reset, count = ", count_after_reset);
        
        LRESULT result = SendMessageW(combo, CB_INSERTSTRING, 0, (LPARAM)L"OpenRouter");
        log_format("[AI Metadata] fill_combo_boxes: InsertString result = ", result);
        int count = (int)SendMessageW(combo, CB_GETCOUNT, 0, 0);
        log_format("[AI Metadata] fill_combo_boxes: After insert 0, count = ", count);
        
        result = SendMessageW(combo, CB_INSERTSTRING, 1, (LPARAM)L"Zhipu (智谱)");
        log_format("[AI Metadata] fill_combo_boxes: InsertString result = ", result);
        count = (int)SendMessageW(combo, CB_GETCOUNT, 0, 0);
        log_format("[AI Metadata] fill_combo_boxes: After insert 1, count = ", count);
        
        result = SendMessageW(combo, CB_INSERTSTRING, 2, (LPARAM)L"Gemini");
        log_format("[AI Metadata] fill_combo_boxes: InsertString result = ", result);
        count = (int)SendMessageW(combo, CB_GETCOUNT, 0, 0);
        log_format("[AI Metadata] fill_combo_boxes: After insert 2, count = ", count);
        
        result = SendMessageW(combo, CB_INSERTSTRING, 3, (LPARAM)L"Ollama");
        log_format("[AI Metadata] fill_combo_boxes: InsertString result = ", result);
        count = (int)SendMessageW(combo, CB_GETCOUNT, 0, 0);
        log_format("[AI Metadata] fill_combo_boxes: After insert 3, count = ", count);
        
        SendMessageW(combo, CB_SETCURSEL, static_cast<int>(m_settings.provider), 0);
        log_format("[AI Metadata] fill_combo_boxes: Set provider to ", static_cast<int>(m_settings.provider));
        
        update_model_combo();
    } else {
        console::print("[AI Metadata] fill_combo_boxes: IDC_PROVIDER not found in this page");
    }
    
    combo = GetDlgItem(m_wnd, IDC_LOG_LEVEL);
    if (combo) {
        console::print("[AI Metadata] fill_combo_boxes: Adding log level items");
        SendMessageW(combo, CB_RESETCONTENT, 0, 0);
        
        SendMessageW(combo, CB_INSERTSTRING, 0, (LPARAM)L"DEBUG");
        SendMessageW(combo, CB_INSERTSTRING, 1, (LPARAM)L"INFO");
        SendMessageW(combo, CB_INSERTSTRING, 2, (LPARAM)L"WARNING");
        SendMessageW(combo, CB_INSERTSTRING, 3, (LPARAM)L"ERROR");
        
        int count = (int)SendMessageW(combo, CB_GETCOUNT, 0, 0);
        log_format("[AI Metadata] fill_combo_boxes: Log level combo count = ", count);
        
        SendMessageW(combo, CB_SETCURSEL, static_cast<int>(m_settings.log_level), 0);
        log_format("[AI Metadata] fill_combo_boxes: Set log level to ", static_cast<int>(m_settings.log_level));
        Logger::instance().set_log_level(m_settings.log_level);
    } else {
        console::print("[AI Metadata] fill_combo_boxes: IDC_LOG_LEVEL not found in this page");
    }
}

void AIPreferencePageInstance::update_controls() {
    console::print("[AI Metadata] update_controls: Starting");
    
    if (GetDlgItem(m_wnd, IDC_USE_ENV_KEY)) {
        CheckDlgButton(m_wnd, IDC_USE_ENV_KEY, m_settings.use_env_key ? BST_CHECKED : BST_UNCHECKED);
    }
    
    if (GetDlgItem(m_wnd, IDC_API_KEY)) {
        update_api_key_for_provider();
    }
    
    if (GetDlgItem(m_wnd, IDC_PYTHON_PATH)) {
        SetDlgItemTextA(m_wnd, IDC_PYTHON_PATH, m_settings.python_path.c_str());
    }
    if (GetDlgItem(m_wnd, IDC_AUTO_INSTALL_PACKAGES)) {
        CheckDlgButton(m_wnd, IDC_AUTO_INSTALL_PACKAGES, m_settings.auto_install_packages ? BST_CHECKED : BST_UNCHECKED);
    }
    
    if (GetDlgItem(m_wnd, IDC_PYTHON_STATUS)) {
        std::string detected_python = SettingsManager::instance().get_python_path();
        std::string status_text = "Status: ";
        if (!detected_python.empty()) {
            status_text += detected_python;
        } else {
            status_text += "Not detected";
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
    if (GetDlgItem(m_wnd, IDC_ENABLE_AI)) {
        CheckDlgButton(m_wnd, IDC_ENABLE_AI, m_settings.enable_ai ? BST_CHECKED : BST_UNCHECKED);
    }
    
    if (GetDlgItem(m_wnd, IDC_LOG_SIZE)) {
        SetDlgItemInt(m_wnd, IDC_LOG_SIZE, m_settings.max_log_file_size_mb, FALSE);
    }
    
    if (GetDlgItem(m_wnd, IDC_MENU_CACHE_STATS)) {
        CheckDlgButton(m_wnd, IDC_MENU_CACHE_STATS, m_settings.menu_cache_stats ? BST_CHECKED : BST_UNCHECKED);
    }
    if (GetDlgItem(m_wnd, IDC_MENU_ROLLBACK)) {
        CheckDlgButton(m_wnd, IDC_MENU_ROLLBACK, m_settings.menu_rollback ? BST_CHECKED : BST_UNCHECKED);
    }
    if (GetDlgItem(m_wnd, IDC_MENU_CLEAR_CACHE)) {
        CheckDlgButton(m_wnd, IDC_MENU_CLEAR_CACHE, m_settings.menu_clear_cache ? BST_CHECKED : BST_UNCHECKED);
    }
}

void AIPreferencePageInstance::on_provider_changed() {
    HWND combo = GetDlgItem(m_wnd, IDC_PROVIDER);
    if (combo) {
        m_settings.provider = static_cast<AIProvider>(ComboBox_GetCurSel(combo));
    }
    update_api_key_for_provider();
    update_model_combo();
    on_changed();
}

void AIPreferencePageInstance::update_api_key_for_provider() {
    auto& config = m_settings.get_current_provider_config();
    SetDlgItemTextA(m_wnd, IDC_API_KEY, config.api_key.c_str());
}

void AIPreferencePageInstance::update_model_combo() {
    console::print("[AI Metadata] update_model_combo: Starting");
    
    HWND combo = GetDlgItem(m_wnd, IDC_MODEL);
    if (!combo) {
        console::print("[AI Metadata] update_model_combo: ERROR - IDC_MODEL not found");
        return;
    }
    
    log_format("[AI Metadata] update_model_combo: IDC_MODEL handle = ", (intptr_t)combo);
    
    console::print("[AI Metadata] update_model_combo: Resetting IDC_MODEL content");
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    int count_after_reset = (int)SendMessageW(combo, CB_GETCOUNT, 0, 0);
    log_format("[AI Metadata] update_model_combo: After reset, count = ", count_after_reset);
    
    auto& config = m_settings.get_current_provider_config();
    log_format("[AI Metadata] update_model_combo: Current provider has ", static_cast<int>(config.models.size()), " models");
    
    int select_index = -1;
    int current_index = 0;
    bool found_selected = false;
    
    for (const auto& model : config.models) {
        LRESULT result = SendMessageA(combo, CB_INSERTSTRING, current_index, (LPARAM)model.name.c_str());
        log_format("[AI Metadata] update_model_combo: Insert model ", current_index, ": ", model.name.c_str(), " (result=", result, ")");
        
        int count = (int)SendMessageA(combo, CB_GETCOUNT, 0, 0);
        log_format("[AI Metadata] update_model_combo: After insert ", current_index, ", count = ", count);
        
        if (model.name == config.selected_model) {
            select_index = current_index;
            found_selected = true;
            log_format("[AI Metadata] update_model_combo: Selected model index = ", select_index);
        }
        current_index++;
    }
    
    // 如果保存的模型不在预定义列表中（自定义模型），添加到列表末尾
    if (!found_selected && !config.selected_model.empty()) {
        SendMessageA(combo, CB_INSERTSTRING, current_index, (LPARAM)config.selected_model.c_str());
        select_index = current_index;
        log_format("[AI Metadata] update_model_combo: Added custom model: ", config.selected_model.c_str());
    }
    
    if (select_index >= 0) {
        SendMessageA(combo, CB_SETCURSEL, select_index, 0);
        log_format("[AI Metadata] update_model_combo: Set selection to index ", select_index);
    } else if (!config.models.empty()) {
        SendMessageA(combo, CB_SETCURSEL, 0, 0);
    } else {
        console::print("[AI Metadata] update_model_combo: No models to display");
    }
    
    int count = (int)SendMessageA(combo, CB_GETCOUNT, 0, 0);
    log_format("[AI Metadata] update_model_combo: IDC_MODEL count = ", count);
}

void AIPreferencePageInstance::save_settings() {
    char buffer[256];
    
    HWND combo = GetDlgItem(m_wnd, IDC_PROVIDER);
    if (combo) {
        m_settings.provider = static_cast<AIProvider>(ComboBox_GetCurSel(combo));
    }
    
    if (GetDlgItem(m_wnd, IDC_API_KEY)) {
        GetDlgItemTextA(m_wnd, IDC_API_KEY, buffer, sizeof(buffer));
        m_settings.get_current_provider_config().api_key = buffer;
    }
    
    combo = GetDlgItem(m_wnd, IDC_MODEL);
    if (combo) {
        auto& config = m_settings.get_current_provider_config();
        int sel = ComboBox_GetCurSel(combo);
        if (sel >= 0 && sel < (int)config.models.size()) {
            config.selected_model = config.models[sel].name;
        } else {
            // 用户手动输入了自定义模型名
            char model_buf[256] = {0};
            GetWindowTextA(combo, model_buf, sizeof(model_buf));
            if (model_buf[0] != '\0') {
                config.selected_model = model_buf;
            }
        }
    }
    
    if (GetDlgItem(m_wnd, IDC_USE_ENV_KEY)) {
        m_settings.use_env_key = IsDlgButtonChecked(m_wnd, IDC_USE_ENV_KEY) == BST_CHECKED;
    }
    
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
    if (GetDlgItem(m_wnd, IDC_ENABLE_AI)) {
        m_settings.enable_ai = IsDlgButtonChecked(m_wnd, IDC_ENABLE_AI) == BST_CHECKED;
    }
    
    combo = GetDlgItem(m_wnd, IDC_LOG_LEVEL);
    if (combo) {
        m_settings.log_level = static_cast<ai_metadata::constants::LogLevel>(ComboBox_GetCurSel(combo));
        Logger::instance().set_log_level(m_settings.log_level);
    }
    if (GetDlgItem(m_wnd, IDC_LOG_SIZE)) {
        m_settings.max_log_file_size_mb = GetDlgItemInt(m_wnd, IDC_LOG_SIZE, NULL, FALSE);
    }
    
    if (GetDlgItem(m_wnd, IDC_MENU_CACHE_STATS)) {
        m_settings.menu_cache_stats = IsDlgButtonChecked(m_wnd, IDC_MENU_CACHE_STATS) == BST_CHECKED;
    }
    if (GetDlgItem(m_wnd, IDC_MENU_ROLLBACK)) {
        m_settings.menu_rollback = IsDlgButtonChecked(m_wnd, IDC_MENU_ROLLBACK) == BST_CHECKED;
    }
    if (GetDlgItem(m_wnd, IDC_MENU_CLEAR_CACHE)) {
        m_settings.menu_clear_cache = IsDlgButtonChecked(m_wnd, IDC_MENU_CLEAR_CACHE) == BST_CHECKED;
    }
}

void AIPreferencePageInstance::on_test_api() {
    // 防止重复点击
    if (m_test_in_progress) {
        return;
    }
    
    save_settings();
    std::string api_key = m_settings.get_current_provider_config().api_key;
    if (m_settings.use_env_key) {
        const char* env_key = std::getenv("OPENROUTER_API_KEY");
        if (env_key) api_key = env_key;
    }
    
    if (api_key.empty()) {
        SetDlgItemTextA(m_wnd, IDC_STATUS_TEXT, "[ERROR] No API key configured");
        popup_message::g_show("Error: No API key configured. Please enter your API key.", "AI Metadata");
        return;
    }
    
    std::string model = m_settings.get_current_provider_config().selected_model;
    std::string provider_name = provider_to_string(m_settings.provider);
    
    AICore* ai_core = get_ai_core_instance();
    if (!ai_core) {
        SetDlgItemTextA(m_wnd, IDC_STATUS_TEXT, "[ERROR] AI Core not initialized");
        popup_message::g_show("Error: AI Core not initialized. Please restart foobar2000.", "AI Metadata");
        return;
    }
    
    if (!ai_core->is_initialized()) {
        SetDlgItemTextA(m_wnd, IDC_STATUS_TEXT, "[1/3] Initializing AI Core...");
        if (!ai_core->initialize()) {
            SetDlgItemTextA(m_wnd, IDC_STATUS_TEXT, "[ERROR] Failed to initialize AI Core");
            popup_message::g_show("Error: Failed to initialize AI Core. Check Python installation.", "AI Metadata");
            return;
        }
    }
    
    // 标记测试进行中，禁用按钮
    m_test_in_progress = true;
    EnableWindow(GetDlgItem(m_wnd, IDC_TEST_API_BTN), FALSE);
    
    SetDlgItemTextA(m_wnd, IDC_STATUS_TEXT, "[2/3] Sending test request to AI provider (timeout: 30s)...");
    
    HWND wnd = m_wnd;
    std::thread([wnd, ai_core, provider_name, model, this]() {
        std::string result_json = ai_core->test_api_connection(provider_name, model, 30000);
        
        fb2k::inMainThread([wnd, result_json, provider_name, model, this]() {
            if (!IsWindow(wnd)) {
                m_test_in_progress = false;
                return;
            }
            
            // 恢复按钮
            m_test_in_progress = false;
            EnableWindow(GetDlgItem(wnd, IDC_TEST_API_BTN), TRUE);
            
            try {
                nlohmann::json result = nlohmann::json::parse(result_json);
                
                if (result["success"].get<bool>()) {
                    std::string message = "API connection successful!\n\n";
                    message += "Provider: " + provider_name + "\n";
                    message += "Model: " + model + "\n";
                    
                    if (result.contains("result") && result["result"].is_object()) {
                        auto& res = result["result"];
                        if (res.contains("test_genre")) {
                            message += "Test Genre: " + res["test_genre"].get<std::string>() + "\n";
                        }
                        if (res.contains("tokens_used")) {
                            message += "Tokens Used: " + std::to_string(res["tokens_used"].get<int>()) + "\n";
                        }
                    }
                    
                    SetDlgItemTextA(wnd, IDC_STATUS_TEXT, "[3/3] API test: SUCCESS");
                    popup_message::g_show(message.c_str(), "API Test Successful");
                } else {
                    std::string error_msg = "API connection failed!\n\n";
                    error_msg += "Provider: " + provider_name + "\n";
                    error_msg += "Model: " + model + "\n";
                    
                    if (result.contains("error")) {
                        error_msg += "Error: " + result["error"].get<std::string>();
                    }
                    
                    SetDlgItemTextA(wnd, IDC_STATUS_TEXT, "[3/3] API test: FAILED");
                    popup_message::g_show(error_msg.c_str(), "API Test Failed");
                }
            } catch (const std::exception& e) {
                std::string error_msg = "Failed to parse test result: ";
                error_msg += e.what();
                SetDlgItemTextA(wnd, IDC_STATUS_TEXT, "[3/3] API test: ERROR");
                popup_message::g_show(error_msg.c_str(), "API Test Error");
            }
        });
    }).detach();
}

void AIPreferencePageInstance::on_open_log_folder() {
    std::string log_path = core_api::get_profile_path();
    log_path += "\\foo_ai_metadata\\logs";
    
    CreateDirectoryA(log_path.c_str(), NULL);
    
    int len = MultiByteToWideChar(CP_UTF8, 0, log_path.c_str(), -1, NULL, 0);
    std::wstring wpath(len, 0);
    MultiByteToWideChar(CP_UTF8, 0, log_path.c_str(), -1, &wpath[0], len);
    
    ShellExecuteW(NULL, L"explore", wpath.c_str(), NULL, NULL, SW_SHOWNORMAL);
}

void AIPreferencePageInstance::on_clear_cache() {
    int result = MessageBoxW(
        m_wnd,
        L"Are you sure you want to clear all cached AI results?",
        L"Clear Cache",
        MB_YESNO | MB_ICONQUESTION
    );
    
    if (result == IDYES) {
        std::string cache_path = core_api::get_profile_path();
        cache_path += "\\foo_ai_metadata\\cache\\ai_metadata_cache.db";
        
        if (DeleteFileA(cache_path.c_str())) {
            SetDlgItemTextA(m_wnd, IDC_STATUS_TEXT, "Cache cleared successfully");
            popup_message::g_show("Cache cleared successfully", "AI Metadata");
        } else {
            SetDlgItemTextA(m_wnd, IDC_STATUS_TEXT, "Cache was already empty or could not be cleared");
        }
    }
}

void AIPreferencePageInstance::on_restart_workers() {
    AICore* ai_core = get_ai_core_instance();
    if (ai_core) {
        ai_core->restart_all_workers();
        SetDlgItemTextA(m_wnd, IDC_STATUS_TEXT, "Workers restarted successfully");
        
        auto workers = ai_core->get_worker_info();
        if (!workers.empty() && workers[0].status == "running") {
            SetDlgItemTextA(m_wnd, IDC_WORKER_STATUS_TEXT, "Status: Healthy");
        } else {
            SetDlgItemTextA(m_wnd, IDC_WORKER_STATUS_TEXT, "Status: Not Running");
        }
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

void AIPreferencePageInstance::on_changed() {
    if (!m_modified) {
        m_modified = true;
        m_callback->on_state_changed();
    }
}

static preferences_page_factory_t<AIPreferencePageRoot> g_preferences_page_root_factory;
static preferences_page_factory_t<AIPreferencePageGeneral> g_preferences_page_general_factory;
static preferences_page_factory_t<AIPreferencePageDataSources> g_preferences_page_data_sources_factory;
static preferences_page_factory_t<AIPreferencePageAdvanced> g_preferences_page_advanced_factory;

}
