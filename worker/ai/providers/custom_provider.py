#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Custom AI Provider
Implements a user-configurable provider that supports both OpenAI Chat Completions
format and Anthropic Messages format.

Users can configure:
- API format (OpenAI Chat Completions or Anthropic Messages)
- Custom request URL
- Model ID
- API key

For OpenAI format, /chat/completions is appended to the custom URL.
For Anthropic format, /v1/messages is appended to the custom URL.
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


class CustomProvider(BaseAIProvider):
    """自定义模型提供商

    支持 OpenAI Chat Completions 格式和 Anthropic Messages 格式。
    用户可自由配置端点地址、模型 ID 和 API 密钥。

    API format 通过 extra_params 中的 "api_format" 字段指定：
    - "openai" (默认): OpenAI Chat Completions 格式
    - "anthropic": Anthropic Messages 格式
    """

    supports_web_search = False

    def __init__(self, config: ProviderConfig):
        super().__init__(config)
        self.api_format = str(
            config.extra_params.get("api_format")
            or getattr(config, "api_format", "")
            or "openai"
        ).strip().lower()
        if self.api_format not in ("openai", "anthropic"):
            logger.warning(f"[CustomProvider] unknown api_format='{self.api_format}', fallback to openai")
            self.api_format = "openai"
        self.base_url = (config.base_url or "").strip(" `'\"")
        logger.info(
            f"[CustomProvider] __init__: base_url='{self.base_url}' "
            f"api_format='{self.api_format}' model='{self.get_model()}' "
            f"extra_params={config.extra_params}"
        )

    def validate_config(self) -> bool:
        if not self.config.api_key:
            logger.error(f"{self.provider_name} API key is required")
            return False
        if not self.get_model():
            logger.error(f"{self.provider_name} model is required")
            return False
        if not self.base_url:
            logger.error(f"{self.provider_name} base URL is required")
            return False
        return True

    def chat_completion(self, messages: List[Dict[str, str]],
                        temperature: float = 0.7,
                        max_tokens: Optional[int] = None,
                        **kwargs) -> AIResponse:
        if self.api_format == "anthropic":
            return self._send_anthropic_request(messages, temperature, max_tokens, json_mode=False, **kwargs)
        return self._send_openai_request(messages, temperature, max_tokens, json_mode=False, **kwargs)

    def chat_completion_json(self, messages: List[Dict[str, str]],
                             temperature: float = 0.3,
                             max_tokens: Optional[int] = None,
                             **kwargs) -> AIResponse:
        if self.api_format == "anthropic":
            return self._send_anthropic_request(messages, temperature, max_tokens, json_mode=True, **kwargs)
        return self._send_openai_request(messages, temperature, max_tokens, json_mode=True, **kwargs)

    def _get_openai_url(self) -> str:
        """获取 OpenAI Chat Completions 格式的完整 URL

        设计约定：
        - 用户填写服务端点地址，不要以斜杠结尾
        - 若尚未包含 /chat/completions，则自动追加
        例：
        - https://cli.example.com/v1 -> https://cli.example.com/v1/chat/completions
        - https://cli.example.com/v1/chat/completions -> 原样使用
        """
        url = self.base_url.rstrip('/')
        if not url.endswith("/chat/completions"):
            url = f"{url}/chat/completions"
        logger.info(
            f"[CustomProvider] _get_openai_url: base_url='{self.base_url}' "
            f"api_format='{self.api_format}' -> url='{url}'"
        )
        return url

    def _get_anthropic_url(self) -> str:
        """获取 Anthropic Messages 格式的完整 URL

        设计约定：
        - 用户填写服务端点地址，不要以斜杠结尾
        - 若尚未包含 /v1/messages，则自动追加
        例：
        - https://cli.example.com -> https://cli.example.com/v1/messages
        - https://cli.example.com/v1/messages -> 原样使用
        """
        url = self.base_url.rstrip('/')
        if not url.endswith("/v1/messages"):
            url = f"{url}/v1/messages"
        logger.info(
            f"[CustomProvider] _get_anthropic_url: base_url='{self.base_url}' "
            f"api_format='{self.api_format}' -> url='{url}'"
        )
        return url

    def _send_openai_request(self, messages: List[Dict[str, str]],
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

            response = self._try_openai_model(model, messages, temperature, max_tokens, json_mode, **kwargs)

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

    def _try_openai_model(self, model: str, messages: List[Dict[str, str]],
                          temperature: float, max_tokens: Optional[int],
                          json_mode: bool, **kwargs) -> AIResponse:
        start_time = time.time()

        url = self._get_openai_url()

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

        headers = {
            "Content-Type": "application/json",
            "Authorization": f"Bearer {self.config.api_key}"
        }

        for attempt in range(self.config.max_retries):
            try:
                if attempt > 0:
                    logger.info(f"Custom provider (OpenAI) retry attempt {attempt + 1}/{self.config.max_retries} for model={model}")
                data = json.dumps(payload).encode('utf-8')
                req = urllib.request.Request(
                    url, data=data, headers=headers, method='POST'
                )

                timeout_sec = self.config.timeout_ms / 1000
                with urllib.request.urlopen(req, timeout=timeout_sec) as response:
                    result = json.loads(response.read().decode('utf-8'))
                    latency_ms = int((time.time() - start_time) * 1000)
                    return self._parse_openai_response(result, model, latency_ms)

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
                        success=False, error=error_msg,
                        model=model, provider=self.provider_name
                    )

                if attempt < self.config.max_retries - 1:
                    delay = self.config.retry_delay_ms * (2 ** attempt) / 1000
                    time.sleep(delay)
                else:
                    return AIResponse(
                        success=False, error=error_msg,
                        model=model, provider=self.provider_name
                    )

            except urllib.error.URLError as e:
                error_msg = f"URL error: {e.reason}"
                if attempt < self.config.max_retries - 1:
                    delay = self.config.retry_delay_ms * (2 ** attempt) / 1000
                    time.sleep(delay)
                else:
                    return AIResponse(
                        success=False, error=error_msg,
                        model=model, provider=self.provider_name
                    )

            except socket.timeout:
                error_msg = f"Request timeout after {self.config.timeout_ms}ms"
                logger.warning(f"Timeout on attempt {attempt + 1}/{self.config.max_retries}: {error_msg}")
                if attempt < self.config.max_retries - 1:
                    delay = self.config.retry_delay_ms * (2 ** attempt) / 1000
                    time.sleep(delay)
                else:
                    return AIResponse(
                        success=False, error=error_msg,
                        model=model, provider=self.provider_name
                    )

            except Exception as e:
                return self._handle_error(e, "Custom provider (OpenAI) request failed")

        return AIResponse(
            success=False, error="Max retries exceeded",
            model=model, provider=self.provider_name
        )

    def _parse_openai_response(self, result: Dict[str, Any], model: str,
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
            return self._handle_error(e, "Failed to parse OpenAI response")

    def _send_anthropic_request(self, messages: List[Dict[str, str]],
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

            response = self._try_anthropic_model(model, messages, temperature, max_tokens, json_mode, **kwargs)

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

    def _try_anthropic_model(self, model: str, messages: List[Dict[str, str]],
                             temperature: float, max_tokens: Optional[int],
                             json_mode: bool, **kwargs) -> AIResponse:
        start_time = time.time()

        url = self._get_anthropic_url()

        # Convert OpenAI-style messages to Anthropic format
        anthropic_messages = []
        system_prompt = None
        for msg in messages:
            role = msg.get("role", "")
            content = msg.get("content", "")
            if role == "system":
                system_prompt = content
            elif role in ("user", "assistant"):
                anthropic_messages.append({
                    "role": role,
                    "content": content
                })

        # Anthropic requires at least one message
        if not anthropic_messages:
            anthropic_messages.append({"role": "user", "content": "(empty request)"})

        # Ensure the last message is from user (Anthropic requirement)
        if anthropic_messages[-1]["role"] != "user":
            anthropic_messages.append({"role": "user", "content": "Please continue."})

        payload: Dict[str, Any] = {
            "model": model,
            "messages": anthropic_messages,
            "temperature": temperature,
            "max_tokens": max_tokens or 4096
        }

        if system_prompt:
            payload["system"] = system_prompt

        if json_mode:
            # Anthropic doesn't have native JSON mode, so we add it to the system prompt
            payload["system"] = (payload.get("system", "") +
                                 "\n\nYou must respond with valid JSON only, no markdown formatting.")

        headers = {
            "Content-Type": "application/json",
            "x-api-key": self.config.api_key,
            "anthropic-version": "2023-06-01"
        }

        for attempt in range(self.config.max_retries):
            try:
                if attempt > 0:
                    logger.info(f"Custom provider (Anthropic) retry attempt {attempt + 1}/{self.config.max_retries} for model={model}")
                data = json.dumps(payload).encode('utf-8')
                req = urllib.request.Request(
                    url, data=data, headers=headers, method='POST'
                )

                timeout_sec = self.config.timeout_ms / 1000
                with urllib.request.urlopen(req, timeout=timeout_sec) as response:
                    result = json.loads(response.read().decode('utf-8'))
                    latency_ms = int((time.time() - start_time) * 1000)
                    return self._parse_anthropic_response(result, model, latency_ms)

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
                            if "code" in err_info.get("type", ""):
                                error_msg = f"{err_info['type']}: {error_msg}"
                        elif "message" in error_data:
                            error_msg = error_data["message"]
                    except:
                        error_msg = f"{error_msg} - {error_body[:200]}"

                if e.code in [401, 403]:
                    return AIResponse(
                        success=False, error=error_msg,
                        model=model, provider=self.provider_name
                    )

                if attempt < self.config.max_retries - 1:
                    delay = self.config.retry_delay_ms * (2 ** attempt) / 1000
                    time.sleep(delay)
                else:
                    return AIResponse(
                        success=False, error=error_msg,
                        model=model, provider=self.provider_name
                    )

            except urllib.error.URLError as e:
                error_msg = f"URL error: {e.reason}"
                if attempt < self.config.max_retries - 1:
                    delay = self.config.retry_delay_ms * (2 ** attempt) / 1000
                    time.sleep(delay)
                else:
                    return AIResponse(
                        success=False, error=error_msg,
                        model=model, provider=self.provider_name
                    )

            except socket.timeout:
                error_msg = f"Request timeout after {self.config.timeout_ms}ms"
                logger.warning(f"Timeout on attempt {attempt + 1}/{self.config.max_retries}: {error_msg}")
                if attempt < self.config.max_retries - 1:
                    delay = self.config.retry_delay_ms * (2 ** attempt) / 1000
                    time.sleep(delay)
                else:
                    return AIResponse(
                        success=False, error=error_msg,
                        model=model, provider=self.provider_name
                    )

            except Exception as e:
                return self._handle_error(e, "Custom provider (Anthropic) request failed")

        return AIResponse(
            success=False, error="Max retries exceeded",
            model=model, provider=self.provider_name
        )

    def _parse_anthropic_response(self, result: Dict[str, Any], model: str,
                                  latency_ms: int) -> AIResponse:
        try:
            content_blocks = result.get("content", [])
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
                    latency_ms=latency_ms
                )

            usage = result.get("usage", {})
            prompt_tokens = usage.get("input_tokens", 0)
            completion_tokens = usage.get("output_tokens", 0)
            total_tokens = usage.get("input_tokens", 0) + usage.get("output_tokens", 0)

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
                raw_response=result
            )

        except Exception as e:
            return self._handle_error(e, "Failed to parse Anthropic response")