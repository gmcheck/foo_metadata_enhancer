#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
AI Provider Factory
Creates and manages AI provider instances
"""

import logging
from typing import Dict, List, Any, Optional, Type

from .base import BaseAIProvider, ProviderConfig, ProviderType
from .openrouter_provider import OpenRouterProvider
from .ollama_provider import OllamaProvider
from .zhipu_provider import ZhipuProvider
from .gemini_provider import GeminiProvider

logger = logging.getLogger(__name__)


class AIProviderFactory:
    """AI提供商工厂类
    
    创建和管理AI提供商实例。
    
    Attributes:
        _providers: 已注册的提供商类型映射
        _instances: 已缓存的提供商实例
    """
    
    _providers: Dict[ProviderType, Type[BaseAIProvider]] = {
        ProviderType.OPENROUTER: OpenRouterProvider,
        ProviderType.OLLAMA: OllamaProvider,
        ProviderType.ZHIPU: ZhipuProvider,
        ProviderType.GEMINI: GeminiProvider,
    }
    
    _instances: Dict[str, BaseAIProvider] = {}
    
    @classmethod
    def register_provider(cls, provider_type: ProviderType, 
                          provider_class: Type[BaseAIProvider]) -> None:
        """注册新的提供商类型
        
        Args:
            provider_type: 提供商类型标识符
            provider_class: 要注册的提供商类
        """
        cls._providers[provider_type] = provider_class
        logger.info(f"Registered AI provider: {provider_type.value}")
    
    @classmethod
    def create(cls, provider_type: ProviderType, 
               config: ProviderConfig,
               instance_id: Optional[str] = None) -> BaseAIProvider:
        """创建新的提供商实例
        
        Args:
            provider_type: 要创建的提供商类型
            config: 提供商配置
            instance_id: 可选的实例ID用于缓存
        
        Returns:
            BaseAIProvider: 新的提供商实例
        
        Raises:
            ValueError: 提供商类型未注册时抛出
        """
        if provider_type not in cls._providers:
            raise ValueError(f"Unknown provider type: {provider_type.value}")
        
        provider_class = cls._providers[provider_type]
        instance = provider_class(config)
        
        if instance_id:
            cls._instances[instance_id] = instance
        
        logger.debug(f"Created {provider_type.value} provider: {instance.get_model()}")
        return instance
    
    @classmethod
    def create_from_config(cls, config_dict: Dict[str, Any],
                           provider_type: Optional[str] = None,
                           instance_id: Optional[str] = None) -> BaseAIProvider:
        """从配置字典创建提供商
        
        Args:
            config_dict: 包含提供商设置的配置字典
            provider_type: 覆盖提供商类型（可选，未提供时使用配置）
            instance_id: 可选的实例ID用于缓存
        
        Returns:
            BaseAIProvider: 新的提供商实例
        """
        if provider_type is None:
            provider_type = config_dict.get("provider_type", "openrouter")
        
        ptype = ProviderType(provider_type.lower())
        
        provider_config = ProviderConfig.from_dict(config_dict, ptype)
        
        logger.info(f"create_from_config: provider={provider_type}, model={provider_config.model}, selected_model={config_dict.get('selected_model', '')}")
        
        return cls.create(ptype, provider_config, instance_id)
    
    @classmethod
    def get_instance(cls, instance_id: str) -> Optional[BaseAIProvider]:
        """获取缓存的提供商实例
        
        Args:
            instance_id: 要查找的实例ID
        
        Returns:
            Optional[BaseAIProvider]: 缓存的提供商实例，未找到返回None
        """
        return cls._instances.get(instance_id)
    
    @classmethod
    def remove_instance(cls, instance_id: str) -> None:
        """移除缓存的提供商实例
        
        Args:
            instance_id: 要移除的实例ID
        """
        if instance_id in cls._instances:
            del cls._instances[instance_id]
    
    @classmethod
    def clear_instances(cls) -> None:
        """清除所有缓存的提供商实例"""
        cls._instances.clear()
    
    @classmethod
    def get_available_providers(cls) -> List[str]:
        """获取可用提供商类型列表
        
        Returns:
            List[str]: 提供商类型名称列表
        """
        return [pt.value for pt in cls._providers.keys()]
    
    @classmethod
    def create_from_yaml(cls, yaml_config: Dict[str, Any],
                         default_provider: Optional[str] = None) -> BaseAIProvider:
        """从YAML配置创建提供商
        
        Args:
            yaml_config: 完整的YAML配置字典
            default_provider: 未指定时使用的默认提供商
        
        Returns:
            BaseAIProvider: 新的提供商实例
        """
        providers_config = yaml_config.get("providers", {})
        
        provider_type = providers_config.get("default", default_provider or "openrouter")
        
        provider_config = providers_config.get(provider_type, {})
        
        if not provider_config:
            raise ValueError(f"No configuration found for provider: {provider_type}")
        
        provider_config["timeout_ms"] = yaml_config.get("worker", {}).get("api_timeout_ms", 180000)
        provider_config["max_retries"] = yaml_config.get("worker", {}).get("max_retries", 3)
        
        return cls.create_from_config(provider_config, provider_type)
    
    @classmethod
    def create_primary_provider(cls, config: Dict[str, Any]) -> BaseAIProvider:
        """从配置创建主提供商
        
        优先级顺序：
        1. providers.default 如果指定且有效
        2. providers.priority_order 列表
        
        Args:
            config: 完整的配置字典
        
        Returns:
            BaseAIProvider: 新的提供商实例
        """
        providers_config = config.get("providers", {})
        
        default_provider = providers_config.get("default", "")
        priority_order = providers_config.get("priority_order", 
                                              ["openrouter", "zhipu", "gemini", "ollama"])
        
        if default_provider:
            try:
                provider_config = providers_config.get(default_provider, {})
                if provider_config:
                    if default_provider == "ollama" and not provider_config.get("enabled", False):
                        pass
                    else:
                        api_key = provider_config.get("api_key", "")
                        if not api_key and default_provider != "ollama":
                            import os
                            env_key = os.getenv(f"{default_provider.upper()}_API_KEY", "")
                            if env_key:
                                provider_config["api_key"] = env_key
                        
                        if provider_config.get("api_key") or default_provider == "ollama":
                            provider_config["timeout_ms"] = config.get("worker", {}).get("api_timeout_ms", 180000)
                            provider_config["max_retries"] = config.get("worker", {}).get("max_retries", 3)
                            
                            selected_model = provider_config.get("selected_model", "")
                            logger.info(f"Using default provider: {default_provider}, selected_model: {selected_model}")
                            return cls.create_from_config(provider_config, default_provider)
            except Exception as e:
                logger.warning(f"Failed to create default provider '{default_provider}': {e}")
        
        for provider_name in priority_order:
            if provider_name == default_provider:
                continue
            
            provider_config = providers_config.get(provider_name, {})
            
            if not provider_config:
                continue
            
            if provider_name == "ollama":
                enabled = provider_config.get("enabled", False)
                if not enabled:
                    continue
            
            api_key = provider_config.get("api_key", "")
            if not api_key and provider_name != "ollama":
                import os
                env_key = os.getenv(f"{provider_name.upper()}_API_KEY", "")
                if env_key:
                    provider_config["api_key"] = env_key
                else:
                    continue
            
            try:
                provider_config["timeout_ms"] = config.get("worker", {}).get("api_timeout_ms", 180000)
                provider_config["max_retries"] = config.get("worker", {}).get("max_retries", 3)
                
                logger.info(f"Using fallback provider: {provider_name}")
                return cls.create_from_config(provider_config, provider_name)
            except Exception as e:
                logger.warning(f"Failed to create {provider_name} provider: {e}")
                continue
        
        raise ValueError("No valid AI provider configuration found")


def create_provider(config: Dict[str, Any], 
                    provider_type: Optional[str] = None) -> BaseAIProvider:
    """创建提供商的便捷函数
    
    Args:
        config: 提供商配置字典
        provider_type: 提供商类型覆盖
    
    Returns:
        BaseAIProvider: 新的提供商实例
    """
    if provider_type:
        return AIProviderFactory.create_from_config(config, provider_type)
    return AIProviderFactory.create_primary_provider(config)
