#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""OpenAI Chat Completions protocol client."""

from __future__ import annotations

import json
import logging
import socket
import time
import urllib.error
import urllib.request
from typing import Any, Dict, List, Optional

from ai.types import AIResponse
from .base import ProtocolClient, ProtocolConfig, ProtocolType, build_endpoint_url, classify_error

logger = logging.getLogger(__name__)


class OpenAIChatClient(ProtocolClient):
    def __init__(self, config: ProtocolConfig):
        super().__init__(config)
        self.endpoint = build_endpoint_url(config.base_url, ProtocolType.OPENAI_CHAT)

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
        payload: Dict[str, Any] = {
            "model": model,
            "messages": messages,
            "temperature": temperature,
        }
        if max_tokens:
            payload["max_tokens"] = max_tokens
        if json_mode:
            payload["response_format"] = {"type": "json_object"}

        extra_params = kwargs.get("extra_params", self.config.extra_params) or {}
        for key in (
            "top_p",
            "top_k",
            "presence_penalty",
            "frequency_penalty",
            "stop",
            "tools",
            "tool_choice",
            "stream",
        ):
            if key in extra_params:
                payload[key] = extra_params[key]

        headers = {
            "Content-Type": "application/json",
            "Authorization": f"Bearer {self.config.api_key}",
        }

        last_error = "Max retries exceeded"
        for attempt in range(self.config.max_retries):
            try:
                if attempt > 0:
                    logger.info(
                        "openai_chat retry %s/%s model=%s",
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
                    return self._parse(result, model, latency_ms)

            except urllib.error.HTTPError as e:
                error_body = ""
                try:
                    error_body = e.read().decode("utf-8")
                except Exception:
                    pass
                error_msg = self._format_http_error(e.code, error_body)
                last_error = error_msg
                if e.code in (401, 403):
                    return AIResponse(
                        success=False,
                        error=error_msg,
                        model=model,
                        provider=self.provider_name,
                    )
                if attempt < self.config.max_retries - 1:
                    time.sleep(self.config.retry_delay_ms * (2 ** attempt) / 1000)
                else:
                    return AIResponse(
                        success=False,
                        error=error_msg,
                        model=model,
                        provider=self.provider_name,
                    )

            except urllib.error.URLError as e:
                last_error = f"URL error: {e.reason}"
                if attempt < self.config.max_retries - 1:
                    time.sleep(self.config.retry_delay_ms * (2 ** attempt) / 1000)
                else:
                    return AIResponse(
                        success=False,
                        error=last_error,
                        model=model,
                        provider=self.provider_name,
                    )

            except socket.timeout:
                last_error = f"Request timeout after {self.config.timeout_ms}ms"
                if attempt < self.config.max_retries - 1:
                    time.sleep(self.config.retry_delay_ms * (2 ** attempt) / 1000)
                else:
                    return AIResponse(
                        success=False,
                        error=last_error,
                        model=model,
                        provider=self.provider_name,
                    )

            except Exception as e:
                logger.exception("openai_chat request failed")
                return AIResponse(
                    success=False,
                    error=str(e),
                    model=model,
                    provider=self.provider_name,
                )

        return AIResponse(
            success=False,
            error=last_error,
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
                    if "code" in err_info:
                        error_msg = f"{err_info['code']}: {error_msg}"
                else:
                    error_msg = str(err_info)
            elif "message" in error_data:
                error_msg = str(error_data["message"])
        except Exception:
            error_msg = f"{error_msg} - {body[:200]}"
        return error_msg

    def _parse(
        self,
        result: Dict[str, Any],
        model: str,
        latency_ms: int,
        status_code: int = 200,
    ) -> AIResponse:
        try:
            choices = result.get("choices") or []
            if not choices:
                return AIResponse(
                    success=False,
                    error="No choices in response",
                    error_category=classify_error("No choices in response").value,
                    model=model,
                    provider=self.provider_name,
                    latency_ms=latency_ms,
                    status_code=status_code,
                )
            message = choices[0].get("message") or {}
            content = message.get("content", "")
            finish_reason = choices[0].get("finish_reason", "")
            usage = result.get("usage") or {}
            prompt_tokens = int(usage.get("prompt_tokens") or 0)
            completion_tokens = int(usage.get("completion_tokens") or 0)
            total_tokens = int(
                usage.get("total_tokens") or (prompt_tokens + completion_tokens)
            )
            # OpenAI/DeepSeek 推理模型把 reasoning_tokens 放在
            # usage.completion_tokens_details.reasoning_tokens
            reasoning_tokens = 0
            details = usage.get("completion_tokens_details") or {}
            if isinstance(details, dict):
                reasoning_tokens = int(details.get("reasoning_tokens") or 0)
            return AIResponse(
                success=True,
                content=content,
                model=result.get("model", model),
                provider=self.provider_name,
                tokens_used=total_tokens,
                prompt_tokens=prompt_tokens,
                completion_tokens=completion_tokens,
                reasoning_tokens=reasoning_tokens,
                latency_ms=latency_ms,
                finish_reason=finish_reason,
                status_code=status_code,
                raw_response=result,
            )
        except Exception as e:
            return AIResponse(
                success=False,
                error=f"Failed to parse OpenAI response: {e}",
                error_category=classify_error(str(e)).value,
                model=model,
                provider=self.provider_name,
                latency_ms=latency_ms,
                status_code=status_code,
            )
