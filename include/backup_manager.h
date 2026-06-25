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

private:
    void init_database();
    
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
