#pragma once

#include "../include/types.h"
#include <string>
#include <vector>
#include <optional>
#include <mutex>
#include <memory>

struct sqlite3;

namespace ai_metadata {

struct ScrapeCacheEntry {
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

struct EnhanceCacheEntry {
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
    std::string error_code;
    std::string error_message;
};

/**
 * @brief 缓存层类，负责管理元数据缓存数据库
 *
 * 使用SQLite数据库存储和检索音轨分析结果，支持批量操作和缓存统计
 *
 * 注意：normalize_alias 表已迁移到 Python 端管理（NormalizeProcessor 内），
 *       本类不再提供 alias 相关接口。
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
    
    std::optional<ScrapeCacheEntry> get_scrape(const std::string& cache_key);
    void set_scrape(const std::string& cache_key, const ScrapeCacheEntry& entry);

    std::optional<EnhanceCacheEntry> get_enhance(const std::string& cache_key);
    void set_enhance(const std::string& cache_key, const EnhanceCacheEntry& entry);
    
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
     * @brief 生成 Enhance 缓存键（包含增强选项）
     * @param input 音轨输入信息
     * @param options 增强选项
     * @return 缓存键字符串
     */
    static std::string generate_enhance_cache_key(const TrackInput& input, const EnhancementOptions& options);
    
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
     * @brief 迁移旧表名到新表名（幂等，仅执行一次）
     *   stage1_cache → scrape_cache
     *   stage2_cache → enhance_cache
     *   normalize_alias（旧空表）→ DROP
     */
    void migrate_legacy_tables();

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
