#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
User Preferences Schema (Layer 3)

用户偏好层的 schema 定义与默认值。
通过 Preferences UI 编辑，持久化到 settings.json 的 prompts.user_prefs 段。

阶段一仅提供 schema 与默认值；阶段三接入 C++ UI 与 settings.json 合并逻辑。
"""

from dataclasses import dataclass, field
from typing import List


@dataclass
class PromptPrefs:
    """用户 Prompt 偏好（Layer 3）

    所有字段都有默认值，缺失时使用默认值，保证向后兼容。
    """
    # 翻译风格：official（官方译名优先）/ literal（字面直译）/ semantic（意译）
    translation_style: str = "official"

    # 流派返回语言：english / chinese / bilingual
    genre_language: str = "english"

    # 不确定时是否保留原文（而非猜测）
    keep_original_when_uncertain: bool = True

    # 最低翻译置信度阈值
    min_translation_confidence: float = 0.5

    # 翻译平台优先级（拖拽排序）
    translation_platform_priority: List[str] = field(
        default_factory=lambda: ["netease", "qq", "spotify", "applemusic"]
    )

    # 自定义翻译 hints，每行 "原文名 = 译名"
    custom_translation_hints: str = ""

    # 自定义附加指令（高级用户自由文本）
    custom_instructions: str = ""


# 默认偏好实例（供 Composer 在 config 缺失时使用）
DEFAULT_USER_PREFS = PromptPrefs()

# 默认偏好字典（供 ConfigManager 合并时使用）
DEFAULT_USER_PREFS_DICT = {
    "translation_style": "official",
    "genre_language": "english",
    "keep_original_when_uncertain": True,
    "min_translation_confidence": 0.5,
    "translation_platform_priority": ["netease", "qq", "spotify", "applemusic"],
    "custom_translation_hints": "",
    "custom_instructions": "",
}


def parse_user_prefs(prefs_dict: dict) -> PromptPrefs:
    """从字典解析用户偏好，缺失字段使用默认值

    Args:
        prefs_dict: 来自 config["prompts"]["user_prefs"] 的字典

    Returns:
        PromptPrefs: 用户偏好对象
    """
    if not prefs_dict:
        return PromptPrefs()

    return PromptPrefs(
        translation_style=prefs_dict.get("translation_style", "official"),
        genre_language=prefs_dict.get("genre_language", "english"),
        keep_original_when_uncertain=prefs_dict.get("keep_original_when_uncertain", True),
        min_translation_confidence=prefs_dict.get("min_translation_confidence", 0.5),
        translation_platform_priority=prefs_dict.get(
            "translation_platform_priority",
            ["netease", "qq", "spotify", "applemusic"]
        ),
        custom_translation_hints=prefs_dict.get("custom_translation_hints", ""),
        custom_instructions=prefs_dict.get("custom_instructions", ""),
    )
