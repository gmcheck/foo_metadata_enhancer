#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Model Adapter
Provides unified interface for local and remote AI models using the provider system
"""

import json
import logging
from typing import Dict, List, Any, Optional
from dataclasses import dataclass
from pathlib import Path

from ai.providers import (
    BaseAIProvider, 
    AIProviderFactory, 
    create_provider,
    ProviderType,
    AIResponse
)
from common.text_utils import clean_dict_values

logger = logging.getLogger(__name__)


@dataclass
class AnalysisResult:
    """分析结果数据类
    
    Attributes:
        success: 是否成功
        result: 结果字典（可选）
        error: 错误信息（可选）
        model: 使用的模型名称
        tokens_used: 使用的令牌数
        model_type: 模型类型（local/remote）
        provider: 提供商名称
    """
    success: bool
    result: Optional[Dict[str, Any]] = None
    error: Optional[str] = None
    model: str = ""
    tokens_used: int = 0
    model_type: str = "remote"
    provider: str = ""


class ModelAdapter:
    """模型适配器类
    
    提供本地和远程AI模型的统一接口，使用提供商系统。
    
    Attributes:
        config: 配置字典
        provider: AI提供商实例
    """
    
    def __init__(self, config: Dict[str, Any], config_path: Optional[Path] = None):
        """初始化模型适配器
        
        Args:
            config: 配置字典
            config_path: 配置文件路径（可选）
        """
        self.config = config
        self.provider: Optional[BaseAIProvider] = None
        self._init_provider()
    
    def _init_provider(self) -> None:
        """从配置初始化AI提供商"""
        try:
            self.provider = AIProviderFactory.create_primary_provider(self.config)
            logger.info(f"Initialized AI provider: {self.provider}")
        except Exception as e:
            logger.error(f"Failed to initialize AI provider: {e}")
            self.provider = None
    
    def analyze(self, messages: List[Dict[str, str]]) -> AnalysisResult:
        """使用配置的提供商进行分析
        
        Args:
            messages: 消息列表，每个消息包含'role'和'content'
        
        Returns:
            AnalysisResult: 分析结果
        """
        logger.debug(f"ModelAdapter::analyze: Starting analysis with {len(messages)} messages")
        
        if not self.provider:
            logger.error("ModelAdapter::analyze: No AI provider configured")
            return AnalysisResult(
                success=False,
                error="No AI provider configured"
            )
        
        logger.debug(f"ModelAdapter::analyze: Calling provider.chat_completion_json")
        logger.debug(f"ModelAdapter::analyze: Messages = {json.dumps(messages, ensure_ascii=False)[:500]}...")
        
        response = self.provider.chat_completion_json(messages)
        
        logger.debug(f"ModelAdapter::analyze: Provider response success = {response.success}, model = {response.model}, tokens_used = {response.tokens_used}")
        
        if not response.success:
            logger.error(f"ModelAdapter::analyze: Provider error = {response.error}")
            return AnalysisResult(
                success=False,
                error=response.error,
                model=response.model,
                provider=response.provider
            )
        
        try:
            logger.debug("ModelAdapter::analyze: Parsing response content")
            result = self._parse_result(response.content)
            logger.debug(f"ModelAdapter::analyze: Successfully parsed result")
            
            return AnalysisResult(
                success=True,
                result=result,
                model=response.model,
                tokens_used=response.tokens_used,
                model_type="local" if response.provider == "ollama" else "remote",
                provider=response.provider
            )
        
        except Exception as e:
            logger.error(f"ModelAdapter::analyze: Failed to parse response: {e}", exc_info=True)
            return AnalysisResult(
                success=False,
                error=f"Failed to parse response: {e}",
                model=response.model,
                provider=response.provider
            )
    
    def _parse_result(self, content: str) -> Dict[str, Any]:
        """从响应内容解析JSON
        
        Args:
            content: 原始响应内容
        
        Returns:
            Dict: 解析后的JSON字典
        
        Raises:
            ValueError: 解析失败时抛出
        """
        if not content:
            raise ValueError("Empty response content")
        
        try:
            result = json.loads(content)
            return self._clean_result(result)
        except json.JSONDecodeError:
            pass
        
        json_start = content.find("{")
        json_end = content.rfind("}") + 1
        if json_start != -1 and json_end > json_start:
            try:
                result = json.loads(content[json_start:json_end])
                return self._clean_result(result)
            except json.JSONDecodeError:
                pass
        
        raise ValueError(f"Failed to parse JSON from response: {content[:200]}...")
    
    def _clean_result(self, result: Dict[str, Any]) -> Dict[str, Any]:
        """清理结果中的无效值
        
        Args:
            result: 原始结果字典
        
        Returns:
            Dict: 清理后的结果字典
        """
        return clean_dict_values(result)
    
    def get_provider_info(self) -> Dict[str, Any]:
        """获取当前提供商信息
        
        Returns:
            Dict: 提供商信息字典
        """
        if not self.provider:
            return {
                "provider": "none",
                "model": "none",
                "status": "not configured"
            }
        
        return {
            "provider": self.provider.provider_name,
            "model": self.provider.get_model(),
            "fallback_models": self.provider.get_fallback_models(),
            "status": "ready"
        }
    
    def switch_provider(self, provider_type: str, config: Optional[Dict[str, Any]] = None) -> bool:
        """切换到其他提供商
        
        Args:
            provider_type: 提供商类型
            config: 可选的提供商特定配置
        
        Returns:
            bool: 切换成功返回True
        """
        try:
            if config:
                self.provider = AIProviderFactory.create_from_config(config, provider_type)
            else:
                providers_config = self.config.get("providers", {})
                provider_config = providers_config.get(provider_type, {})
                if not provider_config:
                    logger.error(f"No configuration found for provider: {provider_type}")
                    return False
                
                provider_config["timeout_ms"] = self.config.get("worker", {}).get("api_timeout_ms", 180000)
                provider_config["max_retries"] = self.config.get("worker", {}).get("max_retries", 3)
                
                self.provider = AIProviderFactory.create_from_config(provider_config, provider_type)
            
            logger.info(f"Switched to provider: {self.provider}")
            return True
        
        except Exception as e:
            logger.error(f"Failed to switch provider: {e}")
            return False
    
    def test_connection(self) -> Dict[str, Any]:
        """测试当前提供商连接
        
        Returns:
            Dict: 测试结果字典
        """
        if not self.provider:
            return {
                "success": False,
                "error": "No provider configured"
            }
        
        test_messages = [
            {"role": "user", "content": "Say 'OK' if you can read this."}
        ]
        
        response = self.provider.chat_completion(test_messages, temperature=0.1)
        
        return {
            "success": response.success,
            "provider": response.provider,
            "model": response.model,
            "latency_ms": response.latency_ms,
            "error": response.error
        }
