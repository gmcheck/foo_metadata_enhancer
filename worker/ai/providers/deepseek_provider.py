#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
DeepSeek AI Provider
Implements the DeepSeek API client.

DeepSeek API is fully OpenAI-compatible, so this provider inherits the generic
OpenAIProvider implementation and only overrides the default endpoint and the
supported model list.

API docs: https://api-docs.deepseek.com/
"""

from typing import Any, Dict

from .openai_provider import OpenAIProvider
from .base import ProviderConfig, ProviderType


class DeepSeekProvider(OpenAIProvider):
    """DeepSeek AI 提供商实现

    DeepSeek 提供 deepseek-chat (V3) 与 deepseek-reasoner (R1) 模型，
    API 与 OpenAI chat/completions 完全兼容。
    """

    DEFAULT_BASE_URL = "https://api.deepseek.com/v1/chat/completions"

    # DeepSeek 端点不兼容 OpenAI Responses API，禁用 web_search 路径
    supports_web_search = False

    SUPPORTED_MODELS = [
        "deepseek-chat",
        "deepseek-reasoner",
    ]

    @classmethod
    def from_config(cls, config_dict: Dict[str, Any]) -> "DeepSeekProvider":
        provider_config = ProviderConfig.from_dict(config_dict, ProviderType.DEEPSEEK)
        return cls(provider_config)
