#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Protocol Client base types and helpers (V1).
"""

from __future__ import annotations

import logging
import re
from abc import ABC, abstractmethod
from dataclasses import dataclass, field
from enum import Enum
from typing import Any, Dict, List, Optional

from ai.types import AIResponse

logger = logging.getLogger(__name__)


class ProtocolType(str, Enum):
    OPENAI_CHAT = "openai_chat"
    ANTHROPIC_MESSAGES = "anthropic_messages"


class ErrorCategory(str, Enum):
    API_KEY_INVALID = "API Key Invalid"
    UNAUTHORIZED = "Unauthorized"
    MODEL_NOT_FOUND = "Model Not Found"
    TIMEOUT = "Timeout"
    NETWORK_ERROR = "Network Error"
    RATE_LIMIT = "Rate Limit"
    INVALID_CONFIG = "Invalid Configuration"
    REQUEST_FAILED = "Request Failed"


@dataclass
class ProtocolConfig:
    """运行时协议配置（来自 providers 表一行 + 全局 timeout/retry）。"""

    protocol: ProtocolType
    api_key: str = ""
    base_url: str = ""
    model: str = ""
    name: str = ""
    provider_id: str = ""
    timeout_ms: int = 180000
    max_retries: int = 3
    retry_delay_ms: int = 1000
    extra_params: Dict[str, Any] = field(default_factory=dict)

    @classmethod
    def from_provider_row(
        cls,
        row: Dict[str, Any],
        *,
        timeout_ms: int = 180000,
        max_retries: int = 3,
        retry_delay_ms: int = 1000,
        extra_params: Optional[Dict[str, Any]] = None,
    ) -> "ProtocolConfig":
        protocol = ProtocolType(str(row.get("protocol", "")).strip().lower())
        return cls(
            protocol=protocol,
            api_key=str(row.get("api_key") or ""),
            base_url=str(row.get("base_url") or ""),
            model=str(row.get("model") or ""),
            name=str(row.get("name") or ""),
            provider_id=str(row.get("id") or ""),
            timeout_ms=timeout_ms,
            max_retries=max_retries,
            retry_delay_ms=retry_delay_ms,
            extra_params=dict(extra_params or {}),
        )


def build_endpoint_url(base_url: str, protocol: ProtocolType) -> str:
    """将 API root 转为完整 endpoint；若已含 path 则不重复追加。"""
    url = (base_url or "").strip().strip(" `'\"").rstrip("/")
    if not url:
        return ""

    if protocol == ProtocolType.OPENAI_CHAT:
        if url.endswith("/chat/completions"):
            return url
        if url.endswith("/completions") and not url.endswith("/chat/completions"):
            # 少数代理可能只暴露 /completions
            return url
        return f"{url}/chat/completions"

    if protocol == ProtocolType.ANTHROPIC_MESSAGES:
        if url.endswith("/v1/messages") or url.endswith("/messages"):
            return url
        return f"{url}/v1/messages"

    return url


def classify_error(error: str, http_code: Optional[int] = None) -> ErrorCategory:
    """将错误信息映射为分类（用于 Test Connection / UI）。"""
    text = (error or "").lower()

    if http_code == 401 or http_code == 403:
        if "api key" in text or "invalid" in text or "key" in text:
            return ErrorCategory.API_KEY_INVALID
        return ErrorCategory.UNAUTHORIZED
    if http_code == 404 or "model" in text and (
        "not found" in text or "does not exist" in text or "unknown" in text
    ):
        if "model" in text:
            return ErrorCategory.MODEL_NOT_FOUND
    if http_code == 429 or "rate limit" in text or "too many requests" in text:
        return ErrorCategory.RATE_LIMIT

    if "timeout" in text or "timed out" in text:
        return ErrorCategory.TIMEOUT
    if (
        "url error" in text
        or "name or service not known" in text
        or "connection" in text
        or "network" in text
        or "dns" in text
        or "errno" in text
    ):
        return ErrorCategory.NETWORK_ERROR
    if "invalid configuration" in text or "required" in text:
        return ErrorCategory.INVALID_CONFIG
    if "api key" in text or "invalid_api_key" in text or "invalid key" in text:
        return ErrorCategory.API_KEY_INVALID
    if "unauthorized" in text or "forbidden" in text or "authentication" in text:
        return ErrorCategory.UNAUTHORIZED
    if "model" in text and ("not found" in text or "does not exist" in text):
        return ErrorCategory.MODEL_NOT_FOUND
    if re.search(r"\b429\b", text):
        return ErrorCategory.RATE_LIMIT

    return ErrorCategory.REQUEST_FAILED


class ProtocolClient(ABC):
    """协议客户端抽象：业务只依赖 chat 接口。"""

    def __init__(self, config: ProtocolConfig):
        self.config = config
        self.provider_name = config.name or config.protocol.value

    def get_model(self) -> str:
        return self.config.model or ""

    def validate_config(self) -> Optional[str]:
        """返回错误信息；None 表示通过。"""
        if not self.config.api_key:
            return "API key is required"
        if not self.get_model():
            return "Model is required"
        if not (self.config.base_url or "").strip():
            return "Base URL is required"
        return None

    @abstractmethod
    def chat_completion(
        self,
        messages: List[Dict[str, str]],
        temperature: float = 0.7,
        max_tokens: Optional[int] = None,
        **kwargs: Any,
    ) -> AIResponse:
        pass

    @abstractmethod
    def chat_completion_json(
        self,
        messages: List[Dict[str, str]],
        temperature: float = 0.3,
        max_tokens: Optional[int] = None,
        **kwargs: Any,
    ) -> AIResponse:
        pass

    def test_connection(self) -> Dict[str, Any]:
        """轻量连接测试：发一条最小 chat 请求。"""
        cfg_err = self.validate_config()
        if cfg_err:
            return {
                "success": False,
                "error": cfg_err or "Invalid configuration",
                "error_category": ErrorCategory.INVALID_CONFIG.value,
                "model": self.get_model() or "",
                "protocol": self.config.protocol.value,
                "provider": self.provider_name or "",
                "latency_ms": 0,
                "content_preview": "",
                "http_status": 0,
            }

        messages = [
            {"role": "user", "content": "Reply with exactly: ok"},
        ]
        response = self.chat_completion(
            messages, temperature=0.0, max_tokens=16
        )
        # AIResponse 字段：success/content/error/error_category/status_code/latency_ms
        # 返回字段一律用可序列化非 null 值；成功/失败由 success 布尔表达
        status_code = int(getattr(response, "status_code", 0) or 0)
        latency_ms = int(getattr(response, "latency_ms", 0) or 0)
        if response.success:
            return {
                "success": True,
                "error": "",
                "error_category": "",
                "model": response.model or self.get_model() or "",
                "protocol": self.config.protocol.value,
                "provider": self.provider_name or "",
                "latency_ms": latency_ms,
                "content_preview": (response.content or "")[:200],
                "http_status": status_code,
            }

        err_msg = response.error or "Connection test failed"
        err_cat = getattr(response, "error_category", "") or ""
        if not err_cat:
            try:
                err_cat = classify_error(err_msg).value
            except Exception:
                err_cat = ErrorCategory.REQUEST_FAILED.value
        return {
            "success": False,
            "error": err_msg,
            "error_category": err_cat,
            "model": self.get_model() or "",
            "protocol": self.config.protocol.value,
            "provider": self.provider_name or "",
            "latency_ms": latency_ms,
            "content_preview": "",
            "http_status": status_code,
        }

    def __repr__(self) -> str:
        return (
            f"{self.__class__.__name__}(name={self.provider_name!r}, "
            f"model={self.get_model()!r})"
        )
