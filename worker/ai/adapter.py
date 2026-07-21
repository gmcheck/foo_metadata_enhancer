#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Model Adapter
Provides unified interface for AI models via ProviderRuntime (V1 current provider).
"""

from __future__ import annotations

import json
import logging
from typing import Dict, List, Any, Optional, Union
from dataclasses import dataclass
from pathlib import Path

from ai.types import AIResponse
from common.text_utils import clean_dict_values

logger = logging.getLogger(__name__)

# ProtocolClient 与旧 BaseAIProvider 都具备 chat_completion / chat_completion_json
ProviderLike = Any


@dataclass
class AnalysisResult:
    """分析结果数据类"""

    success: bool
    result: Optional[Dict[str, Any]] = None
    error: Optional[str] = None
    model: str = ""
    tokens_used: int = 0
    prompt_tokens: int = 0
    completion_tokens: int = 0
    reasoning_tokens: int = 0
    model_type: str = "remote"
    provider: str = ""


class ModelAdapter:
    """模型适配器：业务只依赖当前 Provider（SQLite ProviderStore）。

    Attributes:
        config: 配置字典（timeout 等仍可读 worker 段）
        provider: 当前协议客户端 / 兼容旧 provider
    """

    def __init__(self, config: Dict[str, Any], config_path: Optional[Path] = None):
        self.config = config or {}
        self.provider: Optional[ProviderLike] = None
        self._init_provider()

    def _worker_timeouts(self) -> Dict[str, int]:
        worker = self.config.get("worker", {}) or {}
        return {
            "timeout_ms": int(worker.get("api_timeout_ms", 180000)),
            "max_retries": int(worker.get("max_retries", 3)),
            "retry_delay_ms": int(worker.get("retry_delay_ms", 1000)),
        }

    def _init_provider(self) -> None:
        """从 ProviderRuntime 加载当前 Provider；失败时可选回退旧 factory。"""
        try:
            client = self._load_runtime_client(force_reload=True)
            if client is not None:
                self.provider = client
                logger.info("Initialized AI provider from ProviderRuntime: %s", client)
                return

            # Runtime 可用但无 current：不静默回退到 yaml 槽位，明确失败
            from ai.provider_runtime import get_provider_runtime

            if get_provider_runtime() is not None:
                logger.error(
                    "ProviderRuntime ready but no current provider selected"
                )
                self.provider = None
                return

            # Runtime 尚未初始化：尝试 init 一次
            client = self._ensure_runtime_and_load()
            if client is not None:
                self.provider = client
                logger.info("Initialized AI provider after runtime bootstrap: %s", client)
                return

            logger.error("No current AI provider available from ProviderStore")
            self.provider = None
        except Exception as e:
            logger.error("Failed to initialize AI provider: %s", e, exc_info=True)
            self.provider = None

    def _ensure_runtime_and_load(self) -> Optional[ProviderLike]:
        from ai.provider_runtime import (
            get_provider_runtime,
            init_provider_runtime,
            get_active_provider_client,
        )

        rt = get_provider_runtime()
        if rt is None:
            timeouts = self._worker_timeouts()
            yaml_providers = self.config.get("providers")
            if not isinstance(yaml_providers, dict):
                yaml_providers = None
            init_provider_runtime(
                timeout_ms=timeouts["timeout_ms"],
                max_retries=timeouts["max_retries"],
                retry_delay_ms=timeouts["retry_delay_ms"],
                yaml_providers=yaml_providers,
                bootstrap=True,
            )
        return get_active_provider_client()

    def _load_runtime_client(self, force_reload: bool = False) -> Optional[ProviderLike]:
        from ai.provider_runtime import get_provider_runtime

        rt = get_provider_runtime()
        if rt is None:
            return None
        # 同步 timeout 配置（config 可能在 worker 启动后更新）
        timeouts = self._worker_timeouts()
        rt.timeout_ms = timeouts["timeout_ms"]
        rt.max_retries = timeouts["max_retries"]
        rt.retry_delay_ms = timeouts["retry_delay_ms"]
        return rt.get_active_client(force_reload=force_reload)

    def refresh_provider(self) -> bool:
        """强制从 Store 重载当前 provider（配置变更后调用）。"""
        try:
            client = self._load_runtime_client(force_reload=True)
            if client is None:
                client = self._ensure_runtime_and_load()
            self.provider = client
            return self.provider is not None
        except Exception as e:
            logger.error("refresh_provider failed: %s", e)
            return False

    def analyze(self, messages: List[Dict[str, str]]) -> AnalysisResult:
        logger.debug(
            "ModelAdapter::analyze: Starting analysis with %s messages", len(messages)
        )

        if not self.provider:
            # 再尝试一次热加载（可能刚 set_current）
            self.refresh_provider()

        if not self.provider:
            logger.error("ModelAdapter::analyze: No AI provider configured")
            return AnalysisResult(success=False, error="No AI provider configured")

        logger.debug("ModelAdapter::analyze: Calling provider.chat_completion_json")
        response = self.provider.chat_completion_json(messages)

        logger.info(
            "ModelAdapter::analyze: provider=%s model=%s success=%s "
            "tokens: total=%s prompt=%s completion=%s reasoning=%s latency_ms=%s",
            response.provider,
            response.model,
            response.success,
            response.tokens_used,
            response.prompt_tokens,
            response.completion_tokens,
            response.reasoning_tokens,
            response.latency_ms,
        )

        if not response.success:
            logger.error("ModelAdapter::analyze: Provider error = %s", response.error)
            return AnalysisResult(
                success=False,
                error=response.error,
                model=response.model,
                provider=response.provider,
            )

        try:
            result = self._parse_result(response.content)
            return AnalysisResult(
                success=True,
                result=result,
                model=response.model,
                tokens_used=response.tokens_used,
                prompt_tokens=response.prompt_tokens,
                completion_tokens=response.completion_tokens,
                reasoning_tokens=response.reasoning_tokens,
                model_type="remote",
                provider=response.provider,
            )
        except Exception as e:
            logger.error(
                "ModelAdapter::analyze: Failed to parse response: %s", e, exc_info=True
            )
            return AnalysisResult(
                success=False,
                error=f"Failed to parse response: {e}",
                model=response.model,
                provider=response.provider,
            )

    def chat_completion_json(
        self,
        messages: List[Dict[str, str]],
        temperature: float = 0.3,
        max_tokens: Optional[int] = None,
        **kwargs: Any,
    ) -> AIResponse:
        """直接透传 JSON chat（供 normalize 等使用）。"""
        if not self.provider:
            self.refresh_provider()
        if not self.provider:
            return AIResponse(success=False, error="No AI provider configured")
        return self.provider.chat_completion_json(
            messages, temperature=temperature, max_tokens=max_tokens, **kwargs
        )

    def chat_completion(
        self,
        messages: List[Dict[str, str]],
        temperature: float = 0.7,
        max_tokens: Optional[int] = None,
        **kwargs: Any,
    ) -> AIResponse:
        if not self.provider:
            self.refresh_provider()
        if not self.provider:
            return AIResponse(success=False, error="No AI provider configured")
        return self.provider.chat_completion(
            messages, temperature=temperature, max_tokens=max_tokens, **kwargs
        )

    def _parse_result(self, content: str) -> Dict[str, Any]:
        if not content:
            raise ValueError("Empty response content")

        try:
            result = json.loads(content)
            return self._clean_result(result)
        except json.JSONDecodeError:
            pass

        json_start = content.find("{")
        json_end = content.rfind("}") + 1
        if json_start != -1 and json_end > json_start:
            try:
                result = json.loads(content[json_start:json_end])
                return self._clean_result(result)
            except json.JSONDecodeError:
                pass

        raise ValueError(f"Failed to parse JSON from response: {content[:200]}...")

    def _clean_result(self, result: Dict[str, Any]) -> Dict[str, Any]:
        return clean_dict_values(result)

    def get_provider_info(self) -> Dict[str, Any]:
        if not self.provider:
            self.refresh_provider()
        if not self.provider:
            return {
                "provider": "none",
                "model": "none",
                "status": "not configured",
            }

        info: Dict[str, Any] = {
            "provider": getattr(self.provider, "provider_name", "unknown"),
            "model": self.provider.get_model()
            if hasattr(self.provider, "get_model")
            else "",
            "status": "ready",
        }
        # V1: 可选附带 protocol / provider_id
        cfg = getattr(self.provider, "config", None)
        if cfg is not None:
            protocol = getattr(cfg, "protocol", None)
            if protocol is not None:
                info["protocol"] = getattr(protocol, "value", str(protocol))
            info["provider_id"] = getattr(cfg, "provider_id", "") or ""
            info["base_url"] = getattr(cfg, "base_url", "") or ""
        return info

    def switch_provider(self, provider_type: str, config: Optional[Dict[str, Any]] = None) -> bool:
        """兼容旧接口：V1 忽略厂商名，改为 refresh 当前 Provider。

        若传入 provider_type 且像 uuid，则尝试 set_current 后刷新。
        """
        try:
            if provider_type and len(provider_type) >= 32 and "-" in provider_type:
                from ai.provider_runtime import get_provider_runtime

                rt = get_provider_runtime()
                if rt is not None:
                    rt.store.set_current_provider_id(provider_type)
                    rt.invalidate()
            return self.refresh_provider()
        except Exception as e:
            logger.error("Failed to switch provider: %s", e)
            return False

    def test_connection(self) -> Dict[str, Any]:
        if not self.provider:
            self.refresh_provider()
        if not self.provider:
            return {"success": False, "error": "No provider configured"}

        if hasattr(self.provider, "test_connection"):
            return self.provider.test_connection()

        test_messages = [{"role": "user", "content": "Say 'OK' if you can read this."}]
        response = self.provider.chat_completion(test_messages, temperature=0.1)
        return {
            "success": response.success,
            "provider": response.provider,
            "model": response.model,
            "latency_ms": response.latency_ms,
            "error": response.error,
        }
