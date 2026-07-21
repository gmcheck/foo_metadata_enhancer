#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Shared AI response types (V1)."""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any, Dict, Optional


@dataclass
class AIResponse:
    """AI 调用统一响应。"""

    success: bool
    content: Optional[str] = None
    error: Optional[str] = None
    error_category: str = ""
    model: str = ""
    provider: str = ""
    tokens_used: int = 0
    prompt_tokens: int = 0
    completion_tokens: int = 0
    # reasoning_tokens: 推理模型的思考 token（OpenAI: usage.completion_tokens_details.reasoning_tokens；
    # Anthropic: 不单独返回，包含在 output_tokens 中）。非推理模型为 0。
    reasoning_tokens: int = 0
    latency_ms: int = 0
    finish_reason: str = ""
    status_code: int = 0
    raw_response: Dict[str, Any] = field(default_factory=dict)

    def to_dict(self) -> Dict[str, Any]:
        return {
            "success": self.success,
            "content": self.content,
            "error": self.error,
            "error_category": self.error_category,
            "model": self.model,
            "provider": self.provider,
            "tokens_used": self.tokens_used,
            "prompt_tokens": self.prompt_tokens,
            "completion_tokens": self.completion_tokens,
            "reasoning_tokens": self.reasoning_tokens,
            "latency_ms": self.latency_ms,
            "finish_reason": self.finish_reason,
            "status_code": self.status_code,
        }
