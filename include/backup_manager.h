#pragma once

#include <string>
#include <vector>
#include <map>
#include <set>
#include <optional>
#include <cstdint>
#include <memory>
#include <mutex>

#include "types.h"

namespace ai_metadata {

// 回滚操作类型标识
// 用于在 rollback_snapshot 表中区分不同操作的初始快照
// 同一首歌可能同时存在多种类型的回滚记录
enum class OperationType : int {
    Scrape = 0,     // 歌曲刮削（MusicBrainz/Discogs/AI 元数据获取）
    Translate = 1,  // 翻译（标题/专辑等字段翻译）
    Normalize = 2,  // 歌手规范化（artist/album_artist 字段归一化）
};

// 将 OperationType 转为数据库存储用的字符串标识
inline const char* operation_type_to_string(OperationType t) {
    switch (t) {
        case OperationType::Scrape:    return "scrape";
        case OperationType::Translate: return "translate";
        case OperationType::Normalize: return "normalize";
    }
    return "scrape";
}

// 从字符串标识解析 OperationType，未知值视为 Scrape（兼容旧数据）
inline OperationType operation_type_from_string(const std::string& s) {
    if (s == "translate") return OperationType::Translate;
    if (s == "normalize") return OperationType::Normalize;
    return OperationType::Scrape;
}

static const std::set<std::string> METADATA_BLACKLIST = {
    "TECH",
    "ENCODER",
    "ENCODER_SETTINGS",
    "REPLAYGAIN_ALBUM_GAIN",
    "REPLAYGAIN_ALBUM_PEAK",
    "REPLAYGAIN_TRACK_GAIN",
    "REPLAYGAIN_TRACK_PEAK",
    "REPLAYGAIN_REFERENCE_LOUDNESS",
    "FOOBAR2000_VERSION",
    "FOOBAR2000_COMPONENT_VERSION",
    "__tool",
    "__tagger",
    "_LENGTH",
    "_PLAYCOUNT",
    "_LAST_PLAYED",
    "_RATING",
    "_ADDED",
    "_FIRST_PLAYED",
    "_PLAYBACK_TIME",
    "_BITRATE",
    "_SAMPLE_RATE",
    "_CHANNELS",
    "_BITS_PER_SAMPLE",
    "_CODEC",
    "_FILENAME",
    "_FILENAME_RAW",
    "_PATH",
    "_DIRECTORYNAME",
    "_DISPLAY",
    "_EXTENSION",
};

class BackupManager {
public:
    explicit BackupManager(const std::string& db_path);
    ~BackupManager();

    bool has_snapshot(const std::string& track_id);

    bool save_snapshot(
        const std::string& track_id,
        const std::map<std::string, std::string>& snapshot
    );

    bool ensure_snapshot(
        const std::string& track_id,
        const std::map<std::string, std::string>& snapshot
    );

    std::map<std::string, std::string> get_snapshot(const std::string& track_id);

    std::optional<std::map<std::string, std::string>> rollback(
        const std::string& track_id
    );

    std::map<std::string, std::map<std::string, std::string>> batch_rollback(
        const std::vector<std::string>& track_ids
    );

    bool delete_snapshot(const std::string& track_id);

    std::vector<std::string> get_all_tracks_with_snapshot();

    int get_snapshot_count();

    bool is_healthy() const;

    std::string get_db_path() const { return db_path_; }

    static bool is_field_blacklisted(const std::string& field_name);

    // ============= 多操作类型回滚接口 =============
    // 同一 track_id 可有多种操作的初始快照，按 operation_type 区分
    // 每条记录对应"该操作执行前的最初状态"，回滚即恢复到该状态

    // 检查指定 track + 操作类型 是否已有快照
    bool has_operation_snapshot(const std::string& track_id, OperationType op_type);

    // 仅当 (track_id, op_type) 无记录时插入，保证只保存"最初状态"
    bool ensure_operation_snapshot(
        const std::string& track_id,
        OperationType op_type,
        const std::map<std::string, std::string>& snapshot
    );

    // 获取指定操作的初始快照
    std::map<std::string, std::string> get_operation_snapshot(
        const std::string& track_id, OperationType op_type
    );

    // 回滚指定操作的初始状态
    std::optional<std::map<std::string, std::string>> rollback_operation(
        const std::string& track_id, OperationType op_type
    );

    // 批量回滚某类型操作
    std::map<std::string, std::map<std::string, std::string>> batch_rollback_operations(
        const std::vector<std::string>& track_ids, OperationType op_type
    );

    // 删除指定 track + 操作类型的快照
    bool delete_operation_snapshot(const std::string& track_id, OperationType op_type);

    // 列出该 track 已有哪些操作类型的快照（供 UI 显示哪些可回滚）
    std::vector<OperationType> get_operations_for_track(const std::string& track_id);

    // 批量查询：返回每个 track 拥有的操作类型集合
    std::map<std::string, std::vector<OperationType>> get_operations_for_tracks(
        const std::vector<std::string>& track_ids
    );

    // 删除指定操作类型的所有快照（用户清除某类回滚记录时使用）
    int delete_all_operation_snapshots(OperationType op_type);

private:
    void init_database();

    // 检查并执行 schema 迁移（添加 operation_type 列、调整唯一约束）
    void migrate_schema();

    static std::string serialize_snapshot(
        const std::map<std::string, std::string>& snapshot
    );

    static std::map<std::string, std::string> deserialize_snapshot(
        const std::string& json_str
    );

    static std::string get_current_timestamp();

    std::string db_path_;
    void* db_;
    mutable std::mutex mutex_;
    bool healthy_;
};

}
