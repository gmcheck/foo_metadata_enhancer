#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Create protocol clients from config / provider rows."""

from __future__ import annotations

from typing import Any, Dict, Optional, Union

from .base import ProtocolClient, ProtocolConfig, ProtocolType
from .openai_chat import OpenAIChatClient
from .anthropic_messages import AnthropicMessagesClient


def create_protocol_client(
    config_or_row: Union[ProtocolConfig, Dict[str, Any]],
    *,
    timeout_ms: int = 180000,
    max_retries: int = 3,
    retry_delay_ms: int = 1000,
    extra_params: Optional[Dict[str, Any]] = None,
) -> ProtocolClient:
    if isinstance(config_or_row, ProtocolConfig):
        config = config_or_row
    else:
        config = ProtocolConfig.from_provider_row(
            config_or_row,
            timeout_ms=timeout_ms,
            max_retries=max_retries,
            retry_delay_ms=retry_delay_ms,
            extra_params=extra_params,
        )

    if config.protocol == ProtocolType.OPENAI_CHAT:
        return OpenAIChatClient(config)
    if config.protocol == ProtocolType.ANTHROPIC_MESSAGES:
        return AnthropicMessagesClient(config)
    raise ValueError(f"Unsupported protocol: {config.protocol}")
