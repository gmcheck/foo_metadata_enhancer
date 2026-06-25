#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Core Module
Core processing pipeline for metadata analysis

包含：
- Stage1Processor: 阶段一处理流程
- Stage2Processor: 阶段二处理流程
- Aggregator: 候选聚合器
- Resolver: AI 决策层
"""

from .stage1_processor import Stage1Processor, TrackInput as Stage1TrackInput
from .stage2_processor import Stage2Processor, TrackInput as Stage2TrackInput
from .aggregator import CandidateAggregator, AggregationResult
from .resolver import AIResolver, FinalResult, ResolveContext

__all__ = [
    "Stage1Processor",
    "Stage2Processor",
    "Stage1TrackInput",
    "Stage2TrackInput",
    "CandidateAggregator",
    "AggregationResult",
    "AIResolver",
    "FinalResult",
    "ResolveContext",
]
