#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Query Rewriter
Rewrites queries to improve search results when initial queries fail
"""

import json
import logging
from typing import List, Dict, Any, Optional
from dataclasses import dataclass

from data_sources.base import QueryInput

logger = logging.getLogger(__name__)


@dataclass
class RewrittenQuery:
    """重写后的查询
    
    Attributes:
        query: 重写后的查询输入
        reason: 重写原因
        priority: 优先级（越小越优先）
    """
    query: QueryInput
    reason: str = ""
    priority: int = 0


class QueryRewriter:
    """查询重写器
    
    当原始查询无法获得足够候选时，通过以下策略重写查询：
    1. 标题变体（罗马音、英文翻译、原名）
    2. 艺术家变体（别名、组合名拆分）
    3. 移除括号内容（如 (OST ver.)）
    4. 模糊匹配（仅保留关键词）
    """
    
    REWRITE_SYSTEM_PROMPT = """You are a music search expert. Your task is to generate alternative search queries when the original query fails to find results.

Given a song title and artist, generate alternative queries that might help find the song in music databases.

IMPORTANT: Return ONLY valid JSON, no additional text.

Return JSON format:
{
    "rewrites": [
        {
            "title": "...",
            "artist": "...",
            "reason": "why this might work"
        }
    ]
}

Guidelines for rewriting:
1. Try romanized versions (Japanese/Chinese to English)
2. Try original language versions (English to Japanese/Chinese)
3. Try common misspellings or variations
4. Remove parenthetical content like "(OST ver.)" or "-TV Size-"
5. Try artist aliases or band member names
6. For anime/OST, try adding series name
7. For classical music, try composer as artist
8. Maximum 5 rewrites"""
    
    def __init__(self, config: Dict[str, Any]):
        """初始化查询重写器
        
        Args:
            config: 配置字典
        """
        self._config = config
        self._model_adapter = None
        self._init_model_adapter()
    
    def _init_model_adapter(self) -> None:
        """初始化模型适配器"""
        try:
            from ai.adapter import ModelAdapter
            self._model_adapter = ModelAdapter(self._config)
            logger.info(f"QueryRewriter initialized with provider: {self._model_adapter.get_provider_info()}")
        except Exception as e:
            logger.error(f"Failed to initialize QueryRewriter: {e}")
            self._model_adapter = None
    
    @property
    def is_available(self) -> bool:
        """重写器是否可用"""
        return self._model_adapter is not None
    
    def rewrite(self, query: QueryInput) -> List[RewrittenQuery]:
        """重写查询
        
        Args:
            query: 原始查询输入
        
        Returns:
            List[RewrittenQuery]: 重写后的查询列表
        """
        logger.debug(f"QueryRewriter::rewrite: title='{query.title}', artist='{query.artist}'")
        
        rewrites = []
        
        rule_based = self._rule_based_rewrite(query)
        rewrites.extend(rule_based)
        
        if self.is_available:
            ai_based = self._ai_rewrite(query)
            rewrites.extend(ai_based)
        
        unique_rewrites = self._deduplicate_rewrites(rewrites)
        
        unique_rewrites.sort(key=lambda r: r.priority)
        
        logger.debug(f"QueryRewriter: generated {len(unique_rewrites)} rewrites")
        return unique_rewrites[:5]
    
    def _rule_based_rewrite(self, query: QueryInput) -> List[RewrittenQuery]:
        """基于规则的重写
        
        Args:
            query: 原始查询输入
        
        Returns:
            List[RewrittenQuery]: 重写后的查询列表
        """
        rewrites = []
        
        clean_title = self._remove_parenthetical(query.title)
        if clean_title != query.title:
            rewrites.append(RewrittenQuery(
                query=QueryInput(
                    title=clean_title,
                    artist=query.artist,
                    album=query.album,
                    duration=query.duration
                ),
                reason="Removed parenthetical content",
                priority=1
            ))
        
        clean_artist = self._remove_parenthetical(query.artist)
        if clean_artist != query.artist:
            rewrites.append(RewrittenQuery(
                query=QueryInput(
                    title=query.title,
                    artist=clean_artist,
                    album=query.album,
                    duration=query.duration
                ),
                reason="Cleaned artist name",
                priority=2
            ))
        
        if "feat." in query.title.lower() or "ft." in query.title.lower():
            clean_title = self._remove_featured(query.title)
            rewrites.append(RewrittenQuery(
                query=QueryInput(
                    title=clean_title,
                    artist=query.artist,
                    album=query.album,
                    duration=query.duration
                ),
                reason="Removed featured artist from title",
                priority=1
            ))
        
        keywords = self._extract_keywords(query.title)
        if keywords and keywords != query.title:
            rewrites.append(RewrittenQuery(
                query=QueryInput(
                    title=keywords,
                    artist=query.artist,
                    album=query.album,
                    duration=query.duration
                ),
                reason="Keyword extraction",
                priority=3
            ))
        
        return rewrites
    
    def _ai_rewrite(self, query: QueryInput) -> List[RewrittenQuery]:
        """基于 AI 的重写
        
        Args:
            query: 原始查询输入
        
        Returns:
            List[RewrittenQuery]: 重写后的查询列表
        """
        if not self.is_available:
            return []
        
        try:
            messages = self._build_rewrite_prompt(query)
            
            result = self._model_adapter.analyze(messages)
            
            if not result.success:
                logger.warning(f"AI rewrite failed: {result.error}")
                return []
            
            return self._parse_rewrite_result(result.result, query)
        
        except Exception as e:
            logger.error(f"QueryRewriter AI error: {e}")
            return []
    
    def _build_rewrite_prompt(self, query: QueryInput) -> List[Dict[str, str]]:
        """构建重写提示
        
        Args:
            query: 原始查询输入
        
        Returns:
            List[Dict]: 消息列表
        """
        user_content = f"""Original query that failed to find results:
Title: {query.title}
Artist: {query.artist}
{f"Album: {query.album}" if query.album else ""}

Generate alternative search queries that might help find this song."""

        return [
            {"role": "system", "content": self.REWRITE_SYSTEM_PROMPT},
            {"role": "user", "content": user_content}
        ]
    
    def _parse_rewrite_result(self, result: Dict[str, Any],
                               original_query: QueryInput) -> List[RewrittenQuery]:
        """解析 AI 重写结果
        
        Args:
            result: AI 返回的 JSON 结果
            original_query: 原始查询输入
        
        Returns:
            List[RewrittenQuery]: 重写后的查询列表
        """
        rewrites = []
        
        if not result or "rewrites" not in result:
            return rewrites
        
        for i, item in enumerate(result.get("rewrites", [])):
            title = item.get("title", "")
            artist = item.get("artist", "")
            reason = item.get("reason", "")
            
            if not title and not artist:
                continue
            
            rewritten = RewrittenQuery(
                query=QueryInput(
                    title=title or original_query.title,
                    artist=artist or original_query.artist,
                    album=original_query.album,
                    duration=original_query.duration
                ),
                reason=reason,
                priority=10 + i
            )
            rewrites.append(rewritten)
        
        return rewrites
    
    def _remove_parenthetical(self, text: str) -> str:
        """移除括号内容
        
        Args:
            text: 原始文本
        
        Returns:
            str: 清理后的文本
        """
        import re
        result = re.sub(r'\s*[\(\[（【].*?[\)\]）】]\s*', ' ', text)
        return ' '.join(result.split())
    
    def _remove_featured(self, text: str) -> str:
        """移除 featuring 信息
        
        Args:
            text: 原始文本
        
        Returns:
            str: 清理后的文本
        """
        import re
        result = re.sub(r'\s*(feat\.?|ft\.?)\s+.*$', '', text, flags=re.IGNORECASE)
        return result.strip()
    
    def _extract_keywords(self, text: str) -> str:
        """提取关键词
        
        Args:
            text: 原始文本
        
        Returns:
            str: 关键词
        """
        import re
        clean = re.sub(r'[^\w\s]', ' ', text)
        words = clean.split()
        
        if len(words) <= 2:
            return text
        
        return ' '.join(words[:3])
    
    def _deduplicate_rewrites(self, rewrites: List[RewrittenQuery]) -> List[RewrittenQuery]:
        """去重重写结果
        
        Args:
            rewrites: 重写列表
        
        Returns:
            List[RewrittenQuery]: 去重后的列表
        """
        seen = set()
        unique = []
        
        for rewrite in rewrites:
            key = f"{rewrite.query.title}|{rewrite.query.artist}"
            if key not in seen:
                seen.add(key)
                unique.append(rewrite)
        
        return unique
