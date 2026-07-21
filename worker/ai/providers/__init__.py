#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Legacy package shim.

厂商 Provider 类已删除。业务请使用:
  - ai.provider_runtime / ai.protocols
  - ai.types.AIResponse
"""

from ai.types import AIResponse

__all__ = ["AIResponse"]
