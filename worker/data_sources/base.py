#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Data Source Base Module
Defines base classes and data structures for metadata sources
"""

import logging
from abc import ABC, abstractmethod
from dataclasses import dataclass, field
from enum import Enum
from typing import Optional, List, Dict, Any

logger = logging.getLogger(__name__)


class DataSourceType(str, Enum):
    """数据源类型枚举"""
    MUSICBRAINZ = "musicbrainz"
    DISCOGS = "discogs"
    AI = "ai"


class FallbackLevel(str, Enum):
    """降级级别枚举"""
    NORMAL = "normal"
    ENHANCED = "enhanced"
    REWRITE = "rewrite"
    AI_INFER = "ai_infer"


@dataclass
class QueryInput:
    """查询输入数据结构
    
    用于数据源查询的标准化输入格式。
    
    Attributes:
        track_id: 音轨ID（用于标识）
        title: 歌曲标题
        artist: 艺术家名称
        album: 专辑名称
        duration: 时长（秒）
        raw_data: 原始音轨数据（用于降级时保留更多字段）
    """
    track_id: str = ""
    title: str = ""
    artist: str = ""
    album: str = ""
    duration: int = 0
    raw_data: Optional[Dict[str, Any]] = None
    
    def to_dict(self) -> Dict[str, Any]:
        """转换为字典"""
        return {
            "track_id": self.track_id,
            "title": self.title,
            "artist": self.artist,
            "album": self.album,
            "duration": self.duration,
        }
    
    @classmethod
    def from_dict(cls, data: Dict[str, Any]) -> "QueryInput":
        """从字典创建"""
        return cls(
            track_id=data.get("track_id", ""),
            title=data.get("title", ""),
            artist=data.get("artist", ""),
            album=data.get("album", ""),
            duration=data.get("duration", 0),
        )


@dataclass
class Candidate:
    """候选元数据 - 多源融合的基础单元
    
    表示从单个数据源获取的元数据候选结果，
    用于后续聚合和AI决策。
    
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
    source: DataSourceType = DataSourceType.AI
    confidence: float = 0.0
    match_score: float = 0.0
    sources: List[DataSourceType] = field(default_factory=list)
    raw: Dict[str, Any] = field(default_factory=dict)
    
    def get_merge_key(self) -> str:
        """生成合并用的 key（标准化后的 title|artist）
        
        用于候选聚合时的分组依据。
        
        Returns:
            str: 标准化后的合并键
        """
        import unicodedata
        
        def normalize(s: str) -> str:
            if not s:
                return ""
            s = unicodedata.normalize('NFKD', s)
            s = s.lower()
            s = ''.join(c for c in s if c.isalnum() or c.isspace())
            return ' '.join(s.split())
        
        return f"{normalize(self.title)}|{normalize(self.artist)}"
    
    def to_dict(self) -> Dict[str, Any]:
        """转换为字典"""
        return {
            "title": self.title,
            "artist": self.artist,
            "album": self.album,
            "year": self.year,
            "track_number": self.track_number,
            "disc_number": self.disc_number,
            "genre": self.genre,
            "composer": self.composer,
            "lyricist": self.lyricist,
            "label": self.label,
            "country": self.country,
            "catalog_number": self.catalog_number,
            "musicbrainz_id": self.musicbrainz_id,
            "source": self.source.value if isinstance(self.source, DataSourceType) else self.source,
            "confidence": self.confidence,
            "match_score": self.match_score,
            "sources": [s.value if isinstance(s, DataSourceType) else s for s in self.sources],
            "raw": self.raw,
        }
    
    @classmethod
    def from_dict(cls, data: Dict[str, Any]) -> "Candidate":
        """从字典创建"""
        source = data.get("source", "ai")
        if isinstance(source, str):
            source = DataSourceType(source)
        
        sources_data = data.get("sources", [])
        sources = []
        for s in sources_data:
            if isinstance(s, str):
                sources.append(DataSourceType(s))
            elif isinstance(s, DataSourceType):
                sources.append(s)
        
        return cls(
            title=data.get("title", ""),
            artist=data.get("artist", ""),
            album=data.get("album", ""),
            year=data.get("year", ""),
            track_number=data.get("track_number", ""),
            disc_number=data.get("disc_number", ""),
            genre=data.get("genre", ""),
            composer=data.get("composer", ""),
            lyricist=data.get("lyricist", ""),
            label=data.get("label", ""),
            country=data.get("country", ""),
            catalog_number=data.get("catalog_number", ""),
            musicbrainz_id=data.get("musicbrainz_id", ""),
            source=source,
            confidence=data.get("confidence", 0.0),
            match_score=data.get("match_score", 0.0),
            sources=sources,
            raw=data.get("raw", {}),
        )


@dataclass
class FinalResult:
    """最终决策结果
    
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
        country: 发行国家
        catalog_number: 目录号
        musicbrainz_id: MusicBrainz ID
        conductor: 指挥
        performer: 演奏者
        mood: 情绪
        bpm: 节拍
        confidence: 置信度 (0.0-1.0)
        source: 数据来源标识
        selected_candidate_index: 选中的候选索引
        reasoning: 决策依据说明
        is_fallback: 是否为降级结果
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
    conductor: str = ""
    performer: str = ""
    mood: str = ""
    bpm: str = ""
    confidence: float = 0.0
    source: str = ""
    selected_candidate_index: int = -1
    reasoning: str = ""
    is_fallback: bool = False
    
    def to_dict(self) -> Dict[str, Any]:
        """转换为字典"""
        return {
            "title": self.title,
            "artist": self.artist,
            "album": self.album,
            "year": self.year,
            "track_number": self.track_number,
            "disc_number": self.disc_number,
            "genre": self.genre,
            "composer": self.composer,
            "lyricist": self.lyricist,
            "label": self.label,
            "country": self.country,
            "catalog_number": self.catalog_number,
            "musicbrainz_id": self.musicbrainz_id,
            "conductor": self.conductor,
            "performer": self.performer,
            "mood": self.mood,
            "bpm": self.bpm,
            "confidence": self.confidence,
            "source": self.source,
            "selected_candidate_index": self.selected_candidate_index,
            "reasoning": self.reasoning,
            "is_fallback": self.is_fallback,
        }
    
    @classmethod
    def from_dict(cls, data: Dict[str, Any]) -> "FinalResult":
        """从字典创建"""
        return cls(
            title=data.get("title", ""),
            artist=data.get("artist", ""),
            album=data.get("album", ""),
            year=data.get("year", ""),
            track_number=data.get("track_number", ""),
            disc_number=data.get("disc_number", ""),
            genre=data.get("genre", ""),
            composer=data.get("composer", ""),
            lyricist=data.get("lyricist", ""),
            label=data.get("label", ""),
            country=data.get("country", ""),
            catalog_number=data.get("catalog_number", ""),
            musicbrainz_id=data.get("musicbrainz_id", ""),
            conductor=data.get("conductor", ""),
            performer=data.get("performer", ""),
            mood=data.get("mood", ""),
            bpm=data.get("bpm", ""),
            confidence=data.get("confidence", 0.0),
            source=data.get("source", ""),
            selected_candidate_index=data.get("selected_candidate_index", -1),
            reasoning=data.get("reasoning", ""),
            is_fallback=data.get("is_fallback", False),
        )


@dataclass
class FallbackContext:
    """降级上下文
    
    记录Fallback处理过程中的状态信息。
    
    Attributes:
        level: 降级级别
        original_candidates: 原始候选数量
        rewritten_queries: 重写后的查询列表
        final_source: 最终数据来源
        reasoning: 处理说明
    """
    level: FallbackLevel = FallbackLevel.NORMAL
    original_candidates: int = 0
    rewritten_queries: Optional[List[Dict[str, str]]] = None
    final_source: str = ""
    reasoning: str = ""
    
    def to_dict(self) -> Dict[str, Any]:
        """转换为字典"""
        return {
            "level": self.level.value if isinstance(self.level, FallbackLevel) else self.level,
            "original_candidates": self.original_candidates,
            "rewritten_queries": self.rewritten_queries or [],
            "final_source": self.final_source,
            "reasoning": self.reasoning,
        }
    
    @classmethod
    def from_dict(cls, data: Dict[str, Any]) -> "FallbackContext":
        """从字典创建"""
        level = data.get("level", "normal")
        if isinstance(level, str):
            level = FallbackLevel(level)
        
        return cls(
            level=level,
            original_candidates=data.get("original_candidates", 0),
            rewritten_queries=data.get("rewritten_queries"),
            final_source=data.get("final_source", ""),
            reasoning=data.get("reasoning", ""),
        )


@dataclass
class ReleaseInfo:
    """发行信息"""
    release_id: str
    title: str
    artist: str
    year: str = ""
    country: str = ""
    label: str = ""
    catalog_number: str = ""
    track_count: int = 0
    disc_count: int = 1
    format: str = ""
    tracks: List[Dict[str, Any]] = field(default_factory=list)
    confidence: float = 0.0
    source: DataSourceType = DataSourceType.MUSICBRAINZ
    
    def to_dict(self) -> Dict[str, Any]:
        """转换为字典"""
        return {
            "release_id": self.release_id,
            "title": self.title,
            "artist": self.artist,
            "year": self.year,
            "country": self.country,
            "label": self.label,
            "catalog_number": self.catalog_number,
            "track_count": self.track_count,
            "disc_count": self.disc_count,
            "format": self.format,
            "tracks": self.tracks,
            "confidence": self.confidence,
            "source": self.source.value if isinstance(self.source, DataSourceType) else self.source
        }
    
    @classmethod
    def from_dict(cls, data: Dict[str, Any]) -> "ReleaseInfo":
        """从字典创建"""
        source = data.get("source", "musicbrainz")
        if isinstance(source, str):
            source = DataSourceType(source)
        return cls(
            release_id=data.get("release_id", ""),
            title=data.get("title", ""),
            artist=data.get("artist", ""),
            year=data.get("year", ""),
            country=data.get("country", ""),
            label=data.get("label", ""),
            catalog_number=data.get("catalog_number", ""),
            track_count=data.get("track_count", 0),
            disc_count=data.get("disc_count", 1),
            format=data.get("format", ""),
            tracks=data.get("tracks", []),
            confidence=data.get("confidence", 0.0),
            source=source
        )


@dataclass
class ScrapingOptions:
    """刮削选项 - 阶段一"""
    
    title: str = ""
    artist: str = ""
    album: str = ""
    year: str = ""
    
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
    
    auto_accept_threshold: float = 0.9
    confirm_threshold: float = 0.7
    
    def get_enabled_fields(self) -> List[str]:
        """获取启用的字段列表"""
        fields = []
        field_mapping = {
            "title": self.scrape_title,
            "artist": self.scrape_artist,
            "album": self.scrape_album,
            "year": self.scrape_year,
            "track_number": self.scrape_track_number,
            "disc_number": self.scrape_disc_number,
            "composer": self.scrape_composer,
            "lyricist": self.scrape_lyricist,
            "conductor": self.scrape_conductor,
            "performer": self.scrape_performer,
            "producer": self.scrape_producer,
            "engineer": self.scrape_engineer,
            "orchestra": self.scrape_orchestra,
            "ensemble": self.scrape_ensemble,
            "label": self.scrape_label,
            "country": self.scrape_country,
            "catalog_number": self.scrape_catalog_number,
            "original_artist": self.scrape_original_artist,
            "original_album": self.scrape_original_album,
            "original_year": self.scrape_original_year,
            "musicbrainz_id": self.scrape_musicbrainz_id,
            "isrc": self.scrape_isrc,
        }
        for field_name, enabled in field_mapping.items():
            if enabled:
                fields.append(field_name)
        return fields
    
    def to_dict(self) -> Dict[str, Any]:
        """转换为字典"""
        return {
            "album": self.album,
            "year": self.year,
            "scrape_title": self.scrape_title,
            "scrape_artist": self.scrape_artist,
            "scrape_album": self.scrape_album,
            "scrape_year": self.scrape_year,
            "scrape_track_number": self.scrape_track_number,
            "scrape_disc_number": self.scrape_disc_number,
            "scrape_composer": self.scrape_composer,
            "scrape_lyricist": self.scrape_lyricist,
            "scrape_conductor": self.scrape_conductor,
            "scrape_performer": self.scrape_performer,
            "scrape_producer": self.scrape_producer,
            "scrape_engineer": self.scrape_engineer,
            "scrape_orchestra": self.scrape_orchestra,
            "scrape_ensemble": self.scrape_ensemble,
            "scrape_label": self.scrape_label,
            "scrape_country": self.scrape_country,
            "scrape_catalog_number": self.scrape_catalog_number,
            "scrape_original_artist": self.scrape_original_artist,
            "scrape_original_album": self.scrape_original_album,
            "scrape_original_year": self.scrape_original_year,
            "scrape_musicbrainz_id": self.scrape_musicbrainz_id,
            "scrape_isrc": self.scrape_isrc,
            "enable_musicbrainz": self.enable_musicbrainz,
            "enable_discogs": self.enable_discogs,
            "enable_ai": self.enable_ai,
            "auto_accept_threshold": self.auto_accept_threshold,
            "confirm_threshold": self.confirm_threshold,
        }
    
    @classmethod
    def from_dict(cls, data: Dict[str, Any]) -> "ScrapingOptions":
        """从字典创建"""
        return cls(
            album=data.get("album", ""),
            year=data.get("year", ""),
            scrape_title=data.get("scrape_title", True),
            scrape_artist=data.get("scrape_artist", True),
            scrape_album=data.get("scrape_album", True),
            scrape_year=data.get("scrape_year", True),
            scrape_track_number=data.get("scrape_track_number", True),
            scrape_disc_number=data.get("scrape_disc_number", True),
            scrape_composer=data.get("scrape_composer", True),
            scrape_lyricist=data.get("scrape_lyricist", True),
            scrape_conductor=data.get("scrape_conductor", False),
            scrape_performer=data.get("scrape_performer", False),
            scrape_producer=data.get("scrape_producer", False),
            scrape_engineer=data.get("scrape_engineer", False),
            scrape_orchestra=data.get("scrape_orchestra", False),
            scrape_ensemble=data.get("scrape_ensemble", False),
            scrape_label=data.get("scrape_label", True),
            scrape_country=data.get("scrape_country", False),
            scrape_catalog_number=data.get("scrape_catalog_number", False),
            scrape_original_artist=data.get("scrape_original_artist", False),
            scrape_original_album=data.get("scrape_original_album", False),
            scrape_original_year=data.get("scrape_original_year", False),
            scrape_musicbrainz_id=data.get("scrape_musicbrainz_id", True),
            scrape_isrc=data.get("scrape_isrc", False),
            enable_musicbrainz=data.get("enable_musicbrainz", True),
            enable_discogs=data.get("enable_discogs", True),
            enable_ai=data.get("enable_ai", True),
            auto_accept_threshold=data.get("auto_accept_threshold", 0.9),
            confirm_threshold=data.get("confirm_threshold", 0.7),
        )


@dataclass
class EnhancementOptions:
    """增强选项 - 阶段二"""
    
    translate_title: bool = True
    translate_album: bool = True
    translate_artist: bool = True
    
    classify_genre: bool = True
    identify_edition: bool = True
    
    scrape_mood: bool = False
    scrape_bpm: bool = False
    
    def to_dict(self) -> Dict[str, Any]:
        """转换为字典"""
        return {
            "translate_title": self.translate_title,
            "translate_album": self.translate_album,
            "translate_artist": self.translate_artist,
            "classify_genre": self.classify_genre,
            "identify_edition": self.identify_edition,
            "scrape_mood": self.scrape_mood,
            "scrape_bpm": self.scrape_bpm,
        }
    
    @classmethod
    def from_dict(cls, data: Dict[str, Any]) -> "EnhancementOptions":
        """从字典创建"""
        return cls(
            translate_title=data.get("translate_title", True),
            translate_album=data.get("translate_album", True),
            translate_artist=data.get("translate_artist", True),
            classify_genre=data.get("classify_genre", True),
            identify_edition=data.get("identify_edition", True),
            scrape_mood=data.get("scrape_mood", False),
            scrape_bpm=data.get("scrape_bpm", False),
        )


class DataSourceAdapter(ABC):
    """数据源适配器基类"""
    
    def __init__(self, config: Dict[str, Any]):
        """初始化适配器
        
        Args:
            config: 配置字典
        """
        self._config = config
        self._rate_limiter = None
    
    @property
    @abstractmethod
    def source_type(self) -> DataSourceType:
        """数据源类型"""
        pass
    
    @property
    @abstractmethod
    def is_enabled(self) -> bool:
        """是否启用"""
        pass
    
    @abstractmethod
    def search_candidates(self, query: QueryInput) -> List[Candidate]:
        """搜索并返回候选列表
        
        并发查询接口，返回多个候选结果供后续聚合和决策。
        
        Args:
            query: 查询输入（title, artist, album, duration）
        
        Returns:
            List[Candidate]: 候选列表
        """
        pass
    
    @abstractmethod
    def get_release_info(self, release_id: str) -> Optional[ReleaseInfo]:
        """获取发行/专辑详细信息
        
        使用场景：
        1. 用户选择了某个匹配结果后，获取完整的发行信息
        2. 补全专辑级别的元数据（厂牌、发行国家、目录号等）
        3. 获取完整的音轨列表（用于补全音轨号）
        
        Args:
            release_id: 发行ID（MusicBrainz release ID 或 Discogs release ID）
        
        Returns:
            ReleaseInfo: 发行详细信息
        """
        pass
    
    def _calculate_confidence(self, match_score: float, 
                              has_multiple_matches: bool = False) -> float:
        """计算置信度
        
        Args:
            match_score: 匹配分数 (0.0-1.0)
            has_multiple_matches: 是否有多个匹配结果
        
        Returns:
            float: 置信度 (0.0-1.0)
        """
        base_confidence = match_score
        
        if has_multiple_matches:
            base_confidence *= 0.9
        
        return min(1.0, max(0.0, base_confidence))
    
    def _normalize_string(self, s: str) -> str:
        """标准化字符串用于比较
        
        Args:
            s: 原始字符串
        
        Returns:
            str: 标准化后的字符串
        """
        if not s:
            return ""
        
        import unicodedata
        s = unicodedata.normalize('NFKD', s)
        s = s.lower()
        s = ''.join(c for c in s if c.isalnum() or c.isspace())
        s = ' '.join(s.split())
        
        return s
    
    def _calculate_string_similarity(self, s1: str, s2: str) -> float:
        """计算字符串相似度
        
        Args:
            s1: 字符串1
            s2: 字符串2
        
        Returns:
            float: 相似度 (0.0-1.0)
        """
        from difflib import SequenceMatcher
        
        n1 = self._normalize_string(s1)
        n2 = self._normalize_string(s2)
        
        if not n1 or not n2:
            return 0.0
        
        return SequenceMatcher(None, n1, n2).ratio()
