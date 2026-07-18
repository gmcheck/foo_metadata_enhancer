#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Data Sources Module
Provides unified interface for multiple metadata sources

职责边界（V8.2 三功能分界）：
  本模块属于 Scrape 层 —— 从外部数据源获取本地没有的数据（事实获取）。
  不做：翻译（归 Enhancer）、归一化（归 Normalize）。

包含：
- DataSourceManager : 统一调度多个数据源（含降级策略）
- MusicBrainzAdapter: MusicBrainz 适配器（V8.2 起负责 genre 抓取）
- DiscogsAdapter    : Discogs 适配器
- ScrapingOptions   : Stage1 刮削选项
- EnhancementOptions: Stage2 增强选项（翻译开关；V8.2 已移除 classify_genre/identify_edition）
"""

from .base import (
    DataSourceType,
    FallbackLevel,
    QueryInput,
    Candidate,
    FinalResult,
    FallbackContext,
    ReleaseInfo,
    DataSourceAdapter,
    ScrapingOptions,
    EnhancementOptions,
)
from .manager import DataSourceManager
from .musicbrainz_adapter import MusicBrainzAdapter
from .discogs_adapter import DiscogsAdapter

__all__ = [
    "DataSourceType",
    "FallbackLevel",
    "QueryInput",
    "Candidate",
    "FinalResult",
    "FallbackContext",
    "ReleaseInfo",
    "DataSourceAdapter",
    "DataSourceManager",
    "MusicBrainzAdapter",
    "DiscogsAdapter",
    "ScrapingOptions",
    "EnhancementOptions",
]
