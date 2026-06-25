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
    
    size_t meta_count = info.meta_get_count();
    for (size_t i = 0; i < meta_count; ++i) {
        const char* field_name = info.meta_enum_name(i);
        if (!field_name) continue;
        
        std::string field_upper = field_name;
        std::transform(field_upper.begin(), field_upper.end(), field_upper.begin(), ::toupper);
        
        if (BackupManager::is_field_blacklisted(field_upper)) {
            continue;
        }
        
        const char* value = info.meta_get(field_name, 0);
        if (value && strlen(value) > 0) {
            snapshot[field_upper] = value;
        }
    }
    
    return snapshot;
}

static void apply_snapshot_to_info(file_info& info, const std::map<std::string, std::string>& snapshot) {
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
        if (!field_value.empty()) {
            info.meta_set(field_name.c_str(), field_value.c_str());
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

class Stage1ScrapeCallback : public threaded_process_callback {
public:
    Stage1ScrapeCallback(metadb_handle_list tracks, std::vector<TrackInput> inputs, ScrapingOptions options)
        : m_tracks(tracks)
        , m_inputs(std::move(inputs))
        , m_options(options) {}
    
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
        
        if (!DialogManager::ShowConfirmResultDialog(core_api::get_main_window(), m_results, selected, m_inputs)) {
            popup_message::g_show("Scraping cancelled by user", "AI Metadata V8");
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
                    g_ai_core->ensure_snapshot(
                        m_inputs[i].track_id,
                        original_snapshot
                    );
                    
                    std::string cache_key = g_ai_core->generate_stage1_cache_key(m_inputs[i]);
                    g_ai_core->save_stage1_cache(cache_key, result, m_inputs[i]);
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
            Logger::instance().info("[Stage1] Writing to fb2k metadata database: " + 
                                   std::to_string(modified_tracks.get_count()) + " tracks");
            batch_update_metadata(modified_tracks, modified_infos);
        }
        
        pfc::string8 msg;
        msg << "Stage 1 scraping complete: " << applied << "/" << m_results.size() << " tracks updated";
        popup_message::g_show(msg, "AI Metadata V8");
    }
    
private:
    metadb_handle_list m_tracks;
    std::vector<TrackInput> m_inputs;
    ScrapingOptions m_options;
    std::vector<TrackScrapingResult> m_results;
    std::string m_error_message;
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
                if (should_write_field("genre") && !result.genre_value.empty()) {
                    info.meta_set("GENRE", result.genre_value.c_str());
                    if (result.genre_confidence > 0) {
                        total_conf += result.genre_confidence;
                        conf_count++;
                    }
                }
                if (should_write_field("edition") && !result.edition_value.empty()) {
                    info.meta_set("EDITION", result.edition_value.c_str());
                    if (result.edition_confidence > 0) {
                        total_conf += result.edition_confidence;
                        conf_count++;
                    }
                }
                
                if (conf_count > 0) {
                    std::ostringstream oss;
                    oss << std::fixed << std::setprecision(2) << (total_conf / conf_count);
                    confidence_summary = oss.str();
                }
                
                if (g_ai_core && g_ai_core->is_initialized()) {
                    g_ai_core->ensure_snapshot(
                        m_inputs[i].track_id,
                        original_snapshot
                    );
                    
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
                           "classify_genre=" + std::to_string(input.options.classify_genre) + ", " +
                           "identify_edition=" + std::to_string(input.options.identify_edition) + ", " +
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
static const GUID guid_rollback_initial = 
    { 0xb456789a, 0x3456, 0x789a, { 0xbc, 0xde, 0xf0, 0x12, 0x34, 0x56, 0x78, 0x9a } };
static const GUID guid_cache_stats = 
    { 0x92345678, 0x1234, 0x5678, { 0x9a, 0xbc, 0xde, 0xf0, 0x12, 0x34, 0x56, 0x78 } };
static const GUID guid_clear_cache = 
    { 0xa3456789, 0x2345, 0x6789, { 0xab, 0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89 } };

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
    
    root->add_child(new MenuNodeCommand("Stage 1: Scrape Metadata", guid_stage1_scrape, 
        "Scrape basic metadata from MusicBrainz/Discogs/AI",
        [this](metadb_handle_list_cref data) { stage1_scrape(data); }, has_selection));
    root->add_child(new MenuNodeCommand("Stage 2: Enhance Metadata", guid_stage2_enhance, 
        "Enhance metadata: translate, classify genre, identify edition",
        [this](metadb_handle_list_cref data) { stage2_enhance(data); }, has_selection));
    root->add_child(new MenuNodeSeparator());
    root->add_child(new MenuNodeCommand("Rollback to Initial", guid_rollback_initial, 
        "Rollback all selected tracks to their initial state",
        [this](metadb_handle_list_cref data) { rollback_to_initial(data); }, has_selection));
    root->add_child(new MenuNodeSeparator());
    root->add_child(new MenuNodeCommand("Cache Statistics", guid_cache_stats, 
        "Show cache statistics and hit rate",
        [this](metadb_handle_list_cref data) { (void)data; show_cache_stats(); }, true));
    root->add_child(new MenuNodeCommand("Clear Cache", guid_clear_cache, 
        "Clear cached metadata for selected tracks or all",
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

void V8MenuHandler::rollback_to_initial(metadb_handle_list_cref p_data) {
    Logger::instance().info("[Rollback] ===== START ROLLBACK TO INITIAL =====");
    Logger::instance().info("[Rollback] track count = " + std::to_string(p_data.get_count()));
    
    if (p_data.get_count() == 0) {
        popup_message::g_show("No tracks selected", "AI Metadata V8");
        return;
    }
    if (!ensure_ai_core_initialized()) {
        popup_message::g_show("Failed to initialize AI core", "AI Metadata V8");
        return;
    }
    
    int result = MessageBoxW(core_api::get_main_window(),
        L"Rollback all selected tracks to their initial state?\n\nThis will restore the original metadata before any AI processing.",
        L"Rollback to Initial", MB_YESNO | MB_ICONQUESTION);
    if (result != IDYES) {
        popup_message::g_show("Rollback cancelled", "AI Metadata V8");
        return;
    }
    
    int success_count = 0, no_backup_count = 0, fail_count = 0;
    metadb_handle_list modified_tracks;
    pfc::list_t<file_info_impl> modified_infos;
    
    for (size_t i = 0; i < p_data.get_count(); ++i) {
        metadb_handle_ptr handle = p_data.get_item(i);
        const char* path = handle->get_path();
        t_uint32 subsong = handle->get_subsong_index();
        t_filesize file_size = handle->get_filesize();
        std::string track_id = CacheLayer::generate_track_uid(path ? path : "", subsong,
            file_size != foobar2000_io::filesize_invalid ? file_size : 0);
        
        Logger::instance().info("[Rollback] Processing track " + std::to_string(i+1) + "/" + 
            std::to_string(p_data.get_count()) + ", track_id=" + track_id);
        
        auto snapshot_opt = g_ai_core->rollback_snapshot(track_id);
        if (!snapshot_opt.has_value() || snapshot_opt->empty()) {
            Logger::instance().info("[Rollback] No snapshot found for track_id=" + track_id);
            no_backup_count++;
            continue;
        }
        
        const auto& snapshot = snapshot_opt.value();
        file_info_impl info;
        if (handle->get_info(info)) {
            apply_snapshot_to_info(info, snapshot);
            modified_tracks.add_item(handle);
            modified_infos.add_item(info);
            success_count++;
            Logger::instance().info("[Rollback] Successfully rolled back track_id=" + track_id + 
                ", fields=" + std::to_string(snapshot.size()));
        } else {
            fail_count++;
            Logger::instance().error("[Rollback] Failed to get info for track_id=" + track_id);
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
    
    pfc::string8 msg;
    msg << "Rollback complete:\n\nRolled back: " << success_count << "\nNo snapshot found: " << no_backup_count << "\nFailed: " << fail_count;
    popup_message::g_show(msg, "Rollback to Initial");
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

static contextmenu_item_factory_t<V8MenuHandler> g_v8_menu_handler_factory;

}
