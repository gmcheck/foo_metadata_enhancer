#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
AI Data Source
Provides metadata scraping using AI models via existing ModelAdapter
"""

import json
import logging
from typing import Dict, Any, Optional, List

from data_sources.base import (
    DataSourceType,
    ReleaseInfo,
    DataSourceAdapter,
    QueryInput,
    Candidate,
)
from common.text_utils import clean_value

logger = logging.getLogger(__name__)


class AIAdapter(DataSourceAdapter):
    """AI 数据源适配器
    
    对接现有 AI Provider 系统，支持多种 AI 模型：
    - OpenRouter
    - Zhipu (智谱AI)
    - Gemini (Google)
    - Ollama (本地)
    """
    
    SCRAPE_SYSTEM_PROMPT = """You are a music metadata expert. Given a song title and artist, 
provide accurate metadata information. Return all available fields with confidence scores.

IMPORTANT: Return ONLY valid JSON, no additional text.

Return JSON format:
{
    "title": {"value": "...", "confidence": 0.95},
    "artist": {"value": "...", "confidence": 0.98},
    "album": {"value": "...", "confidence": 0.90},
    "year": {"value": "...", "confidence": 0.85},
    "composer": {"value": "...", "confidence": 0.80},
    "lyricist": {"value": "...", "confidence": 0.75},
    "label": {"value": "...", "confidence": 0.70}
}

Guidelines:
- confidence: 0.0-1.0, where 1.0 means absolutely certain
- Only include fields you can confidently identify
- If uncertain about a field, omit it or use low confidence
- For classical music, composer is very important
- For pop/rock, label and year are often available"""
    
    def __init__(self, config: Dict[str, Any]):
        """初始化 AI 适配器
        
        Args:
            config: 配置字典
        """
        super().__init__(config)
        
        data_sources_config = config.get("data_sources", {})
        ai_config = data_sources_config.get("ai", {})
        
        self._enabled = ai_config.get("enabled", True)
        self._model_adapter = None
        
        self._init_model_adapter()
    
    def _init_model_adapter(self) -> None:
        """初始化模型适配器，对接现有的 AI Provider 系统"""
        try:
            from ai.adapter import ModelAdapter
            self._model_adapter = ModelAdapter(self._config)
            logger.info(f"AIAdapter initialized with provider: {self._model_adapter.get_provider_info()}")
        except Exception as e:
            logger.error(f"Failed to initialize AIAdapter: {e}")
            self._model_adapter = None
    
    @property
    def source_type(self) -> DataSourceType:
        """数据源类型"""
        return DataSourceType.AI
    
    @property
    def is_enabled(self) -> bool:
        """是否启用"""
        return self._enabled and self._model_adapter is not None
    
    def search_candidates(self, query: QueryInput) -> List[Candidate]:
        """使用 AI 进行刮削并返回候选列表
        
        AI 适配器作为兜底数据源，通常返回单个候选结果。
        但为保持接口一致性，仍返回 List[Candidate]。
        
        Args:
            query: 查询输入（title, artist, album, duration）
        
        Returns:
            List[Candidate]: 候选列表（通常只包含一个候选）
        """
        logger.debug(f"AIAdapter::search_candidates: title='{query.title}', artist='{query.artist}'")
        
        if not self._model_adapter:
            logger.warning("AI model adapter not initialized")
            return []
        
        messages = self._build_candidates_prompt(query)
        
        try:
            result = self._model_adapter.analyze(messages)
            
            if not result.success:
                logger.warning(f"AI analysis failed: {result.error}")
                return []
            
            candidate = self._parse_ai_result_to_candidate(result.result, result.model, query)
            
            if candidate:
                return [candidate]
            return []
        
        except Exception as e:
            logger.error(f"AIAdapter search_candidates error: {e}")
            return []
    
    def _build_candidates_prompt(self, query: QueryInput) -> List[Dict[str, str]]:
        """构建 AI 候选查询提示
        
        Args:
            query: 查询输入
        
        Returns:
            List[Dict]: 消息列表
        """
        user_content = f"""Song Information:
Title: {query.title}
Artist: {query.artist}
{f"Album: {query.album}" if query.album else ""}
{f"Duration: {query.duration} seconds" if query.duration else ""}

Please provide metadata with confidence scores.
Return JSON with each field containing 'value' and 'confidence' (0.0-1.0).
Only include fields you can confidently identify."""

        return [
            {"role": "system", "content": self.SCRAPE_SYSTEM_PROMPT},
            {"role": "user", "content": user_content}
        ]
    
    def _parse_ai_result_to_candidate(self, result: Dict[str, Any],
                                       model: str,
                                       query: QueryInput) -> Optional[Candidate]:
        """将 AI 结果解析为候选对象
        
        Args:
            result: AI 返回的 JSON 结果
            model: 使用的模型名称
            query: 原始查询输入
        
        Returns:
            Optional[Candidate]: 候选对象
        """
        if not result:
            return None
        
        def get_field_value(field_name: str) -> str:
            field_data = result.get(field_name, {})
            if isinstance(field_data, dict):
                return clean_value(field_data.get("value", ""))
            return ""
        
        def get_field_confidence(field_name: str) -> float:
            field_data = result.get(field_name, {})
            if isinstance(field_data, dict):
                return field_data.get("confidence", 0.5)
            return 0.5
        
        title = get_field_value("title") or query.title
        artist = get_field_value("artist") or query.artist
        
        avg_confidence = 0.0
        field_count = 0
        for field_name in ["title", "artist", "album", "year", "composer", "lyricist", "label"]:
            if result.get(field_name):
                avg_confidence += get_field_confidence(field_name)
                field_count += 1
        
        if field_count > 0:
            avg_confidence = avg_confidence / field_count
        else:
            avg_confidence = 0.5
        
        candidate = Candidate(
            title=title,
            artist=artist,
            album=get_field_value("album"),
            year=get_field_value("year"),
            track_number=get_field_value("track_number"),
            disc_number=get_field_value("disc_number"),
            genre=get_field_value("genre"),
            composer=get_field_value("composer"),
            lyricist=get_field_value("lyricist"),
            label=get_field_value("label"),
            country=get_field_value("country"),
            catalog_number=get_field_value("catalog_number"),
            musicbrainz_id=get_field_value("musicbrainz_id"),
            source=DataSourceType.AI,
            confidence=min(avg_confidence, 0.85),
            match_score=avg_confidence,
            sources=[DataSourceType.AI],
            raw={"ai_result": result, "model": model}
        )
        
        return candidate
    
    def get_release_info(self, release_id: str) -> Optional[ReleaseInfo]:
        """获取发行信息
        
        AI 适配器不支持通过 release_id 获取发行信息
        
        Args:
            release_id: 发行 ID（对 AI 无意义）
        
        Returns:
            ReleaseInfo: None
        """
        logger.warning("AIAdapter does not support get_release_info")
        return None
    
    def switch_provider(self, provider_type: str) -> bool:
        """切换 AI 提供商
        
        Args:
            provider_type: 'openrouter', 'zhipu', 'gemini', 'ollama'
        
        Returns:
            bool: 切换成功返回 True
        """
        if self._model_adapter:
            return self._model_adapter.switch_provider(provider_type)
        return False
    
    def get_available_providers(self) -> List[str]:
        """获取可用的 AI 提供商列表
        
        Returns:
            List[str]: 提供商名称列表
        """
        try:
            from ai.providers import AIProviderFactory
            return AIProviderFactory.get_available_providers()
        except ImportError:
            return ["openrouter", "zhipu", "gemini", "ollama"]
    
    def get_provider_info(self) -> Dict[str, Any]:
        """获取当前提供商信息
        
        Returns:
            Dict: 提供商信息
        """
        if self._model_adapter:
            return self._model_adapter.get_provider_info()
        return {
            "provider": "none",
            "model": "none",
            "status": "not configured"
        }
