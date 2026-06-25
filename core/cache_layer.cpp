#include "cache_layer.h"
#include "logger.h"
#include "../include/constants.h"
#include "../include/third_party/sqlite/sqlite3.h"
#include <nlohmann/json.hpp>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <algorithm>
#include <functional>
#include <cstring>
#include <fstream>
#include <chrono>

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#endif

namespace ai_metadata {

static std::string get_current_timestamp() {
    auto now = std::time(nullptr);
    auto tm = *std::localtime(&now);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

static std::string sha256_hash(const std::string& input) {
    unsigned char hash[32] = {0};
    
    const uint32_t k[64] = {
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
    };
    
    auto rotr = [](uint32_t x, int n) { return (x >> n) | (x << (32 - n)); };
    
    std::string msg = input;
    uint64_t bit_len = msg.size() * 8;
    
    msg += (char)0x80;
    while (msg.size() % 64 != 56) {
        msg += (char)0x00;
    }
    
    for (int i = 7; i >= 0; --i) {
        msg += (char)((bit_len >> (i * 8)) & 0xFF);
    }
    
    uint32_t h[8] = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
    };
    
    for (size_t chunk = 0; chunk < msg.size(); chunk += 64) {
        uint32_t w[64] = {0};
        
        for (int i = 0; i < 16; ++i) {
            w[i] = ((uint32_t)(unsigned char)msg[chunk + i*4] << 24) |
                   ((uint32_t)(unsigned char)msg[chunk + i*4 + 1] << 16) |
                   ((uint32_t)(unsigned char)msg[chunk + i*4 + 2] << 8) |
                   ((uint32_t)(unsigned char)msg[chunk + i*4 + 3]);
        }
        
        for (int i = 16; i < 64; ++i) {
            uint32_t s0 = rotr(w[i-15], 7) ^ rotr(w[i-15], 18) ^ (w[i-15] >> 3);
            uint32_t s1 = rotr(w[i-2], 17) ^ rotr(w[i-2], 19) ^ (w[i-2] >> 10);
            w[i] = w[i-16] + s0 + w[i-7] + s1;
        }
        
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
        uint32_t e = h[4], f = h[5], g = h[6], hh = h[7];
        
        for (int i = 0; i < 64; ++i) {
            uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            uint32_t ch = (e & f) ^ ((~e) & g);
            uint32_t temp1 = hh + S1 + ch + k[i] + w[i];
            uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            uint32_t temp2 = S0 + maj;
            
            hh = g; g = f; f = e; e = d + temp1;
            d = c; c = b; b = a; a = temp1 + temp2;
        }
        
        h[0] += a; h[1] += b; h[2] += c; h[3] += d;
        h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }
    
    std::ostringstream result;
    for (int i = 0; i < 8; ++i) {
        result << std::hex << std::setfill('0') << std::setw(8) << h[i];
    }
    
    return result.str();
}

std::string CacheLayer::generate_track_uid(const std::string& path, uint32_t subsong, uint64_t file_size) {
    std::string combined = path + "|" + std::to_string(subsong) + "|" + std::to_string(file_size);
    
    std::transform(combined.begin(), combined.end(), combined.begin(), ::tolower);
    
    std::string hash = sha256_hash(combined);
    
    Logger::instance().debug("generate_track_uid: path=" + path + 
                           ", subsong=" + std::to_string(subsong) +
                           ", file_size=" + std::to_string(file_size) +
                           ", combined=" + combined +
                           ", hash=" + hash.substr(0, 16) + "...", __FILE__, __FUNCTION__);
    
    return hash;
}

std::string CacheLayer::generate_cache_key(const TrackInput& input) {
    return input.track_id;
}

std::string CacheLayer::generate_stage2_cache_key(const TrackInput& input, const EnhancementOptions& options) {
    (void)options;
    return input.track_id;
}

CacheLayer::CacheLayer(const std::string& db_path) : db_path_(db_path) {
    init_database();
}

CacheLayer::~CacheLayer() {
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

void CacheLayer::init_database() {
    LOG_INFO("init_database: Opening database at " + db_path_);
    
    int rc = sqlite3_open(db_path_.c_str(), &db_);
    if (rc != SQLITE_OK) {
        LOG_ERROR("init_database: Failed to open database, error = " + std::string(sqlite3_errmsg(db_)));
        db_ = nullptr;
        return;
    }
    
    Logger::instance().debug("init_database: Database opened successfully", __FILE__, __FUNCTION__);
    
    sqlite3_busy_timeout(db_, constants::SQLITE_BUSY_TIMEOUT_MS);
    
    sqlite3_exec(db_, "PRAGMA journal_mode=WAL", nullptr, nullptr, nullptr);
    sqlite3_exec(db_, "PRAGMA synchronous=NORMAL", nullptr, nullptr, nullptr);
    
    Logger::instance().debug("init_database: Creating tables", __FILE__, __FUNCTION__);
    create_tables();
    
    Logger::instance().debug("init_database: Creating indexes", __FILE__, __FUNCTION__);
    create_indexes();
    
    Logger::instance().debug("init_database: Checking integrity", __FILE__, __FUNCTION__);
    if (!check_integrity()) {
        LOG_ERROR("init_database: Integrity check failed");
        sqlite3_close(db_);
        db_ = nullptr;
    }
    
    LOG_INFO("init_database: Initialization complete");
}

void CacheLayer::create_tables() {
    const char* create_stats_table = R"(
        CREATE TABLE IF NOT EXISTS cache_statistics (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            stat_date TEXT NOT NULL UNIQUE,
            total_entries INTEGER DEFAULT 0,
            total_hits INTEGER DEFAULT 0,
            total_misses INTEGER DEFAULT 0,
            hit_rate REAL DEFAULT 0.0,
            api_calls_saved INTEGER DEFAULT 0,
            avg_query_time_ms REAL DEFAULT 0.0,
            created_at TEXT NOT NULL,
            updated_at TEXT NOT NULL
        )
    )";
    
    sqlite3_exec(db_, create_stats_table, nullptr, nullptr, nullptr);
    
    const char* create_config_table = R"(
        CREATE TABLE IF NOT EXISTS cache_config (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            key TEXT NOT NULL UNIQUE,
            value TEXT NOT NULL,
            created_at TEXT NOT NULL,
            updated_at TEXT NOT NULL
        )
    )";
    
    sqlite3_exec(db_, create_config_table, nullptr, nullptr, nullptr);
    
    const char* create_stage1_cache_table = R"(
        CREATE TABLE IF NOT EXISTS stage1_cache (
            cache_key TEXT PRIMARY KEY NOT NULL,
            track_id TEXT,
            file_path TEXT,
            title TEXT NOT NULL,
            artist TEXT NOT NULL,
            album TEXT,
            scraped_fields_json TEXT,
            source TEXT DEFAULT 'ai',
            success INTEGER DEFAULT 1,
            error_code TEXT,
            error_message TEXT,
            cache_hit_count INTEGER DEFAULT 0,
            last_accessed_at TEXT,
            created_at TEXT NOT NULL,
            updated_at TEXT NOT NULL
        )
    )";
    
    sqlite3_exec(db_, create_stage1_cache_table, nullptr, nullptr, nullptr);
    
    const char* create_stage2_cache_table = R"(
        CREATE TABLE IF NOT EXISTS stage2_cache (
            cache_key TEXT PRIMARY KEY NOT NULL,
            track_id TEXT,
            file_path TEXT,
            title TEXT NOT NULL,
            artist TEXT NOT NULL,
            album TEXT,
            success INTEGER DEFAULT 1,
            title_zh TEXT,
            album_zh TEXT,
            artist_zh TEXT,
            translation_confidence REAL DEFAULT 0.0,
            genre_value TEXT,
            genre_confidence REAL DEFAULT 0.0,
            edition_value TEXT,
            edition_confidence REAL DEFAULT 0.0,
            error_code TEXT,
            error_message TEXT,
            cache_hit_count INTEGER DEFAULT 0,
            last_accessed_at TEXT,
            created_at TEXT NOT NULL,
            updated_at TEXT NOT NULL
        )
    )";
    
    sqlite3_exec(db_, create_stage2_cache_table, nullptr, nullptr, nullptr);
    
    std::string init_config_sql = 
        "INSERT OR IGNORE INTO cache_config (key, value, created_at, updated_at) VALUES "
        "('version', '1', datetime('now'), datetime('now')), "
        "('expiration_days', '365', datetime('now'), datetime('now')), "
        "('max_cache_size_mb', '" + std::to_string(constants::DEFAULT_MAX_CACHE_SIZE_MB) + "', datetime('now'), datetime('now')), "
        "('auto_cleanup', 'true', datetime('now'), datetime('now'))";
    
    sqlite3_exec(db_, init_config_sql.c_str(), nullptr, nullptr, nullptr);
}

void CacheLayer::create_indexes() {
    const char* indexes[] = {
        "CREATE INDEX IF NOT EXISTS idx_stat_date ON cache_statistics(stat_date)",
        "CREATE INDEX IF NOT EXISTS idx_stage1_cache_key ON stage1_cache(cache_key)",
        "CREATE INDEX IF NOT EXISTS idx_stage2_cache_key ON stage2_cache(cache_key)"
    };
    
    for (const char* idx : indexes) {
        sqlite3_exec(db_, idx, nullptr, nullptr, nullptr);
    }
}

bool CacheLayer::check_integrity() {
    char* errmsg = nullptr;
    int result = sqlite3_exec(db_, "PRAGMA integrity_check", nullptr, nullptr, &errmsg);
    
    if (result != SQLITE_OK) {
        if (errmsg) {
            sqlite3_free(errmsg);
        }
        return false;
    }
    
    return true;
}

void CacheLayer::clear_all() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!db_) return;
    
    sqlite3_exec(db_, "DELETE FROM stage1_cache", nullptr, nullptr, nullptr);
    sqlite3_exec(db_, "DELETE FROM stage2_cache", nullptr, nullptr, nullptr);
    sqlite3_exec(db_, "DELETE FROM cache_statistics", nullptr, nullptr, nullptr);
}

int CacheLayer::clear_by_track_ids(const std::vector<std::string>& track_ids) {
    if (track_ids.empty()) return 0;
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!db_) return 0;
    
    int total_deleted = 0;
    
    std::string placeholders;
    for (size_t i = 0; i < track_ids.size(); ++i) {
        if (i > 0) placeholders += ",";
        placeholders += "?";
    }
    
    std::string sql1 = "DELETE FROM stage1_cache WHERE track_id IN (" + placeholders + ")";
    sqlite3_stmt* stmt1 = nullptr;
    if (sqlite3_prepare_v2(db_, sql1.c_str(), -1, &stmt1, nullptr) == SQLITE_OK) {
        for (size_t i = 0; i < track_ids.size(); ++i) {
            sqlite3_bind_text(stmt1, static_cast<int>(i + 1), track_ids[i].c_str(), -1, SQLITE_TRANSIENT);
        }
        if (sqlite3_step(stmt1) == SQLITE_DONE) {
            total_deleted += static_cast<int>(sqlite3_changes(db_));
        }
        sqlite3_finalize(stmt1);
    }
    
    std::string sql2 = "DELETE FROM stage2_cache WHERE track_id IN (" + placeholders + ")";
    sqlite3_stmt* stmt2 = nullptr;
    if (sqlite3_prepare_v2(db_, sql2.c_str(), -1, &stmt2, nullptr) == SQLITE_OK) {
        for (size_t i = 0; i < track_ids.size(); ++i) {
            sqlite3_bind_text(stmt2, static_cast<int>(i + 1), track_ids[i].c_str(), -1, SQLITE_TRANSIENT);
        }
        if (sqlite3_step(stmt2) == SQLITE_DONE) {
            total_deleted += static_cast<int>(sqlite3_changes(db_));
        }
        sqlite3_finalize(stmt2);
    }
    
    return total_deleted;
}

CacheStatistics CacheLayer::get_statistics() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    CacheStatistics stats;
    
    if (!db_) return stats;
    
    const char* sql = R"(
        SELECT 
            (SELECT COUNT(*) FROM stage1_cache) + (SELECT COUNT(*) FROM stage2_cache) as total_entries,
            (SELECT COALESCE(SUM(cache_hit_count), 0) FROM stage1_cache) + (SELECT COALESCE(SUM(cache_hit_count), 0) FROM stage2_cache) as total_hits
    )";
    
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            stats.total_entries = sqlite3_column_int(stmt, 0);
            stats.total_hits = sqlite3_column_int(stmt, 1);
            
            stats.total_misses = stats.total_entries > stats.total_hits ? stats.total_entries - stats.total_hits : 0;
            
            int total = stats.total_hits + stats.total_misses;
            if (total > 0) {
                stats.hit_rate = (double)stats.total_hits / total * 100.0;
            }
        }
        sqlite3_finalize(stmt);
    }
    
    stats.api_calls_saved = stats.total_hits;
    stats.db_size_mb = get_database_size_mb();
    
    std::string today = get_current_timestamp().substr(0, 10);
    const char* insert_sql = R"(
        INSERT INTO cache_statistics (stat_date, total_entries, total_hits, total_misses, hit_rate, api_calls_saved, avg_query_time_ms, created_at, updated_at)
        VALUES (?, ?, ?, ?, ?, ?, 0.0, datetime('now'), datetime('now'))
        ON CONFLICT(stat_date) DO UPDATE SET
            total_entries = excluded.total_entries,
            total_hits = excluded.total_hits,
            total_misses = excluded.total_misses,
            hit_rate = excluded.hit_rate,
            api_calls_saved = excluded.api_calls_saved,
            updated_at = datetime('now')
    )";
    
    sqlite3_stmt* insert_stmt = nullptr;
    if (sqlite3_prepare_v2(db_, insert_sql, -1, &insert_stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(insert_stmt, 1, today.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(insert_stmt, 2, stats.total_entries);
        sqlite3_bind_int(insert_stmt, 3, stats.total_hits);
        sqlite3_bind_int(insert_stmt, 4, stats.total_misses);
        sqlite3_bind_double(insert_stmt, 5, stats.hit_rate);
        sqlite3_bind_int(insert_stmt, 6, stats.api_calls_saved);
        sqlite3_step(insert_stmt);
        sqlite3_finalize(insert_stmt);
    }
    
    return stats;
}

std::string CacheLayer::get_config_internal(const std::string& key) {
    if (!db_) return "";
    
    const char* sql = "SELECT value FROM cache_config WHERE key = ?";
    sqlite3_stmt* stmt = nullptr;
    std::string value;
    
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            value = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        }
        sqlite3_finalize(stmt);
    }
    
    return value;
}

std::string CacheLayer::get_config(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    return get_config_internal(key);
}

void CacheLayer::set_config(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!db_) return;
    
    auto escape_sql = [](const std::string& s) -> std::string {
        std::string result;
        result.reserve(s.size() * 2);
        for (char c : s) {
            if (c == '\'') result += "''";
            else result += c;
        }
        return result;
    };
    
    std::string sql = "INSERT OR REPLACE INTO cache_config (key, value, updated_at) VALUES ('" 
                     + escape_sql(key) + "', '" + escape_sql(value) + "', datetime('now'))";
    
    char* err_msg = nullptr;
    if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err_msg) != SQLITE_OK) {
        if (err_msg) sqlite3_free(err_msg);
    }
}

void CacheLayer::optimize_database() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!db_) return;
    
    sqlite3_exec(db_, "REINDEX", nullptr, nullptr, nullptr);
    sqlite3_exec(db_, "ANALYZE", nullptr, nullptr, nullptr);
}

void CacheLayer::vacuum_database() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!db_) return;
    
    sqlite3_exec(db_, "VACUUM", nullptr, nullptr, nullptr);
}

int CacheLayer::get_database_size_mb() {
    if (!db_) return 0;
    
    const char* sql = "SELECT (SUM(pgsize) / 1024.0 / 1024.0) as size_mb FROM dbstat";
    sqlite3_stmt* stmt = nullptr;
    int size_mb = 0;
    
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            size_mb = static_cast<int>(sqlite3_column_double(stmt, 0));
        }
        sqlite3_finalize(stmt);
    }
    
    return size_mb;
}

std::optional<Stage1CacheEntry> CacheLayer::get_stage1(const std::string& cache_key) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!db_) return std::nullopt;
    
    const char* sql = "SELECT track_id, file_path, title, artist, album, scraped_fields_json, source, success, error_code, error_message "
                      "FROM stage1_cache WHERE cache_key = ?";
    sqlite3_stmt* stmt = nullptr;
    
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return std::nullopt;
    }
    
    sqlite3_bind_text(stmt, 1, cache_key.c_str(), -1, SQLITE_TRANSIENT);
    
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        Stage1CacheEntry entry;
        entry.track_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        if (sqlite3_column_type(stmt, 1) != SQLITE_NULL) {
            entry.file_path = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        }
        entry.title = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        entry.artist = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        entry.album = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        
        std::string fields_json = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        std::string source_str = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
        entry.success = sqlite3_column_int(stmt, 7) != 0;
        
        if (sqlite3_column_type(stmt, 8) != SQLITE_NULL) {
            entry.error_code = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));
        }
        if (sqlite3_column_type(stmt, 9) != SQLITE_NULL) {
            entry.error_message = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 9));
        }
        
        if (source_str == "musicbrainz") entry.source = DataSourceType::MUSICBRAINZ;
        else if (source_str == "discogs") entry.source = DataSourceType::DISCOGS;
        else entry.source = DataSourceType::AI;
        
        if (!fields_json.empty()) {
            try {
                auto j = nlohmann::json::parse(fields_json);
                for (auto& [key, val] : j.items()) {
                    ScrapedField field;
                    field.value = val.value("value", "");
                    field.confidence = val.value("confidence", 0.0f);
                    std::string src = val.value("source", "ai");
                    if (src == "musicbrainz") field.source = DataSourceType::MUSICBRAINZ;
                    else if (src == "discogs") field.source = DataSourceType::DISCOGS;
                    else field.source = DataSourceType::AI;
                    entry.scraped_fields[key] = field;
                }
            } catch (...) {}
        }
        
        sqlite3_finalize(stmt);
        
        const char* update_sql = "UPDATE stage1_cache SET cache_hit_count = cache_hit_count + 1, last_accessed_at = datetime('now') WHERE cache_key = ?";
        sqlite3_stmt* update_stmt = nullptr;
        if (sqlite3_prepare_v2(db_, update_sql, -1, &update_stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(update_stmt, 1, cache_key.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(update_stmt);
            sqlite3_finalize(update_stmt);
        }
        
        return entry;
    }
    
    sqlite3_finalize(stmt);
    return std::nullopt;
}

void CacheLayer::set_stage1(const std::string& cache_key, const Stage1CacheEntry& entry) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!db_) return;
    
    nlohmann::json fields_json;
    for (const auto& [key, field] : entry.scraped_fields) {
        nlohmann::json f;
        f["value"] = field.value;
        f["confidence"] = field.confidence;
        std::string src = "ai";
        if (field.source == DataSourceType::MUSICBRAINZ) src = "musicbrainz";
        else if (field.source == DataSourceType::DISCOGS) src = "discogs";
        f["source"] = src;
        fields_json[key] = f;
    }
    
    std::string source_str = "ai";
    if (entry.source == DataSourceType::MUSICBRAINZ) source_str = "musicbrainz";
    else if (entry.source == DataSourceType::DISCOGS) source_str = "discogs";
    
    auto escape_sql = [](const std::string& s) -> std::string {
        std::string result;
        result.reserve(s.size() * 2);
        for (char c : s) {
            if (c == '\'') result += "''";
            else result += c;
        }
        return result;
    };
    
    std::string sql = 
        "INSERT OR REPLACE INTO stage1_cache (cache_key, track_id, file_path, title, artist, album, "
        "scraped_fields_json, source, success, error_code, error_message, cache_hit_count, last_accessed_at, created_at, updated_at) VALUES ("
        "'" + escape_sql(cache_key) + "', "
        "'" + escape_sql(entry.track_id) + "', "
        "'" + escape_sql(entry.file_path) + "', "
        "'" + escape_sql(entry.title) + "', "
        "'" + escape_sql(entry.artist) + "', "
        "'" + escape_sql(entry.album) + "', "
        "'" + escape_sql(fields_json.dump()) + "', "
        "'" + escape_sql(source_str) + "', "
        + std::string(entry.success ? "1" : "0") + ", "
        "'" + escape_sql(entry.error_code) + "', "
        "'" + escape_sql(entry.error_message) + "', "
        "0, datetime('now'), datetime('now'), datetime('now'))";
    
    char* err_msg = nullptr;
    if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err_msg) != SQLITE_OK) {
        if (err_msg) sqlite3_free(err_msg);
    }
}

std::optional<Stage2CacheEntry> CacheLayer::get_stage2(const std::string& cache_key) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!db_) return std::nullopt;
    
    const char* sql = "SELECT track_id, file_path, title, artist, album, success, title_zh, album_zh, artist_zh, "
                      "translation_confidence, genre_value, genre_confidence, edition_value, edition_confidence, error_code, error_message "
                      "FROM stage2_cache WHERE cache_key = ?";
    sqlite3_stmt* stmt = nullptr;
    
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return std::nullopt;
    }
    
    sqlite3_bind_text(stmt, 1, cache_key.c_str(), -1, SQLITE_TRANSIENT);
    
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        Stage2CacheEntry entry;
        entry.track_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        if (sqlite3_column_type(stmt, 1) != SQLITE_NULL) {
            entry.file_path = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        }
        entry.title = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        entry.artist = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        entry.album = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        entry.success = sqlite3_column_int(stmt, 5) != 0;
        entry.title_zh = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
        entry.album_zh = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
        entry.artist_zh = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));
        entry.translation_confidence = static_cast<float>(sqlite3_column_double(stmt, 9));
        entry.genre_value = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 10));
        entry.genre_confidence = static_cast<float>(sqlite3_column_double(stmt, 11));
        entry.edition_value = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 12));
        entry.edition_confidence = static_cast<float>(sqlite3_column_double(stmt, 13));
        
        if (sqlite3_column_type(stmt, 14) != SQLITE_NULL) {
            entry.error_code = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 14));
        }
        if (sqlite3_column_type(stmt, 15) != SQLITE_NULL) {
            entry.error_message = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 15));
        }
        
        sqlite3_finalize(stmt);
        
        const char* update_sql = "UPDATE stage2_cache SET cache_hit_count = cache_hit_count + 1, last_accessed_at = datetime('now') WHERE cache_key = ?";
        sqlite3_stmt* update_stmt = nullptr;
        if (sqlite3_prepare_v2(db_, update_sql, -1, &update_stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(update_stmt, 1, cache_key.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(update_stmt);
            sqlite3_finalize(update_stmt);
        }
        
        return entry;
    }
    
    sqlite3_finalize(stmt);
    return std::nullopt;
}

void CacheLayer::set_stage2(const std::string& cache_key, const Stage2CacheEntry& entry) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!db_) return;
    
    auto escape_sql = [](const std::string& s) -> std::string {
        std::string result;
        result.reserve(s.size() * 2);
        for (char c : s) {
            if (c == '\'') result += "''";
            else result += c;
        }
        return result;
    };
    
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(6);
    
    std::string sql = 
        "INSERT OR REPLACE INTO stage2_cache (cache_key, track_id, file_path, title, artist, album, "
        "success, title_zh, album_zh, artist_zh, translation_confidence, "
        "genre_value, genre_confidence, edition_value, edition_confidence, error_code, error_message, "
        "cache_hit_count, last_accessed_at, created_at, updated_at) VALUES ("
        "'" + escape_sql(cache_key) + "', "
        "'" + escape_sql(entry.track_id) + "', "
        "'" + escape_sql(entry.file_path) + "', "
        "'" + escape_sql(entry.title) + "', "
        "'" + escape_sql(entry.artist) + "', "
        "'" + escape_sql(entry.album) + "', "
        + std::string(entry.success ? "1" : "0") + ", "
        "'" + escape_sql(entry.title_zh) + "', "
        "'" + escape_sql(entry.album_zh) + "', "
        "'" + escape_sql(entry.artist_zh) + "', "
        + std::to_string(entry.translation_confidence) + ", "
        "'" + escape_sql(entry.genre_value) + "', "
        + std::to_string(entry.genre_confidence) + ", "
        "'" + escape_sql(entry.edition_value) + "', "
        + std::to_string(entry.edition_confidence) + ", "
        "'" + escape_sql(entry.error_code) + "', "
        "'" + escape_sql(entry.error_message) + "', "
        "0, datetime('now'), datetime('now'), datetime('now'))";
    
    char* err_msg = nullptr;
    if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err_msg) != SQLITE_OK) {
        if (err_msg) sqlite3_free(err_msg);
    }
}

}
