#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Base Prompt Utilities（薄包装层）

原函数已迁移到分层模块：
- format_json_output_requirements  → system_core.SYSTEM_CORE_JSON_REQUIREMENTS
- format_track_id_requirements     → system_core.TRACK_ID_REQUIREMENTS
- build_batch_example              → system_core.build_batch_example()
- format_confidence_guidelines     → system_core.CONFIDENCE_GUIDELINES
- format_source_priority           → domain_defaults.DEFAULT_SOURCE_PRIORITY
- format_genre_categories          → domain_defaults.DEFAULT_GENRE_CATEGORIES
- format_edition_types             → domain_defaults.DEFAULT_EDITION_TYPES

本文件保留旧函数签名作为薄包装，保证调用方零改动迁移。
"""

from .system_core import (
    SYSTEM_CORE_JSON_REQUIREMENTS,
    TRACK_ID_REQUIREMENTS,
    CONFIDENCE_GUIDELINES,
    build_batch_example as _build_batch_example,
)
from .domain_defaults import (
    DEFAULT_SOURCE_PRIORITY,
    DEFAULT_GENRE_CATEGORIES,
    DEFAULT_EDITION_TYPES,
)


def format_json_output_requirements() -> str:
    """返回 JSON 输出格式要求的通用提示（薄包装，委托 system_core）"""
    return SYSTEM_CORE_JSON_REQUIREMENTS


def format_track_id_requirements() -> str:
    """返回 track_id 处理要求的提示（薄包装，委托 system_core）"""
    return TRACK_ID_REQUIREMENTS


def build_batch_example(num_tracks: int = 2) -> str:
    """构建批量处理示例（薄包装，委托 system_core）"""
    return _build_batch_example(num_tracks)


def format_confidence_guidelines() -> str:
    """返回置信度评估指南（薄包装，委托 system_core）"""
    return CONFIDENCE_GUIDELINES


def format_source_priority() -> str:
    """返回数据源优先级说明（薄包装，委托 domain_defaults）"""
    return DEFAULT_SOURCE_PRIORITY


def format_genre_categories() -> str:
    """返回标准流派分类列表（薄包装，委托 domain_defaults）"""
    return DEFAULT_GENRE_CATEGORIES


def format_edition_types() -> str:
    """返回版本类型列表（薄包装，委托 domain_defaults）"""
    return DEFAULT_EDITION_TYPES
