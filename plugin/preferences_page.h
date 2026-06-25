#pragma once

#include <foobar2000/SDK/foobar2000.h>
#include "../include/constants.h"
#include <string>
#include <functional>
#include <vector>
#include <map>

namespace ai_metadata {

/**
 * @brief AI提供商枚举
 */
enum class AIProvider {
    OpenRouter = 0, ///< OpenRouter提供商
    Zhipu = 1,      ///< 智谱AI提供商
    Gemini = 2,     ///< Google Gemini提供商
    Ollama = 3      ///< Ollama本地提供商
};

/**
 * @brief 将提供商枚举转换为字符串
 * @param p 提供商枚举值
 * @return 提供商字符串
 */
inline const char* provider_to_string(AIProvider p) {
    switch (p) {
        case AIProvider::OpenRouter: return "openrouter";
        case AIProvider::Zhipu: return "zhipu";
        case AIProvider::Gemini: return "gemini";
        case AIProvider::Ollama: return "ollama";
        default: return "openrouter";
    }
}

/**
 * @brief 将字符串转换为提供商枚举
 * @param s 提供商字符串
 * @return 提供商枚举值
 */
inline AIProvider string_to_provider(const std::string& s) {
    if (s == "zhipu") return AIProvider::Zhipu;
    if (s == "gemini") return AIProvider::Gemini;
    if (s == "ollama") return AIProvider::Ollama;
    return AIProvider::OpenRouter;
}

/**
 * @brief 模型信息结构体
 */
struct ModelInfo {
    std::string name;  ///< 模型名称
    int priority;      ///< 优先级
};

/**
 * @brief 提供商配置结构体
 */
struct ProviderConfig {
    std::string api_key;            ///< API密钥
    std::string base_url;           ///< 基础URL
    std::vector<ModelInfo> models;  ///< 可用模型列表
    std::string selected_model;     ///< 选中的模型
};

/**
 * @brief 插件设置结构体
 */
struct PluginSettings {
    AIProvider provider;                              ///< 当前提供商
    std::map<AIProvider, ProviderConfig> provider_configs; ///< 提供商配置映射
    bool use_env_key;                                 ///< 是否使用环境变量密钥
    
    std::string python_path;                          ///< Python 可执行文件路径
    bool auto_install_packages;                       ///< 首次运行自动安装 Python 包
    
    bool cache_enabled;                               ///< 缓存启用
    int cache_expiration_days;                        ///< 缓存过期天数
    int max_cache_size_mb;                            ///< 最大缓存大小（MB）
    bool auto_cleanup;                                ///< 自动清理
    
    bool auto_restart;                                ///< 自动重启Worker
    
    int ai_batch_size;                                ///< AI 批处理大小
    int taskqueue_batch_size;                         ///< TaskQueue 批处理大小
    int concurrency;                                  ///< 并发数
    bool show_progress_dialog;                        ///< 显示进度对话框
    
    bool enable_musicbrainz;                          ///< 启用 MusicBrainz 数据源
    bool enable_discogs;                              ///< 启用 Discogs 数据源
    bool enable_ai;                                   ///< 启用 AI 数据源
    
    int mb_timeout;                                   ///< MusicBrainz HTTP 超时（秒）
    int mb_retries;                                   ///< MusicBrainz 重试次数
    int mb_page_size;                                 ///< MusicBrainz 每页结果数
    int mb_max_pages;                                 ///< MusicBrainz 最大页数
    int mb_score_threshold;                           ///< MusicBrainz 评分阈值
    int mb_score_margin;                              ///< MusicBrainz 评分差距阈值
    int mb_rate_limit;                                ///< MusicBrainz 速率限制（请求/分钟）
    
    ai_metadata::constants::LogLevel log_level;       ///< 日志级别
    int max_log_file_size_mb;                         ///< 最大日志文件大小（MB）
    
    bool menu_analyze_selected;                       ///< 菜单项：分析选中
    bool menu_analyze_all;                            ///< 菜单项：分析全部
    bool menu_cache_stats;                            ///< 菜单项：缓存统计
    bool menu_rollback;                               ///< 菜单项：回滚
    bool menu_clear_cache;                            ///< 菜单项：清除缓存
    
    /**
     * @brief 默认构造函数，初始化默认设置
     */
    PluginSettings()
        : provider(AIProvider::Zhipu)
        , use_env_key(false)
        , python_path("")
        , auto_install_packages(true)
        , cache_enabled(true)
        , cache_expiration_days(365)
        , max_cache_size_mb(500)
        , auto_cleanup(true)
        , auto_restart(true)
        , ai_batch_size(30)
        , taskqueue_batch_size(50)
        , concurrency(3)
        , show_progress_dialog(true)
        , enable_musicbrainz(true)
        , enable_discogs(false)
        , enable_ai(true)
        , mb_timeout(30)
        , mb_retries(3)
        , mb_page_size(10)
        , mb_max_pages(2)
        , mb_score_threshold(85)
        , mb_score_margin(10)
        , mb_rate_limit(50)
        , log_level(ai_metadata::constants::LogLevel::Info)
        , max_log_file_size_mb(10)
        , menu_analyze_selected(true)
        , menu_analyze_all(true)
        , menu_cache_stats(true)
        , menu_rollback(true)
        , menu_clear_cache(true)
    {
        provider_configs[AIProvider::OpenRouter] = ProviderConfig{};
        provider_configs[AIProvider::Zhipu] = ProviderConfig{};
        provider_configs[AIProvider::Gemini] = ProviderConfig{};
        provider_configs[AIProvider::Ollama] = ProviderConfig{};
    }
    
    /**
     * @brief 获取当前提供商配置
     * @return 当前提供商配置引用
     */
    ProviderConfig& get_current_provider_config() {
        return provider_configs[provider];
    }
    
    /**
     * @brief 获取当前提供商配置（常量版本）
     * @return 当前提供商配置常量引用
     */
    const ProviderConfig& get_current_provider_config() const {
        auto it = provider_configs.find(provider);
        if (it != provider_configs.end()) {
            return it->second;
        }
        static ProviderConfig empty;
        return empty;
    }
};

/**
 * @brief 设置管理器类，管理插件配置
 */
class SettingsManager {
public:
    /**
     * @brief 获取设置管理器单例
     * @return 设置管理器引用
     */
    static SettingsManager& instance();
    
    /**
     * @brief 从文件加载设置
     */
    void load();
    
    /**
     * @brief 保存设置到文件
     */
    void save();
    
    /**
     * @brief 重置设置为默认值
     */
    void reset();
    
    /**
     * @brief 从config.yaml加载设置
     */
    void load_from_config_yaml();
    
    /**
     * @brief 确保设置已初始化
     */
    void ensure_initialized() const;
    
    /**
     * @brief 获取设置（可修改）
     * @return 设置引用
     */
    PluginSettings& settings() { ensure_initialized(); return m_settings; }
    
    /**
     * @brief 获取设置（只读）
     * @return 设置常量引用
     */
    const PluginSettings& settings() const { ensure_initialized(); return m_settings; }
    
    /**
     * @brief 获取API密钥
     * @return API密钥字符串
     */
    std::string get_api_key() const;
    
    /**
     * @brief 获取配置文件路径
     * @return 配置文件路径
     */
    std::string get_config_path() const;
    
    /**
     * @brief 获取指定提供商的模型列表
     * @param provider 提供商
     * @return 模型信息列表
     */
    std::vector<ModelInfo> get_models_for_provider(AIProvider provider) const;
    
    /**
     * @brief 获取 Python 可执行文件路径
     * @return Python 路径字符串
     */
    std::string get_python_path() const;
    
    /**
     * @brief 自动检测 Python 路径
     * @return 检测到的 Python 路径，如果未找到返回空字符串
     */
    std::string auto_detect_python_path() const;
    
private:
    /**
     * @brief 私有构造函数（单例模式）
     */
    SettingsManager();
    
    /**
     * @brief 析构函数
     */
    ~SettingsManager() = default;
    
    /**
     * @brief 实际执行初始化
     */
    void do_ensure_initialized();
    
    PluginSettings m_settings;  ///< 设置数据
    std::string m_config_path;  ///< 配置文件路径
    mutable bool m_initialized; ///< 是否已初始化
};

/**
 * @brief AI偏好设置页面实例类
 */
class AIPreferencePageInstance : public preferences_page_instance {
public:
    AIPreferencePageInstance(HWND parent, preferences_page_callback::ptr callback, int dialog_id);
    ~AIPreferencePageInstance();
    
    HWND get_wnd() override;
    t_uint32 get_state() override;
    void apply() override;
    void reset() override;
    
private:
    static INT_PTR CALLBACK dialog_proc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp);
    void on_init_dialog();
    void fill_combo_boxes();
    void load_settings();
    void save_settings();
    void update_controls();
    void on_provider_changed();
    void update_model_combo();
    void update_api_key_for_provider();
    void on_test_api();
    void on_open_log_folder();
    void on_clear_cache();
    void on_restart_workers();
    void on_browse_python();
    void on_changed();
    void update_worker_status_display();
    
    HWND m_wnd;
    preferences_page_callback::ptr m_callback;
    PluginSettings m_settings;
    bool m_modified;
    int m_dialog_id;
    bool m_test_in_progress = false; ///< API测试是否进行中
};

class AIPreferencePageRoot : public preferences_page_v3 {
public:
    static const GUID g_guid;
    
    const char* get_name() override;
    GUID get_guid() override;
    GUID get_parent_guid() override;
    preferences_page_instance::ptr instantiate(HWND parent, preferences_page_callback::ptr callback) override;
};

class AIPreferencePageGeneral : public preferences_page_v3 {
public:
    static const GUID g_guid;
    
    const char* get_name() override;
    GUID get_guid() override;
    GUID get_parent_guid() override;
    preferences_page_instance::ptr instantiate(HWND parent, preferences_page_callback::ptr callback) override;
};

class AIPreferencePageDataSources : public preferences_page_v3 {
public:
    static const GUID g_guid;
    
    const char* get_name() override;
    GUID get_guid() override;
    GUID get_parent_guid() override;
    preferences_page_instance::ptr instantiate(HWND parent, preferences_page_callback::ptr callback) override;
};

class AIPreferencePageAdvanced : public preferences_page_v3 {
public:
    static const GUID g_guid;
    
    const char* get_name() override;
    GUID get_guid() override;
    GUID get_parent_guid() override;
    preferences_page_instance::ptr instantiate(HWND parent, preferences_page_callback::ptr callback) override;
};

}
