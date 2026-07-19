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

    // 新库直接建带 operation_type 的表；旧库由 migrate_schema 处理
    // rollback_snapshot：每次 Scrape/Translate/Normalize 操作前的 tag 字段快照
    //   - 每个 (track_id, operation_type) 仅保留最近一次快照（写入时覆盖）
    //   - 用于 Rollback 菜单恢复指定操作前的字段状态
    //   - 由 BackupManager 管理（C++ 端，UI 同步流程）
    const char* create_table_sql = R"(
        CREATE TABLE IF NOT EXISTS rollback_snapshot (
            id              INTEGER PRIMARY KEY AUTOINCREMENT, -- 自增主键
            track_id        TEXT NOT NULL,                     -- 同 scrape_cache.track_id
            operation_type  TEXT NOT NULL DEFAULT 'scrape',    -- 操作类型：scrape / translate / normalize
            snapshot_data   TEXT NOT NULL,                     -- 操作前 tag 字段 JSON 快照
            field_count     INTEGER DEFAULT 0,                 -- 字段数量
            created_at      TEXT NOT NULL,                     -- 创建时间
            updated_at      TEXT NOT NULL,                     -- 更新时间
            UNIQUE(track_id, operation_type)                  -- 每 track × 每操作仅一条
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

    // 旧库迁移：旧表名 metadata_snapshots → rollback_snapshot，并补 operation_type 列
    migrate_schema();

    const char* create_index_sql = "CREATE INDEX IF NOT EXISTS idx_rollback_snapshot_track_id ON rollback_snapshot(track_id)";
    sqlite3_exec(static_cast<sqlite3*>(db_), create_index_sql, nullptr, nullptr, nullptr);
    const char* create_op_index_sql = "CREATE INDEX IF NOT EXISTS idx_rollback_snapshot_op_type ON rollback_snapshot(operation_type)";
    sqlite3_exec(static_cast<sqlite3*>(db_), create_op_index_sql, nullptr, nullptr, nullptr);

    healthy_ = true;
    Logger::instance().info("[BackupManager] init_database: SUCCESS, db_path=" + db_path_);
}

void BackupManager::migrate_schema() {
    sqlite3* db = static_cast<sqlite3*>(db_);

    // -------------------------------------------------------------------
    // Step 1: 旧表名 metadata_snapshots → rollback_snapshot（幂等）
    // -------------------------------------------------------------------
    auto table_exists = [db](const std::string& name) -> bool {
        sqlite3_stmt* s = nullptr;
        bool exists = false;
        if (sqlite3_prepare_v2(db, "SELECT name FROM sqlite_master WHERE type='table' AND name=?", -1, &s, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(s, 1, name.c_str(), -1, SQLITE_TRANSIENT);
            if (sqlite3_step(s) == SQLITE_ROW) exists = true;
            sqlite3_finalize(s);
        }
        return exists;
    };

    if (table_exists("metadata_snapshots") && !table_exists("rollback_snapshot")) {
        Logger::instance().info("[BackupManager] migrate_schema: Renaming metadata_snapshots → rollback_snapshot");
        sqlite3_exec(db, "DROP INDEX IF EXISTS idx_snapshot_track_id", nullptr, nullptr, nullptr);
        sqlite3_exec(db, "DROP INDEX IF EXISTS idx_snapshot_op_type", nullptr, nullptr, nullptr);
        sqlite3_exec(db, "ALTER TABLE metadata_snapshots RENAME TO rollback_snapshot", nullptr, nullptr, nullptr);
    }

    if (!table_exists("rollback_snapshot")) {
        // 极端情况：旧表也不存在（不应发生，init_database 已建表）
        return;
    }

    // -------------------------------------------------------------------
    // Step 2: 检查 operation_type 列是否存在
    // -------------------------------------------------------------------
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(
        db,
        "PRAGMA table_info(rollback_snapshot)",
        -1, &stmt, nullptr
    );
    if (rc != SQLITE_OK) {
        return;
    }

    bool has_op_type = false;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* col_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        if (col_name && std::string(col_name) == "operation_type") {
            has_op_type = true;
        }
    }
    sqlite3_finalize(stmt);

    // 旧库无 operation_type 列：添加列并把现有数据标为 scrape
    if (!has_op_type) {
        Logger::instance().info("[BackupManager] migrate_schema: adding operation_type column");
        sqlite3_exec(db,
            "ALTER TABLE rollback_snapshot ADD COLUMN operation_type TEXT NOT NULL DEFAULT 'scrape'",
            nullptr, nullptr, nullptr);

        // 旧表有 track_id UNIQUE 约束，新表要改为 UNIQUE(track_id, operation_type)
        // SQLite 无法直接修改约束，需重建表
        // 1. 重命名旧表
        sqlite3_exec(db,
            "ALTER TABLE rollback_snapshot RENAME TO rollback_snapshot_old",
            nullptr, nullptr, nullptr);

        // 2. 创建新表（带复合唯一约束）
        const char* new_table_sql = R"(
            CREATE TABLE rollback_snapshot (
                id              INTEGER PRIMARY KEY AUTOINCREMENT,
                track_id        TEXT NOT NULL,
                operation_type  TEXT NOT NULL DEFAULT 'scrape',
                snapshot_data   TEXT NOT NULL,
                field_count     INTEGER DEFAULT 0,
                created_at      TEXT NOT NULL,
                updated_at      TEXT NOT NULL,
                UNIQUE(track_id, operation_type)
            )
        )";
        sqlite3_exec(db, new_table_sql, nullptr, nullptr, nullptr);

        // 3. 拷贝数据
        sqlite3_exec(db,
            "INSERT INTO rollback_snapshot (id, track_id, operation_type, snapshot_data, "
            "field_count, created_at, updated_at) "
            "SELECT id, track_id, operation_type, snapshot_data, field_count, created_at, updated_at "
            "FROM rollback_snapshot_old",
            nullptr, nullptr, nullptr);

        // 4. 删除旧表
        sqlite3_exec(db,
            "DROP TABLE rollback_snapshot_old",
            nullptr, nullptr, nullptr);

        Logger::instance().info("[BackupManager] migrate_schema: schema migrated to multi-operation");
    }
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
    // 兼容老接口：等价于"是否存在任意类型的快照"
    return has_operation_snapshot(track_id, OperationType::Scrape);
}

bool BackupManager::save_snapshot(
    const std::string& track_id,
    const std::map<std::string, std::string>& snapshot
) {
    // 兼容老接口：等价于保存 Scrape 类型快照
    Logger::instance().info("[BackupManager] save_snapshot(deprecated, use ensure_operation_snapshot): track_id=" + track_id.substr(0, 16) + "...");

    if (has_operation_snapshot(track_id, OperationType::Scrape)) {
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
        INSERT INTO rollback_snapshot (
            track_id, operation_type, snapshot_data, field_count, created_at, updated_at
        ) VALUES (?, ?, ?, ?, ?, ?)
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
    sqlite3_bind_text(stmt, 2, operation_type_to_string(OperationType::Scrape), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, snapshot_json.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 4, field_count);
    sqlite3_bind_text(stmt, 5, timestamp.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, timestamp.c_str(), -1, SQLITE_TRANSIENT);

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
    // 兼容老接口：等价于 ensure_operation_snapshot(track_id, Scrape, snapshot)
    return ensure_operation_snapshot(track_id, OperationType::Scrape, snapshot);
}

std::map<std::string, std::string> BackupManager::get_snapshot(const std::string& track_id) {
    return get_operation_snapshot(track_id, OperationType::Scrape);
}

std::optional<std::map<std::string, std::string>> BackupManager::rollback(
    const std::string& track_id
) {
    return rollback_operation(track_id, OperationType::Scrape);
}

std::map<std::string, std::map<std::string, std::string>> BackupManager::batch_rollback(
    const std::vector<std::string>& track_ids
) {
    return batch_rollback_operations(track_ids, OperationType::Scrape);
}

bool BackupManager::delete_snapshot(const std::string& track_id) {
    return delete_operation_snapshot(track_id, OperationType::Scrape);
}

std::vector<std::string> BackupManager::get_all_tracks_with_snapshot() {
    std::vector<std::string> track_ids;

    std::lock_guard<std::mutex> lock(mutex_);

    if (!healthy_) {
        return track_ids;
    }

    const char* select_sql = "SELECT DISTINCT track_id FROM rollback_snapshot";

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

    const char* select_sql = "SELECT COUNT(*) FROM rollback_snapshot";

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

// ============= 多操作类型回滚接口实现 =============

bool BackupManager::has_operation_snapshot(const std::string& track_id, OperationType op_type) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!healthy_) return false;

    const char* sql = "SELECT 1 FROM rollback_snapshot WHERE track_id = ? AND operation_type = ? LIMIT 1";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(static_cast<sqlite3*>(db_), sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, track_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, operation_type_to_string(op_type), -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_ROW;
}

bool BackupManager::ensure_operation_snapshot(
    const std::string& track_id,
    OperationType op_type,
    const std::map<std::string, std::string>& snapshot
) {
    if (has_operation_snapshot(track_id, op_type)) {
        return true;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    if (!healthy_) {
        Logger::instance().error("[BackupManager] ensure_operation_snapshot: not healthy");
        return false;
    }

    // 再次检查（race condition 防护）
    // 注意：不能调用 has_operation_snapshot，它会再次加锁同一 mutex 导致死锁
    {
        const char* check_sql = "SELECT 1 FROM rollback_snapshot WHERE track_id = ? AND operation_type = ? LIMIT 1";
        sqlite3_stmt* check_stmt = nullptr;
        int rc = sqlite3_prepare_v2(static_cast<sqlite3*>(db_), check_sql, -1, &check_stmt, nullptr);
        if (rc == SQLITE_OK) {
            sqlite3_bind_text(check_stmt, 1, track_id.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(check_stmt, 2, operation_type_to_string(op_type), -1, SQLITE_TRANSIENT);
            rc = sqlite3_step(check_stmt);
            sqlite3_finalize(check_stmt);
            if (rc == SQLITE_ROW) {
                return true;
            }
        }
    }

    std::string snapshot_json = serialize_snapshot(snapshot);
    std::string timestamp = get_current_timestamp();
    int field_count = static_cast<int>(snapshot.size());

    const char* insert_sql = R"(
        INSERT INTO rollback_snapshot (
            track_id, operation_type, snapshot_data, field_count, created_at, updated_at
        ) VALUES (?, ?, ?, ?, ?, ?)
    )";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(static_cast<sqlite3*>(db_), insert_sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        Logger::instance().error("[BackupManager] ensure_operation_snapshot: prepare failed: " +
            std::string(sqlite3_errmsg(static_cast<sqlite3*>(db_))));
        return false;
    }

    sqlite3_bind_text(stmt, 1, track_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, operation_type_to_string(op_type), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, snapshot_json.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 4, field_count);
    sqlite3_bind_text(stmt, 5, timestamp.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, timestamp.c_str(), -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        if (rc == SQLITE_CONSTRAINT) {
            // 并发竞争：已被其他线程插入
            return true;
        }
        Logger::instance().error("[BackupManager] ensure_operation_snapshot: step failed: " +
            std::string(sqlite3_errmsg(static_cast<sqlite3*>(db_))));
        return false;
    }

    Logger::instance().info("[BackupManager] ensure_operation_snapshot: op=" +
        std::string(operation_type_to_string(op_type)) +
        ", track_id=" + track_id.substr(0, 16) + "..." +
        ", field_count=" + std::to_string(field_count));
    return true;
}

std::map<std::string, std::string> BackupManager::get_operation_snapshot(
    const std::string& track_id, OperationType op_type
) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::map<std::string, std::string> result;

    if (!healthy_) return result;

    const char* sql = "SELECT snapshot_data FROM rollback_snapshot WHERE track_id = ? AND operation_type = ?";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(static_cast<sqlite3*>(db_), sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return result;

    sqlite3_bind_text(stmt, 1, track_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, operation_type_to_string(op_type), -1, SQLITE_TRANSIENT);

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

std::optional<std::map<std::string, std::string>> BackupManager::rollback_operation(
    const std::string& track_id, OperationType op_type
) {
    auto snapshot = get_operation_snapshot(track_id, op_type);
    if (snapshot.empty()) {
        Logger::instance().warning("[BackupManager] rollback_operation: no snapshot for op=" +
            std::string(operation_type_to_string(op_type)) +
            ", track=" + track_id.substr(0, 16) + "...");
        return std::nullopt;
    }
    Logger::instance().info("[BackupManager] rollback_operation: op=" +
        std::string(operation_type_to_string(op_type)) +
        ", track=" + track_id.substr(0, 16) + "..." +
        ", field_count=" + std::to_string(snapshot.size()));
    return snapshot;
}

std::map<std::string, std::map<std::string, std::string>> BackupManager::batch_rollback_operations(
    const std::vector<std::string>& track_ids, OperationType op_type
) {
    std::map<std::string, std::map<std::string, std::string>> results;
    if (track_ids.empty()) return results;

    std::lock_guard<std::mutex> lock(mutex_);
    if (!healthy_) return results;

    std::ostringstream placeholders;
    for (size_t i = 0; i < track_ids.size(); ++i) {
        if (i > 0) placeholders << ",";
        placeholders << "?";
    }

    std::string sql = "SELECT track_id, snapshot_data FROM rollback_snapshot "
        "WHERE operation_type = ? AND track_id IN (" + placeholders.str() + ")";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(static_cast<sqlite3*>(db_), sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return results;

    sqlite3_bind_text(stmt, 1, operation_type_to_string(op_type), -1, SQLITE_TRANSIENT);
    for (size_t i = 0; i < track_ids.size(); ++i) {
        sqlite3_bind_text(stmt, static_cast<int>(i + 2), track_ids[i].c_str(), -1, SQLITE_TRANSIENT);
    }

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        const char* tid = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        const unsigned char* json_text = sqlite3_column_text(stmt, 1);
        if (tid && json_text) {
            results[tid] = deserialize_snapshot(reinterpret_cast<const char*>(json_text));
        }
    }
    sqlite3_finalize(stmt);

    Logger::instance().info("[BackupManager] batch_rollback_operations: op=" +
        std::string(operation_type_to_string(op_type)) +
        ", found " + std::to_string(results.size()) + "/" + std::to_string(track_ids.size()));
    return results;
}

bool BackupManager::delete_operation_snapshot(const std::string& track_id, OperationType op_type) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!healthy_) return false;

    const char* sql = "DELETE FROM rollback_snapshot WHERE track_id = ? AND operation_type = ?";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(static_cast<sqlite3*>(db_), sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, track_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, operation_type_to_string(op_type), -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    int changes = sqlite3_changes(static_cast<sqlite3*>(db_));
    sqlite3_finalize(stmt);

    return changes > 0;
}

std::vector<OperationType> BackupManager::get_operations_for_track(const std::string& track_id) {
    std::vector<OperationType> ops;
    std::lock_guard<std::mutex> lock(mutex_);
    if (!healthy_) return ops;

    const char* sql = "SELECT DISTINCT operation_type FROM rollback_snapshot WHERE track_id = ?";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(static_cast<sqlite3*>(db_), sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return ops;

    sqlite3_bind_text(stmt, 1, track_id.c_str(), -1, SQLITE_TRANSIENT);

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        const char* op_str = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        if (op_str) {
            ops.push_back(operation_type_from_string(op_str));
        }
    }
    sqlite3_finalize(stmt);
    return ops;
}

std::map<std::string, std::vector<OperationType>> BackupManager::get_operations_for_tracks(
    const std::vector<std::string>& track_ids
) {
    std::map<std::string, std::vector<OperationType>> results;
    if (track_ids.empty()) {
        Logger::instance().warning("[BackupManager] get_operations_for_tracks: empty track_ids");
        return results;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (!healthy_) {
        Logger::instance().warning("[BackupManager] get_operations_for_tracks: not healthy");
        return results;
    }

    std::ostringstream placeholders;
    for (size_t i = 0; i < track_ids.size(); ++i) {
        if (i > 0) placeholders << ",";
        placeholders << "?";
    }

    std::string sql = "SELECT track_id, operation_type FROM rollback_snapshot WHERE track_id IN (" +
        placeholders.str() + ")";
    Logger::instance().info("[BackupManager] get_operations_for_tracks: querying " +
        std::to_string(track_ids.size()) + " track_ids, first track_id=" +
        (track_ids.empty() ? std::string("<empty>") : track_ids[0].substr(0, 16)) + "...");

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(static_cast<sqlite3*>(db_), sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        Logger::instance().error("[BackupManager] get_operations_for_tracks: prepare failed: " +
            std::string(sqlite3_errmsg(static_cast<sqlite3*>(db_))));
        return results;
    }

    for (size_t i = 0; i < track_ids.size(); ++i) {
        sqlite3_bind_text(stmt, static_cast<int>(i + 1), track_ids[i].c_str(), -1, SQLITE_TRANSIENT);
    }

    int row_count = 0;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        const char* tid = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        const char* op_str = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        if (tid && op_str) {
            results[tid].push_back(operation_type_from_string(op_str));
            row_count++;
        }
    }
    sqlite3_finalize(stmt);

    Logger::instance().info("[BackupManager] get_operations_for_tracks: returned " +
        std::to_string(results.size()) + " track entries, " + std::to_string(row_count) + " total rows");
    return results;
}

int BackupManager::delete_all_operation_snapshots(OperationType op_type) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!healthy_) return 0;

    const char* sql = "DELETE FROM rollback_snapshot WHERE operation_type = ?";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(static_cast<sqlite3*>(db_), sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return 0;

    sqlite3_bind_text(stmt, 1, operation_type_to_string(op_type), -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    int changes = sqlite3_changes(static_cast<sqlite3*>(db_));
    sqlite3_finalize(stmt);

    Logger::instance().info("[BackupManager] delete_all_operation_snapshots: op=" +
        std::string(operation_type_to_string(op_type)) +
        ", deleted " + std::to_string(changes) + " records");
    return changes;
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
