#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
AI Module
Unified AI provider and model management

包含：
- ModelAdapter: 统一模型接口
- Providers: 各 AI 提供商实现（旧路径，逐步废弃）
- ProviderRuntime / protocols: V1 Provider 实例 + 协议客户端
- AIDataSource: AI 作为数据源的适配器
"""

# 注意：避免在包导入时强依赖 ai_data_source（其依赖 prompts 全量图）。
# 需要 AIAdapter 时再 from ai.ai_data_source import AIAdapter

from .adapter import ModelAdapter, AnalysisResult

__all__ = [
    "ModelAdapter",
    "AnalysisResult",
]


def __getattr__(name: str):
    if name == "AIAdapter":
        from .ai_data_source import AIAdapter
        return AIAdapter
    raise AttributeError(f"module {__name__!r} has no attribute {name!r}")
