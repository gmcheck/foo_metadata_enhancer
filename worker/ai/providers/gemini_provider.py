#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Google Gemini AI Provider
Implements the Google Gemini API client
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


class GeminiProvider(BaseAIProvider):
    """Google Gemini API提供商实现
    
    Google Gemini提供多模态AI模型。
    API文档: https://ai.google.dev/api/generate-content
    
    Attributes:
        DEFAULT_BASE_URL: 默认API端点
        GEMINI_MODELS: Gemini模型列表
        base_url: 当前使用的API端点
    """
    
    DEFAULT_BASE_URL = "https://generativelanguage.googleapis.com/v1beta/models"
    
    GEMINI_MODELS = [
        "gemini-2.0-flash",
        "gemini-2.0-flash-lite",
        "gemini-1.5-flash",
        "gemini-1.5-flash-8b",
        "gemini-1.5-pro",
        "gemini-1.0-pro",
        "gemini-exp-1206",
        "gemini-exp-1121",
        "gemini-learning",
        "learnlm-1.5-pro-experimental"
    ]
    
    def __init__(self, config: ProviderConfig):
        """初始化Gemini提供商
        
        Args:
            config: 提供商配置
        """
        super().__init__(config)
        self.base_url = config.base_url or self.DEFAULT_BASE_URL
    
    def validate_config(self) -> bool:
        """验证配置是否有效
        
        Returns:
            bool: 配置有效返回True
        """
        if not self.config.api_key:
            logger.error("Google Gemini API key is required")
            return False
        if not self.get_model():
            logger.error("Google Gemini model is required")
            return False
        return True
    
    def chat_completion(self, messages: List[Dict[str, str]], 
                        temperature: float = 0.7,
                        max_tokens: Optional[int] = None,
                        **kwargs) -> AIResponse:
        """发送聊天完成请求
        
        Args:
            messages: 消息列表
            temperature: 采样温度
            max_tokens: 最大生成令牌数
            **kwargs: 额外参数
        
        Returns:
            AIResponse: AI响应对象
        """
        return self._send_request(messages, temperature, max_tokens, json_mode=False, **kwargs)
    
    def chat_completion_json(self, messages: List[Dict[str, str]],
                             temperature: float = 0.3,
                             max_tokens: Optional[int] = None,
                             **kwargs) -> AIResponse:
        """发送期望JSON响应的聊天完成请求
        
        Args:
            messages: 消息列表
            temperature: 采样温度
            max_tokens: 最大生成令牌数
            **kwargs: 额外参数
        
        Returns:
            AIResponse: AI响应对象
        """
        return self._send_request(messages, temperature, max_tokens, json_mode=True, **kwargs)
    
    def _convert_messages_to_gemini(self, messages: List[Dict[str, str]]) -> List[Dict[str, Any]]:
        """将OpenAI风格消息转换为Gemini格式
        
        Args:
            messages: OpenAI风格消息列表
        
        Returns:
            List[Dict[str, Any]]: Gemini格式消息列表
        """
        gemini_contents = []
        
        for msg in messages:
            role = msg.get("role", "user")
            content = msg.get("content", "")
            
            if role == "system":
                gemini_contents.append({
                    "role": "user",
                    "parts": [{"text": f"System: {content}"}]
                })
                gemini_contents.append({
                    "role": "model",
                    "parts": [{"text": "I understand. I will follow these instructions."}]
                })
            elif role == "assistant":
                gemini_contents.append({
                    "role": "model",
                    "parts": [{"text": content}]
                })
            else:
                gemini_contents.append({
                    "role": "user",
                    "parts": [{"text": content}]
                })
        
        return gemini_contents
    
    def _send_request(self, messages: List[Dict[str, str]],
                      temperature: float,
                      max_tokens: Optional[int],
                      json_mode: bool = False,
                      **kwargs) -> AIResponse:
        """发送API请求（支持备用模型）
        
        Args:
            messages: 消息列表
            temperature: 采样温度
            max_tokens: 最大生成令牌数
            json_mode: 是否启用JSON模式
            **kwargs: 额外参数
        
        Returns:
            AIResponse: AI响应对象
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
        """尝试使用指定模型发送请求
        
        Args:
            model: 模型名称
            messages: 消息列表
            temperature: 采样温度
            max_tokens: 最大生成令牌数
            json_mode: 是否启用JSON模式
            **kwargs: 额外参数
        
        Returns:
            AIResponse: AI响应对象
        """
        start_time = time.time()
        
        gemini_contents = self._convert_messages_to_gemini(messages)
        
        generation_config = {
            "temperature": temperature
        }
        
        if max_tokens:
            generation_config["maxOutputTokens"] = max_tokens
        
        extra_params = kwargs.get("extra_params", self.config.extra_params)
        for key in ["top_p", "top_k", "presence_penalty", "frequency_penalty", "stopSequences"]:
            if key in extra_params:
                generation_config[key] = extra_params[key]
        
        if json_mode:
            generation_config["responseMimeType"] = "application/json"
        
        payload = {
            "contents": gemini_contents,
            "generationConfig": generation_config
        }
        
        if "safety_settings" in extra_params:
            payload["safetySettings"] = extra_params["safety_settings"]
        
        url = f"{self.base_url}/{model}:generateContent?key={self.config.api_key}"
        
        headers = {
            "Content-Type": "application/json"
        }
        
        for attempt in range(self.config.max_retries):
            try:
                data = json.dumps(payload).encode('utf-8')
                req = urllib.request.Request(
                    url,
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
                            if "status" in err_info:
                                error_msg = f"{err_info['status']}: {error_msg}"
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
        """解析API响应
        
        Args:
            result: 原始响应字典
            model: 使用的模型名称
            latency_ms: 延迟毫秒数
        
        Returns:
            AIResponse: 解析后的响应对象
        """
        try:
            candidates = result.get("candidates", [])
            if not candidates:
                return AIResponse(
                    success=False,
                    error="No candidates in response",
                    model=model,
                    provider=self.provider_name,
                    latency_ms=latency_ms
                )
            
            candidate = candidates[0]
            content_parts = candidate.get("content", {}).get("parts", [])
            
            content = ""
            for part in content_parts:
                if "text" in part:
                    content += part["text"]
            
            finish_reason = candidate.get("finishReason", "")
            
            usage_metadata = result.get("usageMetadata", {})
            prompt_tokens = usage_metadata.get("promptTokenCount", 0)
            completion_tokens = usage_metadata.get("candidatesTokenCount", 0)
            total_tokens = usage_metadata.get("totalTokenCount", prompt_tokens + completion_tokens)
            
            return AIResponse(
                success=True,
                content=content,
                model=model,
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
    
    @classmethod
    def from_config(cls, config_dict: Dict[str, Any]) -> "GeminiProvider":
        """从配置字典创建提供商实例
        
        Args:
            config_dict: 配置字典
        
        Returns:
            GeminiProvider: 提供商实例
        """
        provider_config = ProviderConfig.from_dict(config_dict, ProviderType.GEMINI)
        return cls(provider_config)
