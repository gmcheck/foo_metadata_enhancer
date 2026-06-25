#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Pydantic Models for AI Metadata Plugin
Ensures strict JSON structure validation for all data structures
"""

from typing import Optional, List, Dict, Any
from pydantic import BaseModel, Field, field_validator
from datetime import datetime, timezone


class TrackOptions(BaseModel):
    """音轨分析选项配置
    
    Attributes:
        classify_genre: 是否进行流派分类
        identify_edition: 是否识别版本信息
        translate_metadata: 是否翻译元数据
    """
    classify_genre: bool = True
    identify_edition: bool = True
    translate_metadata: bool = True


class TrackInput(BaseModel):
    """音轨输入数据模型
    
    包含从foobar2000传入的音轨元数据信息。
    
    Attributes:
        track_id: 音轨唯一标识符
        title: 标题
        album: 专辑名
        artist: 艺术家
        album_artist: 专辑艺术家
        musicbrainz_id: MusicBrainz ID
        file_hash: 文件哈希
        duration_sec: 时长（秒）
        track_number: 音轨号
        disc_number: 光盘号
        subsong_index: 子音轨索引
        year: 年份
        genre_existing: 现有流派
        comment: 注释
        label: 厂牌
        language_hint: 语言提示
        options: 分析选项
    """
    track_id: str = ""
    title: str = ""
    album: str = ""
    artist: str = ""
    album_artist: str = ""
    musicbrainz_id: str = ""
    file_hash: str = ""
    duration_sec: int = 0
    track_number: int = 0
    disc_number: int = 0
    subsong_index: int = 0
    year: str = ""
    genre_existing: str = ""
    comment: str = ""
    label: str = ""
    language_hint: str = ""
    options: Optional[TrackOptions] = None
    
    def get_options(self) -> TrackOptions:
        """获取分析选项，如果未设置则返回默认选项
        
        Returns:
            TrackOptions: 分析选项实例
        """
        return self.options or TrackOptions()


class GenreInfo(BaseModel):
    """流派信息模型
    
    Attributes:
        value: 流派值
        confidence: 置信度（0.0-1.0）
        source: 来源标识
    """
    value: str = ""
    confidence: float = Field(default=0.0, ge=0.0, le=1.0)
    source: str = "ai"
    
    @field_validator('confidence', mode='before')
    @classmethod
    def normalize_confidence(cls, v):
        """标准化置信度值，将百分比值转换为0-1范围
        
        Args:
            v: 原始置信度值
        
        Returns:
            float: 标准化后的置信度（0.0-1.0）
        """
        if isinstance(v, (int, float)) and v > 1.0:
            return min(v / 100.0, 1.0)
        return v


class EditionInfo(BaseModel):
    """版本信息模型
    
    Attributes:
        value: 版本值（如Remastered, Deluxe等）
        confidence: 置信度（0.0-1.0）
    """
    value: Optional[str] = ""
    confidence: float = Field(default=0.0, ge=0.0, le=1.0)
    
    @field_validator('confidence', mode='before')
    @classmethod
    def normalize_confidence(cls, v):
        """标准化置信度值
        
        Args:
            v: 原始置信度值
        
        Returns:
            float: 标准化后的置信度
        """
        if isinstance(v, (int, float)) and v > 1.0:
            return min(v / 100.0, 1.0)
        return v


class TranslationInfo(BaseModel):
    """翻译信息模型
    
    Attributes:
        title_zh: 中文标题
        album_zh: 中文专辑名
        artist_zh: 中文艺术家名
    """
    title_zh: str = ""
    album_zh: str = ""
    artist_zh: str = ""


class AIResult(BaseModel):
    """AI分析结果模型
    
    Attributes:
        genre: 流派信息
        edition: 版本信息
        translation: 翻译信息
        translation_confidence: 翻译置信度
    """
    genre: Optional[GenreInfo] = None
    edition: Optional[EditionInfo] = None
    translation: Optional[TranslationInfo] = None
    translation_confidence: float = Field(default=0.0, ge=0.0, le=1.0)
    
    @field_validator('translation_confidence', mode='before')
    @classmethod
    def normalize_confidence(cls, v):
        """标准化翻译置信度值
        
        Args:
            v: 原始置信度值
        
        Returns:
            float: 标准化后的置信度
        """
        if isinstance(v, (int, float)) and v > 1.0:
            return min(v / 100.0, 1.0)
        return v


class AnalysisInfo(BaseModel):
    """分析信息模型
    
    Attributes:
        model: 使用的模型名称
        tokens_used: 使用的令牌数
        api_latency_ms: API延迟（毫秒）
        cache_hit: 是否命中缓存
        model_type: 模型类型（local/remote）
        batch_size: 批处理大小
    """
    model: str = ""
    tokens_used: int = 0
    api_latency_ms: int = 0
    cache_hit: bool = False
    model_type: str = "remote"
    batch_size: int = 1


class OriginalMetadata(BaseModel):
    """原始元数据模型
    
    Attributes:
        title: 原始标题
        album: 原始专辑名
        artist: 原始艺术家
        album_artist: 原始专辑艺术家
        year: 原始年份
    """
    title: str = ""
    album: str = ""
    artist: str = ""
    album_artist: str = ""
    year: str = ""


class TrackAnalysisResult(BaseModel):
    """音轨分析结果模型
    
    Attributes:
        track_id: 音轨唯一标识符
        success: 是否成功
        error: 错误信息（如有）
        original: 原始元数据
        ai: AI分析结果
        analysis_info: 分析信息
        timestamp: 时间戳
    """
    track_id: str = ""
    success: bool = False
    error: Optional[str] = None
    original: Optional[OriginalMetadata] = None
    ai: Optional[AIResult] = None
    analysis_info: Optional[AnalysisInfo] = None
    timestamp: str = ""
    
    def __init__(self, **data):
        """初始化音轨分析结果，自动设置时间戳
        
        Args:
            **data: 模型数据
        """
        if "timestamp" not in data or not data["timestamp"]:
            data["timestamp"] = datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")
        super().__init__(**data)


class ErrorInfo(BaseModel):
    """错误信息模型
    
    Attributes:
        code: 错误代码
        message: 错误消息
        retryable: 是否可重试
    """
    code: str = ""
    message: str = ""
    retryable: bool = False


class BatchResponse(BaseModel):
    """批量响应模型
    
    Attributes:
        id: 响应ID
        version: 协议版本
        success: 是否成功
        count: 结果数量
        results: 结果列表
        error: 错误信息
        timestamp: 时间戳
    """
    id: str = ""
    version: int = 1
    success: bool = False
    count: int = 0
    results: List[TrackAnalysisResult] = []
    error: Optional[ErrorInfo] = None
    timestamp: str = ""
    
    def __init__(self, **data):
        """初始化批量响应，自动设置时间戳
        
        Args:
            **data: 模型数据
        """
        if "timestamp" not in data or not data["timestamp"]:
            data["timestamp"] = datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")
        super().__init__(**data)


class IPCRequest(BaseModel):
    """IPC请求模型
    
    Attributes:
        version: 协议版本
        id: 请求ID
        client: 客户端标识
        method: 方法名
        timeout_ms: 超时时间（毫秒）
        priority: 优先级
        params: 参数字典
    """
    version: int = 1
    id: str = ""
    client: str = "foobar2000"
    method: str = "analyze_tracks"
    timeout_ms: int = 30000
    priority: int = 5
    params: Dict[str, Any] = {}


class IPCResponse(BaseModel):
    """IPC响应模型
    
    Attributes:
        version: 协议版本
        id: 响应ID
        success: 是否成功
        results: 结果列表
        error: 错误信息
        timestamp: 时间戳
    """
    version: int = 1
    id: str = ""
    success: bool = False
    results: List[Dict[str, Any]] = []
    error: Optional[Dict[str, Any]] = None
    timestamp: str = ""
    
    def __init__(self, **data):
        """初始化IPC响应，自动设置时间戳
        
        Args:
            **data: 模型数据
        """
        if "timestamp" not in data or not data["timestamp"]:
            data["timestamp"] = datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")
        super().__init__(**data)


class AIGenreResponse(BaseModel):
    """AI流派响应模型
    
    Attributes:
        genre: 流派信息
    """
    genre: Optional[GenreInfo] = None


class AIEditionResponse(BaseModel):
    """AI版本响应模型
    
    Attributes:
        edition: 版本信息
    """
    edition: Optional[EditionInfo] = None


class AITranslationResponse(BaseModel):
    """AI翻译响应模型
    
    Attributes:
        translation: 翻译信息
        translation_confidence: 翻译置信度
    """
    translation: Optional[TranslationInfo] = None
    translation_confidence: float = Field(default=0.0, ge=0.0, le=1.0)
    
    @field_validator('translation_confidence', mode='before')
    @classmethod
    def normalize_confidence(cls, v):
        """标准化翻译置信度值
        
        Args:
            v: 原始置信度值
        
        Returns:
            float: 标准化后的置信度
        """
        if isinstance(v, (int, float)) and v > 1.0:
            return min(v / 100.0, 1.0)
        return v


class AIBatchAnalysisResponse(BaseModel):
    """AI批量分析响应模型
    
    Attributes:
        results: 结果列表
    """
    results: List[Dict[str, Any]] = []


class CacheEntry(BaseModel):
    """缓存条目模型
    
    Attributes:
        cache_key: 缓存键
        track_id: 音轨ID
        title: 标题
        artist: 艺术家
        album: 专辑
        track_number: 音轨号
        disc_number: 光盘号
        subsong_index: 子音轨索引
        year: 年份
        original_metadata: 原始元数据
        ai_result: AI结果
        model: 模型名称
        model_type: 模型类型
        tokens_used: 使用的令牌数
        api_latency_ms: API延迟
        created_at: 创建时间
        expires_at: 过期时间
        last_accessed_at: 最后访问时间
        cache_hit_count: 缓存命中次数
    """
    cache_key: str
    track_id: str = ""
    title: str = ""
    artist: str = ""
    album: str = ""
    track_number: int = 0
    disc_number: int = 0
    subsong_index: int = 0
    year: str = ""
    original_metadata: Optional[Dict[str, Any]] = None
    ai_result: Dict[str, Any] = {}
    model: str = ""
    model_type: str = "remote"
    tokens_used: int = 0
    api_latency_ms: int = 0
    created_at: str = ""
    expires_at: str = ""
    last_accessed_at: Optional[str] = None
    cache_hit_count: int = 0


class CacheStatistics(BaseModel):
    """缓存统计模型
    
    Attributes:
        enabled: 是否启用
        db_path: 数据库路径
        total_entries: 总条目数
        expired_entries: 过期条目数
        total_hits: 总命中数
        total_misses: 总未命中数
        hit_rate: 命中率
        api_calls_saved: 节省的API调用数
        total_tokens_saved: 节省的令牌数
        db_size_mb: 数据库大小（MB）
        max_size_mb: 最大大小（MB）
        expiration_days: 过期天数
    """
    enabled: bool = True
    db_path: str = ""
    total_entries: int = 0
    expired_entries: int = 0
    total_hits: int = 0
    total_misses: int = 0
    hit_rate: float = 0.0
    api_calls_saved: int = 0
    total_tokens_saved: int = 0
    db_size_mb: float = 0.0
    max_size_mb: float = 500.0
    expiration_days: int = 365


class WorkerInfo(BaseModel):
    """Worker信息模型
    
    Attributes:
        id: Worker ID
        status: 状态
        pid: 进程ID
        queue_size: 队列大小
        cpu_usage: CPU使用率
        memory_usage: 内存使用率
    """
    id: int = 0
    status: str = "stopped"
    pid: int = 0
    queue_size: int = 0
    cpu_usage: float = 0.0
    memory_usage: float = 0.0


def validate_track_input(data: Dict[str, Any]) -> TrackInput:
    """验证音轨输入数据
    
    Args:
        data: 原始数据字典
    
    Returns:
        TrackInput: 验证后的音轨输入实例
    """
    return TrackInput.model_validate(data)


def validate_analysis_result(data: Dict[str, Any]) -> TrackAnalysisResult:
    """验证分析结果数据
    
    Args:
        data: 原始数据字典
    
    Returns:
        TrackAnalysisResult: 验证后的分析结果实例
    """
    return TrackAnalysisResult.model_validate(data)


def validate_batch_response(data: Dict[str, Any]) -> BatchResponse:
    """验证批量响应数据
    
    Args:
        data: 原始数据字典
    
    Returns:
        BatchResponse: 验证后的批量响应实例
    """
    return BatchResponse.model_validate(data)


def create_error_result(track_id: str, error_message: str) -> TrackAnalysisResult:
    """创建错误结果
    
    Args:
        track_id: 音轨ID
        error_message: 错误消息
    
    Returns:
        TrackAnalysisResult: 包含错误信息的分析结果
    """
    return TrackAnalysisResult(
        track_id=track_id,
        success=False,
        error=error_message,
        original=OriginalMetadata(),
        ai=AIResult(),
        analysis_info=AnalysisInfo()
    )


def create_success_result(
    track_id: str,
    original: OriginalMetadata,
    ai_result: AIResult,
    analysis_info: AnalysisInfo
) -> TrackAnalysisResult:
    """创建成功结果
    
    Args:
        track_id: 音轨ID
        original: 原始元数据
        ai_result: AI分析结果
        analysis_info: 分析信息
    
    Returns:
        TrackAnalysisResult: 成功的分析结果
    """
    return TrackAnalysisResult(
        track_id=track_id,
        success=True,
        original=original,
        ai=ai_result,
        analysis_info=analysis_info
    )


class ScrapingOptions(BaseModel):
    """V8新增：刮削选项模型 - 阶段一
    
    Attributes:
        scrape_title: 是否刮削标题
        scrape_artist: 是否刮削艺术家
        scrape_album: 是否刮削专辑
        scrape_year: 是否刮削年份
        scrape_track_number: 是否刮削音轨号
        scrape_disc_number: 是否刮削光盘号
        scrape_composer: 是否刮削作曲家
        scrape_lyricist: 是否刮削作词家
        scrape_conductor: 是否刮削指挥
        scrape_performer: 是否刮削演奏者
        scrape_producer: 是否刮削制作人
        scrape_engineer: 是否刮削工程师
        scrape_orchestra: 是否刮削管弦乐团
        scrape_ensemble: 是否刮削乐团
        scrape_label: 是否刮削厂牌
        scrape_country: 是否刮削国家
        scrape_catalog_number: 是否刮削目录号
        scrape_original_artist: 是否刮削原始艺术家
        scrape_original_album: 是否刮削原始专辑
        scrape_original_year: 是否刮削原始年份
        scrape_musicbrainz_id: 是否刮削MusicBrainz ID
        scrape_isrc: 是否刮削ISRC
        enable_musicbrainz: 是否启用MusicBrainz
        enable_discogs: 是否启用Discogs
        enable_ai: 是否启用AI
        auto_accept_threshold: 自动接受阈值
        confirm_threshold: 确认阈值
    """
    scrape_title: bool = True
    scrape_artist: bool = True
    scrape_album: bool = True
    scrape_year: bool = True
    scrape_track_number: bool = True
    scrape_disc_number: bool = True
    
    scrape_composer: bool = True
    scrape_lyricist: bool = True
    scrape_conductor: bool = False
    scrape_performer: bool = False
    scrape_producer: bool = False
    scrape_engineer: bool = False
    scrape_orchestra: bool = False
    scrape_ensemble: bool = False
    
    scrape_label: bool = True
    scrape_country: bool = False
    scrape_catalog_number: bool = False
    scrape_original_artist: bool = False
    scrape_original_album: bool = False
    scrape_original_year: bool = False
    
    scrape_musicbrainz_id: bool = True
    scrape_isrc: bool = False
    
    enable_musicbrainz: bool = True
    enable_discogs: bool = True
    enable_ai: bool = True
    
    auto_accept_threshold: float = Field(default=0.9, ge=0.0, le=1.0)
    confirm_threshold: float = Field(default=0.7, ge=0.0, le=1.0)


class EnhancementOptionsModel(BaseModel):
    """V8新增：增强选项模型 - 阶段二
    
    Attributes:
        translate_title: 是否翻译标题
        translate_album: 是否翻译专辑
        translate_artist: 是否翻译艺术家
        classify_genre: 是否分类流派
        identify_edition: 是否识别版本
        scrape_mood: 是否刮削情绪
        scrape_bpm: 是否刮削BPM
    """
    translate_title: bool = True
    translate_album: bool = True
    translate_artist: bool = True
    classify_genre: bool = True
    identify_edition: bool = True
    scrape_mood: bool = False
    scrape_bpm: bool = False


class DataSourceTypeModel(BaseModel):
    """V8新增：数据源类型模型
    
    注意：data_sources.base.DataSourceType 是枚举类型，
    这个模型用于 Pydantic 验证场景。
    """
    value: str = "ai"
    
    @field_validator('value', mode='before')
    @classmethod
    def validate_source(cls, v):
        valid_sources = ["musicbrainz", "discogs", "ai"]
        if v not in valid_sources:
            return "ai"
        return v


class ScrapedFieldModel(BaseModel):
    """V8新增：刮削字段结果模型
    
    Attributes:
        value: 字段值
        confidence: 置信度
        source: 数据来源
        raw_data: 原始数据
    """
    value: str = ""
    confidence: float = Field(default=0.0, ge=0.0, le=1.0)
    source: str = "ai"
    raw_data: Optional[Dict[str, Any]] = None
    
    @field_validator('confidence', mode='before')
    @classmethod
    def normalize_confidence(cls, v):
        if isinstance(v, (int, float)) and v > 1.0:
            return min(v / 100.0, 1.0)
        return v


class TrackScrapingResultModel(BaseModel):
    """V8新增：音轨刮削结果模型
    
    Attributes:
        track_id: 音轨ID
        success: 是否成功
        scraped_fields: 刮削字段字典
        release_id: 发布ID
        release_source: 发布来源
        error: 错误信息
    """
    track_id: str = ""
    success: bool = False
    scraped_fields: Dict[str, ScrapedFieldModel] = {}
    release_id: Optional[str] = None
    release_source: str = "musicbrainz"
    error: Optional[str] = None


class MissingFieldInfoModel(BaseModel):
    """V8新增：缺失字段信息模型
    
    Attributes:
        track_id: 音轨ID
        missing_fields: 缺失字段列表
    """
    track_id: str = ""
    missing_fields: List[str] = []


class EnhancementResultModel(BaseModel):
    """V8新增：增强结果模型
    
    Attributes:
        track_id: 音轨ID
        success: 是否成功
        title_zh: 中文标题
        album_zh: 中文专辑名
        artist_zh: 中文艺术家名
        translation_confidence: 翻译置信度
        genre_value: 流派值
        genre_confidence: 流派置信度
        edition_value: 版本值
        edition_confidence: 版本置信度
        model: 模型名称
        model_type: 模型类型
        tokens_used: 使用令牌数
        error: 错误信息
    """
    track_id: str = ""
    success: bool = False
    title_zh: str = ""
    album_zh: str = ""
    artist_zh: str = ""
    translation_confidence: float = Field(default=0.0, ge=0.0, le=1.0)
    genre_value: str = ""
    genre_confidence: float = Field(default=0.0, ge=0.0, le=1.0)
    edition_value: str = ""
    edition_confidence: float = Field(default=0.0, ge=0.0, le=1.0)
    model: str = ""
    model_type: str = ""
    tokens_used: int = 0
    error: Optional[str] = None
    
    @field_validator('translation_confidence', 'genre_confidence', 'edition_confidence', mode='before')
    @classmethod
    def normalize_confidence(cls, v):
        if isinstance(v, (int, float)) and v > 1.0:
            return min(v / 100.0, 1.0)
        return v


class FallbackLevelModel(BaseModel):
    """V8.1新增：降级级别枚举模型"""
    value: str = "normal"
    
    @field_validator('value', mode='before')
    @classmethod
    def validate_level(cls, v):
        valid_levels = ["normal", "enhanced", "rewrite", "ai_infer"]
        if v not in valid_levels:
            return "normal"
        return v


class QueryInputModel(BaseModel):
    """V8.1新增：查询输入模型
    
    用于数据源查询的标准化输入格式。
    
    Attributes:
        title: 歌曲标题
        artist: 艺术家名称
        album: 专辑名称
        duration: 时长（秒）
    """
    title: str = ""
    artist: str = ""
    album: str = ""
    duration: int = Field(default=0, ge=0)


class CandidateModel(BaseModel):
    """V8.1新增：候选元数据模型
    
    表示从单个数据源获取的元数据候选结果。
    
    Attributes:
        title: 歌曲标题
        artist: 艺术家名称
        album: 专辑名称
        year: 发行年份
        track_number: 音轨号
        disc_number: 光盘号
        genre: 流派
        composer: 作曲家
        lyricist: 作词家
        label: 厂牌
        country: 发行国家
        catalog_number: 目录号
        musicbrainz_id: MusicBrainz ID
        source: 数据来源类型
        confidence: 置信度 (0.0-1.0)
        match_score: 匹配分数 (0.0-1.0)
        sources: 多源融合时的来源列表
        raw: 原始数据
    """
    title: str = ""
    artist: str = ""
    album: str = ""
    year: str = ""
    track_number: str = ""
    disc_number: str = ""
    genre: str = ""
    composer: str = ""
    lyricist: str = ""
    label: str = ""
    country: str = ""
    catalog_number: str = ""
    musicbrainz_id: str = ""
    source: str = "ai"
    confidence: float = Field(default=0.0, ge=0.0, le=1.0)
    match_score: float = Field(default=0.0, ge=0.0, le=1.0)
    sources: List[str] = []
    raw: Dict[str, Any] = {}
    
    @field_validator('confidence', 'match_score', mode='before')
    @classmethod
    def normalize_confidence(cls, v):
        if isinstance(v, (int, float)) and v > 1.0:
            return min(v / 100.0, 1.0)
        return v
    
    @field_validator('source', mode='before')
    @classmethod
    def validate_source(cls, v):
        valid_sources = ["musicbrainz", "discogs", "ai"]
        if v not in valid_sources:
            return "ai"
        return v


class FinalResultModel(BaseModel):
    """V8.1新增：最终决策结果模型
    
    经过AI决策层处理后的最终元数据结果。
    
    Attributes:
        title: 歌曲标题
        artist: 艺术家名称
        album: 专辑名称
        year: 发行年份
        track_number: 音轨号
        disc_number: 光盘号
        genre: 流派
        composer: 作曲家
        lyricist: 作词家
        label: 厂牌
        confidence: 置信度 (0.0-1.0)
        source: 数据来源标识
        selected_candidate_index: 选中的候选索引
        reasoning: 决策依据说明
    """
    title: str = ""
    artist: str = ""
    album: str = ""
    year: str = ""
    track_number: str = ""
    disc_number: str = ""
    genre: str = ""
    composer: str = ""
    lyricist: str = ""
    label: str = ""
    confidence: float = Field(default=0.0, ge=0.0, le=1.0)
    source: str = ""
    selected_candidate_index: int = Field(default=-1, ge=-1)
    reasoning: str = ""
    
    @field_validator('confidence', mode='before')
    @classmethod
    def normalize_confidence(cls, v):
        if isinstance(v, (int, float)) and v > 1.0:
            return min(v / 100.0, 1.0)
        return v


class FallbackContextModel(BaseModel):
    """V8.1新增：降级上下文模型
    
    记录Fallback处理过程中的状态信息。
    
    Attributes:
        level: 降级级别
        original_candidates: 原始候选数量
        rewritten_queries: 重写后的查询列表
        final_source: 最终数据来源
        reasoning: 处理说明
    """
    level: str = "normal"
    original_candidates: int = Field(default=0, ge=0)
    rewritten_queries: List[Dict[str, str]] = []
    final_source: str = ""
    reasoning: str = ""
    
    @field_validator('level', mode='before')
    @classmethod
    def validate_level(cls, v):
        valid_levels = ["normal", "enhanced", "rewrite", "ai_infer"]
        if v not in valid_levels:
            return "normal"
        return v


class MetadataSearchResultModel(BaseModel):
    """V8.1新增：元数据搜索结果模型
    
    单个音轨的完整搜索结果，包含最终结果、候选列表和降级上下文。
    
    Attributes:
        track_id: 音轨ID
        success: 是否成功
        final: 最终决策结果
        candidates: 候选列表
        fallback_context: 降级上下文
        scraped_fields: 刮削字段字典
        error: 错误信息
    """
    track_id: str = ""
    success: bool = False
    final: Optional[FinalResultModel] = None
    candidates: List[CandidateModel] = []
    fallback_context: Optional[FallbackContextModel] = None
    scraped_fields: Dict[str, ScrapedFieldModel] = {}
    error: Optional[str] = None


class MetadataSearchRequest(BaseModel):
    """V8.1新增：元数据搜索请求模型
    
    Attributes:
        version: 协议版本
        id: 请求ID
        task_id: 任务ID
        method: 方法名
        params: 参数
    """
    version: int = 1
    id: str = ""
    task_id: str = ""
    method: str = "metadata_search"
    params: Dict[str, Any] = {}


class MetadataSearchResponse(BaseModel):
    """V8.1新增：元数据搜索响应模型
    
    Attributes:
        version: 协议版本
        id: 响应ID
        task_id: 任务ID
        success: 是否成功
        results: 结果列表
        count: 结果数量
        error: 错误信息
        timestamp: 时间戳
    """
    version: int = 1
    id: str = ""
    task_id: str = ""
    success: bool = False
    results: List[MetadataSearchResultModel] = []
    count: int = 0
    error: Optional[Dict[str, Any]] = None
    timestamp: str = ""
    
    def __init__(self, **data):
        if "timestamp" not in data or not data["timestamp"]:
            data["timestamp"] = datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")
        super().__init__(**data)


def create_metadata_search_result(
    track_id: str,
    final: Optional[FinalResultModel],
    candidates: List[CandidateModel],
    fallback_context: Optional[FallbackContextModel],
    scraped_fields: Dict[str, ScrapedFieldModel]
) -> MetadataSearchResultModel:
    """创建元数据搜索结果
    
    Args:
        track_id: 音轨ID
        final: 最终决策结果
        candidates: 候选列表
        fallback_context: 降级上下文
        scraped_fields: 刮削字段字典
    
    Returns:
        MetadataSearchResultModel: 元数据搜索结果实例
    """
    return MetadataSearchResultModel(
        track_id=track_id,
        success=final is not None,
        final=final,
        candidates=candidates,
        fallback_context=fallback_context,
        scraped_fields=scraped_fields
    )


def create_metadata_error_result(track_id: str, error_message: str) -> MetadataSearchResultModel:
    """创建元数据搜索错误结果
    
    Args:
        track_id: 音轨ID
        error_message: 错误消息
    
    Returns:
        MetadataSearchResultModel: 包含错误信息的搜索结果
    """
    return MetadataSearchResultModel(
        track_id=track_id,
        success=False,
        final=None,
        candidates=[],
        fallback_context=FallbackContextModel(level="ai_infer", reasoning=error_message),
        scraped_fields={},
        error=error_message
    )


class Stage1ScrapedFieldModel(BaseModel):
    """Stage1 刮削字段模型 - 与 C++ 端格式完全匹配
    
    Attributes:
        value: 字段值
        confidence: 置信度 (0.0-1.0)
        source: 数据来源 (musicbrainz/discogs/ai)
    """
    value: str = ""
    confidence: float = Field(default=0.0, ge=0.0, le=1.0)
    source: str = "ai"
    
    @field_validator('source', mode='before')
    @classmethod
    def validate_source(cls, v):
        valid_sources = ["musicbrainz", "discogs", "ai"]
        if v not in valid_sources:
            return "ai"
        return v
    
    @field_validator('confidence', mode='before')
    @classmethod
    def normalize_confidence(cls, v):
        if isinstance(v, (int, float)) and v > 1.0:
            return min(v / 100.0, 1.0)
        return v


class Stage1ScrapingResultModel(BaseModel):
    """Stage1 刮削结果模型 - 与 C++ 端格式完全匹配
    
    这是 C++ ai_core.cpp 中 stage1_scrape_sync 期望的响应格式。
    
    Attributes:
        track_id: 音轨ID
        success: 是否成功
        scraped_fields: 刮削字段字典，键为字段名，值为 Stage1ScrapedFieldModel
        release_source: 发布来源 (musicbrainz/discogs/ai)
        error: 错误信息（可选）
    """
    track_id: str = ""
    success: bool = False
    scraped_fields: Dict[str, Stage1ScrapedFieldModel] = {}
    release_source: str = "ai"
    error: Optional[str] = None
    
    @field_validator('release_source', mode='before')
    @classmethod
    def validate_release_source(cls, v):
        valid_sources = ["musicbrainz", "discogs", "ai"]
        if v not in valid_sources:
            return "ai"
        return v
    
    def to_cpp_dict(self) -> Dict[str, Any]:
        """转换为 C++ 端期望的字典格式
        
        Returns:
            Dict[str, Any]: C++ 端期望的格式
        """
        result = {
            "track_id": self.track_id,
            "success": self.success,
            "scraped_fields": {
                key: {
                    "value": field.value,
                    "confidence": field.confidence,
                    "source": field.source
                }
                for key, field in self.scraped_fields.items()
            },
            "release_source": self.release_source
        }
        if self.error:
            result["error"] = self.error
        return result


class Stage1ScrapingResponseModel(BaseModel):
    """Stage1 刮削响应模型 - IPC 响应包装
    
    Attributes:
        version: 协议版本
        id: 响应ID
        success: 是否成功
        results: 结果列表
        error: 错误信息（可选）
        timestamp: 时间戳
    """
    version: int = 1
    id: str = ""
    success: bool = False
    results: List[Stage1ScrapingResultModel] = []
    error: Optional[Dict[str, Any]] = None
    timestamp: str = ""
    
    def __init__(self, **data):
        if "timestamp" not in data or not data["timestamp"]:
            data["timestamp"] = datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")
        super().__init__(**data)
    
    def to_ipc_dict(self) -> Dict[str, Any]:
        """转换为 IPC 响应字典格式
        
        Returns:
            Dict[str, Any]: IPC 响应格式
        """
        result = {
            "version": self.version,
            "id": self.id,
            "success": self.success,
            "results": [r.to_cpp_dict() for r in self.results],
            "timestamp": self.timestamp,
            "count": len(self.results)
        }
        if self.error:
            result["error"] = self.error
        return result


def create_stage1_scraping_result(
    track_id: str,
    success: bool,
    scraped_fields: Dict[str, Any],
    release_source: str = "ai",
    error: Optional[str] = None
) -> Stage1ScrapingResultModel:
    """创建 Stage1 刮削结果
    
    Args:
        track_id: 音轨ID
        success: 是否成功
        scraped_fields: 刮削字段字典
        release_source: 发布来源
        error: 错误信息
    
    Returns:
        Stage1ScrapingResultModel: Stage1 刮削结果实例
    """
    fields = {}
    for key, val in scraped_fields.items():
        if isinstance(val, dict):
            fields[key] = Stage1ScrapedFieldModel(**val)
        elif isinstance(val, Stage1ScrapedFieldModel):
            fields[key] = val
        else:
            fields[key] = Stage1ScrapedFieldModel(value=str(val))
    
    return Stage1ScrapingResultModel(
        track_id=track_id,
        success=success,
        scraped_fields=fields,
        release_source=release_source,
        error=error
    )


def create_stage1_error_result(track_id: str, error_message: str) -> Stage1ScrapingResultModel:
    """创建 Stage1 错误结果
    
    Args:
        track_id: 音轨ID
        error_message: 错误消息
    
    Returns:
        Stage1ScrapingResultModel: 包含错误信息的刮削结果
    """
    return Stage1ScrapingResultModel(
        track_id=track_id,
        success=False,
        scraped_fields={},
        release_source="ai",
        error=error_message
    )


class Stage2EnhancementFieldModel(BaseModel):
    """Stage2 增强字段模型 - 与 C++ 端格式完全匹配
    
    Attributes:
        value: 字段值
        confidence: 置信度 (0.0-1.0)
    """
    value: str = ""
    confidence: float = Field(default=0.0, ge=0.0, le=1.0)
    
    @field_validator('confidence', mode='before')
    @classmethod
    def normalize_confidence(cls, v):
        if isinstance(v, (int, float)) and v > 1.0:
            return min(v / 100.0, 1.0)
        return v


class Stage2EnhancementResultModel(BaseModel):
    """Stage2 增强结果模型 - 与 C++ 端格式完全匹配
    
    这是 C++ ai_core.cpp 中 stage2_enhance_sync 期望的响应格式。
    
    Attributes:
        track_id: 音轨ID
        success: 是否成功
        title_zh: 中文标题
        album_zh: 中文专辑名
        artist_zh: 中文艺术家名
        translation_confidence: 翻译置信度
        genre_value: 流派值
        genre_confidence: 流派置信度
        edition_value: 版本值
        edition_confidence: 版本置信度
        model: 使用的模型
        model_type: 模型类型
        tokens_used: 使用的令牌数
        error: 错误信息（可选）
    """
    track_id: str = ""
    success: bool = False
    title_zh: str = ""
    album_zh: str = ""
    artist_zh: str = ""
    translation_confidence: float = Field(default=0.0, ge=0.0, le=1.0)
    genre_value: str = ""
    genre_confidence: float = Field(default=0.0, ge=0.0, le=1.0)
    edition_value: str = ""
    edition_confidence: float = Field(default=0.0, ge=0.0, le=1.0)
    model: str = ""
    model_type: str = ""
    tokens_used: int = 0
    error: Optional[str] = None
    
    @field_validator('translation_confidence', 'genre_confidence', 'edition_confidence', mode='before')
    @classmethod
    def normalize_confidence(cls, v):
        if isinstance(v, (int, float)) and v > 1.0:
            return min(v / 100.0, 1.0)
        return v
    
    def to_cpp_dict(self) -> Dict[str, Any]:
        """转换为 C++ 端期望的字典格式
        
        Returns:
            Dict[str, Any]: C++ 端期望的格式
        """
        result = {
            "track_id": self.track_id,
            "success": self.success,
            "title_zh": self.title_zh,
            "album_zh": self.album_zh,
            "artist_zh": self.artist_zh,
            "translation_confidence": self.translation_confidence,
            "genre_value": self.genre_value,
            "genre_confidence": self.genre_confidence,
            "edition_value": self.edition_value,
            "edition_confidence": self.edition_confidence,
            "model": self.model,
            "model_type": self.model_type,
            "tokens_used": self.tokens_used
        }
        if self.error:
            result["error"] = self.error
        return result


class Stage2EnhancementResponseModel(BaseModel):
    """Stage2 增强响应模型 - IPC 响应包装
    
    Attributes:
        version: 协议版本
        id: 响应ID
        success: 是否成功
        results: 结果列表
        error: 错误信息（可选）
        timestamp: 时间戳
    """
    version: int = 1
    id: str = ""
    success: bool = False
    results: List[Stage2EnhancementResultModel] = []
    error: Optional[Dict[str, Any]] = None
    timestamp: str = ""
    
    def __init__(self, **data):
        if "timestamp" not in data or not data["timestamp"]:
            data["timestamp"] = datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")
        super().__init__(**data)
    
    def to_ipc_dict(self) -> Dict[str, Any]:
        """转换为 IPC 响应字典格式
        
        Returns:
            Dict[str, Any]: IPC 响应格式
        """
        result = {
            "version": self.version,
            "id": self.id,
            "success": self.success,
            "results": [r.to_cpp_dict() for r in self.results],
            "timestamp": self.timestamp,
            "count": len(self.results)
        }
        if self.error:
            result["error"] = self.error
        return result


def create_stage2_enhancement_result(
    track_id: str,
    success: bool,
    title_zh: str = "",
    album_zh: str = "",
    artist_zh: str = "",
    translation_confidence: float = 0.0,
    genre_value: str = "",
    genre_confidence: float = 0.0,
    edition_value: str = "",
    edition_confidence: float = 0.0,
    model: str = "",
    model_type: str = "",
    tokens_used: int = 0,
    error: Optional[str] = None
) -> Stage2EnhancementResultModel:
    """创建 Stage2 增强结果
    
    Args:
        track_id: 音轨ID
        success: 是否成功
        title_zh: 中文标题
        album_zh: 中文专辑名
        artist_zh: 中文艺术家名
        translation_confidence: 翻译置信度
        genre_value: 流派值
        genre_confidence: 流派置信度
        edition_value: 版本值
        edition_confidence: 版本置信度
        model: 使用的模型
        model_type: 模型类型
        tokens_used: 使用的令牌数
        error: 错误信息
    
    Returns:
        Stage2EnhancementResultModel: Stage2 增强结果实例
    """
    return Stage2EnhancementResultModel(
        track_id=track_id,
        success=success,
        title_zh=title_zh,
        album_zh=album_zh,
        artist_zh=artist_zh,
        translation_confidence=translation_confidence,
        genre_value=genre_value,
        genre_confidence=genre_confidence,
        edition_value=edition_value,
        edition_confidence=edition_confidence,
        model=model,
        model_type=model_type,
        tokens_used=tokens_used,
        error=error
    )


def create_stage2_error_result(track_id: str, error_message: str) -> Stage2EnhancementResultModel:
    """创建 Stage2 错误结果
    
    Args:
        track_id: 音轨ID
        error_message: 错误消息
    
    Returns:
        Stage2EnhancementResultModel: 包含错误信息的增强结果
    """
    return Stage2EnhancementResultModel(
        track_id=track_id,
        success=False,
        error=error_message
    )


class MetadataSearchResponseModel(BaseModel):
    """元数据搜索响应模型 - IPC 响应包装
    
    Attributes:
        version: 协议版本
        id: 响应ID
        task_id: 任务ID
        success: 是否成功
        results: 结果列表
        count: 结果数量
        error: 错误信息（可选）
        timestamp: 时间戳
    """
    version: int = 1
    id: str = ""
    task_id: str = ""
    success: bool = False
    results: List[MetadataSearchResultModel] = []
    count: int = 0
    error: Optional[Dict[str, Any]] = None
    timestamp: str = ""
    
    def __init__(self, **data):
        if "timestamp" not in data or not data["timestamp"]:
            data["timestamp"] = datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")
        super().__init__(**data)
    
    def to_ipc_dict(self) -> Dict[str, Any]:
        """转换为 IPC 响应字典格式
        
        Returns:
            Dict[str, Any]: IPC 响应格式
        """
        result = {
            "version": self.version,
            "id": self.id,
            "task_id": self.task_id,
            "success": self.success,
            "results": [r.model_dump(exclude_none=True) for r in self.results],
            "count": len(self.results),
            "timestamp": self.timestamp
        }
        if self.error:
            result["error"] = self.error
        return result


def create_metadata_search_response(
    request_id: str,
    task_id: str,
    results: List[MetadataSearchResultModel],
    success: bool = True,
    error: Optional[Dict[str, Any]] = None
) -> MetadataSearchResponseModel:
    """创建元数据搜索响应
    
    Args:
        request_id: 请求ID
        task_id: 任务ID
        results: 结果列表
        success: 是否成功
        error: 错误信息
    
    Returns:
        MetadataSearchResponseModel: 元数据搜索响应实例
    """
    return MetadataSearchResponseModel(
        id=request_id,
        task_id=task_id,
        success=success,
        results=results,
        error=error
    )
