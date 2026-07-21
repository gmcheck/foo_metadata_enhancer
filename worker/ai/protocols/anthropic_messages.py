#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Anthropic Messages protocol client."""

from __future__ import annotations

import json
import logging
import socket
import time
import urllib.error
import urllib.request
from typing import Any, Dict, List, Optional

from ai.types import AIResponse
from .base import (
    ProtocolClient,
    ProtocolConfig,
    ProtocolType,
    build_endpoint_url,
    classify_error,
)

logger = logging.getLogger(__name__)


class AnthropicMessagesClient(ProtocolClient):
    def __init__(self, config: ProtocolConfig):
        super().__init__(config)
        self.endpoint = build_endpoint_url(
            config.base_url, ProtocolType.ANTHROPIC_MESSAGES
        )

    def chat_completion(
        self,
        messages: List[Dict[str, str]],
        temperature: float = 0.7,
        max_tokens: Optional[int] = None,
        **kwargs: Any,
    ) -> AIResponse:
        return self._send(messages, temperature, max_tokens, json_mode=False, **kwargs)

    def chat_completion_json(
        self,
        messages: List[Dict[str, str]],
        temperature: float = 0.3,
        max_tokens: Optional[int] = None,
        **kwargs: Any,
    ) -> AIResponse:
        return self._send(messages, temperature, max_tokens, json_mode=True, **kwargs)

    def _send(
        self,
        messages: List[Dict[str, str]],
        temperature: float,
        max_tokens: Optional[int],
        json_mode: bool,
        **kwargs: Any,
    ) -> AIResponse:
        cfg_err = self.validate_config()
        if cfg_err:
            return AIResponse(
                success=False,
                error=cfg_err,
                model=self.get_model(),
                provider=self.provider_name,
            )
        if not self.endpoint:
            return AIResponse(
                success=False,
                error="Base URL is required",
                model=self.get_model(),
                provider=self.provider_name,
            )

        model = self.get_model()
        start_time = time.time()

        anthropic_messages: List[Dict[str, str]] = []
        system_prompt: Optional[str] = None
        for msg in messages:
            role = msg.get("role", "")
            content = msg.get("content", "")
            if role == "system":
                system_prompt = content if system_prompt is None else f"{system_prompt}\n{content}"
            elif role in ("user", "assistant"):
                anthropic_messages.append({"role": role, "content": content})

        if not anthropic_messages:
            anthropic_messages.append({"role": "user", "content": "(empty request)"})
        if anthropic_messages[-1]["role"] != "user":
            anthropic_messages.append({"role": "user", "content": "Please continue."})

        payload: Dict[str, Any] = {
            "model": model,
            "messages": anthropic_messages,
            "temperature": temperature,
            "max_tokens": max_tokens or 4096,
        }
        if system_prompt:
            payload["system"] = system_prompt
        if json_mode:
            payload["system"] = (
                (payload.get("system") or "")
                + "\n\nYou must respond with valid JSON only, no markdown formatting."
            ).strip()

        headers = {
            "Content-Type": "application/json",
            "x-api-key": self.config.api_key,
            "anthropic-version": "2023-06-01",
        }

        last_error = "Max retries exceeded"
        for attempt in range(self.config.max_retries):
            try:
                if attempt > 0:
                    logger.info(
                        "anthropic_messages retry %s/%s model=%s",
                        attempt + 1,
                        self.config.max_retries,
                        model,
                    )
                data = json.dumps(payload).encode("utf-8")
                req = urllib.request.Request(
                    self.endpoint, data=data, headers=headers, method="POST"
                )
                timeout_sec = self.config.timeout_ms / 1000
                with urllib.request.urlopen(req, timeout=timeout_sec) as response:
                    result = json.loads(response.read().decode("utf-8"))
                    latency_ms = int((time.time() - start_time) * 1000)
                    return self._parse(
                        result,
                        model,
                        latency_ms,
                        status_code=int(getattr(response, "status", 200) or 200),
                    )

            except urllib.error.HTTPError as e:
                error_body = ""
                try:
                    error_body = e.read().decode("utf-8")
                except Exception:
                    pass
                error_msg = self._format_http_error(e.code, error_body)
                last_error = error_msg
                err_cat = classify_error(error_msg, e.code).value
                if e.code in (401, 403):
                    return AIResponse(
                        success=False,
                        error=error_msg,
                        error_category=err_cat,
                        model=model,
                        provider=self.provider_name,
                        status_code=int(e.code or 0),
                    )
                if attempt < self.config.max_retries - 1:
                    time.sleep(self.config.retry_delay_ms * (2 ** attempt) / 1000)
                else:
                    return AIResponse(
                        success=False,
                        error=error_msg,
                        error_category=err_cat,
                        model=model,
                        provider=self.provider_name,
                        status_code=int(e.code or 0),
                    )

            except urllib.error.URLError as e:
                last_error = f"URL error: {e.reason}"
                err_cat = classify_error(last_error).value
                if attempt < self.config.max_retries - 1:
                    time.sleep(self.config.retry_delay_ms * (2 ** attempt) / 1000)
                else:
                    return AIResponse(
                        success=False,
                        error=last_error,
                        error_category=err_cat,
                        model=model,
                        provider=self.provider_name,
                    )

            except socket.timeout:
                last_error = f"Request timeout after {self.config.timeout_ms}ms"
                err_cat = classify_error(last_error).value
                if attempt < self.config.max_retries - 1:
                    time.sleep(self.config.retry_delay_ms * (2 ** attempt) / 1000)
                else:
                    return AIResponse(
                        success=False,
                        error=last_error,
                        error_category=err_cat,
                        model=model,
                        provider=self.provider_name,
                    )

            except Exception as e:
                logger.exception("anthropic_messages request failed")
                return AIResponse(
                    success=False,
                    error=str(e),
                    error_category=classify_error(str(e)).value,
                    model=model,
                    provider=self.provider_name,
                )

        return AIResponse(
            success=False,
            error=last_error,
            error_category=classify_error(last_error).value,
            model=model,
            provider=self.provider_name,
        )

    @staticmethod
    def _format_http_error(code: int, body: str) -> str:
        error_msg = f"HTTP error: {code}"
        if not body:
            return error_msg
        try:
            error_data = json.loads(body)
            if "error" in error_data:
                err_info = error_data["error"]
                if isinstance(err_info, dict):
                    error_msg = err_info.get("message", error_msg)
                    err_type = err_info.get("type")
                    if err_type:
                        error_msg = f"{err_type}: {error_msg}"
                else:
                    error_msg = str(err_info)
            elif "message" in error_data:
                error_msg = str(error_data["message"])
        except Exception:
            error_msg = f"{error_msg} - {body[:200]}"
        return error_msg

    def _parse(self, result: Dict[str, Any], model: str, latency_ms: int) -> AIResponse:
        try:
            content_blocks = result.get("content") or []
            content_text = ""
            for block in content_blocks:
                if block.get("type") == "text":
                    content_text += block.get("text", "")

            if not content_text:
                return AIResponse(
                    success=False,
                    error="No text content in Anthropic response",
                    model=model,
                    provider=self.provider_name,
                    latency_ms=latency_ms,
                )

            usage = result.get("usage") or {}
            prompt_tokens = int(usage.get("input_tokens") or 0)
            completion_tokens = int(usage.get("output_tokens") or 0)
            total_tokens = prompt_tokens + completion_tokens
            finish_reason = result.get("stop_reason", "")
            if finish_reason == "end_turn":
                finish_reason = "stop"

            return AIResponse(
                success=True,
                content=content_text,
                model=result.get("model", model),
                provider=self.provider_name,
                tokens_used=total_tokens,
                prompt_tokens=prompt_tokens,
                completion_tokens=completion_tokens,
                latency_ms=latency_ms,
                finish_reason=finish_reason,
                raw_response=result,
            )
        except Exception as e:
            return AIResponse(
                success=False,
                error=f"Failed to parse Anthropic response: {e}",
                model=model,
                provider=self.provider_name,
                latency_ms=latency_ms,
            )
