#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
System Core Prompts (Layer 1)

不可修改的 Prompt 核心规则，与代码版本绑定。
包含：AI 角色、JSON 输出契约、track_id 规则、防幻觉约束、批量对齐规则、输出 schema。

设计原则：
- 这些规则一旦被破坏会导致 JSON 解析失败、track_id 错乱、批量结果错位等不可恢复故障
- 必须留在代码内，不外置、不开放用户修改
- 与代码版本绑定，随版本发布更新
"""

# =============================================================================
# AI 角色定义（所有 Stage 共享）
# =============================================================================

SYSTEM_CORE_ROLE = """You are a music metadata expert. Your task is to process music tracks and provide accurate metadata."""

SYSTEM_CORE_ROLE_RESOLVE = """You are a music metadata expert. Your task is to process multiple tracks and select the best metadata for each."""

SYSTEM_CORE_ROLE_ENHANCED = """You are a music metadata expert working in enhanced mode."""

SYSTEM_CORE_ROLE_ENHANCE = """You are a music metadata expert. Analyze the provided tracks and return enhanced metadata for each."""

SYSTEM_CORE_ROLE_INFERENCE = """You are a music metadata expert. Based on the limited information provided, infer the most likely metadata for this track."""

# =============================================================================
# JSON 输出强约束（CRITICAL REQUIREMENTS）
# 一旦破坏会导致解析失败，不可由用户修改
# =============================================================================

SYSTEM_CORE_JSON_REQUIREMENTS = """CRITICAL REQUIREMENTS:
1. You MUST return EXACTLY the same number of results as input tracks
2. You MUST copy track_id EXACTLY as provided - do NOT modify, truncate, or generate new ones
3. Return ONLY a valid JSON array, no markdown, no code blocks, no additional text
4. Each result MUST have a track_id field"""

# =============================================================================
# track_id 处理要求
# =============================================================================

TRACK_ID_REQUIREMENTS = """IMPORTANT: track_id must match the input track_id exactly"""

# =============================================================================
# 防幻觉原则
# =============================================================================

ANTI_HALLUCINATION_RULES = """Anti-Hallucination Rules:
- Do NOT fabricate information you cannot confidently determine
- If uncertain about a field, leave it empty or use lower confidence
- Prefer retaining original information over guessing
- Never invent MusicBrainz IDs, catalog numbers, or other identifiers"""

# =============================================================================
# 置信度评估指南（行为规则，非领域知识）
# =============================================================================

CONFIDENCE_GUIDELINES = """Confidence Guidelines:
- 0.9-1.0: Very confident, multiple authoritative sources agree
- 0.7-0.9: Confident, at least one authoritative source
- 0.5-0.7: Moderate confidence, sources partially agree
- 0.3-0.5: Low confidence, significant uncertainty
- 0.0-0.3: Very low confidence, mostly inferred or guessed"""

# =============================================================================
# Stage 1 输出 Schema（决策结果）
# =============================================================================

SCRAPE_OUTPUT_SCHEMA = """Return JSON format (array of results):
[
    {
        "track_id": "EXACT_COPY_OF_INPUT_TRACK_ID",
        "title": "...",
        "artist": "...",
        "album": "...",
        "year": "...",
        "track_number": "...",
        "disc_number": "...",
        "genre": "...",
        "composer": "...",
        "lyricist": "...",
        "label": "...",
        "country": "...",
        "catalog_number": "...",
        "musicbrainz_id": "...",
        "confidence": 0.0-1.0,
        "reasoning": "Brief explanation"
    }
]"""

SCRAPE_ENHANCED_OUTPUT_SCHEMA = """Return JSON format (array of results):
[
    {
        "track_id": "EXACT_COPY_OF_INPUT_TRACK_ID",
        "title": "...",
        "artist": "...",
        "album": "...",
        "year": "...",
        "track_number": "...",
        "disc_number": "...",
        "genre": "...",
        "composer": "...",
        "lyricist": "...",
        "label": "...",
        "country": "...",
        "catalog_number": "...",
        "musicbrainz_id": "...",
        "confidence": 0.0-1.0,
        "reasoning": "Brief explanation",
        "inferred_fields": ["list of fields you inferred"]
    }
]"""

# =============================================================================
# Stage 2 输出 Schema（仅翻译）
# =============================================================================

ENHANCE_OUTPUT_SCHEMA = """Return JSON format (array of results):
[
    {
        "track_id": "original track_id from input",
        "title_zh": "中文标题",
        "album_zh": "中文专辑名",
        "artist_zh": "中文艺术家名",
        "translation_confidence": 0.95
    }
]

Note: Enhance ONLY performs metadata translation (deriving new value from existing data).
Genre (a factual attribute) is now sourced from MusicBrainz in Scrape.
Edition identification has been removed (was unreliable from AI inference).

TRANSLATION IS MANDATORY for non-Chinese content:
- English/Japanese/Korean/other non-Chinese titles MUST be translated to Chinese
- Always fill title_zh/album_zh/artist_zh with the best Chinese translation
- Set translation_confidence to 0.5-1.0 based on your confidence in the translation

No-translation case (RARE - only for already-Chinese content):
- ONLY when the original title/album/artist is ALREADY in Chinese characters
- In that case, leave the corresponding *_zh field EMPTY and set confidence to 0.0
- Do NOT skip translation for English or other non-Chinese content

CRITICAL: Empty *_zh fields with confidence=0.0 means "already Chinese, no translation needed".
For any non-Chinese content, you MUST provide a translation."""

# =============================================================================
# Fallback 输出 Schema（单首推断）
# =============================================================================

FALLBACK_OUTPUT_SCHEMA = """Return JSON format:
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
}"""

# =============================================================================
# AIAdapter（作为数据源）输出 Schema
# =============================================================================

AI_SCRAPE_OUTPUT_SCHEMA = """Return JSON format:
{
    "title": {"value": "...", "confidence": 0.95},
    "artist": {"value": "...", "confidence": 0.98},
    "album": {"value": "...", "confidence": 0.90},
    "year": {"value": "...", "confidence": 0.85},
    "composer": {"value": "...", "confidence": 0.80},
    "lyricist": {"value": "...", "confidence": 0.75},
    "label": {"value": "...", "confidence": 0.70}
}"""

# =============================================================================
# 批量示例（track_id 对齐示范）
# =============================================================================

def build_batch_example(num_tracks: int = 2) -> str:
    """构建批量处理示例（强化 track_id 对齐规则）

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
