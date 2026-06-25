#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Base Prompt Utilities
Common prompt building blocks and utilities
"""

from typing import List, Optional


def format_json_output_requirements() -> str:
    """返回 JSON 输出格式要求的通用提示
    
    Returns:
        str: JSON 格式要求提示文本
    """
    return """CRITICAL REQUIREMENTS:
1. You MUST return EXACTLY the same number of results as input tracks
2. You MUST copy track_id EXACTLY as provided - do NOT modify, truncate, or generate new ones
3. Return ONLY a valid JSON array, no markdown, no code blocks, no additional text
4. Each result MUST have a track_id field"""


def format_track_id_requirements() -> str:
    """返回 track_id 处理要求的提示
    
    Returns:
        str: track_id 要求提示文本
    """
    return """IMPORTANT: track_id must match the input track_id exactly"""


def build_batch_example(num_tracks: int = 2) -> str:
    """构建批量处理示例
    
    Args:
        num_tracks: 示例中的曲目数量
    
    Returns:
        str: 示例文本
    """
    examples = []
    for i in range(num_tracks):
        examples.append(f'{{"track_id": "track{i+1}", "title": "...", "artist": "..."}}')
    
    return f"""Example input with {num_tracks} tracks:
Input: [{{"track_id": "track1", "query": {{...}}}}, {{"track_id": "track2", "query": {{...}}}}]

Your response MUST be:
[
    {', '.join(examples)}
]"""


def format_confidence_guidelines() -> str:
    """返回置信度评估指南
    
    Returns:
        str: 置信度指南文本
    """
    return """Confidence Guidelines:
- 0.9-1.0: Very confident, multiple authoritative sources agree
- 0.7-0.9: Confident, at least one authoritative source
- 0.5-0.7: Moderate confidence, sources partially agree
- 0.3-0.5: Low confidence, significant uncertainty
- 0.0-0.3: Very low confidence, mostly inferred or guessed"""


def format_source_priority() -> str:
    """返回数据源优先级说明
    
    Returns:
        str: 数据源优先级文本
    """
    return """Source Priority (highest to lowest):
1. MusicBrainz - authoritative music database
2. Discogs - comprehensive release database
3. AI inference - when other sources unavailable"""


def format_genre_categories() -> str:
    """返回标准流派分类列表
    
    Returns:
        str: 流派分类文本
    """
    return """Standard Genre Categories:
Rock, Pop, Classical, Jazz, Electronic, Hip-Hop, R&B, Country, Folk, Blues,
Metal, Punk, Reggae, Latin, World, Soul, Funk, Disco, Techno, House,
Ambient, Experimental, Indie, Alternative, Soundtrack, Musical, Opera,
Gospel, Christian, New Age, Comedy, Spoken Word, Podcast"""


def format_edition_types() -> str:
    """返回版本类型列表
    
    Returns:
        str: 版本类型文本
    """
    return """Common Edition Types:
- Original Release
- Remastered
- Deluxe Edition
- Anniversary Edition
- Live
- Demo
- Acoustic
- Instrumental
- Remix
- Compilation
- Soundtrack
- Single
- EP
- Album"""
