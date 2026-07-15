#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
OpenAI-Compatible AI Provider (base)

Implements a generic OpenAI chat/completions client. Providers whose APIs are
OpenAI-compatible (Zhipu GLM, DeepSeek, Moonshot, etc.) can subclass this and
override only DEFAULT_BASE_URL and the model list.
"""

import json
import time
import logging
import socket
import urllib.request
import urllib.error
from typing import Dict, List, Any, Optional

from .base import BaseAIProvider, AIResponse, ProviderConfig, ProviderType

logger = logging.getLogger(__name__)


class OpenAIProvider(BaseAIProvider):
    """OpenAI-compatible API provider base class.

    Subclasses only need to override:
        DEFAULT_BASE_URL: str
        SUPPORTED_MODELS: List[str]
    """

    DEFAULT_BASE_URL = "https://api.openai.com/v1/chat/completions"
    SUPPORTED_MODELS: List[str] = ["gpt-4o", "gpt-4o-mini", "gpt-4-turbo", "gpt-3.5-turbo"]

    def __init__(self, config: ProviderConfig):
        super().__init__(config)
        self.base_url = config.base_url or self.DEFAULT_BASE_URL

    def validate_config(self) -> bool:
        if not self.config.api_key:
            logger.error(f"{self.provider_name} API key is required")
            return False
        if not self.get_model():
            logger.error(f"{self.provider_name} model is required")
            return False
        return True

    def chat_completion(self, messages: List[Dict[str, str]],
                        temperature: float = 0.7,
                        max_tokens: Optional[int] = None,
                        **kwargs) -> AIResponse:
        return self._send_request(messages, temperature, max_tokens, json_mode=False, **kwargs)

    def chat_completion_json(self, messages: List[Dict[str, str]],
                             temperature: float = 0.3,
                             max_tokens: Optional[int] = None,
                             **kwargs) -> AIResponse:
        return self._send_request(messages, temperature, max_tokens, json_mode=True, **kwargs)

    def _send_request(self, messages: List[Dict[str, str]],
                      temperature: float,
                      max_tokens: Optional[int],
                      json_mode: bool = False,
                      **kwargs) -> AIResponse:
        if not self.validate_config():
            return AIResponse(
                success=False,
                error="Invalid configuration",
                provider=self.provider_name
            )

        models_to_try = [self.get_model()] + self.get_fallback_models()
        last_error = None

        for model in models_to_try:
            if not model:
                continue

            response = self._try_model(model, messages, temperature, max_tokens, json_mode, **kwargs)

            if response.success:
                return response

            last_error = response.error

            if self._should_stop_fallback(str(last_error)):
                break

        return AIResponse(
            success=False,
            error=last_error or "All models failed",
            provider=self.provider_name
        )

    def _try_model(self, model: str, messages: List[Dict[str, str]],
                   temperature: float, max_tokens: Optional[int],
                   json_mode: bool, **kwargs) -> AIResponse:
        start_time = time.time()

        payload: Dict[str, Any] = {
            "model": model,
            "messages": messages,
            "temperature": temperature
        }

        if max_tokens:
            payload["max_tokens"] = max_tokens

        if json_mode:
            payload["response_format"] = {"type": "json_object"}

        extra_params = kwargs.get("extra_params", self.config.extra_params)
        for key in ["top_p", "top_k", "presence_penalty", "frequency_penalty", "stop", "tools", "tool_choice"]:
            if key in extra_params:
                payload[key] = extra_params[key]

        if "stream" in extra_params:
            payload["stream"] = extra_params["stream"]

        headers = {
            "Content-Type": "application/json",
            "Authorization": f"Bearer {self.config.api_key}"
        }

        for attempt in range(self.config.max_retries):
            try:
                data = json.dumps(payload).encode('utf-8')
                req = urllib.request.Request(
                    self.base_url,
                    data=data,
                    headers=headers,
                    method='POST'
                )

                timeout_sec = self.config.timeout_ms / 1000
                with urllib.request.urlopen(req, timeout=timeout_sec) as response:
                    result = json.loads(response.read().decode('utf-8'))

                    latency_ms = int((time.time() - start_time) * 1000)

                    return self._parse_response(result, model, latency_ms)

            except urllib.error.HTTPError as e:
                error_body = ""
                try:
                    error_body = e.read().decode('utf-8')
                except:
                    pass

                error_msg = f"HTTP error: {e.code}"
                if error_body:
                    try:
                        error_data = json.loads(error_body)
                        if "error" in error_data:
                            err_info = error_data["error"]
                            error_msg = err_info.get("message", error_msg)
                            if "code" in err_info:
                                error_msg = f"{err_info['code']}: {error_msg}"
                        elif "message" in error_data:
                            error_msg = error_data["message"]
                    except:
                        error_msg = f"{error_msg} - {error_body[:200]}"

                if e.code in [401, 403]:
                    return AIResponse(
                        success=False,
                        error=error_msg,
                        model=model,
                        provider=self.provider_name
                    )

                if attempt < self.config.max_retries - 1:
                    delay = self.config.retry_delay_ms * (2 ** attempt) / 1000
                    time.sleep(delay)
                else:
                    return AIResponse(
                        success=False,
                        error=error_msg,
                        model=model,
                        provider=self.provider_name
                    )

            except urllib.error.URLError as e:
                error_msg = f"URL error: {e.reason}"
                if attempt < self.config.max_retries - 1:
                    delay = self.config.retry_delay_ms * (2 ** attempt) / 1000
                    time.sleep(delay)
                else:
                    return AIResponse(
                        success=False,
                        error=error_msg,
                        model=model,
                        provider=self.provider_name
                    )

            except socket.timeout:
                error_msg = f"Request timeout after {self.config.timeout_ms}ms"
                logger.warning(f"Timeout on attempt {attempt + 1}/{self.config.max_retries}: {error_msg}")
                if attempt < self.config.max_retries - 1:
                    delay = self.config.retry_delay_ms * (2 ** attempt) / 1000
                    time.sleep(delay)
                else:
                    return AIResponse(
                        success=False,
                        error=error_msg,
                        model=model,
                        provider=self.provider_name
                    )

            except Exception as e:
                return self._handle_error(e, "Request failed")

        return AIResponse(
            success=False,
            error="Max retries exceeded",
            model=model,
            provider=self.provider_name
        )

    def _parse_response(self, result: Dict[str, Any], model: str,
                        latency_ms: int) -> AIResponse:
        try:
            choices = result.get("choices", [])
            if not choices:
                return AIResponse(
                    success=False,
                    error="No choices in response",
                    model=model,
                    provider=self.provider_name,
                    latency_ms=latency_ms
                )

            message = choices[0].get("message", {})
            content = message.get("content", "")
            finish_reason = choices[0].get("finish_reason", "")

            usage = result.get("usage", {})
            prompt_tokens = usage.get("prompt_tokens", 0)
            completion_tokens = usage.get("completion_tokens", 0)
            total_tokens = usage.get("total_tokens", prompt_tokens + completion_tokens)

            return AIResponse(
                success=True,
                content=content,
                model=result.get("model", model),
                provider=self.provider_name,
                tokens_used=total_tokens,
                prompt_tokens=prompt_tokens,
                completion_tokens=completion_tokens,
                latency_ms=latency_ms,
                finish_reason=finish_reason,
                raw_response=result
            )

        except Exception as e:
            return self._handle_error(e, "Failed to parse response")

    def get_supported_models(self) -> List[str]:
        return list(self.SUPPORTED_MODELS)

    @classmethod
    def from_config(cls, config_dict: Dict[str, Any]) -> "OpenAIProvider":
        provider_config = ProviderConfig.from_dict(config_dict, ProviderType.OPENAI)
        return cls(provider_config)
