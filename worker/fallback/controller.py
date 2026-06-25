#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Fallback Controller
Controls the fallback pipeline when primary data sources fail
"""

import logging
from typing import List, Dict, Any, Optional
from dataclasses import dataclass
from enum import Enum

from data_sources.base import Candidate, QueryInput, DataSourceType
from data_sources.manager import DataSourceManager
from core.aggregator import CandidateAggregator
from core.resolver import AIResolver, FinalResult
from .query_rewriter import QueryRewriter

logger = logging.getLogger(__name__)


class FallbackLevel(str, Enum):
    """降级级别"""
    NORMAL = "normal"
    ENHANCED = "enhanced"
    REWRITE = "rewrite"
    AI_INFER = "ai_infer"


@dataclass
class FallbackContext:
    """降级上下文
    
    Attributes:
        original_query: 原始查询
        current_level: 当前降级级别
        attempts: 尝试次数
        rewritten_queries: 已尝试的重写查询
    """
    original_query: QueryInput
    current_level: FallbackLevel = FallbackLevel.NORMAL
    attempts: int = 0
    rewritten_queries: List[QueryInput] = None
    
    def __post_init__(self):
        if self.rewritten_queries is None:
            self.rewritten_queries = []


class FallbackController:
    """降级控制器
    
    处理数据源查询失败时的降级策略：
    
    1. 候选充足 (>=3): 正常 AI 决策
    2. 候选不足 (1-2): 增强模式 AI 决策
    3. 无候选: 查询重写 → 再次查询 → AI 推断
    """
    
    MIN_CANDIDATES_NORMAL = 3
    MIN_CANDIDATES_ENHANCED = 1
    MAX_REWRITE_ATTEMPTS = 3
    
    def __init__(self, config: Dict[str, Any]):
        """初始化降级控制器
        
        Args:
            config: 配置字典
        """
        self._config = config
        self._data_source_manager: Optional[DataSourceManager] = None
        self._aggregator: Optional[CandidateAggregator] = None
        self._resolver: Optional[AIResolver] = None
        self._query_rewriter: Optional[QueryRewriter] = None
        
        self._init_components()
    
    def _init_components(self) -> None:
        """初始化组件"""
        try:
            self._data_source_manager = DataSourceManager(self._config)
            self._aggregator = CandidateAggregator()
            self._resolver = AIResolver(self._config)
            self._query_rewriter = QueryRewriter(self._config)
            
            logger.info("FallbackController initialized successfully")
        except Exception as e:
            logger.error(f"Failed to initialize FallbackController: {e}")
    
    def handle(self, query: QueryInput, candidates: List[Candidate] = None) -> FinalResult:
        """处理查询，根据候选数量选择策略
        
        Args:
            query: 查询输入
            candidates: 已有候选（可选）
        
        Returns:
            FinalResult: 最终元数据结果
        """
        logger.debug(f"FallbackController::handle: title='{query.title}', artist='{query.artist}'")
        
        context = FallbackContext(original_query=query)
        
        if candidates is None:
            candidates = self._fetch_candidates(query)
        
        return self._process_with_context(query, candidates, context)
    
    def _process_with_context(self, query: QueryInput, 
                               candidates: List[Candidate],
                               context: FallbackContext) -> FinalResult:
        context.attempts += 1
        
        if len(candidates) >= self.MIN_CANDIDATES_NORMAL:
            logger.debug(f"FallbackController: normal mode with {len(candidates)} candidates")
            context.current_level = FallbackLevel.NORMAL
            return self._resolve_normal(query, candidates)
        
        if len(candidates) >= self.MIN_CANDIDATES_ENHANCED:
            logger.debug(f"FallbackController: enhanced mode with {len(candidates)} candidates")
            context.current_level = FallbackLevel.ENHANCED
            return self._resolve_enhanced(query, candidates)
        
        if context.attempts <= self.MAX_REWRITE_ATTEMPTS:
            logger.debug(f"FallbackController: rewrite mode (attempt {context.attempts})")
            context.current_level = FallbackLevel.REWRITE
            return self._handle_rewrite(query, candidates, context)
        
        logger.debug("FallbackController: AI infer mode (final fallback)")
        context.current_level = FallbackLevel.AI_INFER
        return self._ai_infer(query)
    
    def _fetch_candidates(self, query: QueryInput) -> List[Candidate]:
        """获取候选列表
        
        Args:
            query: 查询输入
        
        Returns:
            List[Candidate]: 候选列表
        """
        if not self._data_source_manager:
            logger.warning("DataSourceManager not initialized")
            return []
        
        candidates = self._data_source_manager.fetch_all(query)
        
        if candidates and self._aggregator:
            aggregation_result = self._aggregator.aggregate(candidates)
            return aggregation_result.candidates
        
        return candidates
    
    def _resolve_normal(self, query: QueryInput, candidates: List[Candidate]) -> FinalResult:
        """正常模式决策
        
        Args:
            query: 查询输入
            candidates: 候选列表
        
        Returns:
            FinalResult: 最终结果
        """
        if self._resolver:
            return self._resolver.resolve(query, candidates)
        
        return self._select_best_fallback(candidates, query)
    
    def _resolve_enhanced(self, query: QueryInput, candidates: List[Candidate]) -> FinalResult:
        """增强模式决策
        
        Args:
            query: 查询输入
            candidates: 候选列表
        
        Returns:
            FinalResult: 最终结果
        """
        if self._resolver:
            return self._resolver.resolve_enhanced(query, candidates)
        
        return self._select_best_fallback(candidates, query)
    
    def _handle_rewrite(self, query: QueryInput, 
                         candidates: List[Candidate],
                         context: FallbackContext) -> FinalResult:
        """处理查询重写
        
        Args:
            query: 查询输入
            candidates: 当前候选列表
            context: 降级上下文
        
        Returns:
            FinalResult: 最终结果
        """
        if not self._query_rewriter:
            logger.warning("QueryRewriter not available, falling back to AI infer")
            return self._ai_infer(query)
        
        rewrites = self._query_rewriter.rewrite(query)
        
        for rewrite in rewrites:
            if rewrite.query in context.rewritten_queries:
                continue
            
            context.rewritten_queries.append(rewrite.query)
            
            logger.debug(f"Trying rewritten query: title='{rewrite.query.title}', "
                       f"artist='{rewrite.query.artist}', reason='{rewrite.reason}'")
            
            new_candidates = self._fetch_candidates(rewrite.query)
            
            if len(new_candidates) >= self.MIN_CANDIDATES_ENHANCED:
                all_candidates = candidates + new_candidates
                
                if self._aggregator:
                    aggregation_result = self._aggregator.aggregate(all_candidates)
                    all_candidates = aggregation_result.candidates
                
                return self._process_with_context(query, all_candidates, context)
        
        return self._ai_infer(query)
    
    def _ai_infer(self, query: QueryInput) -> FinalResult:
        """AI 推断模式
        
        当所有数据源都无法获取候选时的最终降级策略
        
        Args:
            query: 查询输入
        
        Returns:
            FinalResult: AI 推断结果
        """
        if not self._resolver or not self._resolver.is_available:
            logger.warning("AI resolver not available for inference")
            return FinalResult(
                title=query.title,
                artist=query.artist,
                album=query.album,
                confidence=0.0,
                source=DataSourceType.AI,
                sources=[],
                is_fallback=True
            )
        
        return self._resolver.resolve_enhanced(query, [])
    
    def _select_best_fallback(self, candidates: List[Candidate], query: QueryInput) -> FinalResult:
        """选择最佳候选作为降级结果
        
        Args:
            candidates: 候选列表
            query: 查询输入
        
        Returns:
            FinalResult: 最终结果
        """
        if not candidates:
            return FinalResult(
                title=query.title,
                artist=query.artist,
                album=query.album,
                confidence=0.0,
                source=DataSourceType.AI,
                sources=[],
                is_fallback=True
            )
        
        sorted_candidates = sorted(candidates, key=lambda c: c.confidence, reverse=True)
        best = sorted_candidates[0]
        
        sources = [best.source]
        if best.sources:
            sources = best.sources
        
        return FinalResult(
            title=best.title or query.title,
            artist=best.artist or query.artist,
            album=best.album,
            year=best.year,
            track_number=best.track_number,
            disc_number=best.disc_number,
            genre=best.genre,
            composer=best.composer,
            lyricist=best.lyricist,
            label=best.label,
            country=best.country,
            catalog_number=best.catalog_number,
            musicbrainz_id=best.musicbrainz_id,
            confidence=best.confidence,
            source=best.source,
            sources=sources,
            is_fallback=True
        )
    
    def get_fallback_statistics(self, context: FallbackContext) -> Dict[str, Any]:
        """获取降级统计信息
        
        Args:
            context: 降级上下文
        
        Returns:
            Dict[str, Any]: 统计信息
        """
        return {
            "original_query": {
                "title": context.original_query.title,
                "artist": context.original_query.artist,
                "album": context.original_query.album
            },
            "final_level": context.current_level.value,
            "total_attempts": context.attempts,
            "rewrite_count": len(context.rewritten_queries)
        }
