#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Pydantic Models for AI Metadata Plugin (V8.2 单一来源)

本模块仅定义 IPC 边界（ai_worker.py ↔ C++ ai_core.cpp）的 JSON 校验模型。

V8.2 数据模型统一边界：
  - IPC 边界模型（本文件）：Stage1Scraping* / Stage2Enhancement* / IPCResponse
  - 运行时 dataclass 模型：core/types.py（TrackInput）、data_sources/base.py
    （Candidate / FinalResult / QueryInput / ScrapingOptions / EnhancementOptions /
    DataSourceType / FallbackLevel）
  - 三者不重叠，不再有 Pydantic 镜像 dataclass 的孤儿类。

历史 V8.1 模型（TrackOptions / GenreInfo / EditionInfo / TranslationInfo / AIResult /
AnalysisInfo / OriginalMetadata / TrackAnalysisResult / ErrorInfo / BatchResponse /
IPCRequest / AIGenreResponse / AIEditionResponse / AIBatchAnalysisResponse /
CacheEntry / CacheStatistics / WorkerInfo / EnhancementOptionsModel /
DataSourceTypeModel / ScrapedFieldModel / TrackScrapingResultModel /
MissingFieldInfoModel / EnhancementResultModel / FallbackLevelModel /
QueryInputModel / CandidateModel / FinalResultModel / MetadataSearchResultModel /
MetadataSearchResponseModel / FallbackContextModel 及其工厂函数）
已在 V8.2 移除——它们的职责由上述两类模型分担。
"""

from typing import Optional, List, Dict, Any
from pydantic import BaseModel, Field, field_validator
from datetime import datetime, timezone


# =============================================================================
# IPC 通用响应包装
# =============================================================================

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


# =============================================================================
# Stage1（Scrape 层）IPC 模型
# =============================================================================

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
    """Stage1 刮削结果模型 - 单首音轨的刮削结果

    Attributes:
        track_id: 音轨ID
        success: 是否成功
        scraped_fields: 刮削字段字典，键为字段名，值为 Stage1ScrapedFieldModel
        release_source: 主来源标识
        error: 错误信息（可选）
    """
    track_id: str = ""
    success: bool = False
    scraped_fields: Dict[str, Stage1ScrapedFieldModel] = {}
    release_source: str = "ai"
    error: Optional[str] = None

    def to_cpp_dict(self) -> Dict[str, Any]:
        """转换为 C++ 端期望的字典格式

        Returns:
            Dict[str, Any]: C++ 端期望的格式
        """
        result = {
            "track_id": self.track_id,
            "success": self.success,
            "scraped_fields": {
                k: v.model_dump() if hasattr(v, 'model_dump') else v
                for k, v in self.scraped_fields.items()
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
    fields: Dict[str, Stage1ScrapedFieldModel] = {}
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


# =============================================================================
# Stage2（Enhancer 层）IPC 模型
# =============================================================================

class Stage2EnhancementResultModel(BaseModel):
    """Stage2 增强结果模型 - 与 C++ 端格式完全匹配

    这是 C++ ai_core.cpp 中 stage2_enhance_sync 期望的响应格式。

    V8.2: genre / edition 字段已移除。
    - genre 改由 Stage1 从 MusicBrainz 抓取
    - edition 已废弃（AI 推断不可靠）
    Stage2 仅保留翻译结果。

    Attributes:
        track_id: 音轨ID
        success: 是否成功
        title_zh: 中文标题
        album_zh: 中文专辑名
        artist_zh: 中文艺术家名
        translation_confidence: 翻译置信度
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
    model: str = ""
    model_type: str = ""
    tokens_used: int = 0
    error: Optional[str] = None

    @field_validator('translation_confidence', mode='before')
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
