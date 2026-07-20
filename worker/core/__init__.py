#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Core Module
Core processing pipeline for metadata analysis

三功能边界（V8.2）：
  Scrape    (ScrapeProcessor)  : 从外部数据源获取本地没有的数据（事实获取）。
                                 数据源：MusicBrainz / Discogs / AI 降级。
                                 产物：title / artist / album / year / genre / ...
                                 V8.2：genre 改由 Scrape 从 MusicBrainz recording 详情获取。
  Enhancer  (EnhanceProcessor) : 基于已有元数据生成新价值（不获取新事实）。
                                 当前能力：中文翻译（title_zh / album_zh / artist_zh）。
                                 V8.2：移除 edition 识别；genre 不再由本层产出。
  Normalize (NormalizeProcessor): 已有 Tag → 标准 Tag（一致性归一化）。
                                 当前能力：歌手名规范化（alias → canonical）。
                                 未来扩展：Genre 映射等。
                                 不写 SQLite、不修改 Tag；由调用方在用户确认后写入。

包含模块：
- ScrapeProcessor  : Scrape 层处理流程（含 Aggregator / Resolver）
- EnhanceProcessor : Enhancer 层处理流程（翻译）
- NormalizeProcessor: Normalize 层处理流程（实体归一化）
- Aggregator       : Scrape 候选聚合器
- Resolver         : Scrape AI 决策层
- types.TrackInput : Scrape/Enhance 共享的运行时音轨输入 dataclass（单一来源）
"""

from .scrape_processor import ScrapeProcessor
from .enhance_processor import EnhanceProcessor
from .aggregator import CandidateAggregator, AggregationResult
from .resolver import AIResolver, FinalResult, ResolveContext
from .normalize_processor import NormalizeProcessor
from .types import TrackInput

__all__ = [
    "ScrapeProcessor",
    "EnhanceProcessor",
    "TrackInput",
    "CandidateAggregator",
    "AggregationResult",
    "AIResolver",
    "FinalResult",
    "ResolveContext",
    "NormalizeProcessor",
]
