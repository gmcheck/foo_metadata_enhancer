#pragma once

#include "../include/types.h"
#include <string>
#include <vector>
#include <optional>
#include <mutex>
#include <memory>

struct sqlite3;

namespace ai_metadata {

struct Stage1CacheEntry {
    std::string track_id;
    std::string file_path;
    std::string title;
    std::string artist;
    std::string album;
    std::map<std::string, ScrapedField> scraped_fields;
    DataSourceType source = DataSourceType::AI;
    bool success = false;
    std::string error_code;
    std::string error_message;
};

struct Stage2CacheEntry {
    std::string track_id;
    std::string file_path;
    std::string title;
    std::string artist;
    std::string album;
    bool success = false;
    std::string title_zh;
    std::string album_zh;
    std::string artist_zh;
    float translation_confidence = 0.0f;
    std::string genre_value;
    float genre_confidence = 0.0f;
    std::string edition_value;
    float edition_confidence = 0.0f;
    std::string error_code;
    std::string error_message;
};

/**
 * @brief 缓存层类，负责管理元数据缓存数据库
 * 
 * 使用SQLite数据库存储和检索音轨分析结果，支持批量操作和缓存统计
 */
class CacheLayer {
public:
    /**
     * @brief 构造函数，初始化缓存层
     * @param db_path 数据库文件路径
     */
    explicit CacheLayer(const std::string& db_path);
    
    /**
     * @brief 析构函数，关闭数据库连接
     */
    ~CacheLayer();
    
    CacheLayer(const CacheLayer&) = delete;
    CacheLayer& operator=(const CacheLayer&) = delete;
    
    std::optional<Stage1CacheEntry> get_stage1(const std::string& cache_key);
    void set_stage1(const std::string& cache_key, const Stage1CacheEntry& entry);
    
    std::optional<Stage2CacheEntry> get_stage2(const std::string& cache_key);
    void set_stage2(const std::string& cache_key, const Stage2CacheEntry& entry);
    
    /**
     * @brief 清除所有缓存条目
     */
    void clear_all();
    
    /**
     * @brief 根据track_id列表清除缓存条目
     * @param track_ids 要清除的track_id列表
     * @return 清除的条目数量
     */
    int clear_by_track_ids(const std::vector<std::string>& track_ids);
    
    /**
     * @brief 获取缓存统计信息
     * @return 缓存统计结构体
     */
    CacheStatistics get_statistics();
    
    /**
     * @brief 获取配置值
     * @param key 配置键
     * @return 配置值字符串
     */
    std::string get_config(const std::string& key);
    
    /**
     * @brief 设置配置值
     * @param key 配置键
     * @param value 配置值
     */
    void set_config(const std::string& key, const std::string& value);
    
    /**
     * @brief 优化数据库（执行VACUUM和ANALYZE）
     */
    void optimize_database();
    
    /**
     * @brief 执行VACUUM操作，回收数据库空间
     */
    void vacuum_database();
    
    /**
     * @brief 获取数据库大小（MB）
     * @return 数据库大小（兆字节）
     */
    int get_database_size_mb();
    
    /**
     * @brief 生成缓存键
     * @param input 音轨输入信息
     * @return 缓存键字符串
     */
    static std::string generate_cache_key(const TrackInput& input);
    
    /**
     * @brief 生成Stage2缓存键（包含增强选项）
     * @param input 音轨输入信息
     * @param options 增强选项
     * @return 缓存键字符串
     */
    static std::string generate_stage2_cache_key(const TrackInput& input, const EnhancementOptions& options);
    
    /**
     * @brief 生成音轨唯一标识符
     * @param path 文件路径
     * @param subsong 子音轨索引
     * @param file_size 文件大小
     * @return TrackUID字符串（SHA256哈希）
     */
    static std::string generate_track_uid(const std::string& path, uint32_t subsong, uint64_t file_size);
    
    /**
     * @brief 检查数据库是否有效
     * @return 如果数据库连接有效返回true
     */
    bool is_valid() const { return db_ != nullptr; }
    
private:
    /**
     * @brief 初始化数据库连接
     */
    void init_database();
    
    /**
     * @brief 创建数据库表
     */
    void create_tables();
    
    /**
     * @brief 创建数据库索引
     */
    void create_indexes();
    
    /**
     * @brief 检查数据库完整性
     * @return 如果完整性检查通过返回true
     */
    bool check_integrity();
    
    /**
     * @brief 内部获取配置值（不加锁）
     * @param key 配置键
     * @return 配置值字符串
     */
    std::string get_config_internal(const std::string& key);
    
    ::sqlite3* db_ = nullptr;
    std::string db_path_;
    std::mutex mutex_;
};

}
