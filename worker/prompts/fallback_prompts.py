#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Fallback Prompts
AI inference prompts for last-resort metadata inference

Fallback 职责：
1. 当所有数据源查询失败时，使用 AI 推断元数据
2. 作为最后的降级手段
"""

from .base import format_json_output_requirements


def build_inference_prompt(
    title: str = "",
    artist: str = "",
    album: str = "",
    duration: int = None,
    previous_errors: list = None
) -> str:
    """构建 AI 推断提示词
    
    Args:
        title: 歌曲标题
        artist: 艺术家
        album: 专辑
        duration: 时长（秒）
        previous_errors: 之前的错误列表
    
    Returns:
        str: 完整的提示词
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


INFERENCE_SYSTEM_PROMPT = """You are a music metadata expert. Based on the limited information provided, infer the most likely metadata for this track.

""" + format_json_output_requirements() + """

Return JSON format:
{
    "title": "inferred title",
    "artist": "inferred artist",
    "album": "inferred album",
    "year": "inferred year",
    "genre": "inferred genre",
    "composer": "inferred composer",
    "label": "inferred record label",
    "country": "inferred country of origin",
    "confidence": 0.0-1.0,
    "reasoning": "brief explanation of your inference"
}

Guidelines:
- Only provide information you can reasonably infer
- Set confidence to 0.0-0.5 for uncertain inferences
- Leave fields empty if you cannot infer them
- This is a fallback when all data sources failed"""
