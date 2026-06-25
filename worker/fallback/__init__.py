#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Fallback Module
Provides fallback mechanisms for metadata retrieval when primary sources fail
"""

from .controller import FallbackController
from .query_rewriter import QueryRewriter
from .ai_inferencer import AIInferencer, InferenceContext

__all__ = [
    "FallbackController",
    "QueryRewriter",
    "AIInferencer",
    "InferenceContext",
]
