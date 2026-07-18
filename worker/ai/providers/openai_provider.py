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

    # OpenAI 官方端点支持 Responses API + web_search_preview 工具
    # 子类（Zhipu/DeepSeek 等兼容端点）应 override 为 False
    supports_web_search = True

    def _get_responses_url(self) -> str:
        """从 chat completions base_url 推导 Responses API 端点

        - https://api.openai.com/v1/chat/completions → https://api.openai.com/v1/responses
        - 自定义 base_url 同样替换最后一段为 responses
        """
        url = self.base_url.rstrip('/')
        # 去掉末尾的 /chat/completions
        if url.endswith("/chat/completions"):
            url = url[:-len("/chat/completions")]
        elif url.endswith("/completions"):
            url = url[:-len("/completions")]
        return url + "/responses"

    def chat_completion_json_with_web_search(self, messages: List[Dict[str, str]],
                                             temperature: float = 0.0,
                                             max_tokens: Optional[int] = None,
                                             **kwargs) -> AIResponse:
        """通过 OpenAI Responses API + web_search_preview 实现联网 JSON 推理

        Responses API 与 Chat Completions API 的差异：
        - 端点：/v1/responses（非 /v1/chat/completions）
        - 入参：input 接收 messages 数组；text.format 控制输出格式（替代 response_format）
        - tools: [{"type": "web_search_preview"}] 启用联网搜索
        - 响应：output 数组，含 web_search_call 项（搜索动作）和 message 项（最终文本）

        本实现仅对 OpenAI 官方端点启用；若 base_url 被改为第三方兼容端点，
        应将 supports_web_search override 为 False。
        """
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

            response = self._try_model_responses_web_search(
                model, messages, temperature, max_tokens, **kwargs
            )

            if response.success:
                return response

            last_error = response.error

            if self._should_stop_fallback(str(last_error)):
                break

        return AIResponse(
            success=False,
            error=last_error or "All models failed (web search)",
            provider=self.provider_name
        )

    def _try_model_responses_web_search(self, model: str,
                                        messages: List[Dict[str, str]],
                                        temperature: float,
                                        max_tokens: Optional[int],
                                        **kwargs) -> AIResponse:
        """单次 Responses API + web_search 请求"""
        start_time = time.time()

        payload: Dict[str, Any] = {
            "model": model,
            "input": messages,
            "temperature": temperature,
            # 启用 web_search_preview 工具，模型自主决定何时联网
            "tools": [{"type": "web_search_preview"}],
            # 强制 JSON 输出（Responses API 用 text.format 而非 response_format）
            "text": {"format": {"type": "json_object"}},
        }

        if max_tokens:
            payload["max_output_tokens"] = max_tokens

        # 允许通过 extra_params 覆盖搜索上下文大小
        extra_params = kwargs.get("extra_params", self.config.extra_params)
        if "search_context_size" in extra_params:
            payload["tools"] = [{
                "type": "web_search_preview",
                "search_context_size": extra_params["search_context_size"],
            }]

        headers = {
            "Content-Type": "application/json",
            "Authorization": f"Bearer {self.config.api_key}",
        }

        url = self._get_responses_url()

        for attempt in range(self.config.max_retries):
            try:
                data = json.dumps(payload).encode('utf-8')
                req = urllib.request.Request(
                    url, data=data, headers=headers, method='POST'
                )

                timeout_sec = self.config.timeout_ms / 1000
                # web_search 请求通常更慢，给 2x 超时余量
                with urllib.request.urlopen(req, timeout=timeout_sec * 2) as response:
                    result = json.loads(response.read().decode('utf-8'))
                    latency_ms = int((time.time() - start_time) * 1000)
                    return self._parse_responses_api_result(result, model, latency_ms)

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

                # 404 通常意味着端点不支持 Responses API（第三方兼容端点）
                # 直接 fallback 到普通 chat completion，不再重试
                if e.code in [401, 403]:
                    return AIResponse(
                        success=False, error=error_msg,
                        model=model, provider=self.provider_name
                    )
                if e.code == 404:
                    logger.warning(
                        f"Responses API not available at {url} (404), "
                        f"falling back to chat_completion_json"
                    )
                    return self.chat_completion_json(
                        messages, temperature=temperature,
                        max_tokens=max_tokens, **kwargs
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
                error_msg = f"Request timeout after {self.config.timeout_ms * 2}ms"
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
                return self._handle_error(e, "Responses API request failed")

        return AIResponse(
            success=False, error="Max retries exceeded",
            model=model, provider=self.provider_name
        )

    def _parse_responses_api_result(self, result: Dict[str, Any], model: str,
                                    latency_ms: int) -> AIResponse:
        """解析 Responses API 响应

        响应结构：
        {
          "output": [
            {"type": "web_search_call", "id": "...", ...},  # 搜索动作（可多个）
            {"type": "message", "content": [
                {"type": "output_text", "text": "{...json...}", "annotations": [...]}
            ]}
          ],
          "usage": {...},
          "model": "gpt-4o-...",
          "status": "completed"
        }
        """
        try:
            output = result.get("output", [])
            if not output:
                return AIResponse(
                    success=False,
                    error="No output in Responses API result",
                    model=model,
                    provider=self.provider_name,
                    latency_ms=latency_ms
                )

            content_text = ""
            web_search_count = 0
            for item in output:
                item_type = item.get("type", "")
                if item_type == "web_search_call":
                    web_search_count += 1
                elif item_type == "message":
                    for c in item.get("content", []):
                        if c.get("type") == "output_text":
                            content_text += c.get("text", "")
                        elif c.get("type") == "text":
                            # 部分兼容端点可能用 "text" 而非 "output_text"
                            content_text += c.get("text", "")

            if not content_text:
                return AIResponse(
                    success=False,
                    error="No output_text in Responses API message",
                    model=model,
                    provider=self.provider_name,
                    latency_ms=latency_ms
                )

            usage = result.get("usage", {})
            prompt_tokens = usage.get("input_tokens", 0)
            completion_tokens = usage.get("output_tokens", 0)
            total_tokens = usage.get("total_tokens", prompt_tokens + completion_tokens)

            if web_search_count > 0:
                logger.info(
                    f"Responses API: performed {web_search_count} web search call(s)"
                )

            return AIResponse(
                success=True,
                content=content_text,
                model=result.get("model", model),
                provider=self.provider_name,
                tokens_used=total_tokens,
                prompt_tokens=prompt_tokens,
                completion_tokens=completion_tokens,
                latency_ms=latency_ms,
                finish_reason=result.get("status", ""),
                raw_response=result
            )

        except Exception as e:
            return self._handle_error(e, "Failed to parse Responses API result")

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
