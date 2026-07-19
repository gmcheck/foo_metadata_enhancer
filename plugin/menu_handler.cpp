#include "menu_handler.h"
#include "../core/ai_core.h"
#include "dialogs.h"
#include "preferences_page.h"
#include "resource.h"
#include "../include/constants.h"
#include "../core/cache_layer.h"
#include "../include/backup_manager.h"
#include <memory>
#include <chrono>
#include <algorithm>
#include <set>
#include <sstream>
#include <iomanip>

namespace ai_metadata {

static std::unique_ptr<AICore> g_ai_core;

static void batch_update_metadata(
    const metadb_handle_list& tracks,
    const pfc::list_t<file_info_impl>& infos
) {
    size_t total = tracks.get_count();
    if (total == 0) return;
    
    pfc::list_t<const file_info*> info_ptrs;
    info_ptrs.prealloc(total);
    for (size_t i = 0; i < total; ++i) {
        info_ptrs.add_item(&infos[i]);
    }
    
    Logger::instance().info("[batch_update] Calling update_info_async_simple for " + std::to_string(total) + " tracks");
    
    static_api_ptr_t<metadb_io_v3>()->update_info_async_simple(
        tracks,
        info_ptrs,
        core_api::get_main_window(),
        metadb_io_v3::op_flag_delay_ui,
        nullptr
    );
    
    Logger::instance().info("[batch_update] Complete: " + std::to_string(total) + " tracks updated");
}

static std::map<std::string, std::string> extract_full_snapshot(const file_info& info) {
    std::map<std::string, std::string> snapshot;

    // multi-value 字段的分隔符：使用 ASCII Unit Separator (0x1F)
    // 该字符不会出现在正常 metadata 中，且不会被 serialize_snapshot 转义，
    // 因此向后兼容（单 value 字段不含此分隔符，反序列化后与原值一致）。
    static const char kMultiValueSep = '\x1F';

    size_t meta_count = info.meta_get_count();
    for (size_t i = 0; i < meta_count; ++i) {
        const char* field_name = info.meta_enum_name(i);
        if (!field_name) continue;

        std::string field_upper = field_name;
        std::transform(field_upper.begin(), field_upper.end(), field_upper.begin(), ::toupper);

        if (BackupManager::is_field_blacklisted(field_upper)) {
            continue;
        }

        // 保存 multi-value 字段的所有 values（foobar2000 支持一字段多值，
        // 如合辑 ARTIST = ["Various Artists", "미도와 파라솔"]）。
        // 原实现只取 meta_get(field, 0)，回滚时会丢失后续 values。
        const t_size value_count = info.meta_get_count_by_name(field_name);
        std::string combined;
        for (t_size v = 0; v < value_count; ++v) {
            const char* value = info.meta_get(field_name, v);
            if (!value || strlen(value) == 0) continue;
            if (!combined.empty()) {
                combined.push_back(kMultiValueSep);
            }
            combined.append(value);
        }
        if (!combined.empty()) {
            snapshot[field_upper] = combined;
        }
    }

    return snapshot;
}

static void apply_snapshot_to_info(file_info& info, const std::map<std::string, std::string>& snapshot) {
    static const char kMultiValueSep = '\x1F';

    size_t meta_count = info.meta_get_count();
    std::vector<std::string> fields_to_remove;

    for (size_t i = 0; i < meta_count; ++i) {
        const char* field_name = info.meta_enum_name(i);
        if (!field_name) continue;

        std::string field_upper = field_name;
        std::transform(field_upper.begin(), field_upper.end(), field_upper.begin(), ::toupper);

        if (!BackupManager::is_field_blacklisted(field_upper)) {
            fields_to_remove.push_back(field_upper);
        }
    }

    for (const auto& field : fields_to_remove) {
        info.meta_remove_field(field.c_str());
    }

    for (const auto& [field_name, field_value] : snapshot) {
        if (field_value.empty()) continue;

        // 解析 multi-value：按 kMultiValueSep 拆分，逐个 meta_add
        // 单 value 字段不含分隔符，拆分结果为 1 个元素，等价于 meta_set
        std::vector<std::string> values;
        std::string current;
        for (char c : field_value) {
            if (c == kMultiValueSep) {
                values.push_back(current);
                current.clear();
            } else {
                current.push_back(c);
            }
        }
        values.push_back(current);

        for (const auto& v : values) {
            if (!v.empty()) {
                info.meta_add(field_name.c_str(), v.c_str());
            }
        }
    }
}

// 返回每种操作类型影响的字段集合（大写）
// 三功能边界（V8.2）：
//   Scrape   : 从外部数据源获取本地没有的数据（MusicBrainz/Discogs/AI 降级）→ 影响全量字段
//   Enhancer : 基于已有元数据生成新价值（翻译），不获取新事实 → 仅影响 TITLE_ZH/ALBUM_ZH/ARTIST_ZH
//   Normalize: 已有 Tag → 标准 Tag（歌手规范化、Genre 映射等）→ 影响用户选定的 field
// Scrape 影响：全量（返回空 set 表示全量）
// Translate 影响：TITLE_ZH / ALBUM_ZH / ARTIST_ZH（Stage2 翻译产物）
// Normalize 影响：ARTIST / ALBUM ARTIST / COMPOSER / PERFORMER 等（用户选的 field）
static std::set<std::string> get_operation_fields(ai_metadata::OperationType op_type) {
    std::set<std::string> fields;
    switch (op_type) {
        case ai_metadata::OperationType::Scrape:
            // 空 set = 全量回滚（删除所有非黑名单字段后重设）
            break;
        case ai_metadata::OperationType::Translate:
            fields = {"TITLE_ZH", "ALBUM_ZH", "ARTIST_ZH"};
            break;
        case ai_metadata::OperationType::Normalize:
            // Normalize 通常改 ARTIST / ALBUM ARTIST，但也可能扩展到其他字段
            // 这里列常见的归一化目标字段
            fields = {"ARTIST", "ALBUM ARTIST", "ALBUM_ARTIST",
                      "COMPOSER", "PERFORMER", "ALBUMARTIST"};
            break;
    }
    return fields;
}

// 仅应用快照中属于指定字段集合的字段（部分回滚）
// fields 为空时退化为全量应用（与 apply_snapshot_to_info 等价）
static void apply_partial_snapshot_to_info(
    file_info& info,
    const std::map<std::string, std::string>& snapshot,
    const std::set<std::string>& fields
) {
    static const char kMultiValueSep = '\x1F';

    if (fields.empty()) {
        apply_snapshot_to_info(info, snapshot);
        return;
    }

    // 1. 删除当前 info 中属于 fields 的字段（清掉该操作改过的字段）
    for (const auto& f : fields) {
        info.meta_remove_field(f.c_str());
    }

    // 2. 从快照中恢复属于 fields 的字段
    for (const auto& [field_name, field_value] : snapshot) {
        std::string field_upper = field_name;
        std::transform(field_upper.begin(), field_upper.end(), field_upper.begin(), ::toupper);
        if (!fields.count(field_upper) || field_value.empty()) continue;

        // 解析 multi-value：按 kMultiValueSep 拆分，逐个 meta_add
        // 单 value 字段不含分隔符，拆分结果为 1 个元素，等价于 meta_set
        std::vector<std::string> values;
        std::string current;
        for (char c : field_value) {
            if (c == kMultiValueSep) {
                values.push_back(current);
                current.clear();
            } else {
                current.push_back(c);
            }
        }
        values.push_back(current);

        for (const auto& v : values) {
            if (!v.empty()) {
                info.meta_add(field_name.c_str(), v.c_str());
            }
        }
    }
}

static bool ensure_ai_core_initialized() {
    Logger::instance().info("[ensure_ai_core_initialized] START");
    
    if (!g_ai_core) {
        Logger::instance().info("[ensure_ai_core_initialized] Creating new AICore instance");
        g_ai_core = std::make_unique<AICore>();
    }
    
    const PluginSettings& settings = SettingsManager::instance().settings();
    g_ai_core->set_taskqueue_batch_size(settings.taskqueue_batch_size);
    g_ai_core->set_ai_batch_size(settings.ai_batch_size);
    
    if (!g_ai_core->is_initialized()) {
        // 设置数据库路径为 {fb2k_profile}/foo_metadata_enhancer.db
        std::string profile_path = core_api::get_profile_path();
        if (profile_path.find("file://") == 0) {
            profile_path = profile_path.substr(7);
        }
        g_ai_core->set_cache_path(profile_path + "\\" + constants::cache_db_name());
        Logger::instance().info("[ensure_ai_core_initialized] AICore not initialized, calling initialize()");
        bool result = g_ai_core->initialize();
        Logger::instance().info("[ensure_ai_core_initialized] initialize() returned: " + std::string(result ? "true" : "false"));
        return result;
    }
    
    Logger::instance().info("[ensure_ai_core_initialized] AICore already initialized, returning true");
    return true;
}

enum class V8MenuItemID {
    STAGE1_SCRAPE = 10,
    STAGE2_ENHANCE = 11,
    ROLLBACK_VERSION = 12,
};

// 前向声明：Stage2 启动函数，实现在 Stage2EnhanceCallback 定义之后
static void launch_stage2_enhance_auto(metadb_handle_list tracks,
                                        std::vector<TrackInput> inputs,
                                        EnhancementOptions options);

class Stage1ScrapeCallback : public threaded_process_callback {
public:
    Stage1ScrapeCallback(metadb_handle_list tracks, std::vector<TrackInput> inputs, ScrapingOptions options,
                         bool chain_to_stage2 = false, EnhancementOptions stage2_options = EnhancementOptions{})
        : m_tracks(tracks)
        , m_inputs(std::move(inputs))
        , m_options(options)
        , m_chain_to_stage2(chain_to_stage2)
        , m_stage2_options(stage2_options) {}
    
    void on_init(HWND p_wnd) override {
        console::print("AI Metadata V8: Stage 1 scraping started...");
    }
    
    void run(threaded_process_status& p_status, abort_callback& p_abort) override {
        int total = static_cast<int>(m_inputs.size());
        
        p_status.set_progress(0, 100);
        p_status.set_title("Stage 1 Scraping - 0%");
        
        auto results = g_ai_core->stage1_scrape_sync(
            m_inputs,
            m_options,
            [this, &p_status, total, &p_abort](int current, int total_tracks, const std::string& message) {
                if (p_abort.is_aborting()) return;
                
                p_status.set_progress(current, total_tracks);
                
                pfc::string8 title;
                title << "Stage 1 Scraping - " << message.c_str();
                p_status.set_title(title);
            },
            [&p_abort]() {
                return p_abort.is_aborting();
            }
        );
        
        if (p_abort.is_aborting()) {
            return;
        }
        
        if (results.empty()) {
            m_error_message = "No results returned from scraping";
            return;
        }
        
        m_results = std::move(results);
    }
    
    void on_done(HWND p_wnd, bool p_was_aborted) override {
        if (p_was_aborted) {
            popup_message::g_show("Scraping cancelled by user", "AI Metadata V8");
            return;
        }
        
        if (!m_error_message.empty()) {
            popup_message::g_show(m_error_message.c_str(), "AI Metadata V8");
            return;
        }
        
        if (m_results.empty()) {
            popup_message::g_show("No results returned from scraping", "AI Metadata V8");
            return;
        }
        
        size_t success_count = 0;
        size_t empty_count = 0;
        for (const auto& result : m_results) {
            if (result.success && !result.scraped_fields.empty()) {
                success_count++;
            } else {
                empty_count++;
            }
        }
        
        {
            std::ostringstream oss;
            oss << "AI Metadata V8: Scraping completed - " << success_count << " successful";
            if (empty_count > 0) {
                oss << ", " << empty_count << " failed (will retry on next run)";
            }
            console::print(oss.str().c_str());
        }
        
        std::vector<bool> selected(m_results.size(), true);

        for (size_t i = 0; i < m_results.size(); ++i) {
            if (!m_results[i].success || m_results[i].scraped_fields.empty()) {
                selected[i] = false;
            }
        }

        // chain 模式跳过确认对话框，自动应用所有成功结果
        if (!m_chain_to_stage2) {
            if (!DialogManager::ShowConfirmResultDialog(core_api::get_main_window(), m_results, selected, m_inputs)) {
                popup_message::g_show("Scraping cancelled by user", "AI Metadata V8");
                return;
            }
        }
        
        int applied = 0;
        metadb_handle_list modified_tracks;
        pfc::list_t<file_info_impl> modified_infos;
        
        for (size_t i = 0; i < m_results.size() && i < m_tracks.get_count() && i < m_inputs.size(); ++i) {
            if (!selected[i]) continue;

            const auto& result = m_results[i];
            if (!result.success) continue;

            metadb_handle_ptr handle = m_tracks.get_item(i);
            file_info_impl info;
            if (handle->get_info(info)) {
                std::map<std::string, std::string> original_snapshot = extract_full_snapshot(info);

                std::set<std::string> sources;
                float total_confidence = 0.0f;
                int confidence_count = 0;

                auto should_scrape_field = [](const std::string& field) -> bool {
                    return ConfirmResultDialog::IsFieldSelected(field);
                };

                for (const auto& [field_name, field_value] : result.scraped_fields) {
                    if (!should_scrape_field(field_name)) {
                        continue;
                    }

                    std::string field_upper = field_name;
                    std::transform(field_upper.begin(), field_upper.end(), field_upper.begin(), ::toupper);

                    if (field_upper == "YEAR") {
                        field_upper = "DATE";
                    }

                    info.meta_set(field_upper.c_str(), field_value.value.c_str());

                    if (field_value.source == DataSourceType::MUSICBRAINZ) sources.insert("musicbrainz");
                    else if (field_value.source == DataSourceType::DISCOGS) sources.insert("discogs");
                    else sources.insert("ai");
                    total_confidence += field_value.confidence;
                    confidence_count++;
                }

                std::string data_source;
                for (const auto& s : sources) {
                    if (!data_source.empty()) data_source += ",";
                    data_source += s;
                }

                std::string confidence_summary;
                if (confidence_count > 0) {
                    float avg = total_confidence / confidence_count;
                    std::ostringstream oss;
                    oss << std::fixed << std::setprecision(2) << avg;
                    confidence_summary = oss.str();
                }

                if (g_ai_core && g_ai_core->is_initialized()) {
                    // 备份刮削前的状态，支持回滚（Scrape 类型）
                    Logger::instance().info("[Stage1] Saving Scrape snapshot for track_id=" + m_inputs[i].track_id);
                    bool snap_ok = g_ai_core->ensure_operation_snapshot(
                        m_inputs[i].track_id,
                        ai_metadata::OperationType::Scrape,
                        original_snapshot
                    );
                    Logger::instance().info("[Stage1] ensure_operation_snapshot(Scrape) returned " +
                        std::string(snap_ok ? "true" : "false") + ", track_id=" + m_inputs[i].track_id);

                    std::string cache_key = g_ai_core->generate_stage1_cache_key(m_inputs[i]);
                    g_ai_core->save_stage1_cache(cache_key, result, m_inputs[i]);
                }

                modified_tracks.add_item(handle);
                modified_infos.add_item(info);
                applied++;
                Logger::instance().info("[Stage1] Added track " + std::to_string(i+1) + " to modified list, applied=" + std::to_string(applied));
            } else {
                Logger::instance().warning("[Stage1] Failed to get info for track " + std::to_string(i+1));
            }
        }
        
        Logger::instance().info("[Stage2] Total modified tracks: " + std::to_string(modified_tracks.get_count()));
        
        if (modified_tracks.get_count() > 0) {
            Logger::instance().info("[Stage1] Writing to fb2k metadata database: " + 
                                   std::to_string(modified_tracks.get_count()) + " tracks");
            batch_update_metadata(modified_tracks, modified_infos);
        }
        
        if (m_chain_to_stage2 && modified_tracks.get_count() > 0) {
            // chain 模式：用已修改的 info 构建 stage2 输入，自动启动增强
            std::vector<TrackInput> stage2_inputs;
            for (size_t i = 0; i < modified_tracks.get_count(); ++i) {
                file_info_impl info;
                if (modified_tracks.get_item(i)->get_info(info)) {
                    stage2_inputs.push_back(extract_track_input(modified_tracks.get_item(i)));
                } else {
                    // fallback: 用 modified_infos 中的数据
                    const auto& fi = modified_infos[i];
                    TrackInput input;
                    const char* path = modified_tracks.get_item(i)->get_path();
                    uint32_t subsong = modified_tracks.get_item(i)->get_subsong_index();
                    t_filestats stats = modified_tracks.get_item(i)->get_filestats();
                    input.track_id = CacheLayer::generate_track_uid(path ? path : "", subsong, stats.m_size);
                    input.file_path = path ? path : "";
                    input.subsong_index = subsong;
                    input.title = fi.meta_get("TITLE", 0) ? fi.meta_get("TITLE", 0) : "";
                    input.artist = fi.meta_get("ARTIST", 0) ? fi.meta_get("ARTIST", 0) : "";
                    input.album = fi.meta_get("ALBUM", 0) ? fi.meta_get("ALBUM", 0) : "";
                    input.album_artist = fi.meta_get("ALBUM ARTIST", 0) ? fi.meta_get("ALBUM ARTIST", 0) : "";
                    const char* date_str = fi.meta_get("DATE", 0);
                    if (date_str && strlen(date_str) > 0) {
                        input.year = date_str;
                        try { input.year_int = std::stoi(date_str); } catch (...) { input.year_int = 0; }
                    }
                    input.genre = fi.meta_get("GENRE", 0) ? fi.meta_get("GENRE", 0) : "";
                    stage2_inputs.push_back(input);
                }
            }

            if (!stage2_inputs.empty()) {
                console::print("AI Metadata V8: Auto-chaining to Stage 2 Enhancement...");
                launch_stage2_enhance_auto(modified_tracks, std::move(stage2_inputs), m_stage2_options);
            }
            return;
        }

        pfc::string8 msg;
        msg << "Scraping complete: " << applied << "/" << m_results.size() << " tracks updated";
        if (m_chain_to_stage2) {
            msg << "\n\nNo tracks to enhance (scraping produced no results).";
        }
        popup_message::g_show(msg, "AI Metadata V8");
    }

private:
    metadb_handle_list m_tracks;
    std::vector<TrackInput> m_inputs;
    ScrapingOptions m_options;
    std::vector<TrackScrapingResult> m_results;
    std::string m_error_message;
    bool m_chain_to_stage2 = false;
    EnhancementOptions m_stage2_options;
};

class Stage2EnhanceCallback : public threaded_process_callback {
public:
    Stage2EnhanceCallback(metadb_handle_list tracks, std::vector<TrackInput> inputs, EnhancementOptions options)
        : m_tracks(tracks)
        , m_inputs(std::move(inputs))
        , m_options(options) {}
    
    void on_init(HWND p_wnd) override {
        console::print("AI Metadata V8: Stage 2 enhancement started...");
    }
    
    void run(threaded_process_status& p_status, abort_callback& p_abort) override {
        int total = static_cast<int>(m_inputs.size());
        
        p_status.set_progress(0, 100);
        p_status.set_title("Stage 2 Enhancement - 0%");
        
        auto results = g_ai_core->stage2_enhance_sync(
            m_inputs,
            m_options,
            [this, &p_status, total, &p_abort](int current, int total_tracks, const std::string& message) {
                if (p_abort.is_aborting()) return;
                
                p_status.set_progress(current, total_tracks);
                
                pfc::string8 title;
                title << "Stage 2 Enhancement - " << message.c_str();
                p_status.set_title(title);
            },
            [&p_abort]() {
                return p_abort.is_aborting();
            }
        );
        
        if (p_abort.is_aborting()) {
            return;
        }
        
        if (results.empty()) {
            m_error_message = "No results returned from enhancement";
            return;
        }
        
        m_results = std::move(results);
    }
    
    void on_done(HWND p_wnd, bool p_was_aborted) override {
        if (p_was_aborted) {
            popup_message::g_show("Enhancement cancelled by user", "AI Metadata V8");
            return;
        }
        
        if (!m_error_message.empty()) {
            popup_message::g_show(m_error_message.c_str(), "AI Metadata V8");
            return;
        }
        
        if (m_results.empty()) {
            popup_message::g_show("No results returned from enhancement", "AI Metadata V8");
            return;
        }
        
        size_t success_count = 0;
        size_t failed_count = 0;
        for (const auto& result : m_results) {
            if (result.success) {
                success_count++;
            } else {
                failed_count++;
            }
        }
        
        {
            std::ostringstream oss;
            oss << "AI Metadata V8: Enhancement completed - " << success_count << " successful";
            if (failed_count > 0) {
                oss << ", " << failed_count << " failed (will retry on next run)";
            }
            console::print(oss.str().c_str());
        }
        
        std::vector<bool> selected(m_results.size(), true);
        
        for (size_t i = 0; i < m_results.size(); ++i) {
            if (!m_results[i].success) {
                selected[i] = false;
            }
        }
        
        if (!DialogManager::ShowEnhanceConfirmDialog(core_api::get_main_window(), m_results, selected, m_options, m_inputs)) {
            popup_message::g_show("Enhancement cancelled by user", "AI Metadata V8");
            return;
        }
        
        int applied = 0;
        metadb_handle_list modified_tracks;
        pfc::list_t<file_info_impl> modified_infos;
        
        for (size_t i = 0; i < m_results.size() && i < m_tracks.get_count(); ++i) {
            if (!selected[i]) continue;
            
            const auto& result = m_results[i];
            if (!result.success) continue;
            
            metadb_handle_ptr handle = m_tracks.get_item(i);
            
            const char* track_path = handle->get_path();
            uint32_t subsong_index = handle->get_subsong_index();
            Logger::instance().info("[Stage2] Processing track " + std::to_string(i+1) + 
                                   ", path=" + (track_path ? track_path : "null") + 
                                   ", subsong=" + std::to_string(subsong_index));
            
            file_info_impl info;
            if (handle->get_info(info)) {
                std::map<std::string, std::string> original_snapshot = extract_full_snapshot(info);
                
                std::string confidence_summary;
                int conf_count = 0;
                float total_conf = 0.0f;
                
                auto should_write_field = [](const std::string& field) -> bool {
                    return EnhanceConfirmDialog::IsFieldSelected(field);
                };
                
                if (should_write_field("title_zh") && !result.title_zh.empty()) {
                    info.meta_set("TITLE_ZH", result.title_zh.c_str());
                    Logger::instance().info("[Stage2] Set TITLE_ZH: " + result.title_zh);
                    if (result.translation_confidence > 0) {
                        total_conf += result.translation_confidence;
                        conf_count++;
                    }
                }
                if (should_write_field("album_zh") && !result.album_zh.empty()) {
                    info.meta_set("ALBUM_ZH", result.album_zh.c_str());
                }
                if (should_write_field("artist_zh") && !result.artist_zh.empty()) {
                    info.meta_set("ARTIST_ZH", result.artist_zh.c_str());
                }
                
                if (conf_count > 0) {
                    std::ostringstream oss;
                    oss << std::fixed << std::setprecision(2) << (total_conf / conf_count);
                    confidence_summary = oss.str();
                }
                
                if (g_ai_core && g_ai_core->is_initialized()) {
                    // 备份 Enhance 前的状态，支持回滚（Translate 类型）
                    Logger::instance().info("[Stage2] Saving Translate snapshot for track_id=" + m_inputs[i].track_id);
                    bool snap_ok = g_ai_core->ensure_operation_snapshot(
                        m_inputs[i].track_id,
                        ai_metadata::OperationType::Translate,
                        original_snapshot
                    );
                    Logger::instance().info("[Stage2] ensure_operation_snapshot(Translate) returned " +
                        std::string(snap_ok ? "true" : "false") + ", track_id=" + m_inputs[i].track_id);

                    std::string cache_key = g_ai_core->generate_stage2_cache_key(m_inputs[i], m_options);
                    g_ai_core->save_stage2_cache(cache_key, result, m_inputs[i], m_options);
                }
                
                modified_tracks.add_item(handle);
                modified_infos.add_item(info);
                applied++;
                Logger::instance().info("[Stage2] Added track to modified list, applied=" + std::to_string(applied));
            } else {
                Logger::instance().warning("[Stage2] Failed to get info for track " + std::to_string(i+1));
            }
        }
        
        Logger::instance().info("[Stage2] Total modified tracks: " + std::to_string(modified_tracks.get_count()));
        
        if (modified_tracks.get_count() > 0) {
            Logger::instance().info("[Stage2] Writing to fb2k metadata database: " + 
                                   std::to_string(modified_tracks.get_count()) + " tracks");
            batch_update_metadata(modified_tracks, modified_infos);
        }
        
        pfc::string8 msg;
        msg << "Stage 2 enhancement complete: " << applied << "/" << m_results.size() << " tracks updated";
        popup_message::g_show(msg, "AI Metadata V8");
    }
    
private:
    metadb_handle_list m_tracks;
    std::vector<TrackInput> m_inputs;
    EnhancementOptions m_options;
    std::vector<EnhancementResult> m_results;
    std::string m_error_message;
};

// 实现 chain 模式：启动 Stage2 增强流程
static void launch_stage2_enhance_auto(metadb_handle_list tracks,
                                        std::vector<TrackInput> inputs,
                                        EnhancementOptions options) {
    service_ptr_t<Stage2EnhanceCallback> cb = new service_impl_t<Stage2EnhanceCallback>(
        tracks, std::move(inputs), options);
    threaded_process::g_run_modeless(cb,
        threaded_process::flag_show_progress | threaded_process::flag_show_abort,
        core_api::get_main_window(), "Enhance Metadata (Auto)");
}

TrackInput extract_track_input(metadb_handle_ptr handle) {
    TrackInput input;
    
    if (handle.is_empty()) {
        return input;
    }
    
    const char* path = handle->get_path();
    uint32_t subsong = handle->get_subsong_index();
    t_filestats stats = handle->get_filestats();
    uint64_t file_size = stats.m_size;
    
    input.track_id = CacheLayer::generate_track_uid(path, subsong, file_size);
    input.subsong_index = subsong;
    
    file_info_impl info;
    if (handle->get_info(info)) {
        const char* val = nullptr;
        
        val = info.meta_get("TITLE", 0);
        if (val) input.title = val;
        
        val = info.meta_get("ALBUM", 0);
        if (val) input.album = val;
        
        val = info.meta_get("ARTIST", 0);
        if (val) input.artist = val;
        
        val = info.meta_get("ALBUM ARTIST", 0);
        if (val) input.album_artist = val;
        
        val = info.meta_get("MUSICBRAINZ_TRACKID", 0);
        if (val) input.musicbrainz_id = val;
        
        input.duration_sec = static_cast<uint32_t>(info.get_length());
        
        val = info.meta_get("TRACKNUMBER", 0);
        if (val) input.track_number = atoi(val);
        
        val = info.meta_get("DISCNUMBER", 0);
        if (val) input.disc_number = atoi(val);
        
        val = info.meta_get("DATE", 0);
        if (val) input.year = val;
        
        val = info.meta_get("GENRE", 0);
        if (val) input.genre_existing = val;
        
        val = info.meta_get("COMMENT", 0);
        if (val) input.comment = val;
        
        val = info.meta_get("LABEL", 0);
        if (val) input.label = val;
        
        val = info.meta_get("LANGUAGE", 0);
        if (val) input.language_hint = val;
    }
    
    input.file_hash = "";
    
    std::string path_str(path ? path : "");
    std::string title_str = input.title.empty() ? "none" : input.title;
    std::string album_str = input.album.empty() ? "none" : input.album;
    std::string artist_str = input.artist.empty() ? "none" : input.artist;
    std::string album_artist_str = input.album_artist.empty() ? "none" : input.album_artist;
    std::string musicbrainz_id_str = input.musicbrainz_id.empty() ? "none" : input.musicbrainz_id;
    std::string year_str = input.year.empty() ? "none" : input.year;
    std::string genre_existing_str = input.genre_existing.empty() ? "none" : input.genre_existing;
    std::string comment_str = input.comment.empty() ? "none" : input.comment;
    std::string label_str = input.label.empty() ? "none" : input.label;
    std::string language_hint_str = input.language_hint.empty() ? "none" : input.language_hint;
    std::string file_hash_str = input.file_hash.empty() ? "none" : input.file_hash;
    
    Logger::instance().debug(std::string("[AI Metadata] [STAGE 1] Original data from foobar2000: ") +
                           "track_id=" + input.track_id + ", " +
                           "path=" + path_str + ", " +
                           "subsong=" + std::to_string(subsong) + ", " +
                           "file_size=" + std::to_string(file_size) + ", " +
                           "title=" + title_str + ", " +
                           "album=" + album_str + ", " +
                           "artist=" + artist_str + ", " +
                           "album_artist=" + album_artist_str + ", " +
                           "musicbrainz_id=" + musicbrainz_id_str + ", " +
                           "duration_sec=" + std::to_string(input.duration_sec) + ", " +
                           "track_number=" + std::to_string(input.track_number) + ", " +
                           "disc_number=" + std::to_string(input.disc_number) + ", " +
                           "year=" + year_str + ", " +
                           "genre_existing=" + genre_existing_str + ", " +
                           "comment=" + comment_str + ", " +
                           "label=" + label_str + ", " +
                           "language_hint=" + language_hint_str + ", " +
                           "file_hash=" + file_hash_str + ", " +
                           "translate_metadata=" + std::to_string(input.options.translate_metadata));
    
    return input;
}

AICore* get_ai_core_instance() {
    if (!g_ai_core) {
        g_ai_core = std::make_unique<AICore>();
    }
    return g_ai_core.get();
}

bool restart_all_workers() {
    AICore* core = get_ai_core_instance();
    if (!core || !core->is_initialized()) {
        return false;
    }
    return core->restart_all_workers();
}

bool are_workers_healthy() {
    AICore* core = get_ai_core_instance();
    if (!core || !core->is_initialized()) {
        return false;
    }
    return core->is_worker_healthy();
}

static const GUID guid_ai_metadata = 
    { 0x11111111, 0x2222, 0x3333, { 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb } };
static const GUID guid_stage1_scrape = 
    { 0x6f708192, 0x34a5, 0x6789, { 0x4a, 0x5b, 0x6c, 0x7d, 0x8e, 0x9f, 0xa0, 0xb1 } };
static const GUID guid_stage2_enhance =
    { 0x70819234, 0xa567, 0x89ab, { 0x5b, 0x6c, 0x7d, 0x8e, 0x9f, 0xa0, 0xb1, 0xc2 } };
static const GUID guid_scrape_and_enhance =
    { 0x81923456, 0x6789, 0xabcd, { 0x6c, 0x7d, 0x8e, 0x9f, 0xa0, 0xb1, 0xc2, 0xd3 } };
static const GUID guid_rollback_initial = 
    { 0xb456789a, 0x3456, 0x789a, { 0xbc, 0xde, 0xf0, 0x12, 0x34, 0x56, 0x78, 0x9a } };
static const GUID guid_cache_stats = 
    { 0x92345678, 0x1234, 0x5678, { 0x9a, 0xbc, 0xde, 0xf0, 0x12, 0x34, 0x56, 0x78 } };
static const GUID guid_clear_cache =
    { 0xa3456789, 0x2345, 0x6789, { 0xab, 0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89 } };
static const GUID guid_normalize =
    { 0xc456789a, 0xbcd0, 0x1234, { 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0, 0x12, 0x34 } };

class V8MenuHandler : public contextmenu_item_v2 {
public:
    unsigned get_num_items() override { return 1; }
    GUID get_item_guid(unsigned p_index) override { return guid_ai_metadata; }
    void get_item_name(unsigned p_index, pfc::string_base& p_out) override { p_out = "AI Metadata"; }
    bool get_item_description(unsigned p_index, pfc::string_base& p_out) override {
        p_out = "AI Metadata - Scrape, Enhance, Translate";
        return true;
    }
    double get_sort_priority() override { return static_cast<double>(contextmenu_priorities::root_tagging) + 0.5; }
    GUID get_parent() override { return contextmenu_groups::root; }
    contextmenu_item_node_root* instantiate_item(unsigned p_index, metadb_handle_list_cref p_data, const GUID& p_caller) override;
    void item_execute_simple(unsigned p_index, const GUID& p_node, metadb_handle_list_cref p_data, const GUID& p_caller) override {}
    
    void stage1_scrape(metadb_handle_list_cref p_data);
    void stage2_enhance(metadb_handle_list_cref p_data);
    void scrape_and_enhance(metadb_handle_list_cref p_data);
    void normalize_metadata(metadb_handle_list_cref p_data);
    void rollback_to_initial(metadb_handle_list_cref p_data);
    void show_cache_stats();
    void clear_cache(metadb_handle_list_cref p_data);
    
    t_enabled_state get_enabled_state(unsigned p_index) override {
        (void)p_index;
        return DEFAULT_ON;
    }
    
private:
    class MenuNodeRoot : public contextmenu_item_node_root_popup {
    public:
        MenuNodeRoot() {}
        bool get_display_data(pfc::string_base& p_out, unsigned& p_displayflags, metadb_handle_list_cref p_data, const GUID& p_caller) override {
            (void)p_data; (void)p_caller;
            p_displayflags = 0;
            p_out = "AI Metadata";
            return true;
        }
        t_type get_type() override { return TYPE_POPUP; }
        void execute(metadb_handle_list_cref p_data, const GUID& p_caller) override {}
        bool get_description(pfc::string_base& p_out) override { p_out = "AI Metadata"; return true; }
        GUID get_guid() override { return guid_ai_metadata; }
        bool is_mappable_shortcut() override { return false; }
        t_size get_children_count() override { return m_children.size(); }
        contextmenu_item_node* get_child(t_size p_index) override {
            return p_index < m_children.size() ? m_children[p_index] : nullptr;
        }
        void add_child(contextmenu_item_node* child) { m_children.push_back(child); }
    private:
        std::vector<contextmenu_item_node*> m_children;
    };
    
    class MenuNodeCommand : public contextmenu_item_node_leaf {
    public:
        MenuNodeCommand(const char* name, const GUID& guid, const char* desc, 
                        std::function<void(metadb_handle_list_cref)> func, bool enabled = true)
            : m_name(name), m_guid(guid), m_desc(desc), m_func(func), m_enabled(enabled) {}
        virtual ~MenuNodeCommand() {}
        bool get_display_data(pfc::string_base& p_out, unsigned& p_displayflags, metadb_handle_list_cref p_data, const GUID& p_caller) override {
            (void)p_caller;
            p_out = m_name;
            p_displayflags = (!m_enabled || p_data.get_count() == 0) ? FLAG_DISABLED : 0;
            return true;
        }
        t_type get_type() override { return TYPE_COMMAND; }
        void execute(metadb_handle_list_cref p_data, const GUID& p_caller) override {
            (void)p_caller;
            if (m_func) m_func(p_data);
        }
        bool get_description(pfc::string_base& p_out) override {
            if (m_desc && strlen(m_desc) > 0) { p_out = m_desc; return true; }
            return false;
        }
        GUID get_guid() override { return m_guid; }
        bool is_mappable_shortcut() override { return true; }
    private:
        const char* m_name;
        const GUID m_guid;
        const char* m_desc;
        std::function<void(metadb_handle_list_cref)> m_func;
        bool m_enabled;
    };
    
    class MenuNodeSeparator : public contextmenu_item_node_separator {
    public:
        virtual ~MenuNodeSeparator() {}
    };
};

contextmenu_item_node_root* V8MenuHandler::instantiate_item(unsigned p_index, metadb_handle_list_cref p_data, const GUID& p_caller) {
    (void)p_index; (void)p_caller;
    auto root = new MenuNodeRoot();
    bool has_selection = p_data.get_count() > 0;

    // 主操作组：刮削 + 增强
    root->add_child(new MenuNodeCommand("Scrape Metadata", guid_stage1_scrape,
        "Scrape basic metadata (title, artist, album, year, etc.) from MusicBrainz, Discogs, and AI sources.",
        [this](metadb_handle_list_cref data) { stage1_scrape(data); }, has_selection));
    root->add_child(new MenuNodeCommand("Enhance Metadata", guid_stage2_enhance,
        "Enhance metadata: translate title/album/artist to Chinese based on existing tags.",
        [this](metadb_handle_list_cref data) { stage2_enhance(data); }, has_selection));
    root->add_child(new MenuNodeCommand("Scrape & Enhance (Auto)", guid_scrape_and_enhance,
        "Run Scrape then Enhance automatically in one step. Skips intermediate confirmation dialogs.",
        [this](metadb_handle_list_cref data) { scrape_and_enhance(data); }, has_selection));

    root->add_child(new MenuNodeSeparator());

    // Normalize 组
    root->add_child(new MenuNodeCommand("Normalize...", guid_normalize,
        "Normalize metadata: unify different writings of the same entity (e.g. BEYOND vs Beyond). "
        "Opens a field selection dialog, then uses SQLite knowledge base + AI to suggest canonical names.",
        [this](metadb_handle_list_cref data) { normalize_metadata(data); }, has_selection));

    root->add_child(new MenuNodeSeparator());

    // 回滚组
    root->add_child(new MenuNodeCommand("Rollback", guid_rollback_initial,
        "Rollback selected operations: choose which to undo (scrape / enhance / normalize).",
        [this](metadb_handle_list_cref data) { rollback_to_initial(data); }, has_selection));

    root->add_child(new MenuNodeSeparator());

    // 工具组
    root->add_child(new MenuNodeCommand("Cache Statistics", guid_cache_stats,
        "Show cache statistics: total entries, hit rate, database size, API calls saved.",
        [this](metadb_handle_list_cref data) { (void)data; show_cache_stats(); }, true));
    root->add_child(new MenuNodeCommand("Clear Cache", guid_clear_cache,
        "Clear cached metadata for selected tracks or the entire cache.",
        [this](metadb_handle_list_cref data) { clear_cache(data); }, true));
    return root;
}

void V8MenuHandler::stage1_scrape(metadb_handle_list_cref p_data) {
    Logger::instance().info("[V8MenuHandler] stage1_scrape: CALLED, track count = " + std::to_string(p_data.get_count()));
    console::print("AI Metadata V8: stage1_scrape called");
    
    if (!ensure_ai_core_initialized()) {
        Logger::instance().error("[V8MenuHandler] stage1_scrape: ensure_ai_core_initialized FAILED");
        popup_message::g_show("Failed to initialize AI core", "AI Metadata V8");
        return;
    }
    Logger::instance().info("[V8MenuHandler] stage1_scrape: AI core initialized successfully");
    
    std::vector<MissingFieldInfo> missing;
    for (size_t i = 0; i < p_data.get_count(); ++i) {
        file_info_impl info;
        if (p_data.get_item(i)->get_info(info)) {
            const char* title = info.meta_get("TITLE", 0);
            const char* artist = info.meta_get("ARTIST", 0);
            if (!title || strlen(title) == 0 || !artist || strlen(artist) == 0) {
                MissingFieldInfo mfi;
                const char* path = p_data.get_item(i)->get_path();
                mfi.track_id = path ? path : "";
                if (!title || strlen(title) == 0) mfi.missing_fields.push_back("TITLE");
                if (!artist || strlen(artist) == 0) mfi.missing_fields.push_back("ARTIST");
                missing.push_back(mfi);
            }
        }
    }
    
    if (!missing.empty()) {
        pfc::string8 msg;
        msg << "Cannot scrape: " << missing.size() << " track(s) missing required fields:\n\n";
        for (size_t i = 0; i < missing.size() && i < 5; ++i) {
            msg << missing[i].track_id.c_str() << ": missing ";
            for (size_t j = 0; j < missing[i].missing_fields.size(); ++j) {
                if (j > 0) msg << ", ";
                msg << missing[i].missing_fields[j].c_str();
            }
            msg << "\n";
        }
        if (missing.size() > 5) msg << "... and " << (missing.size() - 5) << " more";
        popup_message::g_show(msg, "AI Metadata - Missing Fields");
        return;
    }
    
    ScrapingOptions options;
    options.enable_musicbrainz = SettingsManager::instance().settings().enable_musicbrainz;
    options.enable_discogs = SettingsManager::instance().settings().enable_discogs;
    options.enable_ai = SettingsManager::instance().settings().enable_ai;
    if (!DialogManager::ShowScrapingOptionsDialog(core_api::get_main_window(), options)) return;
    
    std::vector<TrackInput> inputs;
    for (size_t i = 0; i < p_data.get_count(); ++i) {
        file_info_impl info;
        if (p_data.get_item(i)->get_info(info)) {
            TrackInput input;
            const char* path = p_data.get_item(i)->get_path();
            uint32_t subsong = p_data.get_item(i)->get_subsong_index();
            t_filestats stats = p_data.get_item(i)->get_filestats();
            uint64_t file_size = stats.m_size;
            input.track_id = CacheLayer::generate_track_uid(path ? path : "", subsong, file_size);
            input.file_path = path ? path : "";
            input.subsong_index = subsong;
            input.title = info.meta_get("TITLE", 0) ? info.meta_get("TITLE", 0) : "";
            input.artist = info.meta_get("ARTIST", 0) ? info.meta_get("ARTIST", 0) : "";
            input.album = info.meta_get("ALBUM", 0) ? info.meta_get("ALBUM", 0) : "";
            input.album_artist = info.meta_get("ALBUM ARTIST", 0) ? info.meta_get("ALBUM ARTIST", 0) : "";
            const char* date_str = info.meta_get("DATE", 0);
            if (date_str && strlen(date_str) > 0) {
                input.year = date_str;
                try { input.year_int = std::stoi(date_str); } catch (...) { input.year_int = 0; }
            }
            const char* track_str = info.meta_get("TRACKNUMBER", 0);
            if (track_str && strlen(track_str) > 0) {
                try { input.track_number = std::stoi(track_str); } catch (...) { input.track_number = 0; }
            }
            const char* disc_str = info.meta_get("DISCNUMBER", 0);
            if (disc_str && strlen(disc_str) > 0) {
                try { input.disc_number = std::stoi(disc_str); } catch (...) { input.disc_number = 0; }
            }
            input.genre = info.meta_get("GENRE", 0) ? info.meta_get("GENRE", 0) : "";
            input.composer = info.meta_get("COMPOSER", 0) ? info.meta_get("COMPOSER", 0) : "";
            input.lyricist = info.meta_get("LYRICIST", 0) ? info.meta_get("LYRICIST", 0) : "";
            input.conductor = info.meta_get("CONDUCTOR", 0) ? info.meta_get("CONDUCTOR", 0) : "";
            input.performer = info.meta_get("PERFORMER", 0) ? info.meta_get("PERFORMER", 0) : "";
            input.label = info.meta_get("LABEL", 0) ? info.meta_get("LABEL", 0) : "";
            inputs.push_back(input);
        }
    }
    
    service_ptr_t<Stage1ScrapeCallback> callback = new service_impl_t<Stage1ScrapeCallback>(p_data, std::move(inputs), options);
    threaded_process::g_run_modeless(callback, threaded_process::flag_show_progress | threaded_process::flag_show_abort,
        core_api::get_main_window(), "Stage 1: Scrape Metadata");
}

void V8MenuHandler::stage2_enhance(metadb_handle_list_cref p_data) {
    Logger::instance().info("[V8MenuHandler] stage2_enhance: CALLED, track count = " + std::to_string(p_data.get_count()));
    console::print("AI Metadata V8: stage2_enhance called");
    
    if (!ensure_ai_core_initialized()) {
        Logger::instance().error("[V8MenuHandler] stage2_enhance: ensure_ai_core_initialized FAILED");
        popup_message::g_show("Failed to initialize AI core", "AI Metadata V8");
        return;
    }
    Logger::instance().info("[V8MenuHandler] stage2_enhance: AI core initialized successfully");
    
    EnhancementOptions options;
    if (!DialogManager::ShowEnhancementOptionsDialog(core_api::get_main_window(), options)) return;
    
    std::vector<TrackInput> inputs;
    for (size_t i = 0; i < p_data.get_count(); ++i) {
        file_info_impl info;
        if (p_data.get_item(i)->get_info(info)) {
            TrackInput input;
            const char* path = p_data.get_item(i)->get_path();
            uint32_t subsong = p_data.get_item(i)->get_subsong_index();
            t_filestats stats = p_data.get_item(i)->get_filestats();
            uint64_t file_size = stats.m_size;
            input.track_id = CacheLayer::generate_track_uid(path ? path : "", subsong, file_size);
            input.file_path = path ? path : "";
            input.subsong_index = subsong;
            input.title = info.meta_get("TITLE", 0) ? info.meta_get("TITLE", 0) : "";
            input.artist = info.meta_get("ARTIST", 0) ? info.meta_get("ARTIST", 0) : "";
            input.album = info.meta_get("ALBUM", 0) ? info.meta_get("ALBUM", 0) : "";
            input.album_artist = info.meta_get("ALBUM ARTIST", 0) ? info.meta_get("ALBUM ARTIST", 0) : "";
            const char* date_str = info.meta_get("DATE", 0);
            if (date_str && strlen(date_str) > 0) {
                input.year = date_str;
                try { input.year_int = std::stoi(date_str); } catch (...) { input.year_int = 0; }
            }
            input.genre = info.meta_get("GENRE", 0) ? info.meta_get("GENRE", 0) : "";
            inputs.push_back(input);
        }
    }
    
    service_ptr_t<Stage2EnhanceCallback> callback = new service_impl_t<Stage2EnhanceCallback>(p_data, std::move(inputs), options);
    threaded_process::g_run_modeless(callback, threaded_process::flag_show_progress | threaded_process::flag_show_abort,
        core_api::get_main_window(), "Stage 2: Enhance Metadata");
}

void V8MenuHandler::scrape_and_enhance(metadb_handle_list_cref p_data) {
    Logger::instance().info("[V8MenuHandler] scrape_and_enhance: CALLED, track count = " + std::to_string(p_data.get_count()));
    console::print("AI Metadata V8: scrape_and_enhance (auto chain) called");

    if (!ensure_ai_core_initialized()) {
        Logger::instance().error("[V8MenuHandler] scrape_and_enhance: ensure_ai_core_initialized FAILED");
        popup_message::g_show("Failed to initialize AI core", "AI Metadata V8");
        return;
    }

    // 检查必填字段（同 stage1_scrape）
    std::vector<MissingFieldInfo> missing;
    for (size_t i = 0; i < p_data.get_count(); ++i) {
        file_info_impl info;
        if (p_data.get_item(i)->get_info(info)) {
            const char* title = info.meta_get("TITLE", 0);
            const char* artist = info.meta_get("ARTIST", 0);
            if (!title || strlen(title) == 0 || !artist || strlen(artist) == 0) {
                MissingFieldInfo mfi;
                const char* path = p_data.get_item(i)->get_path();
                mfi.track_id = path ? path : "";
                if (!title || strlen(title) == 0) mfi.missing_fields.push_back("TITLE");
                if (!artist || strlen(artist) == 0) mfi.missing_fields.push_back("ARTIST");
                missing.push_back(mfi);
            }
        }
    }
    if (!missing.empty()) {
        pfc::string8 msg;
        msg << "Cannot scrape: " << missing.size() << " track(s) missing required fields:\n\n";
        for (size_t i = 0; i < missing.size() && i < 5; ++i) {
            msg << missing[i].track_id.c_str() << ": missing ";
            for (size_t j = 0; j < missing[i].missing_fields.size(); ++j) {
                if (j > 0) msg << ", ";
                msg << missing[i].missing_fields[j].c_str();
            }
            msg << "\n";
        }
        if (missing.size() > 5) msg << "... and " << (missing.size() - 5) << " more";
        popup_message::g_show(msg, "AI Metadata - Missing Fields");
        return;
    }

    // 刮削选项对话框（让用户选择数据源）
    ScrapingOptions options;
    options.enable_musicbrainz = SettingsManager::instance().settings().enable_musicbrainz;
    options.enable_discogs = SettingsManager::instance().settings().enable_discogs;
    options.enable_ai = SettingsManager::instance().settings().enable_ai;
    if (!DialogManager::ShowScrapingOptionsDialog(core_api::get_main_window(), options)) return;

    // 增强选项对话框（让用户选择翻译等）
    EnhancementOptions stage2_options;
    if (!DialogManager::ShowEnhancementOptionsDialog(core_api::get_main_window(), stage2_options)) return;

    // 构建 stage1 输入
    std::vector<TrackInput> inputs;
    for (size_t i = 0; i < p_data.get_count(); ++i) {
        file_info_impl info;
        if (p_data.get_item(i)->get_info(info)) {
            TrackInput input;
            const char* path = p_data.get_item(i)->get_path();
            uint32_t subsong = p_data.get_item(i)->get_subsong_index();
            t_filestats stats = p_data.get_item(i)->get_filestats();
            uint64_t file_size = stats.m_size;
            input.track_id = CacheLayer::generate_track_uid(path ? path : "", subsong, file_size);
            input.file_path = path ? path : "";
            input.subsong_index = subsong;
            input.title = info.meta_get("TITLE", 0) ? info.meta_get("TITLE", 0) : "";
            input.artist = info.meta_get("ARTIST", 0) ? info.meta_get("ARTIST", 0) : "";
            input.album = info.meta_get("ALBUM", 0) ? info.meta_get("ALBUM", 0) : "";
            input.album_artist = info.meta_get("ALBUM ARTIST", 0) ? info.meta_get("ALBUM ARTIST", 0) : "";
            const char* date_str = info.meta_get("DATE", 0);
            if (date_str && strlen(date_str) > 0) {
                input.year = date_str;
                try { input.year_int = std::stoi(date_str); } catch (...) { input.year_int = 0; }
            }
            const char* track_str = info.meta_get("TRACKNUMBER", 0);
            if (track_str && strlen(track_str) > 0) {
                try { input.track_number = std::stoi(track_str); } catch (...) { input.track_number = 0; }
            }
            const char* disc_str = info.meta_get("DISCNUMBER", 0);
            if (disc_str && strlen(disc_str) > 0) {
                try { input.disc_number = std::stoi(disc_str); } catch (...) { input.disc_number = 0; }
            }
            input.genre = info.meta_get("GENRE", 0) ? info.meta_get("GENRE", 0) : "";
            input.composer = info.meta_get("COMPOSER", 0) ? info.meta_get("COMPOSER", 0) : "";
            input.lyricist = info.meta_get("LYRICIST", 0) ? info.meta_get("LYRICIST", 0) : "";
            input.conductor = info.meta_get("CONDUCTOR", 0) ? info.meta_get("CONDUCTOR", 0) : "";
            input.performer = info.meta_get("PERFORMER", 0) ? info.meta_get("PERFORMER", 0) : "";
            input.label = info.meta_get("LABEL", 0) ? info.meta_get("LABEL", 0) : "";
            inputs.push_back(input);
        }
    }

    // 启动 stage1，chain_to_stage2=true，自动触发 stage2
    service_ptr_t<Stage1ScrapeCallback> callback = new service_impl_t<Stage1ScrapeCallback>(
        p_data, std::move(inputs), options, true, stage2_options);
    threaded_process::g_run_modeless(callback, threaded_process::flag_show_progress | threaded_process::flag_show_abort,
        core_api::get_main_window(), "Scrape & Enhance (Auto) - Stage 1");
}

void V8MenuHandler::rollback_to_initial(metadb_handle_list_cref p_data) {
    Logger::instance().info("[Rollback] ===== START ROLLBACK =====");
    Logger::instance().info("[Rollback] track count = " + std::to_string(p_data.get_count()));

    if (p_data.get_count() == 0) {
        popup_message::g_show("No tracks selected", "AI Metadata V8");
        return;
    }
    if (!ensure_ai_core_initialized()) {
        popup_message::g_show("Failed to initialize AI core", "AI Metadata V8");
        return;
    }

    // 1. 计算每个 track 的 track_id，并收集
    std::vector<std::string> track_ids;
    std::vector<metadb_handle_ptr> handles;
    track_ids.reserve(p_data.get_count());
    handles.reserve(p_data.get_count());

    for (size_t i = 0; i < p_data.get_count(); ++i) {
        metadb_handle_ptr handle = p_data.get_item(i);
        const char* path = handle->get_path();
        t_uint32 subsong = handle->get_subsong_index();
        t_filesize file_size = handle->get_filesize();
        std::string track_id = CacheLayer::generate_track_uid(path ? path : "", subsong,
            file_size != foobar2000_io::filesize_invalid ? file_size : 0);
        track_ids.push_back(track_id);
        handles.push_back(handle);
        Logger::instance().info("[Rollback] track[" + std::to_string(i) + "] track_id=" + track_id +
            ", path=" + (path ? path : "<null>") + ", subsong=" + std::to_string(subsong) +
            ", file_size=" + std::to_string(file_size));
    }

    // 2. 批量查询每个 track 拥有哪些可回滚的操作类型
    Logger::instance().info("[Rollback] Querying operations for " + std::to_string(track_ids.size()) + " track_ids");
    auto ops_per_track = g_ai_core->get_operations_for_tracks(track_ids);
    Logger::instance().info("[Rollback] get_operations_for_tracks returned " +
        std::to_string(ops_per_track.size()) + " entries");

    // 统计每种类型有多少 track 可回滚
    std::map<ai_metadata::OperationType, int> type_counts;
    std::set<ai_metadata::OperationType> available_set;
    for (const auto& [tid, ops] : ops_per_track) {
        for (auto op : ops) {
            type_counts[op]++;
            available_set.insert(op);
            Logger::instance().info("[Rollback] track_id=" + tid + " has op=" +
                std::string(ai_metadata::operation_type_to_string(op)));
        }
    }

    if (available_set.empty()) {
        Logger::instance().warning("[Rollback] No snapshots found - showing popup and returning");
        popup_message::g_show("No rollback snapshots found for any selected track.\n"
                              "Please run Scrape / Enhance / Normalize first to create snapshots.",
                              "AI Metadata V8");
        return;
    }

    // 3. 弹出多选对话框，让用户选要回滚哪些类型
    std::vector<ai_metadata::OperationType> available_types(available_set.begin(), available_set.end());
    Logger::instance().info("[Rollback] Available types count=" + std::to_string(available_types.size()));
    // 排序：按枚举值固定顺序
    std::sort(available_types.begin(), available_types.end(),
        [](ai_metadata::OperationType a, ai_metadata::OperationType b) {
            return static_cast<int>(a) < static_cast<int>(b);
        });

    std::vector<ai_metadata::OperationType> selected_types;
    Logger::instance().info("[Rollback] Opening RollbackTypeDialog");
    bool dialog_ok = RollbackTypeDialog::Show(core_api::get_main_window(),
                                              available_types, type_counts, selected_types);
    Logger::instance().info("[Rollback] Dialog returned " + std::string(dialog_ok ? "true" : "false") +
        ", selected_types count=" + std::to_string(selected_types.size()) +
        ", last_dialog_result=" + std::to_string((INT_PTR)RollbackTypeDialog::s_last_dialog_result) +
        ", last_error_code=" + std::to_string(RollbackTypeDialog::s_last_error_code));
    if (!dialog_ok) {
        // 区分用户取消和对话框创建失败，提供更准确的反馈
        if (RollbackTypeDialog::s_last_dialog_result == -1) {
            std::string err_msg = "Failed to open rollback dialog (error code: " +
                                  std::to_string(RollbackTypeDialog::s_last_error_code) + ").\n"
                                  "This may indicate the dialog resource is missing or the DLL is outdated.\n"
                                  "Please rebuild the plugin or check logs/core.log for details.";
            popup_message::g_show(err_msg.c_str(), "AI Metadata V8 - Dialog Error");
        } else {
            popup_message::g_show("Rollback cancelled", "AI Metadata V8");
        }
        return;
    }

    if (selected_types.empty()) {
        popup_message::g_show("No rollback type selected.", "AI Metadata V8");
        return;
    }

    // 4. 按选中的类型逐类型回滚
    // 每个类型独立处理：从该类型的快照中恢复对应字段
    // 多类型合并到同一 info 上：依次应用各类型的字段恢复
    int success_count = 0, no_backup_count = 0, fail_count = 0;
    metadb_handle_list modified_tracks;
    pfc::list_t<file_info_impl> modified_infos;

    for (size_t i = 0; i < handles.size(); ++i) {
        metadb_handle_ptr handle = handles[i];
        const std::string& track_id = track_ids[i];

        Logger::instance().info("[Rollback] Processing track " + std::to_string(i + 1) + "/" +
            std::to_string(handles.size()) + ", track_id=" + track_id);

        file_info_impl info;
        if (!handle->get_info(info)) {
            fail_count++;
            Logger::instance().error("[Rollback] Failed to get info for track_id=" + track_id);
            continue;
        }

        bool any_applied = false;
        for (auto op_type : selected_types) {
            auto snapshot_opt = g_ai_core->rollback_operation(track_id, op_type);
            if (!snapshot_opt.has_value() || snapshot_opt->empty()) {
                Logger::instance().info("[Rollback] No snapshot for op=" +
                    std::string(ai_metadata::operation_type_to_string(op_type)) +
                    ", track_id=" + track_id);
                continue;
            }

            // 部分回滚：仅覆盖该操作影响的字段
            auto fields = get_operation_fields(op_type);
            apply_partial_snapshot_to_info(info, snapshot_opt.value(), fields);
            any_applied = true;
            Logger::instance().info("[Rollback] Applied op=" +
                std::string(ai_metadata::operation_type_to_string(op_type)) +
                ", track_id=" + track_id +
                ", fields=" + std::to_string(snapshot_opt->size()));
        }

        if (any_applied) {
            modified_tracks.add_item(handle);
            modified_infos.add_item(info);
            success_count++;
        } else {
            no_backup_count++;
        }
    }

    if (modified_tracks.get_count() > 0) {
        Logger::instance().info("[Rollback] Writing to fb2k metadata database: " +
            std::to_string(modified_tracks.get_count()) + " tracks");
        batch_update_metadata(modified_tracks, modified_infos);
    }

    Logger::instance().info("[Rollback] ===== END ROLLBACK =====");
    Logger::instance().info("[Rollback] success=" + std::to_string(success_count) +
        ", no_backup=" + std::to_string(no_backup_count) + ", failed=" + std::to_string(fail_count));

    // 汇总消息包含选中的类型
    std::ostringstream oss;
    oss << "Rollback complete.\n\nSelected types: ";
    for (size_t i = 0; i < selected_types.size(); ++i) {
        if (i > 0) oss << ", ";
        oss << ai_metadata::operation_type_to_string(selected_types[i]);
    }
    oss << "\n\nRolled back: " << success_count
        << "\nNo snapshot found: " << no_backup_count
        << "\nFailed: " << fail_count;
    popup_message::g_show(oss.str().c_str(), "Rollback");
}

void V8MenuHandler::show_cache_stats() {
    Logger::instance().info("[V8MenuHandler] show_cache_stats: CALLED");
    console::print("AI Metadata V8: show_cache_stats called");
    
    if (!ensure_ai_core_initialized()) {
        Logger::instance().error("[V8MenuHandler] show_cache_stats: AI core not initialized");
        popup_message::g_show("AI Core not initialized", "AI Metadata V8");
        return;
    }
    
    auto stats = g_ai_core->get_cache_statistics();
    pfc::string8 msg;
    msg << "Cache Statistics:\n\nTotal Entries: " << stats.total_entries << "\nCache Hits: " << stats.total_hits
        << "\nCache Misses: " << stats.total_misses << "\nHit Rate: " << stats.hit_rate 
        << "%\nDatabase Size: " << stats.db_size_mb << " MB\nAPI Calls Saved: " << stats.api_calls_saved;
    popup_message::g_show(msg, "Cache Statistics");
}

struct ClearCacheDialogParams {
    bool clear_all;
    int selected_count;
};

static INT_PTR CALLBACK ClearCacheDlgProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) {
    static ClearCacheDialogParams* params = nullptr;
    
    switch (msg) {
        case WM_INITDIALOG: {
            params = reinterpret_cast<ClearCacheDialogParams*>(lp);
            CheckDlgButton(wnd, IDC_CLEAR_ALL_CACHE, BST_UNCHECKED);
            
            std::ostringstream oss;
            oss << "Clear cache for " << params->selected_count << " selected track(s).";
            SetDlgItemTextA(wnd, IDC_STATIC, oss.str().c_str());
            return TRUE;
        }
        case WM_COMMAND:
            switch (LOWORD(wp)) {
                case IDOK: {
                    params->clear_all = (IsDlgButtonChecked(wnd, IDC_CLEAR_ALL_CACHE) == BST_CHECKED);
                    EndDialog(wnd, IDOK);
                    return TRUE;
                }
                case IDCANCEL:
                    EndDialog(wnd, IDCANCEL);
                    return TRUE;
            }
            break;
    }
    return FALSE;
}

void V8MenuHandler::clear_cache(metadb_handle_list_cref p_data) {
    Logger::instance().info("[V8MenuHandler] clear_cache: CALLED");
    console::print("AI Metadata V8: clear_cache called");
    
    if (!ensure_ai_core_initialized()) {
        Logger::instance().error("[V8MenuHandler] clear_cache: AI core not initialized");
        popup_message::g_show("AI Core not initialized", "AI Metadata V8");
        return;
    }
    
    ClearCacheDialogParams params;
    params.clear_all = false;
    params.selected_count = static_cast<int>(p_data.get_count());
    
    INT_PTR result = DialogBoxParam(
        core_api::get_my_instance(),
        MAKEINTRESOURCE(IDD_CLEAR_CACHE),
        core_api::get_main_window(),
        ClearCacheDlgProc,
        reinterpret_cast<LPARAM>(&params)
    );
    
    if (result != IDOK) {
        Logger::instance().info("[V8MenuHandler] clear_cache: User cancelled");
        return;
    }
    
    if (params.clear_all) {
        Logger::instance().info("[V8MenuHandler] clear_cache: Clearing ALL cache");
        g_ai_core->clear_cache();
        popup_message::g_show("All cache cleared successfully", "AI Metadata V8");
    } else {
        std::vector<std::string> track_ids;
        for (size_t i = 0; i < p_data.get_count(); ++i) {
            metadb_handle_ptr handle = p_data.get_item(i);
            const char* path = handle->get_path();
            uint32_t subsong = handle->get_subsong_index();
            t_filestats stats = handle->get_filestats();
            uint64_t file_size = stats.m_size;
            
            if (path && strlen(path) > 0) {
                std::string track_id = CacheLayer::generate_track_uid(path, subsong, file_size);
                track_ids.push_back(track_id);
            }
        }
        
        if (track_ids.empty()) {
            popup_message::g_show("No tracks selected", "AI Metadata V8");
            return;
        }
        
        Logger::instance().info("[V8MenuHandler] clear_cache: Clearing cache for " + std::to_string(track_ids.size()) + " tracks");
        int deleted = g_ai_core->clear_cache_by_track_ids(track_ids);
        
        std::ostringstream oss;
        oss << "Cleared " << deleted << " cache entries for " << track_ids.size() << " track(s)";
        popup_message::g_show(oss.str().c_str(), "AI Metadata V8");
    }
}

// ==================== Normalize ====================

class NormalizeCallback : public threaded_process_callback {
public:
    NormalizeCallback(metadb_handle_list tracks, std::vector<TrackInput> inputs, NormalizeOptions options)
        : m_tracks(tracks)
        , m_inputs(std::move(inputs))
        , m_options(options) {
        // 读取每个 track 当前 field 的所有 values（multi-value 支持），
        // 传给 Python 用于构造 track_updates。Python 端用 normalize_key 匹配，
        // 避免 C++ 端因 Unicode 表示差异（NFC/NFD 韩文、尾部全角空格等）
        // 导致 alias_to_canonical.find(val) 漏匹配。
        std::string tag_field = m_options.field;
        std::transform(tag_field.begin(), tag_field.end(), tag_field.begin(), ::toupper);
        if (tag_field == "ALBUM_ARTIST") tag_field = "ALBUM ARTIST";

        m_tag_field_upper = tag_field;
        m_track_field_values.reserve(m_tracks.get_count());
        for (size_t i = 0; i < m_tracks.get_count(); ++i) {
            file_info_impl info;
            std::vector<std::string> values;
            if (m_tracks.get_item(i)->get_info(info)) {
                const t_size value_count = info.meta_get_count_by_name(tag_field.c_str());
                for (t_size v = 0; v < value_count; ++v) {
                    const char* val = info.meta_get(tag_field.c_str(), v);
                    if (val && strlen(val) > 0) {
                        values.push_back(val);
                    }
                }
            }
            m_track_field_values.push_back(std::move(values));
        }
    }

    void on_init(HWND p_wnd) override {
        console::print("AI Metadata V8: Normalize started...");
    }

    void run(threaded_process_status& p_status, abort_callback& p_abort) override {
        p_status.set_progress(0, 100);
        p_status.set_title("Normalize - querying knowledge base + AI");

        auto result = g_ai_core->normalize_sync(
            m_inputs,
            m_options,
            m_track_field_values,
            [this, &p_status, &p_abort](int current, int total, const std::string& message) {
                if (p_abort.is_aborting()) return;
                p_status.set_progress(current, total);
                pfc::string8 title;
                title << "Normalize - " << message.c_str();
                p_status.set_title(title);
            },
            [&p_abort]() {
                return p_abort.is_aborting();
            }
        );

        if (p_abort.is_aborting()) return;

        if (!result.has_value()) {
            m_error_message = "Normalize failed: no result returned from AI core";
            return;
        }

        m_result = std::move(result.value());
    }

    void on_done(HWND p_wnd, bool p_was_aborted) override {
        if (p_was_aborted) {
            popup_message::g_show("Normalize cancelled by user", "AI Metadata V8");
            return;
        }

        if (!m_error_message.empty()) {
            popup_message::g_show(m_error_message.c_str(), "AI Metadata V8");
            return;
        }

        if (m_result.groups.empty() && m_result.uncertain.empty()) {
            popup_message::g_show("No normalization suggestions returned.", "AI Metadata V8");
            return;
        }

        // 显示确认对话框
        std::vector<bool> selected_groups(m_result.groups.size(), true);
        if (!DialogManager::ShowNormalizeConfirmDialog(
                core_api::get_main_window(),
                m_options.field,
                m_result,
                selected_groups)) {
            popup_message::g_show("Normalize cancelled by user", "AI Metadata V8");
            return;
        }

        // 构建用户选中 groups 的 alias → canonical 映射。
        // 用户可能在 UI 把 uncertain 的 alias 手动加入 group，所以必须基于
        // 用户确认后的 groups 重新构造映射，不能直接用 Python 的 track_updates.matched。
        // 但 Python 的 track_updates.new_values 仍然有用（它用 normalize_key 匹配，
        // 能处理 Unicode 表示差异如尾部全角空格、NFC/NFD 韩文等）。
        //
        // 策略：
        //   - 对每个 track，优先用 Python 的 track_updates[i].new_values（若 matched 且
        //     canonical 在选中集合中）。
        //   - 若 track_updates[i].matched=false（Python 未识别，或值已是 canonical），
        //     则用 alias_to_canonical 对 original_values 做精确匹配重新构造 new_values。
        //     original_values 与 aliases 都是 Python 传过来的字符串，Unicode 表示一致，
        //     精确匹配可靠。这样用户手动把 uncertain alias 加入 group 后也能正确写入。
        std::map<std::string, std::string> alias_to_canonical;
        std::set<std::string> selected_canonicals;
        std::vector<AICore::NormalizeAliasEntry> aliases_to_save;
        for (size_t i = 0; i < m_result.groups.size(); ++i) {
            if (!selected_groups[i]) continue;
            const auto& g = m_result.groups[i];

            selected_canonicals.insert(g.canonical_name);

            // 对当前 group 的 aliases 去重（保留首次出现顺序，跳过空串）
            std::vector<std::string> deduped_aliases;
            std::set<std::string> seen_in_group;
            for (const auto& alias : g.aliases) {
                if (alias.empty()) continue;
                if (seen_in_group.insert(alias).second) {
                    deduped_aliases.push_back(alias);
                }
            }
            for (const auto& alias : deduped_aliases) {
                // alias 精确匹配：同一个 alias 可能出现在多个 group 中，
                // 以首次出现的 group 为准（理论上不应重复）。
                if (alias_to_canonical.find(alias) == alias_to_canonical.end()) {
                    alias_to_canonical[alias] = g.canonical_name;
                }
                AICore::NormalizeAliasEntry ae;
                ae.alias_name = alias;
                ae.canonical_name = g.canonical_name;
                ae.source = "ai";
                ae.confidence = g.confidence;
                ae.confirmed = true;
                ae.reason = g.reason;
                aliases_to_save.push_back(std::move(ae));
            }
        }

        if (selected_canonicals.empty()) {
            popup_message::g_show("No groups selected. Nothing to apply.", "AI Metadata V8");
            return;
        }

        // 通过 IPC 通知 Python worker 写入 normalize_alias 表（Python 端管理）
        // Python 端会自动用 _normalize_key(alias_name) 计算 alias_key 并写入
        if (!aliases_to_save.empty()) {
            bool ok = g_ai_core->save_normalize_aliases(m_options.field, aliases_to_save);
            if (!ok) {
                console::print("AI Metadata V8: Warning - save_normalize_aliases failed, "
                               "tag writes will continue but aliases not persisted");
            }
        }

        // 应用到 Tag。
        // 对每个 track：优先用 Python 的 track_updates.new_values（若 matched 且 canonical 选中），
        // 否则用 alias_to_canonical 对 original_values 精确匹配重新构造 new_values。
        const std::string& tag_field = m_tag_field_upper;
        int applied = 0;
        metadb_handle_list modified_tracks;
        pfc::list_t<file_info_impl> modified_infos;

        for (const auto& update : m_result.track_updates) {
            if (update.track_index < 0 ||
                (size_t)update.track_index >= m_tracks.get_count()) {
                continue;
            }

            // 决定该 track 的 new_values：
            //   - 若 Python 已 matched 且 canonical 在选中集合 → 直接用 new_values
            //   - 否则 → 用 alias_to_canonical 对 original_values 精确匹配重新构造
            std::vector<std::string> new_values;
            bool any_matched = false;
            std::string matched_canonical;

            bool use_python = update.matched &&
                              !update.canonical_name.empty() &&
                              selected_canonicals.find(update.canonical_name) != selected_canonicals.end();

            if (use_python) {
                new_values = update.new_values;
                matched_canonical = update.canonical_name;
                any_matched = true;
            } else {
                // 用户可能在 UI 把 uncertain 的 alias 加入 group，
                // 用 alias_to_canonical 对 original_values 精确匹配重新构造。
                std::set<std::string> seen_canonicals;
                for (const auto& val : update.original_values) {
                    auto it = alias_to_canonical.find(val);
                    if (it != alias_to_canonical.end() &&
                        selected_canonicals.find(it->second) != selected_canonicals.end()) {
                        if (seen_canonicals.insert(it->second).second) {
                            new_values.push_back(it->second);
                        }
                        if (matched_canonical.empty()) matched_canonical = it->second;
                        any_matched = true;
                    } else {
                        new_values.push_back(val);
                    }
                }
            }

            if (!any_matched) continue;

            // 值未变化则跳过（已是 canonical，无需写入）
            bool changed = new_values != update.original_values;
            if (!changed) {
                Logger::instance().info(
                    "[Normalize] Skipping track_id=" + update.track_id +
                    " (values already canonical, no change needed)");
                continue;
            }

            metadb_handle_ptr handle = m_tracks.get_item(update.track_index);
            file_info_impl info;
            if (!handle->get_info(info)) continue;

            // 校验：当前 tag 的 values 数量应与 original_values 一致。
            const t_size cur_count = info.meta_get_count_by_name(tag_field.c_str());
            if (cur_count != update.original_values.size()) {
                Logger::instance().warning(
                    "[Normalize] Value count mismatch for track_id=" + update.track_id +
                    ", cur=" + std::to_string(cur_count) +
                    ", original=" + std::to_string(update.original_values.size()) +
                    ", skipping");
                continue;
            }

            Logger::instance().info(
                "[Normalize] Applying track_update track_id=" + update.track_id +
                ", canonical='" + matched_canonical + "'" +
                ", new_values_count=" + std::to_string(new_values.size()) +
                ", source=" + (use_python ? "python" : "cpp_rematch"));

            // 备份规范化前的状态，支持回滚（Normalize 类型）
            std::map<std::string, std::string> snapshot = extract_full_snapshot(info);
            Logger::instance().info("[Normalize] Saving Normalize snapshot for track_id=" + update.track_id);
            bool snap_ok = g_ai_core->ensure_operation_snapshot(
                update.track_id,
                ai_metadata::OperationType::Normalize,
                snapshot
            );
            Logger::instance().info("[Normalize] ensure_operation_snapshot(Normalize) returned " +
                std::string(snap_ok ? "true" : "false") + ", track_id=" + update.track_id);

            // 重新设置字段：先移除所有 values，再按 new_values 逐个添加（保留 multi-value 结构）
            info.meta_remove_field(tag_field.c_str());
            for (const auto& v : new_values) {
                info.meta_add(tag_field.c_str(), v.c_str());
            }
            modified_tracks.add_item(handle);
            modified_infos.add_item(info);
            applied++;
        }

        if (applied > 0) {
            batch_update_metadata(modified_tracks, modified_infos);
            std::ostringstream oss;
            oss << "Normalize complete: " << applied << " track(s) updated for field '" << m_options.field << "'.";
            console::print(oss.str().c_str());
            popup_message::g_show(oss.str().c_str(), "AI Metadata V8");
        } else {
            popup_message::g_show("No tracks needed normalization (all values already canonical or no matching alias).",
                                  "AI Metadata V8");
        }
    }

private:
    metadb_handle_list m_tracks;
    std::vector<TrackInput> m_inputs;
    NormalizeOptions m_options;
    std::string m_tag_field_upper;                          ///< 目标 Tag 字段名（大写）
    std::vector<std::vector<std::string>> m_track_field_values;  ///< 每个 track 当前 field 的所有 values
    NormalizeResult m_result;
    std::string m_error_message;
};

void V8MenuHandler::normalize_metadata(metadb_handle_list_cref p_data) {
    Logger::instance().info("[V8MenuHandler] normalize_metadata: CALLED, track count = " +
                            std::to_string(p_data.get_count()));
    console::print("AI Metadata V8: normalize_metadata called");

    if (!ensure_ai_core_initialized()) {
        Logger::instance().error("[V8MenuHandler] normalize_metadata: ensure_ai_core_initialized FAILED");
        popup_message::g_show("Failed to initialize AI core", "AI Metadata V8");
        return;
    }

    if (p_data.get_count() == 0) {
        popup_message::g_show("No tracks selected.", "AI Metadata V8");
        return;
    }

    // 字段选择对话框（多选）
    std::vector<std::string> selected_fields;
    if (!DialogManager::ShowNormalizeFieldDialog(
            core_api::get_main_window(), selected_fields)) {
        return;
    }
    if (selected_fields.empty()) {
        popup_message::g_show("No fields selected.", "AI Metadata V8");
        return;
    }

    // 提取 TrackInput
    std::vector<TrackInput> inputs;
    for (size_t i = 0; i < p_data.get_count(); ++i) {
        file_info_impl info;
        if (p_data.get_item(i)->get_info(info)) {
            TrackInput input;
            const char* path = p_data.get_item(i)->get_path();
            uint32_t subsong = p_data.get_item(i)->get_subsong_index();
            t_filestats stats = p_data.get_item(i)->get_filestats();
            uint64_t file_size = stats.m_size;
            input.track_id = CacheLayer::generate_track_uid(path ? path : "", subsong, file_size);
            input.file_path = path ? path : "";
            input.subsong_index = subsong;
            input.title = info.meta_get("TITLE", 0) ? info.meta_get("TITLE", 0) : "";
            input.artist = info.meta_get("ARTIST", 0) ? info.meta_get("ARTIST", 0) : "";
            input.album = info.meta_get("ALBUM", 0) ? info.meta_get("ALBUM", 0) : "";
            input.album_artist = info.meta_get("ALBUM ARTIST", 0) ? info.meta_get("ALBUM ARTIST", 0) : "";
            input.genre = info.meta_get("GENRE", 0) ? info.meta_get("GENRE", 0) : "";
            input.label = info.meta_get("LABEL", 0) ? info.meta_get("LABEL", 0) : "";
            input.composer = info.meta_get("COMPOSER", 0) ? info.meta_get("COMPOSER", 0) : "";
            inputs.push_back(input);
        }
    }

    if (inputs.empty()) {
        popup_message::g_show("Failed to read track metadata.", "AI Metadata V8");
        return;
    }

    // 当前阶段：仅处理第一个选中字段（通常为 artist）；多字段支持留待后续扩展
    NormalizeOptions options;
    options.field = selected_fields[0];

    service_ptr_t<NormalizeCallback> callback =
        new service_impl_t<NormalizeCallback>(p_data, std::move(inputs), options);
    threaded_process::g_run_modeless(callback,
        threaded_process::flag_show_progress | threaded_process::flag_show_abort,
        core_api::get_main_window(), "Normalize Metadata");
}

static contextmenu_item_factory_t<V8MenuHandler> g_v8_menu_handler_factory;

}
