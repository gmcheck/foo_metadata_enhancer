#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Provider Profiles

V1：不再按厂商名分支。保留统一默认 profile，以及按 protocol 的轻量差异（可选）。
旧厂商 key 映射仅用于兼容历史调用，语义收敛到 default。
"""

from __future__ import annotations

from typing import Any, Dict

# 统一默认（所有 Provider 实例共用）
DEFAULT_PROVIDER_PROFILE: Dict[str, Any] = {
    "extra_instructions": "Ensure JSON is valid. Avoid markdown wrapping.",
    "default_temperature": 0.3,
}

# 兼容旧 import：PROVIDER_PROFILES（历史厂商表）。V1 不再维护差异，统一指向默认。
PROVIDER_PROFILES: Dict[str, Dict[str, Any]] = {
    "zhipu": dict(DEFAULT_PROVIDER_PROFILE),
    "deepseek": dict(DEFAULT_PROVIDER_PROFILE),
    "openrouter": dict(DEFAULT_PROVIDER_PROFILE),
    "openai": dict(DEFAULT_PROVIDER_PROFILE),
    "custom": dict(DEFAULT_PROVIDER_PROFILE),
    "gemini": dict(DEFAULT_PROVIDER_PROFILE),
    "ollama": dict(DEFAULT_PROVIDER_PROFILE),
}

# 按协议的可选覆盖（V1 很轻）
PROTOCOL_PROFILES: Dict[str, Dict[str, Any]] = {
    "openai_chat": {
        "extra_instructions": "Ensure JSON is valid. Avoid markdown wrapping.",
        "default_temperature": 0.3,
    },
    "anthropic_messages": {
        "extra_instructions": "Return raw JSON only, no markdown code fences.",
        "default_temperature": 0.3,
    },
}

# 历史厂商名 → protocol profile
_LEGACY_ALIASES = {
    "zhipu": "openai_chat",
    "deepseek": "openai_chat",
    "openrouter": "openai_chat",
    "openai": "openai_chat",
    "custom": "openai_chat",
    "gemini": "default",
    "ollama": "default",
}


def get_provider_profile(provider: str) -> dict:
    """获取 Profile。

    Args:
        provider: 可为 protocol 名、legacy 厂商名、或 Provider 显示名。
                  未知时返回 DEFAULT_PROVIDER_PROFILE。
    """
    key = (provider or "").strip().lower()
    if not key:
        return dict(DEFAULT_PROVIDER_PROFILE)

    if key in PROTOCOL_PROFILES:
        return dict(PROTOCOL_PROFILES[key])

    mapped = _LEGACY_ALIASES.get(key)
    if mapped and mapped in PROTOCOL_PROFILES:
        return dict(PROTOCOL_PROFILES[mapped])

    return dict(DEFAULT_PROVIDER_PROFILE)
