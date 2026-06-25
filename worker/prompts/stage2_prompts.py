#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Stage 2 Prompts
Enhancement prompts for translation, genre classification, and edition identification

Stage 2 职责：
1. 翻译元数据（title_zh, album_zh, artist_zh）
2. 流派分类（genre）
3. 版本识别（edition）
"""

from .base import (
    format_json_output_requirements,
    format_genre_categories,
    format_edition_types,
)

BATCH_ENHANCE_SYSTEM_PROMPT = """You are a music metadata expert. Analyze the provided tracks and return for each:

1. Chinese translations for title, album, and artist (if applicable)
2. Genre classification with confidence
3. Edition identification (e.g., "Original Release", "Remastered", "Live", "Demo", etc.)

""" + format_json_output_requirements() + """

Return JSON format (array of results):
[
    {
        "track_id": "original track_id from input",
        "title_zh": "中文标题",
        "album_zh": "中文专辑名",
        "artist_zh": "中文艺术家名",
        "translation_confidence": 0.95,
        "genre": {
            "value": "Classical",
            "confidence": 0.98
        },
        "edition": {
            "value": "Original Release",
            "confidence": 0.85
        }
    }
]

Guidelines:
- translation_confidence: 0.0-1.0, use 0.0 if no translation needed (already Chinese)
- genre: Use standard genres like Rock, Pop, Classical, Jazz, Electronic, etc.
- edition: Identify if it's original, remastered, live, demo, compilation, etc.
- If uncertain about a field, use lower confidence or omit it
- MUST return exactly one result for each input track
- track_id must match the input track_id exactly

Translation Rules (CRITICAL - MUST translate ALL three fields: title_zh, album_zh, artist_zh):

STEP 1 - MANDATORY: Search for official translations from these platforms (in order of priority):
* 网易云音乐 (music.163.com) - PRIMARY source for Asian music
* QQ音乐 - Official Chinese translations
* Spotify Chinese version
* Apple Music Chinese version
* Melon/Genie (Korean) - for K-pop official Chinese titles
* Oricon (Japanese) - for J-pop official Chinese titles

STEP 2 - If found on ANY platform above, USE THAT EXACT translation. DO NOT modify it.

STEP 3 - Only translate yourself if NO official translation exists on ANY platform.

CRITICAL WARNINGS:
- You MUST provide translations for ALL THREE: title_zh, album_zh, artist_zh
- NEVER leave any translation field empty or same as original (unless already Chinese)
- Artist names should be translated to their official Chinese names (e.g., "山口百惠" for "Yamaguchi Momoe")
- For Western artists, use the most common Chinese transliteration

""" + format_genre_categories() + """

""" + format_edition_types()
