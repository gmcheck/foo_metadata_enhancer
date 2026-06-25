#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
AI Module
Unified AI provider and model management

包含：
- ModelAdapter: 统一模型接口
- Providers: 各 AI 提供商实现
- AIDataSource: AI 作为数据源的适配器
"""

from .adapter import ModelAdapter, AnalysisResult
from .ai_data_source import AIAdapter

__all__ = [
    "ModelAdapter",
    "AnalysisResult",
    "AIAdapter",
]
