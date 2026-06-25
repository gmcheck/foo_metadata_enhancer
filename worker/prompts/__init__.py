#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Prompts Package
Unified prompt management for AI metadata analysis
"""

from .stage1_prompts import (
    BATCH_RESOLVE_SYSTEM_PROMPT,
    BATCH_ENHANCED_SYSTEM_PROMPT,
)

from .stage2_prompts import (
    BATCH_ENHANCE_SYSTEM_PROMPT,
)

from .fallback_prompts import (
    build_inference_prompt,
    INFERENCE_SYSTEM_PROMPT,
)

from .base import (
    format_json_output_requirements,
    format_track_id_requirements,
)

__all__ = [
    "BATCH_RESOLVE_SYSTEM_PROMPT",
    "BATCH_ENHANCED_SYSTEM_PROMPT",
    "BATCH_ENHANCE_SYSTEM_PROMPT",
    "build_inference_prompt",
    "INFERENCE_SYSTEM_PROMPT",
    "format_json_output_requirements",
    "format_track_id_requirements",
]
