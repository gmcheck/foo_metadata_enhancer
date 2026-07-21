#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Pydantic Models for AI Metadata Plugin (V8.2 单一来源)

本模块仅定义 IPC 边界（ai_worker.py ↔ C++ ai_core.cpp）的 JSON 校验模型。

V8.2 数据模型统一边界：
  - IPC 边界模型（本文件）：Scrape* / Enhance* / IPCResponse / Provider*
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
# Scrape 层IPC 模型
# =============================================================================

class ScrapeFieldModel(BaseModel):
    """Scrape 刮削字段模型 - 与 C++ 端格式完全匹配

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


class ScrapeResultModel(BaseModel):
    """Scrape 刮削结果模型 - 单首音轨的刮削结果

    Attributes:
        track_id: 音轨ID
        success: 是否成功
        scraped_fields: 刮削字段字典，键为字段名，值为 ScrapeFieldModel
        release_source: 主来源标识
        error: 错误信息（可选）
    """
    track_id: str = ""
    success: bool = False
    scraped_fields: Dict[str, ScrapeFieldModel] = {}
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


class ScrapeResponseModel(BaseModel):
    """Scrape 刮削响应模型 - IPC 响应包装

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
    results: List[ScrapeResultModel] = []
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


def create_scrape_result(
    track_id: str,
    success: bool,
    scraped_fields: Dict[str, Any],
    release_source: str = "ai",
    error: Optional[str] = None
) -> ScrapeResultModel:
    """创建 Scrape 刮削结果

    Args:
        track_id: 音轨ID
        success: 是否成功
        scraped_fields: 刮削字段字典
        release_source: 发布来源
        error: 错误信息

    Returns:
        ScrapeResultModel: Scrape 刮削结果实例
    """
    fields: Dict[str, ScrapeFieldModel] = {}
    for key, val in scraped_fields.items():
        if isinstance(val, dict):
            fields[key] = ScrapeFieldModel(**val)
        elif isinstance(val, ScrapeFieldModel):
            fields[key] = val
        else:
            fields[key] = ScrapeFieldModel(value=str(val))

    return ScrapeResultModel(
        track_id=track_id,
        success=success,
        scraped_fields=fields,
        release_source=release_source,
        error=error
    )


def create_scrape_error_result(track_id: str, error_message: str) -> ScrapeResultModel:
    """创建 Scrape 错误结果

    Args:
        track_id: 音轨ID
        error_message: 错误消息

    Returns:
        ScrapeResultModel: 包含错误信息的刮削结果
    """
    return ScrapeResultModel(
        track_id=track_id,
        success=False,
        scraped_fields={},
        release_source="ai",
        error=error_message
    )


# =============================================================================
# Enhancer 层IPC 模型
# =============================================================================

class EnhanceResultModel(BaseModel):
    """Enhance 增强结果模型 - 与 C++ 端格式完全匹配

    这是 C++ ai_core.cpp 中 enhance_sync 期望的响应格式。

    V8.2: genre / edition 字段已移除。
    - genre 改由 Scrape 从 MusicBrainz 抓取
    - edition 已废弃（AI 推断不可靠）
    Enhance 仅保留翻译结果。

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
    cache_hit: bool = False
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
            "tokens_used": self.tokens_used,
            "cache_hit": self.cache_hit
        }
        if self.error:
            result["error"] = self.error
        return result


class EnhanceResponseModel(BaseModel):
    """Enhance 增强响应模型 - IPC 响应包装

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
    results: List[EnhanceResultModel] = []
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


def create_enhance_result(
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
) -> EnhanceResultModel:
    """创建 Enhance 增强结果

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
        EnhanceResultModel: Enhance 增强结果实例
    """
    return EnhanceResultModel(
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


def create_enhance_error_result(track_id: str, error_message: str) -> EnhanceResultModel:
    """创建 Enhance 错误结果

    Args:
        track_id: 音轨ID
        error_message: 错误消息

    Returns:
        EnhanceResultModel: 包含错误信息的增强结果
    """
    return EnhanceResultModel(
        track_id=track_id,
        success=False,
        error=error_message
    )


# =============================================================================
# Providers 层 IPC 模型（V1）
# =============================================================================

STATUS_SUCCESS = "success"
STATUS_FAILED = "failed"
DEFAULT_PROVIDER_ERROR_CATEGORY = "Request Failed"


class ProviderTestDataModel(BaseModel):
    """providers.test 的 data 载荷。

    Attributes:
        id: 被测 provider id（按 id 测时）
        provider: 显示名
        model: 模型名
        protocol: openai_chat | anthropic_messages
        latency_ms: 耗时毫秒
        content_preview: 响应内容预览
        http_status: HTTP 状态码（0 表示未知）
    """
    id: str = ""
    provider: str = ""
    model: str = ""
    protocol: str = ""
    latency_ms: int = 0
    content_preview: str = ""
    http_status: int = 0

    @classmethod
    def from_runtime_result(
        cls,
        raw: Optional[Dict[str, Any]] = None,
        *,
        provider_id: str = "",
    ) -> "ProviderTestDataModel":
        raw = raw or {}
        http_status = raw.get("http_status")
        try:
            http_status_i = int(http_status or 0)
        except (TypeError, ValueError):
            http_status_i = 0
        try:
            latency_i = int(raw.get("latency_ms") or 0)
        except (TypeError, ValueError):
            latency_i = 0
        return cls(
            id=str(provider_id or raw.get("id") or ""),
            provider=str(raw.get("provider") or ""),
            model=str(raw.get("model") or ""),
            protocol=str(raw.get("protocol") or ""),
            latency_ms=latency_i,
            content_preview=str(raw.get("content_preview") or ""),
            http_status=http_status_i,
        )


class ProviderActionResultModel(BaseModel):
    """providers.* 的 results[0] 统一载荷。

    成功示例::
        {
          "status": "success",
          "action": "test",
          "data": { "provider": "Zhipu", "model": "...", ... }
        }

    失败示例::
        {
          "status": "failed",
          "action": "test",
          "error": "...",
          "error_category": "Invalid Configuration",
          "data": {}
        }

    规则：
      - status 仅允许 success / failed
      - data 始终为 object（可空）
      - 成功时不输出 error / error_category
      - 失败时 error / error_category 为非空字符串，禁止 null
    """
    status: str = STATUS_FAILED
    action: str = ""
    data: Dict[str, Any] = Field(default_factory=dict)
    error: Optional[str] = None
    error_category: Optional[str] = None

    @field_validator("status", mode="before")
    @classmethod
    def validate_status(cls, v):
        if v in (STATUS_SUCCESS, STATUS_FAILED):
            return v
        if v is True or str(v).lower() in ("1", "true", "ok", "success"):
            return STATUS_SUCCESS
        return STATUS_FAILED

    @field_validator("data", mode="before")
    @classmethod
    def validate_data(cls, v):
        if v is None:
            return {}
        if isinstance(v, BaseModel):
            return v.model_dump()
        if isinstance(v, dict):
            return v
        return {}

    @property
    def success(self) -> bool:
        return self.status == STATUS_SUCCESS

    def to_ipc_dict(self) -> Dict[str, Any]:
        """序列化为 IPC results[0]（无 null 字段）。"""
        out: Dict[str, Any] = {
            "status": self.status if self.status in (STATUS_SUCCESS, STATUS_FAILED) else STATUS_FAILED,
            "action": self.action or "",
            "data": self.data if isinstance(self.data, dict) else {},
        }
        if out["status"] == STATUS_FAILED:
            out["error"] = (self.error or "Unknown error").strip() or "Unknown error"
            out["error_category"] = (
                self.error_category or DEFAULT_PROVIDER_ERROR_CATEGORY
            ).strip() or DEFAULT_PROVIDER_ERROR_CATEGORY
        return out

    @classmethod
    def ok(cls, action: str, data: Optional[Any] = None) -> "ProviderActionResultModel":
        if isinstance(data, BaseModel):
            data_dict = data.model_dump()
        elif isinstance(data, dict):
            data_dict = data
        else:
            data_dict = {}
        return cls(status=STATUS_SUCCESS, action=action or "", data=data_dict)

    @classmethod
    def fail(
        cls,
        action: str,
        error: str,
        *,
        error_category: str = DEFAULT_PROVIDER_ERROR_CATEGORY,
        data: Optional[Any] = None,
    ) -> "ProviderActionResultModel":
        if isinstance(data, BaseModel):
            data_dict = data.model_dump()
        elif isinstance(data, dict):
            data_dict = data
        else:
            data_dict = {}
        return cls(
            status=STATUS_FAILED,
            action=action or "",
            data=data_dict,
            error=str(error or "Unknown error"),
            error_category=str(error_category or DEFAULT_PROVIDER_ERROR_CATEGORY),
        )


def create_provider_success(action: str, data: Optional[Any] = None) -> Dict[str, Any]:
    """创建 providers 成功 results[0] 字典。"""
    return ProviderActionResultModel.ok(action, data).to_ipc_dict()


def create_provider_failure(
    action: str,
    error: str,
    *,
    error_category: str = DEFAULT_PROVIDER_ERROR_CATEGORY,
    data: Optional[Any] = None,
) -> Dict[str, Any]:
    """创建 providers 失败 results[0] 字典。"""
    return ProviderActionResultModel.fail(
        action, error, error_category=error_category, data=data
    ).to_ipc_dict()
