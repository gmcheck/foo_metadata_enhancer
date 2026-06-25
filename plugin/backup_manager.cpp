#include "backup_manager.h"
#include "../core/logger.h"

#include <sstream>
#include <chrono>
#include <iomanip>
#include <ctime>
#include <stdexcept>
#include <algorithm>

#ifdef _WIN32
#include <sqlite3.h>
#else
#include <sqlite3.h>
#endif

namespace ai_metadata {

BackupManager::BackupManager(const std::string& db_path)
    : db_path_(db_path)
    , db_(nullptr)
    , healthy_(false)
{
    init_database();
}

BackupManager::~BackupManager() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (db_) {
        sqlite3_close(static_cast<sqlite3*>(db_));
        db_ = nullptr;
    }
}

void BackupManager::init_database() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    int rc = sqlite3_open(db_path_.c_str(), reinterpret_cast<sqlite3**>(&db_));
    if (rc != SQLITE_OK) {
        healthy_ = false;
        return;
    }
    
    const char* create_table_sql = R"(
        CREATE TABLE IF NOT EXISTS metadata_snapshots (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            track_id TEXT NOT NULL UNIQUE,
            snapshot_data TEXT NOT NULL,
            field_count INTEGER DEFAULT 0,
            created_at TEXT NOT NULL,
            updated_at TEXT NOT NULL
        )
    )";
    
    char* err_msg = nullptr;
    rc = sqlite3_exec(static_cast<sqlite3*>(db_), create_table_sql, nullptr, nullptr, &err_msg);
    
    if (rc != SQLITE_OK) {
        Logger::instance().error("[BackupManager] init_database: Failed to create table: " + std::string(err_msg ? err_msg : "unknown"));
        if (err_msg) sqlite3_free(err_msg);
        healthy_ = false;
        return;
    }
    
    const char* create_index_sql = "CREATE INDEX IF NOT EXISTS idx_snapshot_track_id ON metadata_snapshots(track_id)";
    sqlite3_exec(static_cast<sqlite3*>(db_), create_index_sql, nullptr, nullptr, nullptr);
    
    healthy_ = true;
    Logger::instance().info("[BackupManager] init_database: SUCCESS, db_path=" + db_path_);
}

bool BackupManager::is_field_blacklisted(const std::string& field_name) {
    std::string upper_field = field_name;
    std::transform(upper_field.begin(), upper_field.end(), upper_field.begin(), ::toupper);
    
    if (METADATA_BLACKLIST.count(upper_field) > 0) {
        return true;
    }
    
    if (upper_field.size() > 0 && upper_field[0] == '_') {
        return true;
    }
    
    if (upper_field.find("REPLAYGAIN_") == 0) {
        return true;
    }
    
    if (upper_field.find("FOOBAR2000_") == 0) {
        return true;
    }
    
    return false;
}

bool BackupManager::has_snapshot(const std::string& track_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!healthy_) {
        return false;
    }
    
    const char* select_sql = "SELECT 1 FROM metadata_snapshots WHERE track_id = ? LIMIT 1";
    
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(
        static_cast<sqlite3*>(db_),
        select_sql,
        -1,
        &stmt,
        nullptr
    );
    
    if (rc != SQLITE_OK) {
        return false;
    }
    
    sqlite3_bind_text(stmt, 1, track_id.c_str(), -1, SQLITE_TRANSIENT);
    
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    return rc == SQLITE_ROW;
}

bool BackupManager::save_snapshot(
    const std::string& track_id,
    const std::map<std::string, std::string>& snapshot
) {
    Logger::instance().info("[BackupManager] save_snapshot: track_id=" + track_id.substr(0, 16) + "...");
    
    if (has_snapshot(track_id)) {
        Logger::instance().debug("[BackupManager] save_snapshot: already exists");
        return false;
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!healthy_) {
        Logger::instance().error("[BackupManager] save_snapshot: not healthy");
        return false;
    }
    
    std::string snapshot_json = serialize_snapshot(snapshot);
    std::string timestamp = get_current_timestamp();
    int field_count = static_cast<int>(snapshot.size());
    
    const char* insert_sql = R"(
        INSERT INTO metadata_snapshots (
            track_id, snapshot_data, field_count, created_at, updated_at
        ) VALUES (?, ?, ?, ?, ?)
    )";
    
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(
        static_cast<sqlite3*>(db_),
        insert_sql,
        -1,
        &stmt,
        nullptr
    );
    
    if (rc != SQLITE_OK) {
        Logger::instance().error("[BackupManager] save_snapshot: prepare failed: " + std::string(sqlite3_errmsg(static_cast<sqlite3*>(db_))));
        return false;
    }
    
    sqlite3_bind_text(stmt, 1, track_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, snapshot_json.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, field_count);
    sqlite3_bind_text(stmt, 4, timestamp.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, timestamp.c_str(), -1, SQLITE_TRANSIENT);
    
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    if (rc != SQLITE_DONE) {
        if (rc == SQLITE_CONSTRAINT) {
            Logger::instance().debug("[BackupManager] save_snapshot: already exists (race condition)");
        } else {
            Logger::instance().error("[BackupManager] save_snapshot: step failed: " + std::string(sqlite3_errmsg(static_cast<sqlite3*>(db_))));
        }
        return false;
    }
    
    Logger::instance().info("[BackupManager] save_snapshot: SUCCESS, field_count=" + std::to_string(field_count));
    return true;
}

bool BackupManager::ensure_snapshot(
    const std::string& track_id,
    const std::map<std::string, std::string>& snapshot
) {
    if (has_snapshot(track_id)) {
        return true;
    }
    return save_snapshot(track_id, snapshot);
}

std::map<std::string, std::string> BackupManager::get_snapshot(const std::string& track_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::map<std::string, std::string> result;
    
    if (!healthy_) {
        return result;
    }
    
    const char* select_sql = "SELECT snapshot_data FROM metadata_snapshots WHERE track_id = ?";
    
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(
        static_cast<sqlite3*>(db_),
        select_sql,
        -1,
        &stmt,
        nullptr
    );
    
    if (rc != SQLITE_OK) {
        return result;
    }
    
    sqlite3_bind_text(stmt, 1, track_id.c_str(), -1, SQLITE_TRANSIENT);
    
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        const unsigned char* json_text = sqlite3_column_text(stmt, 0);
        if (json_text) {
            result = deserialize_snapshot(reinterpret_cast<const char*>(json_text));
        }
    }
    
    sqlite3_finalize(stmt);
    return result;
}

std::optional<std::map<std::string, std::string>> BackupManager::rollback(
    const std::string& track_id
) {
    auto snapshot = get_snapshot(track_id);
    if (snapshot.empty()) {
        Logger::instance().warning("[BackupManager] rollback: no snapshot found for track " + track_id.substr(0, 16) + "...");
        return std::nullopt;
    }
    
    Logger::instance().info("[BackupManager] rollback: SUCCESS for track " + track_id.substr(0, 16) + "..., field_count=" + std::to_string(snapshot.size()));
    return snapshot;
}

std::map<std::string, std::map<std::string, std::string>> BackupManager::batch_rollback(
    const std::vector<std::string>& track_ids
) {
    std::map<std::string, std::map<std::string, std::string>> results;
    
    if (track_ids.empty()) {
        return results;
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!healthy_) {
        return results;
    }
    
    std::ostringstream placeholders;
    for (size_t i = 0; i < track_ids.size(); ++i) {
        if (i > 0) placeholders << ",";
        placeholders << "?";
    }
    
    std::string select_sql = "SELECT track_id, snapshot_data FROM metadata_snapshots WHERE track_id IN (" + placeholders.str() + ")";
    
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(
        static_cast<sqlite3*>(db_),
        select_sql.c_str(),
        -1,
        &stmt,
        nullptr
    );
    
    if (rc != SQLITE_OK) {
        return results;
    }
    
    for (size_t i = 0; i < track_ids.size(); ++i) {
        sqlite3_bind_text(stmt, static_cast<int>(i + 1), track_ids[i].c_str(), -1, SQLITE_TRANSIENT);
    }
    
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        const char* track_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        const unsigned char* json_text = sqlite3_column_text(stmt, 1);
        
        if (track_id && json_text) {
            results[track_id] = deserialize_snapshot(reinterpret_cast<const char*>(json_text));
        }
    }
    
    sqlite3_finalize(stmt);
    
    Logger::instance().info("[BackupManager] batch_rollback: found " + std::to_string(results.size()) + "/" + std::to_string(track_ids.size()) + " tracks");
    return results;
}

bool BackupManager::delete_snapshot(const std::string& track_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!healthy_) {
        return false;
    }
    
    const char* delete_sql = "DELETE FROM metadata_snapshots WHERE track_id = ?";
    
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(
        static_cast<sqlite3*>(db_),
        delete_sql,
        -1,
        &stmt,
        nullptr
    );
    
    if (rc != SQLITE_OK) {
        return false;
    }
    
    sqlite3_bind_text(stmt, 1, track_id.c_str(), -1, SQLITE_TRANSIENT);
    
    rc = sqlite3_step(stmt);
    int changes = sqlite3_changes(static_cast<sqlite3*>(db_));
    
    sqlite3_finalize(stmt);
    
    if (changes > 0) {
        Logger::instance().info("[BackupManager] delete_snapshot: deleted for track " + track_id.substr(0, 16) + "...");
    }
    
    return changes > 0;
}

std::vector<std::string> BackupManager::get_all_tracks_with_snapshot() {
    std::vector<std::string> track_ids;
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!healthy_) {
        return track_ids;
    }
    
    const char* select_sql = "SELECT track_id FROM metadata_snapshots";
    
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(
        static_cast<sqlite3*>(db_),
        select_sql,
        -1,
        &stmt,
        nullptr
    );
    
    if (rc != SQLITE_OK) {
        return track_ids;
    }
    
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        const char* track_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        if (track_id) {
            track_ids.push_back(track_id);
        }
    }
    
    sqlite3_finalize(stmt);
    return track_ids;
}

int BackupManager::get_snapshot_count() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!healthy_) {
        return 0;
    }
    
    const char* select_sql = "SELECT COUNT(*) FROM metadata_snapshots";
    
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(
        static_cast<sqlite3*>(db_),
        select_sql,
        -1,
        &stmt,
        nullptr
    );
    
    if (rc != SQLITE_OK) {
        return 0;
    }
    
    int count = 0;
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        count = sqlite3_column_int(stmt, 0);
    }
    
    sqlite3_finalize(stmt);
    return count;
}

bool BackupManager::is_healthy() const {
    return healthy_;
}

std::string BackupManager::serialize_snapshot(
    const std::map<std::string, std::string>& snapshot
) {
    std::ostringstream oss;
    oss << "{";
    
    bool first = true;
    for (const auto& [key, value] : snapshot) {
        if (!first) {
            oss << ",";
        }
        first = false;
        
        oss << "\"" << key << "\":";
        
        std::string escaped_value;
        for (char c : value) {
            switch (c) {
                case '"': escaped_value += "\\\""; break;
                case '\\': escaped_value += "\\\\"; break;
                case '\n': escaped_value += "\\n"; break;
                case '\r': escaped_value += "\\r"; break;
                case '\t': escaped_value += "\\t"; break;
                default: escaped_value += c; break;
            }
        }
        oss << "\"" << escaped_value << "\"";
    }
    
    oss << "}";
    return oss.str();
}

std::map<std::string, std::string> BackupManager::deserialize_snapshot(
    const std::string& json_str
) {
    std::map<std::string, std::string> result;
    
    if (json_str.empty() || json_str[0] != '{') {
        return result;
    }
    
    size_t pos = 1;
    while (pos < json_str.size() && json_str[pos] != '}') {
        while (pos < json_str.size() && (json_str[pos] == ' ' || json_str[pos] == ',')) {
            pos++;
        }
        
        if (pos >= json_str.size() || json_str[pos] != '"') {
            break;
        }
        
        pos++;
        std::string key;
        while (pos < json_str.size() && json_str[pos] != '"') {
            if (json_str[pos] == '\\' && pos + 1 < json_str.size()) {
                pos++;
                switch (json_str[pos]) {
                    case '"': key += '"'; break;
                    case '\\': key += '\\'; break;
                    case 'n': key += '\n'; break;
                    case 'r': key += '\r'; break;
                    case 't': key += '\t'; break;
                    default: key += json_str[pos]; break;
                }
            } else {
                key += json_str[pos];
            }
            pos++;
        }
        pos++;
        
        while (pos < json_str.size() && json_str[pos] != ':') {
            pos++;
        }
        pos++;
        
        while (pos < json_str.size() && json_str[pos] != '"') {
            pos++;
        }
        pos++;
        
        std::string value;
        while (pos < json_str.size() && json_str[pos] != '"') {
            if (json_str[pos] == '\\' && pos + 1 < json_str.size()) {
                pos++;
                switch (json_str[pos]) {
                    case '"': value += '"'; break;
                    case '\\': value += '\\'; break;
                    case 'n': value += '\n'; break;
                    case 'r': value += '\r'; break;
                    case 't': value += '\t'; break;
                    default: value += json_str[pos]; break;
                }
            } else {
                value += json_str[pos];
            }
            pos++;
        }
        pos++;
        
        result[key] = value;
    }
    
    return result;
}

std::string BackupManager::get_current_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    
    std::tm tm_buf;
#ifdef _WIN32
    localtime_s(&tm_buf, &time);
#else
    localtime_r(&time, &tm_buf);
#endif
    
    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%Y-%m-%dT%H:%M:%S");
    return oss.str();
}

}
