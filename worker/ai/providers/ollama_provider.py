#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Ollama Local AI Provider
Implements the Ollama local model client
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


class OllamaProvider(BaseAIProvider):
    """Ollama本地模型提供商实现
    
    Ollama在本地运行模型并提供OpenAI兼容的API。
    文档: https://github.com/ollama/ollama/blob/main/docs/api.md
    
    Attributes:
        DEFAULT_BASE_URL: 默认API端点
        DEFAULT_MODEL: 默认模型
        POPULAR_MODELS: 常用模型列表
        base_url: 当前使用的API端点
    """
    
    DEFAULT_BASE_URL = "http://localhost:11434/api/chat"
    DEFAULT_MODEL = "llama3"
    
    POPULAR_MODELS = [
        "llama3",
        "llama3:8b",
        "llama3:70b",
        "llama3.1",
        "llama3.1:8b",
        "llama3.1:70b",
        "llama3.2",
        "llama3.2:1b",
        "llama3.2:3b",
        "mistral",
        "mistral:7b",
        "mixtral",
        "mixtral:8x7b",
        "mixtral:8x22b",
        "qwen2.5",
        "qwen2.5:7b",
        "qwen2.5:14b",
        "qwen2.5:32b",
        "qwen2.5:72b",
        "gemma2",
        "gemma2:9b",
        "gemma2:27b",
        "codellama",
        "deepseek-coder",
        "deepseek-r1"
    ]
    
    def __init__(self, config: ProviderConfig):
        """初始化Ollama提供商
        
        Args:
            config: 提供商配置
        """
        super().__init__(config)
        self.base_url = config.base_url or self.DEFAULT_BASE_URL
        self._check_ollama_available()
    
    def _check_ollama_available(self) -> bool:
        """检查Ollama服务是否可用
        
        Returns:
            bool: 服务可用返回True
        """
        try:
            base_url = self.base_url.replace("/api/chat", "")
            req = urllib.request.Request(f"{base_url}/api/tags", method='GET')
            with urllib.request.urlopen(req, timeout=5) as response:
                return response.status == 200
        except:
            return False
    
    def validate_config(self) -> bool:
        """验证配置是否有效
        
        Returns:
            bool: 配置有效返回True
        """
        if not self.get_model():
            logger.error("Ollama model is required")
            return False
        
        if not self._check_ollama_available():
            logger.warning("Ollama server may not be running. Start with: ollama serve")
        
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
        
        payload = {
            "model": model,
            "messages": messages,
            "stream": False,
            "options": {
                "temperature": temperature
            }
        }
        
        if max_tokens:
            payload["options"]["num_predict"] = max_tokens
        
        if json_mode:
            payload["format"] = "json"
        
        extra_params = kwargs.get("extra_params", self.config.extra_params)
        for key in ["top_p", "top_k", "repeat_penalty", "seed", "num_ctx"]:
            if key in extra_params:
                payload["options"][key] = extra_params[key]
        
        headers = {
            "Content-Type": "application/json"
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
                        error_msg = error_data.get("error", error_msg)
                    except:
                        error_msg = f"{error_msg} - {error_body[:200]}"
                
                if "model" in error_msg.lower() and "not found" in error_msg.lower():
                    return AIResponse(
                        success=False,
                        error=f"Model '{model}' not found. Pull it with: ollama pull {model}",
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
                error_msg = f"Connection error: {e.reason}. Is Ollama running? (ollama serve)"
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
            message = result.get("message", {})
            content = message.get("content", "")
            
            if not content:
                return AIResponse(
                    success=False,
                    error="Empty response from model",
                    model=model,
                    provider=self.provider_name,
                    latency_ms=latency_ms
                )
            
            eval_count = result.get("eval_count", 0)
            prompt_eval_count = result.get("prompt_eval_count", 0)
            total_tokens = eval_count + prompt_eval_count
            
            done_reason = result.get("done_reason", "")
            
            return AIResponse(
                success=True,
                content=content,
                model=result.get("model", model),
                provider=self.provider_name,
                tokens_used=total_tokens,
                prompt_tokens=prompt_eval_count,
                completion_tokens=eval_count,
                latency_ms=latency_ms,
                finish_reason=done_reason,
                raw_response=result
            )
        
        except Exception as e:
            return self._handle_error(e, "Failed to parse response")
    
    def list_models(self) -> List[str]:
        """列出可用的本地模型
        
        Returns:
            List[str]: 模型名称列表
        """
        try:
            base_url = self.base_url.replace("/api/chat", "")
            req = urllib.request.Request(f"{base_url}/api/tags", method='GET')
            with urllib.request.urlopen(req, timeout=5) as response:
                result = json.loads(response.read().decode('utf-8'))
                models = result.get("models", [])
                return [m.get("name", "") for m in models]
        except:
            return []
    
    def pull_model(self, model_name: str) -> bool:
        """从Ollama注册表拉取模型
        
        Args:
            model_name: 要拉取的模型名称
        
        Returns:
            bool: 拉取成功返回True
        """
        try:
            base_url = self.base_url.replace("/api/chat", "")
            payload = {"name": model_name, "stream": False}
            data = json.dumps(payload).encode('utf-8')
            req = urllib.request.Request(
                f"{base_url}/api/pull",
                data=data,
                headers={"Content-Type": "application/json"},
                method='POST'
            )
            with urllib.request.urlopen(req, timeout=300) as response:
                return response.status == 200
        except Exception as e:
            logger.error(f"Failed to pull model {model_name}: {e}")
            return False
    
    @classmethod
    def from_config(cls, config_dict: Dict[str, Any]) -> "OllamaProvider":
        """从配置字典创建提供商实例
        
        Args:
            config_dict: 配置字典
        
        Returns:
            OllamaProvider: 提供商实例
        """
        provider_config = ProviderConfig.from_dict(config_dict, ProviderType.OLLAMA)
        return cls(provider_config)
