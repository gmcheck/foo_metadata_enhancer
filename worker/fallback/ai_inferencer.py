#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
AI Inferencer
Last-resort metadata inference using AI
"""

import logging
from typing import Dict, Any, Optional, List
from dataclasses import dataclass

from data_sources.base import QueryInput, Candidate, DataSourceType
from core.resolver import FinalResult
from common.text_utils import clean_dict_values
from prompts import build_inference_prompt

logger = logging.getLogger(__name__)


@dataclass
class InferenceContext:
    """推断上下文
    
    Attributes:
        original_query: 原始查询
        rewritten_query: 重写后的查询
        attempt_count: 尝试次数
        previous_errors: 之前的错误列表
    """
    original_query: QueryInput
    rewritten_query: Optional[QueryInput] = None
    attempt_count: int = 0
    previous_errors: List[str] = None
    
    def __post_init__(self):
        if self.previous_errors is None:
            self.previous_errors = []


class AIInferencer:
    """AI 推断器
    
    作为最后手段，使用 AI 从有限信息推断元数据。
    仅在以下场景使用：
    1. 所有数据源查询失败
    2. 查询重写后仍无结果
    3. 用户启用推断功能
    """
    
    def __init__(self, config: Dict[str, Any], ai_client=None):
        """初始化 AI 推断器
        
        Args:
            config: 配置字典
            ai_client: AI 客户端实例
        """
        self._config = config
        self._ai_client = ai_client
        self._enabled = config.get("fallback", {}).get("ai_inference", {}).get("enabled", True)
        self._max_attempts = config.get("fallback", {}).get("ai_inference", {}).get("max_attempts", 2)
    
    def infer(self, query: QueryInput, context: InferenceContext = None) -> FinalResult:
        """推断元数据
        
        Args:
            query: 查询输入
            context: 推断上下文
        
        Returns:
            FinalResult: 推断结果
        """
        if not self._enabled:
            logger.warning("AI inference is disabled")
            return self._create_empty_result(query)
        
        if context is None:
            context = InferenceContext(original_query=query)
        
        logger.debug(f"AIInferencer::infer: title='{query.title}', artist='{query.artist}'")
        
        try:
            inference_result = self._call_ai_for_inference(query, context)
            
            if inference_result:
                final_result = FinalResult(
                    title=inference_result.get("title", query.title),
                    artist=inference_result.get("artist", query.artist),
                    album=inference_result.get("album", query.album),
                    year=inference_result.get("year", ""),
                    genre=inference_result.get("genre", ""),
                    composer=inference_result.get("composer", ""),
                    lyricist=inference_result.get("lyricist", ""),
                    label=inference_result.get("label", ""),
                    country=inference_result.get("country", ""),
                    confidence=inference_result.get("confidence", 0.5),
                    source=DataSourceType.AI,
                    sources=[DataSourceType.AI],
                    is_fallback=True,
                    reasoning=inference_result.get("reasoning", "AI inference from limited information")
                )
                
                logger.debug(f"AIInferencer: inference successful, confidence={final_result.confidence:.2f}")
                return final_result
            else:
                logger.warning("AIInferencer: inference returned empty result")
                return self._create_empty_result(query)
                
        except Exception as e:
            logger.error(f"AIInferencer::infer failed: {e}")
            return self._create_empty_result(query)
    
    def _call_ai_for_inference(self, query: QueryInput, context: InferenceContext) -> Optional[Dict[str, Any]]:
        """调用 AI 进行推断
        
        Args:
            query: 查询输入
            context: 推断上下文
        
        Returns:
            Optional[Dict]: 推断结果字典
        """
        if self._ai_client is None:
            logger.warning("AI client not available for inference")
            return None
        
        prompt = self._build_inference_prompt(query, context)
        
        try:
            response = self._ai_client.chat(prompt)
            
            if response:
                return self._parse_inference_response(response)
            
        except Exception as e:
            logger.error(f"AI inference call failed: {e}")
        
        return None
    
    def _build_inference_prompt(self, query: QueryInput, context: InferenceContext) -> str:
        return build_inference_prompt(
            title=query.title,
            artist=query.artist,
            album=query.album,
            duration=query.duration,
            previous_errors=context.previous_errors if context else None
        )
    
    def _parse_inference_response(self, response: str) -> Optional[Dict[str, Any]]:
        """解析推断响应
        
        Args:
            response: AI 响应字符串
        
        Returns:
            Optional[Dict]: 解析后的结果字典
        """
        import json
        import re
        
        try:
            json_match = re.search(r'\{[\s\S]*\}', response)
            if json_match:
                result = json.loads(json_match.group())
                
                result = clean_dict_values(result)
                
                if "confidence" in result:
                    try:
                        result["confidence"] = float(result["confidence"])
                        result["confidence"] = max(0.0, min(1.0, result["confidence"]))
                    except (ValueError, TypeError):
                        result["confidence"] = 0.5
                
                return result
                
        except json.JSONDecodeError as e:
            logger.error(f"Failed to parse inference response: {e}")
        
        return None
    
    def _create_empty_result(self, query: QueryInput) -> FinalResult:
        """创建空结果
        
        Args:
            query: 查询输入
        
        Returns:
            FinalResult: 空结果
        """
        result = FinalResult(
            title=query.title,
            artist=query.artist,
            album=query.album,
            confidence=0.0,
            source=DataSourceType.AI,
            sources=[],
            is_fallback=True,
            reasoning="No inference possible - insufficient information"
        )
        
        if query.raw_data:
            if query.raw_data.get("year"):
                result.year = str(query.raw_data.get("year", ""))
            if query.raw_data.get("genre"):
                result.genre = str(query.raw_data.get("genre", ""))
            if query.raw_data.get("composer"):
                result.composer = str(query.raw_data.get("composer", ""))
            if query.raw_data.get("lyricist"):
                result.lyricist = str(query.raw_data.get("lyricist", ""))
            if query.raw_data.get("label"):
                result.label = str(query.raw_data.get("label", ""))
            if query.raw_data.get("track_number"):
                result.track_number = str(query.raw_data.get("track_number", ""))
            if query.raw_data.get("disc_number"):
                result.disc_number = str(query.raw_data.get("disc_number", ""))
            if query.raw_data.get("musicbrainz_id"):
                result.musicbrainz_id = str(query.raw_data.get("musicbrainz_id", ""))
        
        return result
    
    def can_infer(self, query: QueryInput) -> bool:
        """检查是否可以进行推断
        
        Args:
            query: 查询输入
        
        Returns:
            bool: 是否可以推断
        """
        if not self._enabled:
            return False
        
        has_title = bool(query.title and query.title.strip())
        has_artist = bool(query.artist and query.artist.strip())
        
        return has_title or has_artist
    
    def get_inference_confidence(self, query: QueryInput) -> float:
        """预估推断置信度
        
        Args:
            query: 查询输入
        
        Returns:
            float: 预估置信度
        """
        confidence = 0.0
        
        if query.title and query.title.strip():
            confidence += 0.2
        
        if query.artist and query.artist.strip():
            confidence += 0.2
        
        if query.album and query.album.strip():
            confidence += 0.1
        
        if query.duration and query.duration > 0:
            confidence += 0.1
        
        return min(confidence, 0.6)
