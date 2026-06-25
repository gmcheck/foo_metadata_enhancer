#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Result Formatter
Formats final results for IPC response
"""

import logging
from typing import Dict, Any, List, Optional
from dataclasses import dataclass, field, asdict

from data_sources.base import Candidate, QueryInput, DataSourceType
from core.resolver import FinalResult

logger = logging.getLogger(__name__)


@dataclass
class FormattedResult:
    """格式化后的结果
    
    Attributes:
        track_id: 音轨ID
        success: 是否成功
        final: 最终元数据
        candidates: 候选列表
        fallback_context: 降级上下文
        scraped_fields: 刮削字段
        error: 错误信息
    """
    track_id: str
    success: bool
    final: Dict[str, Any] = field(default_factory=dict)
    candidates: List[Dict[str, Any]] = field(default_factory=list)
    fallback_context: Dict[str, Any] = field(default_factory=dict)
    scraped_fields: Dict[str, Any] = field(default_factory=dict)
    error: Optional[str] = None
    
    def to_dict(self) -> Dict[str, Any]:
        """转换为字典"""
        return asdict(self)


class ResultFormatter:
    """结果格式化器
    
    负责将内部数据结构转换为 IPC 响应格式
    """
    
    def __init__(self, config: Dict[str, Any] = None):
        """初始化结果格式化器
        
        Args:
            config: 配置字典
        """
        self._config = config or {}
        self._include_raw_data = self._config.get("include_raw_data", False)
        self._max_candidates = self._config.get("max_candidates", 5)
    
    def format_success(self, track_id: str,
                       final_result: FinalResult,
                       candidates: List[Candidate],
                       fallback_level: str = "normal",
                       fallback_used: bool = False) -> FormattedResult:
        """格式化成功结果
        
        Args:
            track_id: 音轨ID
            final_result: 最终元数据结果
            candidates: 候选列表
            fallback_level: 降级级别
            fallback_used: 是否使用了降级
        
        Returns:
            FormattedResult: 格式化后的结果
        """
        final_dict = self._format_final_result(final_result)
        
        candidates_list = self._format_candidates(candidates)
        
        fallback_context = {
            "level": fallback_level,
            "original_candidates": len(candidates),
            "fallback_used": fallback_used
        }
        
        scraped_fields = self._extract_scraped_fields(final_result)
        
        return FormattedResult(
            track_id=track_id,
            success=True,
            final=final_dict,
            candidates=candidates_list,
            fallback_context=fallback_context,
            scraped_fields=scraped_fields
        )
    
    def format_error(self, track_id: str, error_message: str) -> FormattedResult:
        """格式化错误结果
        
        Args:
            track_id: 音轨ID
            error_message: 错误消息
        
        Returns:
            FormattedResult: 格式化后的结果
        """
        return FormattedResult(
            track_id=track_id,
            success=False,
            error=error_message
        )
    
    def format_batch(self, results: List[FormattedResult]) -> Dict[str, Any]:
        """格式化批量结果
        
        Args:
            results: 结果列表
        
        Returns:
            Dict[str, Any]: 批量结果字典
        """
        success_count = sum(1 for r in results if r.success)
        
        return {
            "total": len(results),
            "success_count": success_count,
            "failed_count": len(results) - success_count,
            "results": [r.to_dict() for r in results]
        }
    
    def _format_final_result(self, result: FinalResult) -> Dict[str, Any]:
        """格式化最终结果
        
        Args:
            result: 最终元数据结果
        
        Returns:
            Dict[str, Any]: 格式化后的字典
        """
        formatted = result.to_dict()
        
        if not self._include_raw_data:
            formatted.pop("raw", None)
        
        return formatted
    
    def _format_candidates(self, candidates: List[Candidate]) -> List[Dict[str, Any]]:
        """格式化候选列表
        
        Args:
            candidates: 候选列表
        
        Returns:
            List[Dict[str, Any]]: 格式化后的列表
        """
        formatted = []
        
        for candidate in candidates[:self._max_candidates]:
            candidate_dict = {
                "title": candidate.title,
                "artist": candidate.artist,
                "album": candidate.album,
                "year": candidate.year,
                "genre": candidate.genre,
                "source": candidate.source.value if isinstance(candidate.source, DataSourceType) else candidate.source,
                "confidence": candidate.confidence,
                "match_score": candidate.match_score,
                "sources": [s.value if isinstance(s, DataSourceType) else s for s in candidate.sources]
            }
            
            if self._include_raw_data and candidate.raw:
                candidate_dict["raw"] = candidate.raw
            
            formatted.append(candidate_dict)
        
        return formatted
    
    def _extract_scraped_fields(self, result: FinalResult) -> Dict[str, Any]:
        """提取刮削字段
        
        Args:
            result: 最终元数据结果
        
        Returns:
            Dict[str, Any]: 刮削字段字典
        """
        fields = {}
        
        field_names = [
            "title", "artist", "album", "year", "track_number",
            "disc_number", "genre", "composer", "lyricist",
            "label", "country", "catalog_number", "musicbrainz_id"
        ]
        
        for field_name in field_names:
            value = getattr(result, field_name, "")
            if value:
                fields[field_name] = {
                    "value": value,
                    "confidence": result.confidence,
                    "source": result.source.value if isinstance(result.source, DataSourceType) else result.source
                }
        
        return fields
    
    def format_for_display(self, result: FormattedResult) -> str:
        """格式化为显示字符串
        
        Args:
            result: 格式化后的结果
        
        Returns:
            str: 显示字符串
        """
        if not result.success:
            return f"[ERROR] {result.track_id}: {result.error}"
        
        lines = [
            f"Track: {result.track_id}",
            f"  Title: {result.final.get('title', '')}",
            f"  Artist: {result.final.get('artist', '')}",
            f"  Album: {result.final.get('album', '')}",
            f"  Year: {result.final.get('year', '')}",
            f"  Confidence: {result.final.get('confidence', 0):.2f}",
            f"  Source: {result.final.get('source', '')}",
            f"  Candidates: {len(result.candidates)}",
            f"  Fallback: {result.fallback_context.get('fallback_used', False)}"
        ]
        
        return "\n".join(lines)
    
    def format_summary(self, results: List[FormattedResult]) -> str:
        """格式化摘要
        
        Args:
            results: 结果列表
        
        Returns:
            str: 摘要字符串
        """
        total = len(results)
        success = sum(1 for r in results if r.success)
        failed = total - success
        
        avg_confidence = 0.0
        if success > 0:
            confidences = [r.final.get("confidence", 0) for r in results if r.success]
            avg_confidence = sum(confidences) / len(confidences) if confidences else 0.0
        
        fallback_count = sum(1 for r in results if r.success and r.fallback_context.get("fallback_used", False))
        
        lines = [
            f"Total tracks: {total}",
            f"Successful: {success}",
            f"Failed: {failed}",
            f"Average confidence: {avg_confidence:.2f}",
            f"Fallback used: {fallback_count}"
        ]
        
        return "\n".join(lines)
