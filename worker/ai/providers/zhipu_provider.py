#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Zhipu AI Provider (智谱AI)
Implements the Zhipu AI API client (GLM models).

Zhipu AI's chat/completions endpoint is OpenAI-compatible, so this provider
inherits the generic OpenAIProvider implementation and only overrides the
default endpoint and the supported model list.
"""

from typing import Any, Dict

from .openai_provider import OpenAIProvider
from .base import ProviderConfig, ProviderType


class ZhipuProvider(OpenAIProvider):
    """智谱AI提供商实现

    API文档: https://open.bigmodel.cn/dev/api
    """

    DEFAULT_BASE_URL = "https://open.bigmodel.cn/api/paas/v4/chat/completions"

    SUPPORTED_MODELS = [
        "glm-4-plus",
        "glm-4-0520",
        "glm-4-air",
        "glm-4-airx",
        "glm-4-long",
        "glm-4-flash",
        "glm-4v-plus",
        "glm-4v-flash",
        "glm-z1-air",
        "glm-z1-airx",
        "glm-z1-flash",
        "chatglm-turbo",
        "chatglm_pro",
        "chatglm_std",
        "chatglm_lite"
    ]

    @classmethod
    def from_config(cls, config_dict: Dict[str, Any]) -> "ZhipuProvider":
        provider_config = ProviderConfig.from_dict(config_dict, ProviderType.ZHIPU)
        return cls(provider_config)
