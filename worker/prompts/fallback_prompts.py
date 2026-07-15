#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Fallback Prompts（薄包装层）

原硬编码 Prompt 已迁移到 PromptComposer 分层组装。
本文件保留旧常量名与函数作为向后兼容别名。

build_inference_prompt() 构建 user message 内容（含曲目信息），保持原实现不变。
INFERENCE_SYSTEM_PROMPT 走 Composer（默认 config）。
"""

from .composer import _build_default_fallback_prompt


def build_inference_prompt(
    title: str = "",
    artist: str = "",
    album: str = "",
    duration: int = None,
    previous_errors: list = None
) -> str:
    """构建 AI 推断 user message（曲目信息部分）

    此函数构建的是 user message 内容（非 system prompt），保持原实现不变。

    Args:
        title: 歌曲标题
        artist: 艺术家
        album: 专辑
        duration: 时长（秒）
        previous_errors: 之前的错误列表

    Returns:
        str: 完整的 user message 提示词
    """
    prompt_parts = [
        "You are a music metadata expert. Based on the limited information provided,",
        "infer the most likely metadata for this track.",
        "",
        "Available information:",
        f"- Title: {title or 'Unknown'}",
        f"- Artist: {artist or 'Unknown'}",
        f"- Album: {album or 'Unknown'}",
        f"- Duration: {duration} seconds" if duration else "",
    ]

    if previous_errors:
        prompt_parts.append("")
        prompt_parts.append("Previous search attempts failed:")
        for error in previous_errors[:3]:
            prompt_parts.append(f"- {error}")

    prompt_parts.extend([
        "",
        "Please provide your best inference in the following JSON format:",
        "{",
        '  "title": "inferred title",',
        '  "artist": "inferred artist",',
        '  "album": "inferred album",',
        '  "year": "inferred year",',
        '  "genre": "inferred genre",',
        '  "composer": "inferred composer",',
        '  "label": "inferred record label",',
        '  "country": "inferred country of origin",',
        '  "confidence": 0.0-1.0,',
        '  "reasoning": "brief explanation of your inference"',
        "}",
        "",
        "Important:",
        "- Only provide information you can reasonably infer",
        "- Set confidence to 0.0-0.5 for uncertain inferences",
        "- Leave fields empty if you cannot infer them",
        "- Respond ONLY with the JSON object, no additional text"
    ])

    return "\n".join(prompt_parts)


# 向后兼容别名：模块加载时用默认 config 构建一次，内容固定
# 如需动态 Prompt，请使用 get_composer(config).build_fallback_system_prompt()
INFERENCE_SYSTEM_PROMPT = _build_default_fallback_prompt()
