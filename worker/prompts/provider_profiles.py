#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Provider Profiles

不同 AI Provider 的 Prompt 层差异。
与 retry / JSON 修复 / 温度参数等代码逻辑紧耦合，因此放在代码内而非外部文件。

每个 Profile 包含：
- extra_instructions: 注入到 system prompt 的 Provider 特定提示
- default_temperature: 默认温度（可被 config 中的 extra_params.temperature 覆盖）
"""

PROVIDER_PROFILES = {
    "zhipu": {
        "extra_instructions": "Ensure JSON is valid. Avoid markdown wrapping.",
        "default_temperature": 0.3,
    },
    "gemini": {
        "extra_instructions": "Return raw JSON without code fences.",
        "default_temperature": 0.2,
    },
    "openrouter": {
        "extra_instructions": "",
        "default_temperature": 0.3,
    },
    "ollama": {
        "extra_instructions": "Be concise. Local model token budget is limited.",
        "default_temperature": 0.1,
    },
    "deepseek": {
        "extra_instructions": "Ensure JSON is valid. Avoid markdown wrapping.",
        "default_temperature": 0.3,
    },
}

# 默认 Profile（未知 Provider 时使用）
DEFAULT_PROVIDER_PROFILE = {
    "extra_instructions": "",
    "default_temperature": 0.3,
}


def get_provider_profile(provider: str) -> dict:
    """获取 Provider Profile

    Args:
        provider: Provider 名称（zhipu/gemini/openrouter/ollama/deepseek）

    Returns:
        dict: 包含 extra_instructions 和 default_temperature 的字典
    """
    return PROVIDER_PROFILES.get(provider, DEFAULT_PROVIDER_PROFILE)
