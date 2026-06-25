#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
AI Provider Base Module
Defines the abstract base class for all AI providers
"""

import json
import logging
from abc import ABC, abstractmethod
from dataclasses import dataclass, field
from enum import Enum
from typing import Dict, List, Any, Optional

from common.text_utils import clean_dict_values, clean_list_values

logger = logging.getLogger(__name__)


class ProviderType(Enum):
    """AI提供商类型枚举
    
    Attributes:
        OPENROUTER: OpenRouter提供商
        OLLAMA: Ollama本地提供商
        ZHIPU: 智谱AI提供商
        GEMINI: Google Gemini提供商
    """
    OPENROUTER = "openrouter"
    OLLAMA = "ollama"
    ZHIPU = "zhipu"
    GEMINI = "gemini"


@dataclass
class AIResponse:
    """AI响应数据类
    
    Attributes:
        success: 是否成功
        content: 响应内容（可选）
        error: 错误信息（可选）
        model: 使用的模型名称
        provider: 提供商名称
        tokens_used: 使用的总令牌数
        prompt_tokens: 提示令牌数
        completion_tokens: 完成令牌数
        latency_ms: 延迟（毫秒）
        finish_reason: 完成原因
        raw_response: 原始响应字典
    """
    success: bool
    content: Optional[str] = None
    error: Optional[str] = None
    model: str = ""
    provider: str = ""
    tokens_used: int = 0
    prompt_tokens: int = 0
    completion_tokens: int = 0
    latency_ms: int = 0
    finish_reason: str = ""
    raw_response: Dict[str, Any] = field(default_factory=dict)
    
    def to_dict(self) -> Dict[str, Any]:
        """转换为字典
        
        Returns:
            Dict[str, Any]: 响应字典
        """
        return {
            "success": self.success,
            "content": self.content,
            "error": self.error,
            "model": self.model,
            "provider": self.provider,
            "tokens_used": self.tokens_used,
            "prompt_tokens": self.prompt_tokens,
            "completion_tokens": self.completion_tokens,
            "latency_ms": self.latency_ms,
            "finish_reason": self.finish_reason
        }


@dataclass
class ProviderConfig:
    """提供商配置数据类
    
    Attributes:
        provider_type: 提供商类型
        api_key: API密钥
        base_url: 基础URL
        model: 默认模型
        models: 模型列表
        timeout_ms: 超时时间（毫秒）
        max_retries: 最大重试次数
        retry_delay_ms: 重试延迟（毫秒）
        extra_params: 额外参数
    """
    provider_type: ProviderType
    api_key: str = ""
    base_url: str = ""
    model: str = ""
    models: List[Dict[str, Any]] = field(default_factory=list)
    timeout_ms: int = 30000
    max_retries: int = 3
    retry_delay_ms: int = 1000
    extra_params: Dict[str, Any] = field(default_factory=dict)
    
    @classmethod
    def from_dict(cls, data: Dict[str, Any], provider_type: ProviderType) -> "ProviderConfig":
        """从字典创建配置
        
        Args:
            data: 配置字典
            provider_type: 提供商类型
        
        Returns:
            ProviderConfig: 配置实例
        """
        return cls(
            provider_type=provider_type,
            api_key=data.get("api_key", ""),
            base_url=data.get("base_url", ""),
            model=data.get("selected_model") or data.get("model", ""),
            models=data.get("models", []),
            timeout_ms=data.get("timeout_ms", 30000),
            max_retries=data.get("max_retries", 3),
            retry_delay_ms=data.get("retry_delay_ms", 1000),
            extra_params=data.get("extra_params", {})
        )


class BaseAIProvider(ABC):
    """AI提供商抽象基类
    
    定义所有AI提供商必须实现的接口。
    """
    
    def __init__(self, config: ProviderConfig):
        """初始化提供商
        
        Args:
            config: 提供商配置
        """
        self.config = config
        self.provider_name = self.__class__.__name__.replace("Provider", "").lower()
    
    @abstractmethod
    def chat_completion(self, messages: List[Dict[str, str]], 
                        temperature: float = 0.7,
                        max_tokens: Optional[int] = None,
                        **kwargs) -> AIResponse:
        """发送聊天完成请求
        
        Args:
            messages: 消息列表，每个消息包含'role'和'content'
            temperature: 采样温度（0.0-2.0）
            max_tokens: 最大生成令牌数
            **kwargs: 额外的提供商特定参数
        
        Returns:
            AIResponse: AI响应对象
        """
        pass
    
    @abstractmethod
    def chat_completion_json(self, messages: List[Dict[str, str]],
                             temperature: float = 0.3,
                             max_tokens: Optional[int] = None,
                             **kwargs) -> AIResponse:
        """发送期望JSON响应的聊天完成请求
        
        Args:
            messages: 消息列表，每个消息包含'role'和'content'
            temperature: 采样温度（0.0-2.0）
            max_tokens: 最大生成令牌数
            **kwargs: 额外的提供商特定参数
        
        Returns:
            AIResponse: AI响应对象（内容应为有效JSON）
        """
        pass
    
    def get_model(self) -> str:
        """获取当前模型名称（最高优先级）
        
        Returns:
            str: 模型名称
        """
        if self.config.model:
            return self.config.model
        if self.config.models:
            sorted_models = self._get_sorted_models()
            if sorted_models:
                return sorted_models[0].get("name", "")
        return ""
    
    def get_fallback_models(self) -> List[str]:
        """获取备用模型名称列表（按优先级排序）
        
        Returns:
            List[str]: 备用模型名称列表
        """
        if len(self.config.models) > 1:
            sorted_models = self._get_sorted_models()
            if len(sorted_models) > 1:
                return [m.get("name", "") for m in sorted_models[1:]]
        return []
    
    def _get_sorted_models(self) -> List[Dict[str, Any]]:
        """获取按优先级排序的模型列表（数字越小优先级越高）
        
        Returns:
            List[Dict[str, Any]]: 排序后的模型配置列表
        """
        if not self.config.models:
            return []
        
        def get_priority(model: Dict[str, Any]) -> int:
            return model.get("priority", 999)
        
        return sorted(self.config.models, key=get_priority)
    
    @abstractmethod
    def validate_config(self) -> bool:
        """验证提供商配置
        
        Returns:
            bool: 配置有效返回True
        """
        pass
    
    def _parse_json_response(self, content: str) -> Optional[Dict[str, Any]]:
        """从响应内容解析JSON
        
        Args:
            content: 原始响应内容
        
        Returns:
            Optional[Dict[str, Any]]: 解析后的JSON字典，失败返回None
        """
        if not content:
            return None
        
        try:
            result = json.loads(content)
            return self._clean_json_result(result)
        except json.JSONDecodeError:
            pass
        
        json_start = content.find("{")
        json_end = content.rfind("}") + 1
        if json_start != -1 and json_end > json_start:
            try:
                result = json.loads(content[json_start:json_end])
                return self._clean_json_result(result)
            except json.JSONDecodeError:
                pass
        
        json_start = content.find("[")
        json_end = content.rfind("]") + 1
        if json_start != -1 and json_end > json_start:
            try:
                result = json.loads(content[json_start:json_end])
                return self._clean_json_result(result)
            except json.JSONDecodeError:
                pass
        
        return None
    
    def _clean_json_result(self, result: Any) -> Any:
        """清理JSON结果中的无效值
        
        Args:
            result: 原始结果
        
        Returns:
            Any: 清理后的结果
        """
        if isinstance(result, dict):
            return clean_dict_values(result)
        elif isinstance(result, list):
            return clean_list_values(result)
        return result
    
    def _handle_error(self, error: Exception, context: str = "") -> AIResponse:
        """处理错误并创建错误响应
        
        Args:
            error: 发生的异常
            context: 错误发生的上下文
        
        Returns:
            AIResponse: 包含错误信息的响应
        """
        error_msg = f"{context}: {str(error)}" if context else str(error)
        logger.error(f"AI Provider error: {error_msg}")
        
        return AIResponse(
            success=False,
            error=error_msg,
            model=self.get_model(),
            provider=self.provider_name
        )
    
    def _should_stop_fallback(self, error: str) -> bool:
        """检查错误是否应停止备用模型尝试
        
        某些错误表明账户级别的问题，尝试其他模型也无法解决。
        注意：配额/余额错误现在会触发备用模型切换。
        
        Args:
            error: 错误消息字符串
        
        Returns:
            bool: 应停止尝试其他模型返回True，继续尝试返回False
        """
        if not error:
            return False
        
        error_lower = error.lower()
        
        auth_keywords = [
            "401", "403", 
            "invalid_api_key", "invalid key", "api key", "apikey",
            "unauthorized", "forbidden", "authentication", "auth failed"
        ]
        
        for keyword in auth_keywords:
            if keyword in error_lower:
                return True
        
        return False
    
    def __repr__(self) -> str:
        """获取提供商的字符串表示
        
        Returns:
            str: 提供商表示字符串
        """
        return f"{self.__class__.__name__}(model={self.get_model()})"
