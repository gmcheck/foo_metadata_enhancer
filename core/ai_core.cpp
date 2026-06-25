#include "ai_core.h"
#include "logger.h"
#include "../include/constants.h"
#include <nlohmann/json.hpp>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <algorithm>
#include <random>
#include <thread>
#include <fstream>
#include <chrono>

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#endif

namespace ai_metadata {

static std::string generate_uuid() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 15);
    std::uniform_int_distribution<> dis2(8, 11);
    
    std::stringstream ss;
    ss << std::hex;
    for (int i = 0; i < 8; i++) ss << dis(gen);
    ss << "-";
    for (int i = 0; i < 4; i++) ss << dis(gen);
    ss << "-4";
    for (int i = 0; i < 3; i++) ss << dis(gen);
    ss << "-";
    ss << dis2(gen);
    for (int i = 0; i < 3; i++) ss << dis(gen);
    ss << "-";
    for (int i = 0; i < 12; i++) ss << dis(gen);
    return ss.str();
}

static std::string get_current_timestamp() {
    auto now = std::time(nullptr);
    auto tm = *std::localtime(&now);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

#ifdef _WIN32
static std::string get_profile_path() {
    char path[MAX_PATH] = {0};
    if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, path))) {
        return std::string(path) + "\\foobar2000";
    }
    return ".";
}

static std::string get_dll_dir() {
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
    return "";
}
#endif

AICore::AICore() = default;

AICore::~AICore() {
    shutdown();
}

bool AICore::initialize() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (initialized_) {
        return true;
    }
    
#ifdef _WIN32
    std::string dll_dir = get_dll_dir();
    
    if (cache_path_.empty() && !dll_dir.empty()) {
        std::string base_dir = dll_dir + "\\foo_ai_metadata";
        std::string cache_dir = base_dir + "\\cache";
        std::string abort_dir = base_dir + "\\abort";
        
        CreateDirectoryA(cache_dir.c_str(), NULL);
        CreateDirectoryA(abort_dir.c_str(), NULL);
        
        cache_path_ = cache_dir + "\\" + constants::cache_db_name();
        abort_dir_ = abort_dir;
    }
    
    if (worker_path_.empty() && !dll_dir.empty()) {
        worker_path_ = dll_dir + "\\foo_ai_metadata\\worker\\ai_worker.py";
    }
    
    Logger::instance().debug("initialize: cache_path = " + cache_path_, __FILE__, __FUNCTION__);
    Logger::instance().debug("initialize: worker_path = " + worker_path_, __FILE__, __FUNCTION__);
    Logger::instance().debug("initialize: abort_dir = " + abort_dir_, __FILE__, __FUNCTION__);
#endif
    
    cache_ = std::make_unique<CacheLayer>(cache_path_);
    if (!cache_->is_valid()) {
        LOG_ERROR("initialize: Cache initialization failed");
        cache_.reset();
        return false;
    }
    
    std::string backup_db_path = cache_path_;
    Logger::instance().debug("initialize: backup_db_path = " + backup_db_path, __FILE__, __FUNCTION__);
    backup_manager_ = std::make_unique<BackupManager>(backup_db_path);
    if (!backup_manager_->is_healthy()) {
        LOG_ERROR("initialize: BackupManager initialization failed");
        backup_manager_.reset();
        cache_.reset();
        return false;
    }
    
    worker_manager_ = std::make_unique<WorkerManager>(worker_path_);
    worker_manager_->set_timeout_ms(TIMEOUT_MS);
    
    if (!worker_manager_->initialize()) {
        LOG_ERROR("initialize: WorkerManager initialization failed");
        worker_manager_.reset();
        cache_.reset();
        return false;
    }
    
    initialized_ = true;
    LOG_INFO("initialize: Initialization successful");
    return true;
}

void AICore::shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!initialized_) {
        return;
    }
    
    processing_ = false;
    
    if (worker_manager_) {
        worker_manager_->shutdown();
        worker_manager_.reset();
    }
    
    backup_manager_.reset();
    cache_.reset();
    initialized_ = false;
}

std::string AICore::generate_request_id() {
    return generate_uuid();
}

CacheStatistics AICore::get_cache_statistics() {
    if (cache_) {
        return cache_->get_statistics();
    }
    return CacheStatistics{};
}

void AICore::clear_cache() {
    if (cache_) {
        cache_->clear_all();
    }
}

int AICore::clear_cache_by_track_ids(const std::vector<std::string>& track_ids) {
    if (cache_) {
        return cache_->clear_by_track_ids(track_ids);
    }
    return 0;
}

std::string AICore::get_config(const std::string& key) {
    if (cache_) {
        return cache_->get_config(key);
    }
    return "";
}

void AICore::set_config(const std::string& key, const std::string& value) {
    if (cache_) {
        cache_->set_config(key, value);
    }
}

bool AICore::restart_all_workers() {
    if (worker_manager_) {
        worker_manager_->restart_worker();
        return true;
    }
    return false;
}

bool AICore::is_worker_healthy() const {
    if (worker_manager_) {
        return worker_manager_->is_healthy();
    }
    return false;
}

std::vector<WorkerInfo> AICore::get_worker_info() const {
    if (worker_manager_) {
        return worker_manager_->get_worker_info();
    }
    return {};
}

std::string AICore::test_api_connection(
    const std::string& provider,
    const std::string& model,
    uint32_t timeout_ms
) {
    if (!initialized_ || !worker_manager_) {
        nlohmann::json error_result;
        error_result["success"] = false;
        error_result["error"] = "AICore not initialized";
        return error_result.dump();
    }
    
    std::string request_id = generate_uuid();
    
    nlohmann::json request;
    request["id"] = request_id;
    request["method"] = "test_api";
    request["jsonrpc"] = "2.0";
    
    nlohmann::json params;
    params["provider"] = provider;
    params["model"] = model;
    request["params"] = params;
    
    std::string request_str = request.dump();
    
    auto response_promise = std::make_shared<std::promise<BatchResponse>>();
    std::future<BatchResponse> response_future = response_promise->get_future();
    
    bool sent = worker_manager_->send_request(
        request_id,
        request_str,
        [response_promise](const std::string&, const BatchResponse& resp) {
            response_promise->set_value(resp);
        },
        [response_promise](const std::string&, const ErrorInfo& err) {
            BatchResponse resp;
            resp.success = false;
            resp.error = err;
            response_promise->set_value(resp);
        }
    );
    
    if (!sent) {
        nlohmann::json error_result;
        error_result["success"] = false;
        error_result["error"] = "Failed to send request to worker";
        return error_result.dump();
    }
    
    auto status = response_future.wait_for(std::chrono::milliseconds(timeout_ms));
    
    if (status != std::future_status::ready) {
        nlohmann::json error_result;
        error_result["success"] = false;
        error_result["error"] = "API test timed out after " + std::to_string(timeout_ms) + "ms";
        return error_result.dump();
    }
    
    BatchResponse response = response_future.get();
    
    nlohmann::json result;
    result["success"] = response.success;
    
    if (response.success && !response.results.empty()) {
        result["result"] = response.results[0];
    } else if (response.error.has_value()) {
        result["error"] = response.error->message;
        result["error_code"] = response.error->code;
    } else {
        result["error"] = "Unknown error";
    }
    
    return result.dump();
}

std::vector<TrackScrapingResult> AICore::stage1_scrape_sync(
    const std::vector<TrackInput>& tracks,
    const ScrapingOptions& options,
    ProgressCallback on_progress,
    AbortCallback on_abort
) {
    std::vector<TrackScrapingResult> results;
    
    if (!initialized_ || tracks.empty()) {
        return results;
    }
    
    processing_ = true;
    LOG_INFO("stage1_scrape_sync: Started for " + std::to_string(tracks.size()) + " tracks");
    
    auto filter_fields_by_options = [&options](const std::map<std::string, ScrapedField>& all_fields) {
        std::map<std::string, ScrapedField> filtered;
        
        auto check_field = [&filtered, &all_fields](const std::string& field_name, bool should_include) {
            if (should_include) {
                auto it = all_fields.find(field_name);
                if (it != all_fields.end()) {
                    filtered[field_name] = it->second;
                }
            }
        };
        
        auto always_include_field = [&filtered, &all_fields](const std::string& field_name) {
            auto it = all_fields.find(field_name);
            if (it != all_fields.end()) {
                filtered[field_name] = it->second;
            }
        };
        
        always_include_field("title");
        always_include_field("artist");
        always_include_field("album");
        
        check_field("year", options.scrape_year);
        check_field("track_number", options.scrape_track_number);
        check_field("disc_number", options.scrape_disc_number);
        check_field("composer", options.scrape_composer);
        check_field("lyricist", options.scrape_lyricist);
        check_field("conductor", options.scrape_conductor);
        check_field("performer", options.scrape_performer);
        check_field("label", options.scrape_label);
        
        return filtered;
    };
    
    std::vector<std::string> cache_keys;
    std::map<std::string, size_t> key_to_index;
    std::vector<bool> cache_hit(tracks.size(), false);
    std::vector<TrackScrapingResult> all_results(tracks.size());
    
    for (size_t i = 0; i < tracks.size(); ++i) {
        std::string cache_key = CacheLayer::generate_cache_key(tracks[i]);
        cache_keys.push_back(cache_key);
        key_to_index[cache_key] = i;
        
        auto cached = cache_->get_stage1(cache_key);
        if (cached) {
            cache_hit[i] = true;
            all_results[i].track_id = tracks[i].track_id;
            all_results[i].success = cached->success;
            all_results[i].scraped_fields = filter_fields_by_options(cached->scraped_fields);
            all_results[i].release_source = cached->source;
            all_results[i].error = cached->error_message;
            Logger::instance().debug("stage1_scrape_sync: Cache hit for track " + tracks[i].track_id, __FILE__, __FUNCTION__);
        }
    }
    
    std::vector<TrackInput> uncached_tracks;
    std::vector<size_t> uncached_indices;
    for (size_t i = 0; i < tracks.size(); ++i) {
        if (!cache_hit[i]) {
            uncached_tracks.push_back(tracks[i]);
            uncached_indices.push_back(i);
        }
    }
    
    Logger::instance().debug("stage1_scrape_sync: Cache hits: " + std::to_string(tracks.size() - uncached_tracks.size()) + 
                           ", need to process: " + std::to_string(uncached_tracks.size()), __FILE__, __FUNCTION__);
    
    if (!uncached_tracks.empty()) {
        const size_t total_tracks = uncached_tracks.size();
        const size_t total_batches = (total_tracks + batch_size_ - 1) / batch_size_;
        
        Logger::instance().debug("stage1_scrape_sync: Processing " + std::to_string(total_tracks) + 
                               " uncached tracks in " + std::to_string(total_batches) + " batches (batch_size=" + 
                               std::to_string(batch_size_) + ")", __FILE__, __FUNCTION__);
        
        bool was_aborted = false;
        size_t processed_count = 0;
        std::vector<FailedTrackInfo> failed_tracks;
        
        for (size_t batch_idx = 0; batch_idx < total_batches; ++batch_idx) {
            if (on_abort && on_abort()) {
                Logger::instance().debug("stage1_scrape_sync: Abort requested before batch " + std::to_string(batch_idx + 1), __FILE__, __FUNCTION__);
                was_aborted = true;
                break;
            }
            
            if (on_progress) {
                int percent = static_cast<int>(batch_idx * 100 / total_batches);
                std::string msg = "Stage1: " + std::to_string(percent) + "% (batch " + 
                                 std::to_string(batch_idx) + "/" + 
                                 std::to_string(total_batches) + ")";
                on_progress(percent, 100, msg);
            }
            
            size_t start_idx = batch_idx * batch_size_;
            size_t end_idx = (std::min)(start_idx + batch_size_, total_tracks);
            
            std::vector<TrackInput> batch_tracks(
                uncached_tracks.begin() + start_idx,
                uncached_tracks.begin() + end_idx
            );
            
            std::string task_id = generate_request_id();
            
            LOG_INFO("stage1_scrape_sync: Processing batch " + 
                                   std::to_string(batch_idx + 1) + "/" + std::to_string(total_batches) +
                                   " (tracks " + std::to_string(start_idx + 1) + "-" + std::to_string(end_idx) + ")");
            
            auto batch_results = process_batch(batch_tracks, options, task_id, on_abort);
            
            if (on_abort && on_abort()) {
                Logger::instance().debug("stage1_scrape_sync: Abort detected after batch " + std::to_string(batch_idx + 1), __FILE__, __FUNCTION__);
                was_aborted = true;
                clear_abort(task_id);
            }
            
            if (batch_results.empty() || batch_results.size() < batch_tracks.size()) {
                size_t failed_start = batch_results.size();
                for (size_t j = failed_start; j < batch_tracks.size(); ++j) {
                    FailedTrackInfo fi;
                    fi.track_id = batch_tracks[j].track_id;
                    fi.reason = FailureReason::Timeout;
                    fi.error_message = "Batch timeout or worker crash";
                    fi.retry_count = 0;
                    failed_tracks.push_back(fi);
                    Logger::instance().warning("stage1_scrape_sync: Track " + batch_tracks[j].track_id + " failed, will retry", __FILE__, __FUNCTION__);
                }
            }
            
            for (size_t j = 0; j < batch_results.size() && start_idx + j < uncached_indices.size(); ++j) {
                size_t original_idx = uncached_indices[start_idx + j];
                all_results[original_idx] = batch_results[j];
            }
            
            processed_count += batch_tracks.size();
            
            if (on_progress) {
                int percent = static_cast<int>((batch_idx + 1) * 100 / total_batches);
                std::string msg = "Stage1: " + std::to_string(percent) + "% (batch " + 
                                 std::to_string(batch_idx + 1) + "/" + 
                                 std::to_string(total_batches) + ")";
                on_progress(percent, 100, msg);
            }
            
            if (was_aborted) {
                break;
            }
        }
        
        if (!failed_tracks.empty() && !was_aborted) {
            Logger::instance().info("stage1_scrape_sync: Retrying " + std::to_string(failed_tracks.size()) + " failed tracks...", __FILE__, __FUNCTION__);
            
            std::map<std::string, size_t> track_id_to_uncached_idx;
            for (size_t i = 0; i < uncached_tracks.size(); ++i) {
                track_id_to_uncached_idx[uncached_tracks[i].track_id] = i;
            }
            
            const size_t retry_batch_size = batch_size_;
            const size_t total_retry_batches = (failed_tracks.size() + retry_batch_size - 1) / retry_batch_size;
            
            for (size_t retry_batch_idx = 0; retry_batch_idx < total_retry_batches; ++retry_batch_idx) {
                if (on_abort && on_abort()) {
                    Logger::instance().debug("stage1_scrape_sync: Abort requested during retry", __FILE__, __FUNCTION__);
                    was_aborted = true;
                    break;
                }
                
                size_t retry_start = retry_batch_idx * retry_batch_size;
                size_t retry_end = (std::min)(retry_start + retry_batch_size, failed_tracks.size());
                
                std::vector<TrackInput> retry_tracks;
                std::vector<size_t> retry_uncached_indices;
                
                for (size_t i = retry_start; i < retry_end; ++i) {
                    const auto& ft = failed_tracks[i];
                    auto it = track_id_to_uncached_idx.find(ft.track_id);
                    if (it != track_id_to_uncached_idx.end()) {
                        retry_tracks.push_back(uncached_tracks[it->second]);
                        retry_uncached_indices.push_back(it->second);
                    }
                }
                
                if (retry_tracks.empty()) continue;
                
                std::string retry_task_id = generate_request_id();
                LOG_INFO("stage1_scrape_sync: Retry batch " + std::to_string(retry_batch_idx + 1) + "/" + 
                                       std::to_string(total_retry_batches) + " (" + std::to_string(retry_tracks.size()) + " tracks)");
                
                auto retry_results = process_batch(retry_tracks, options, retry_task_id, on_abort);
                
                for (size_t j = 0; j < retry_results.size() && j < retry_uncached_indices.size(); ++j) {
                    size_t uncached_idx = retry_uncached_indices[j];
                    size_t original_idx = uncached_indices[uncached_idx];
                    all_results[original_idx] = retry_results[j];
                    
                    if (retry_results[j].success) {
                        Logger::instance().info("stage1_scrape_sync: Retry succeeded for track " + retry_results[j].track_id, __FILE__, __FUNCTION__);
                    }
                }
                
                for (size_t i = retry_start; i < retry_end; ++i) {
                    failed_tracks[i].retry_count = 1;
                }
            }
        }
        
        if (was_aborted) {
            Logger::instance().debug("stage1_scrape_sync: Processing was aborted after " + 
                                   std::to_string(processed_count) + " tracks", __FILE__, __FUNCTION__);
        }
        
        size_t final_success = 0;
        size_t final_failed = 0;
        for (const auto& r : all_results) {
            if (r.success && !r.scraped_fields.empty()) {
                final_success++;
            } else {
                final_failed++;
            }
        }
        
        if (final_failed > 0) {
            Logger::instance().info("stage1_scrape_sync: Statistics - Success: " + std::to_string(final_success) + 
                                   ", Failed: " + std::to_string(final_failed) + 
                                   " (failed tracks will be cached on next run)", __FILE__, __FUNCTION__);
        }
    }
    
    for (size_t i = 0; i < tracks.size(); ++i) {
        results.push_back(all_results[i]);
    }
    
    processing_ = false;
    LOG_INFO("stage1_scrape_sync: Complete, returning " + std::to_string(results.size()) + " results");
    return results;
}

std::vector<EnhancementResult> AICore::stage2_enhance_sync(
    const std::vector<TrackInput>& tracks,
    const EnhancementOptions& options,
    ProgressCallback on_progress,
    AbortCallback on_abort
) {
    std::vector<EnhancementResult> results;
    
    if (!initialized_ || tracks.empty()) {
        return results;
    }
    
    processing_ = true;
    LOG_INFO("stage2_enhance_sync: Started for " + std::to_string(tracks.size()) + " tracks");
    
    std::vector<std::string> cache_keys;
    std::vector<bool> cache_fully_hit(tracks.size(), false);
    std::vector<EnhancementResult> cached_results(tracks.size());
    
    for (size_t i = 0; i < tracks.size(); ++i) {
        std::string cache_key = CacheLayer::generate_stage2_cache_key(tracks[i], options);
        cache_keys.push_back(cache_key);
        
        auto cached = cache_->get_stage2(cache_key);
        if (cached) {
            cached_results[i].track_id = tracks[i].track_id;
            cached_results[i].success = cached->success;
            cached_results[i].title_zh = cached->title_zh;
            cached_results[i].album_zh = cached->album_zh;
            cached_results[i].artist_zh = cached->artist_zh;
            cached_results[i].translation_confidence = cached->translation_confidence;
            cached_results[i].genre_value = cached->genre_value;
            cached_results[i].genre_confidence = cached->genre_confidence;
            cached_results[i].edition_value = cached->edition_value;
            cached_results[i].edition_confidence = cached->edition_confidence;
            cached_results[i].error = cached->error_message;
            
            bool all_needed_fields_cached = true;
            if (options.translate_title && cached->title_zh.empty()) all_needed_fields_cached = false;
            if (options.translate_album && cached->album_zh.empty()) all_needed_fields_cached = false;
            if (options.translate_artist && cached->artist_zh.empty()) all_needed_fields_cached = false;
            if (options.classify_genre && cached->genre_value.empty()) all_needed_fields_cached = false;
            if (options.identify_edition && cached->edition_value.empty()) all_needed_fields_cached = false;
            
            if (all_needed_fields_cached && cached->success) {
                cache_fully_hit[i] = true;
                Logger::instance().debug("stage2_enhance_sync: Full cache hit for track " + tracks[i].track_id, __FILE__, __FUNCTION__);
            } else {
                Logger::instance().debug("stage2_enhance_sync: Partial cache hit for track " + tracks[i].track_id + ", need to fetch missing fields", __FILE__, __FUNCTION__);
            }
        }
    }
    
    std::vector<TrackInput> uncached_tracks;
    std::vector<size_t> uncached_indices;
    for (size_t i = 0; i < tracks.size(); ++i) {
        if (!cache_fully_hit[i]) {
            uncached_tracks.push_back(tracks[i]);
            uncached_indices.push_back(i);
        }
    }
    
    Logger::instance().debug("stage2_enhance_sync: Full cache hits: " + std::to_string(tracks.size() - uncached_tracks.size()) + 
                           ", need to process: " + std::to_string(uncached_tracks.size()), __FILE__, __FUNCTION__);
    
    if (!uncached_tracks.empty()) {
        const size_t total_tracks = uncached_tracks.size();
        const size_t total_batches = (total_tracks + batch_size_ - 1) / batch_size_;
        
        const uint32_t BASE_TIMEOUT_MS = constants::BASE_TIMEOUT_MS;
        const uint32_t PER_TRACK_TIMEOUT_MS = constants::PER_TRACK_TIMEOUT_MS;
        uint32_t dynamic_timeout_ms = BASE_TIMEOUT_MS + static_cast<uint32_t>(batch_size_) * PER_TRACK_TIMEOUT_MS;
        uint32_t old_max_silence = worker_manager_->get_max_silence_time_ms();
        worker_manager_->set_max_silence_time_ms(dynamic_timeout_ms);
        Logger::instance().debug("stage2_enhance_sync: Set dynamic timeout to " + std::to_string(dynamic_timeout_ms / 1000) + 
                               "s per batch (batch_size=" + std::to_string(batch_size_) + ")", __FILE__, __FUNCTION__);
        
        auto restore_timeout = [this, old_max_silence]() {
            worker_manager_->set_max_silence_time_ms(old_max_silence);
        };
        
        Logger::instance().debug("stage2_enhance_sync: Processing " + std::to_string(total_tracks) + 
                               " uncached tracks in " + std::to_string(total_batches) + " batches", __FILE__, __FUNCTION__);
        
        bool was_aborted = false;
        std::vector<FailedTrackInfo> failed_tracks;
        
        for (size_t batch_idx = 0; batch_idx < total_batches; ++batch_idx) {
            if (on_abort && on_abort()) {
                Logger::instance().debug("stage2_enhance_sync: Abort requested before batch " + std::to_string(batch_idx + 1), __FILE__, __FUNCTION__);
                was_aborted = true;
                break;
            }
            
            if (on_progress) {
                int percent = static_cast<int>(batch_idx * 100 / total_batches);
                std::string msg = "Stage2: " + std::to_string(percent) + "% (batch " + 
                                 std::to_string(batch_idx) + "/" + 
                                 std::to_string(total_batches) + ")";
                on_progress(percent, 100, msg);
            }
            
            size_t start_idx = batch_idx * batch_size_;
            size_t end_idx = (std::min)(start_idx + batch_size_, total_tracks);
            
            std::vector<TrackInput> batch_tracks(
                uncached_tracks.begin() + start_idx,
                uncached_tracks.begin() + end_idx
            );
            
            std::string task_id = generate_request_id();
            
            LOG_INFO("stage2_enhance_sync: Processing batch " + 
                                   std::to_string(batch_idx + 1) + "/" + std::to_string(total_batches) +
                                   " (tracks " + std::to_string(start_idx + 1) + "-" + std::to_string(end_idx) + ")");
            
            nlohmann::json request;
            request["method"] = "stage2_enhance";
            request["id"] = task_id;
            request["version"] = 1;
            request["task_id"] = task_id;
            
            nlohmann::json params;
            params["task_id"] = task_id;
            params["abort_dir"] = abort_dir_;
            
            nlohmann::json tracks_json = nlohmann::json::array();
            for (const auto& track : batch_tracks) {
                nlohmann::json t;
                t["track_id"] = track.track_id;
                t["title"] = track.title;
                t["artist"] = track.artist;
                t["album"] = track.album;
                t["album_artist"] = track.album_artist;
                t["year"] = track.year;
                t["genre"] = track.genre;
                tracks_json.push_back(t);
            }
            params["tracks"] = tracks_json;
            
            nlohmann::json options_json;
            options_json["translate_title"] = options.translate_title;
            options_json["translate_album"] = options.translate_album;
            options_json["translate_artist"] = options.translate_artist;
            options_json["classify_genre"] = options.classify_genre;
            options_json["identify_edition"] = options.identify_edition;
            options_json["target_language"] = options.target_language;
            params["options"] = options_json;
            
            request["params"] = params;
            
            std::string request_str = request.dump();
            
            auto response_promise = std::make_shared<std::promise<BatchResponse>>();
            std::future<BatchResponse> response_future = response_promise->get_future();
            
            bool sent = worker_manager_->send_request(
                request["id"].get<std::string>(),
                request_str,
                [response_promise](const std::string&, const BatchResponse& resp) {
                    response_promise->set_value(resp);
                },
                [response_promise](const std::string&, const ErrorInfo& err) {
                    BatchResponse resp;
                    resp.success = false;
                    resp.error = err;
                    response_promise->set_value(resp);
                }
            );
            
            if (!sent) {
                for (size_t j = 0; j < batch_tracks.size(); ++j) {
                    FailedTrackInfo fi;
                    fi.track_id = batch_tracks[j].track_id;
                    fi.reason = FailureReason::NetworkError;
                    fi.error_message = "Failed to send request";
                    failed_tracks.push_back(fi);
                }
                continue;
            }
            
            auto start_time = std::chrono::steady_clock::now();
            const int check_interval_ms = constants::CHECK_INTERVAL_MS;
            bool batch_timeout = false;
            
            while (true) {
                auto status = response_future.wait_for(std::chrono::milliseconds(check_interval_ms));
                
                if (status == std::future_status::ready) {
                    break;
                }
                
                if (on_abort && on_abort()) {
                    Logger::instance().debug("stage2_enhance_sync: Abort requested by user", __FILE__, __FUNCTION__);
                    request_abort(task_id);
                    was_aborted = true;
                    break;
                }
                
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - start_time).count();
                if (elapsed >= dynamic_timeout_ms) {
                    LOG_ERROR("stage2_enhance_sync: Batch timeout");
                    batch_timeout = true;
                    break;
                }
            }
            
            if (was_aborted) {
                clear_abort(task_id);
                break;
            }
            
            if (batch_timeout) {
                for (size_t j = 0; j < batch_tracks.size(); ++j) {
                    FailedTrackInfo fi;
                    fi.track_id = batch_tracks[j].track_id;
                    fi.reason = FailureReason::Timeout;
                    fi.error_message = "Batch timeout";
                    failed_tracks.push_back(fi);
                    Logger::instance().warning("stage2_enhance_sync: Track " + batch_tracks[j].track_id + " failed, will retry", __FILE__, __FUNCTION__);
                }
                continue;
            }
            
            BatchResponse response = response_future.get();
            
            if (!response.success) {
                LOG_ERROR("stage2_enhance_sync: " + 
                    (response.error ? response.error->message : "Unknown error"));
                for (size_t j = 0; j < batch_tracks.size(); ++j) {
                    FailedTrackInfo fi;
                    fi.track_id = batch_tracks[j].track_id;
                    fi.reason = FailureReason::WorkerCrash;
                    fi.error_message = response.error ? response.error->message : "Unknown error";
                    failed_tracks.push_back(fi);
                }
                continue;
            }
            
            for (size_t j = 0; j < response.results.size() && start_idx + j < uncached_indices.size(); ++j) {
                const auto& r = response.results[j];
                size_t i = uncached_indices[start_idx + j];
                
                EnhancementResult result;
                result.track_id = r.value("track_id", "");
                result.success = r.value("success", false);
                
                result.title_zh = r.value("title_zh", "");
                result.album_zh = r.value("album_zh", "");
                result.artist_zh = r.value("artist_zh", "");
                result.translation_confidence = r.value("translation_confidence", 0.0f);
                
                result.genre_value = r.value("genre_value", "");
                result.genre_confidence = r.value("genre_confidence", 0.0f);
                
                result.edition_value = r.value("edition_value", "");
                result.edition_confidence = r.value("edition_confidence", 0.0f);
                
                if (r.contains("error") && !r["error"].is_null()) {
                    result.error = r["error"].get<std::string>();
                }
                
                if (!result.title_zh.empty()) cached_results[i].title_zh = result.title_zh;
                if (!result.album_zh.empty()) cached_results[i].album_zh = result.album_zh;
                if (!result.artist_zh.empty()) cached_results[i].artist_zh = result.artist_zh;
                if (result.translation_confidence > 0) cached_results[i].translation_confidence = result.translation_confidence;
                if (!result.genre_value.empty()) cached_results[i].genre_value = result.genre_value;
                if (result.genre_confidence > 0) cached_results[i].genre_confidence = result.genre_confidence;
                if (!result.edition_value.empty()) cached_results[i].edition_value = result.edition_value;
                if (result.edition_confidence > 0) cached_results[i].edition_confidence = result.edition_confidence;
                cached_results[i].track_id = result.track_id;
                cached_results[i].success = result.success;
                if (!result.error.empty()) cached_results[i].error = result.error;
            }
            
            if (response.results.size() < batch_tracks.size()) {
                size_t failed_start = response.results.size();
                for (size_t j = failed_start; j < batch_tracks.size(); ++j) {
                    FailedTrackInfo fi;
                    fi.track_id = batch_tracks[j].track_id;
                    fi.reason = FailureReason::Unknown;
                    fi.error_message = "Missing result in response";
                    failed_tracks.push_back(fi);
                }
            }
            
            if (on_progress) {
                int percent = static_cast<int>((batch_idx + 1) * 100 / total_batches);
                std::string msg = "Stage2: " + std::to_string(percent) + "% (batch " + 
                                 std::to_string(batch_idx + 1) + "/" + 
                                 std::to_string(total_batches) + ")";
                on_progress(percent, 100, msg);
            }
        }
        
        if (!failed_tracks.empty() && !was_aborted) {
            Logger::instance().info("stage2_enhance_sync: Retrying " + std::to_string(failed_tracks.size()) + " failed tracks...", __FILE__, __FUNCTION__);
            
            std::map<std::string, size_t> track_id_to_uncached_idx;
            for (size_t i = 0; i < uncached_tracks.size(); ++i) {
                track_id_to_uncached_idx[uncached_tracks[i].track_id] = i;
            }
            
            const size_t retry_batch_size = batch_size_;
            const size_t total_retry_batches = (failed_tracks.size() + retry_batch_size - 1) / retry_batch_size;
            
            for (size_t retry_batch_idx = 0; retry_batch_idx < total_retry_batches; ++retry_batch_idx) {
                if (on_abort && on_abort()) {
                    Logger::instance().debug("stage2_enhance_sync: Abort requested during retry", __FILE__, __FUNCTION__);
                    was_aborted = true;
                    break;
                }
                
                size_t retry_start = retry_batch_idx * retry_batch_size;
                size_t retry_end = (std::min)(retry_start + retry_batch_size, failed_tracks.size());
                
                std::vector<TrackInput> retry_tracks;
                std::vector<size_t> retry_uncached_indices;
                
                for (size_t i = retry_start; i < retry_end; ++i) {
                    const auto& ft = failed_tracks[i];
                    auto it = track_id_to_uncached_idx.find(ft.track_id);
                    if (it != track_id_to_uncached_idx.end()) {
                        retry_tracks.push_back(uncached_tracks[it->second]);
                        retry_uncached_indices.push_back(it->second);
                    }
                }
                
                if (retry_tracks.empty()) continue;
                
                std::string retry_task_id = generate_request_id();
                LOG_INFO("stage2_enhance_sync: Retry batch " + std::to_string(retry_batch_idx + 1) + "/" + 
                                       std::to_string(total_retry_batches) + " (" + std::to_string(retry_tracks.size()) + " tracks)");
                
                nlohmann::json retry_request;
                retry_request["method"] = "stage2_enhance";
                retry_request["id"] = retry_task_id;
                retry_request["version"] = 1;
                retry_request["task_id"] = retry_task_id;
                
                nlohmann::json retry_params;
                retry_params["task_id"] = retry_task_id;
                retry_params["abort_dir"] = abort_dir_;
                
                nlohmann::json retry_tracks_json = nlohmann::json::array();
                for (const auto& track : retry_tracks) {
                    nlohmann::json t;
                    t["track_id"] = track.track_id;
                    t["title"] = track.title;
                    t["artist"] = track.artist;
                    t["album"] = track.album;
                    t["album_artist"] = track.album_artist;
                    t["year"] = track.year;
                    t["genre"] = track.genre;
                    retry_tracks_json.push_back(t);
                }
                retry_params["tracks"] = retry_tracks_json;
                
                nlohmann::json retry_options_json;
                retry_options_json["translate_title"] = options.translate_title;
                retry_options_json["translate_album"] = options.translate_album;
                retry_options_json["translate_artist"] = options.translate_artist;
                retry_options_json["classify_genre"] = options.classify_genre;
                retry_options_json["identify_edition"] = options.identify_edition;
                retry_options_json["target_language"] = options.target_language;
                retry_params["options"] = retry_options_json;
                
                retry_request["params"] = retry_params;
                
                auto retry_response_promise = std::make_shared<std::promise<BatchResponse>>();
                std::future<BatchResponse> retry_response_future = retry_response_promise->get_future();
                
                bool retry_sent = worker_manager_->send_request(
                    retry_request["id"].get<std::string>(),
                    retry_request.dump(),
                    [retry_response_promise](const std::string&, const BatchResponse& resp) {
                        retry_response_promise->set_value(resp);
                    },
                    [retry_response_promise](const std::string&, const ErrorInfo& err) {
                        BatchResponse resp;
                        resp.success = false;
                        resp.error = err;
                        retry_response_promise->set_value(resp);
                    }
                );
                
                if (!retry_sent) continue;
                
                auto retry_status = retry_response_future.wait_for(std::chrono::milliseconds(dynamic_timeout_ms));
                if (retry_status != std::future_status::ready) continue;
                
                BatchResponse retry_response = retry_response_future.get();
                if (!retry_response.success) continue;
                
                for (size_t j = 0; j < retry_response.results.size() && j < retry_uncached_indices.size(); ++j) {
                    const auto& r = retry_response.results[j];
                    size_t uncached_idx = retry_uncached_indices[j];
                    size_t i = uncached_indices[uncached_idx];
                    
                    EnhancementResult result;
                    result.track_id = r.value("track_id", "");
                    result.success = r.value("success", false);
                    result.title_zh = r.value("title_zh", "");
                    result.album_zh = r.value("album_zh", "");
                    result.artist_zh = r.value("artist_zh", "");
                    result.translation_confidence = r.value("translation_confidence", 0.0f);
                    result.genre_value = r.value("genre_value", "");
                    result.genre_confidence = r.value("genre_confidence", 0.0f);
                    result.edition_value = r.value("edition_value", "");
                    result.edition_confidence = r.value("edition_confidence", 0.0f);
                    
                    if (r.contains("error") && !r["error"].is_null()) {
                        result.error = r["error"].get<std::string>();
                    }
                    
                    if (!result.title_zh.empty()) cached_results[i].title_zh = result.title_zh;
                    if (!result.album_zh.empty()) cached_results[i].album_zh = result.album_zh;
                    if (!result.artist_zh.empty()) cached_results[i].artist_zh = result.artist_zh;
                    if (result.translation_confidence > 0) cached_results[i].translation_confidence = result.translation_confidence;
                    if (!result.genre_value.empty()) cached_results[i].genre_value = result.genre_value;
                    if (result.genre_confidence > 0) cached_results[i].genre_confidence = result.genre_confidence;
                    if (!result.edition_value.empty()) cached_results[i].edition_value = result.edition_value;
                    if (result.edition_confidence > 0) cached_results[i].edition_confidence = result.edition_confidence;
                    cached_results[i].track_id = result.track_id;
                    cached_results[i].success = result.success;
                    if (!result.error.empty()) cached_results[i].error = result.error;
                    
                    if (result.success) {
                        Logger::instance().info("stage2_enhance_sync: Retry succeeded for track " + result.track_id, __FILE__, __FUNCTION__);
                    }
                }
            }
        }
        
        restore_timeout();
        
        if (was_aborted) {
            Logger::instance().debug("stage2_enhance_sync: Processing was aborted", __FILE__, __FUNCTION__);
        }
        
        size_t final_success = 0;
        size_t final_failed = 0;
        for (const auto& r : cached_results) {
            if (r.success) {
                final_success++;
            } else {
                final_failed++;
            }
        }
        
        if (final_failed > 0) {
            Logger::instance().info("stage2_enhance_sync: Statistics - Success: " + std::to_string(final_success) + 
                                   ", Failed: " + std::to_string(final_failed), __FILE__, __FUNCTION__);
        }
    }
    
    for (size_t i = 0; i < tracks.size(); ++i) {
        results.push_back(cached_results[i]);
    }
    
    processing_ = false;
    LOG_INFO("stage2_enhance_sync: Complete, returning " + std::to_string(results.size()) + " results");
    return results;
}

std::optional<std::map<std::string, std::string>> AICore::rollback_snapshot(const std::string& track_id) {
    if (!initialized_ || track_id.empty() || !backup_manager_) {
        LOG_ERROR("rollback_snapshot: preconditions failed");
        return std::nullopt;
    }
    
    auto snapshot = backup_manager_->rollback(track_id);
    if (snapshot.has_value()) {
        Logger::instance().debug("rollback_snapshot: SUCCESS for track " + track_id.substr(0, 16) + 
                               ", fields=" + std::to_string(snapshot->size()), __FILE__, __FUNCTION__);
    } else {
        LOG_WARN("rollback_snapshot: no snapshot found for track " + track_id.substr(0, 16) + "...");
    }
    
    return snapshot;
}

bool AICore::ensure_snapshot(
    const std::string& track_id,
    const std::map<std::string, std::string>& snapshot
) {
    if (!initialized_ || track_id.empty() || !backup_manager_) {
        return false;
    }
    return backup_manager_->ensure_snapshot(track_id, snapshot);
}

bool AICore::save_snapshot(
    const std::string& track_id,
    const std::map<std::string, std::string>& snapshot
) {
    Logger::instance().debug("save_snapshot called: track_id=" + track_id.substr(0, 16) + 
                           ", fields=" + std::to_string(snapshot.size()), __FILE__, __FUNCTION__);
    
    if (!initialized_ || track_id.empty() || !backup_manager_) {
        LOG_ERROR("save_snapshot: preconditions failed");
        return false;
    }
    
    return backup_manager_->save_snapshot(track_id, snapshot);
}

std::map<std::string, std::string> AICore::get_snapshot(const std::string& track_id) {
    if (!initialized_ || track_id.empty() || !backup_manager_) {
        return {};
    }
    return backup_manager_->get_snapshot(track_id);
}

bool AICore::has_snapshot(const std::string& track_id) {
    if (!initialized_ || track_id.empty() || !backup_manager_) {
        return false;
    }
    return backup_manager_->has_snapshot(track_id);
}

std::string AICore::generate_stage1_cache_key(const TrackInput& input) {
    return CacheLayer::generate_cache_key(input);
}

std::string AICore::generate_stage2_cache_key(const TrackInput& input, const EnhancementOptions& options) {
    return CacheLayer::generate_stage2_cache_key(input, options);
}

void AICore::save_stage1_cache(
    const std::string& cache_key,
    const TrackScrapingResult& result,
    const TrackInput& input
) {
    if (!initialized_ || !cache_ || cache_key.empty()) {
        return;
    }
    
    Stage1CacheEntry cache_entry;
    cache_entry.track_id = result.track_id;
    cache_entry.file_path = input.file_path;
    cache_entry.title = input.title;
    cache_entry.artist = input.artist;
    cache_entry.album = input.album;
    cache_entry.scraped_fields = result.scraped_fields;
    cache_entry.source = result.release_source;
    cache_entry.success = result.success;
    cache_entry.error_message = result.error;
    
    cache_->set_stage1(cache_key, cache_entry);
    Logger::instance().debug("save_stage1_cache: Saved cache for track " + result.track_id, __FILE__, __FUNCTION__);
}

void AICore::save_stage2_cache(
    const std::string& cache_key,
    const EnhancementResult& result,
    const TrackInput& input,
    const EnhancementOptions& options
) {
    if (!initialized_ || !cache_ || cache_key.empty()) {
        return;
    }
    
    Stage2CacheEntry cache_entry;
    cache_entry.track_id = result.track_id;
    cache_entry.file_path = input.file_path;
    cache_entry.title = input.title;
    cache_entry.artist = input.artist;
    cache_entry.album = input.album;
    cache_entry.success = result.success;
    cache_entry.title_zh = result.title_zh;
    cache_entry.album_zh = result.album_zh;
    cache_entry.artist_zh = result.artist_zh;
    cache_entry.translation_confidence = result.translation_confidence;
    cache_entry.genre_value = result.genre_value;
    cache_entry.genre_confidence = result.genre_confidence;
    cache_entry.edition_value = result.edition_value;
    cache_entry.edition_confidence = result.edition_confidence;
    cache_entry.error_message = result.error;
    
    cache_->set_stage2(cache_key, cache_entry);
    Logger::instance().debug("save_stage2_cache: Saved cache for track " + result.track_id, __FILE__, __FUNCTION__);
}

void AICore::request_abort(const std::string& task_id) {
    if (abort_dir_.empty() || task_id.empty()) {
        LOG_WARN("request_abort: abort_dir or task_id is empty");
        return;
    }
    
    std::string abort_file = abort_dir_ + "\\abort_" + task_id + ".flag";
    
    std::ofstream file(abort_file);
    if (file.is_open()) {
        file << get_current_timestamp();
        file.close();
        Logger::instance().debug("request_abort: Created abort flag for task " + task_id, __FILE__, __FUNCTION__);
    } else {
        LOG_ERROR("request_abort: Failed to create abort flag file: " + abort_file);
    }
}

void AICore::clear_abort(const std::string& task_id) {
    if (abort_dir_.empty() || task_id.empty()) {
        return;
    }
    
    std::string abort_file = abort_dir_ + "\\abort_" + task_id + ".flag";
    
#ifdef _WIN32
    DeleteFileA(abort_file.c_str());
#else
    std::remove(abort_file.c_str());
#endif
    
    Logger::instance().debug("clear_abort: Cleared abort flag for task " + task_id, __FILE__, __FUNCTION__);
}

bool AICore::is_abort_requested(const std::string& task_id) {
    if (abort_dir_.empty() || task_id.empty()) {
        return false;
    }
    
    std::string abort_file = abort_dir_ + "\\abort_" + task_id + ".flag";
    
    std::ifstream file(abort_file);
    bool exists = file.good();
    file.close();
    
    return exists;
}

std::vector<TrackScrapingResult> AICore::process_batch(
    const std::vector<TrackInput>& tracks,
    const ScrapingOptions& options,
    const std::string& task_id,
    AbortCallback on_abort
) {
    std::vector<TrackScrapingResult> results;
    
    if (tracks.empty()) {
        return results;
    }
    
    const uint32_t BASE_TIMEOUT_MS = constants::BASE_TIMEOUT_MS;
    const uint32_t PER_TRACK_TIMEOUT_MS = constants::PER_TRACK_TIMEOUT_MS;
    uint32_t dynamic_timeout_ms = BASE_TIMEOUT_MS + static_cast<uint32_t>(tracks.size()) * PER_TRACK_TIMEOUT_MS;
    uint32_t old_max_silence = worker_manager_->get_max_silence_time_ms();
    worker_manager_->set_max_silence_time_ms(dynamic_timeout_ms);
    Logger::instance().debug("process_batch: Set dynamic timeout to " + std::to_string(dynamic_timeout_ms / 1000) + 
                           "s for " + std::to_string(tracks.size()) + " tracks (was " + 
                           std::to_string(old_max_silence / 1000) + "s)", __FILE__, __FUNCTION__);
    
    auto restore_timeout = [this, old_max_silence]() {
        worker_manager_->set_max_silence_time_ms(old_max_silence);
    };
    
    nlohmann::json request;
    request["method"] = "stage1_scrape";
    request["id"] = task_id;
    request["version"] = 1;
    request["task_id"] = task_id;
    
    nlohmann::json params;
    params["task_id"] = task_id;
    params["abort_dir"] = abort_dir_;
    
    nlohmann::json tracks_json = nlohmann::json::array();
    for (const auto& track : tracks) {
        nlohmann::json t;
        t["track_id"] = track.track_id;
        t["title"] = track.title;
        t["artist"] = track.artist;
        t["album"] = track.album;
        t["album_artist"] = track.album_artist;
        t["year"] = track.year;
        t["track_number"] = track.track_number;
        t["disc_number"] = track.disc_number;
        t["genre"] = track.genre;
        t["composer"] = track.composer;
        t["lyricist"] = track.lyricist;
        t["conductor"] = track.conductor;
        t["performer"] = track.performer;
        t["label"] = track.label;
        tracks_json.push_back(t);
    }
    params["tracks"] = tracks_json;
    
    nlohmann::json options_json;
    options_json["album"] = options.album;
    options_json["year"] = options.year;
    options_json["scrape_title"] = options.scrape_title;
    options_json["scrape_artist"] = options.scrape_artist;
    options_json["scrape_album"] = options.scrape_album;
    options_json["scrape_year"] = options.scrape_year;
    options_json["scrape_track_number"] = options.scrape_track_number;
    options_json["scrape_disc_number"] = options.scrape_disc_number;
    options_json["scrape_composer"] = options.scrape_composer;
    options_json["scrape_lyricist"] = options.scrape_lyricist;
    options_json["scrape_conductor"] = options.scrape_conductor;
    options_json["scrape_performer"] = options.scrape_performer;
    options_json["scrape_label"] = options.scrape_label;
    options_json["enable_musicbrainz"] = options.enable_musicbrainz;
    options_json["enable_discogs"] = options.enable_discogs;
    options_json["enable_ai"] = options.enable_ai;
    options_json["auto_accept_threshold"] = options.auto_accept_threshold;
    options_json["confirm_threshold"] = options.confirm_threshold;
    params["options"] = options_json;
    
    request["params"] = params;
    
    std::string request_str = request.dump();
    
    auto response_promise = std::make_shared<std::promise<BatchResponse>>();
    std::future<BatchResponse> response_future = response_promise->get_future();
    
    bool sent = worker_manager_->send_request(
        request["id"].get<std::string>(),
        request_str,
        [response_promise](const std::string&, const BatchResponse& resp) {
            response_promise->set_value(resp);
        },
        [response_promise](const std::string&, const ErrorInfo& err) {
            BatchResponse resp;
            resp.success = false;
            resp.error = err;
            response_promise->set_value(resp);
        }
    );
    
    if (!sent) {
        LOG_ERROR("process_batch: Failed to send request");
        restore_timeout();
        return results;
    }
    
    auto start_time = std::chrono::steady_clock::now();
    const int check_interval_ms = constants::CHECK_INTERVAL_MS;
    
    while (true) {
        auto status = response_future.wait_for(std::chrono::milliseconds(check_interval_ms));
        
        if (status == std::future_status::ready) {
            break;
        }
        
        if (on_abort && on_abort()) {
            Logger::instance().debug("process_batch: Abort requested", __FILE__, __FUNCTION__);
            request_abort(task_id);
            restore_timeout();
            return results;
        }
        
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start_time).count();
        if (elapsed >= TIMEOUT_MS) {
            LOG_ERROR("process_batch: Timeout");
            restore_timeout();
            return results;
        }
    }
    
    BatchResponse response = response_future.get();
    
    if (!response.success) {
        LOG_ERROR("process_batch: " + 
            (response.error ? response.error->message : "Unknown error"));
        restore_timeout();
        return results;
    }
    
    for (const auto& r : response.results) {
        TrackScrapingResult result;
        result.track_id = r.value("track_id", "");
        result.success = r.value("success", false);
        
        if (r.contains("scraped_fields")) {
            for (auto& [key, val] : r["scraped_fields"].items()) {
                ScrapedField field;
                field.value = val.value("value", "");
                field.confidence = val.value("confidence", 0.0f);
                std::string source = val.value("source", "ai");
                if (source == "musicbrainz") field.source = DataSourceType::MUSICBRAINZ;
                else if (source == "discogs") field.source = DataSourceType::DISCOGS;
                else field.source = DataSourceType::AI;
                result.scraped_fields[key] = field;
            }
        }
        
        if (r.contains("error") && !r["error"].is_null()) {
            result.error = r["error"].get<std::string>();
        }
        
        std::string source_str = r.value("release_source", "ai");
        if (source_str == "musicbrainz") result.release_source = DataSourceType::MUSICBRAINZ;
        else if (source_str == "discogs") result.release_source = DataSourceType::DISCOGS;
        else result.release_source = DataSourceType::AI;
        
        results.push_back(result);
    }
    
    restore_timeout();
    return results;
}

}
