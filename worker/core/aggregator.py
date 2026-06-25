#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Candidate Aggregator - 候选聚合器

聚合来自多个数据源（MusicBrainz、Discogs、AI）的查询结果，实现去重、合并、排序。

核心流程:
    输入: [Candidate1, Candidate2, ...] (来自多个数据源)
             ↓
        1. 统计数据源数量
             ↓
        2. 按标准化 key 分组 (title + artist)
             ↓
        3. 合并同组候选
             ↓
        4. 过滤低置信度 (< 0.3)
             ↓
        5. 按置信度排序
             ↓
        6. 限制最大数量 (10)
             ↓
    输出: AggregationResult

合并策略:
    - 分组键: 标准化后的 title + artist（去除大小写、特殊字符、Unicode规范化）
    - 置信度: 取同组候选中的最大值
    - 来源列表: 合并所有数据源
    - 字段值: 优先取置信度最高的候选的字段

设计目的:
    1. 去重: 多个数据源可能返回相同的歌曲，需要合并
    2. 提升置信度: 多个来源确认同一结果 → 更可信
    3. 字段补全: 不同数据源可能提供不同字段，合并后更完整
    4. 控制数量: 避免返回过多候选，影响用户体验
    5. 减少候选数量，避免 AI 决策处理过多低质量数据

示例:
    输入:
        Candidate(title="Yesterday", artist="The Beatles", confidence=0.85, source="musicbrainz")
        Candidate(title="Yesterday", artist="Beatles", confidence=0.72, source="discogs")
    
    合并后:
        Candidate(
            title="Yesterday",
            artist="The Beatles",
            confidence=0.85,
            sources=["musicbrainz", "discogs"]
        )

    AggregationResult(
        candidates=[...],      # 最终候选列表
        total_sources=2,       # 参与的数据源数量
        merge_count=1,         # 合并次数 (原始数量 - 合并后数量)
        confidence_avg=0.85    # 平均置信度
    )
)    
"""

import logging
import unicodedata
from typing import List, Dict, Any, Optional
from dataclasses import dataclass, field

from data_sources.base import Candidate, DataSourceType

logger = logging.getLogger(__name__)


@dataclass
class AggregationResult:
    """聚合结果
    
    Attributes:
        candidates: 聚合后的候选列表
        total_sources: 参与聚合的数据源数量
        merge_count: 合并次数
        confidence_avg: 平均置信度
    """
    candidates: List[Candidate] = field(default_factory=list)
    total_sources: int = 0
    merge_count: int = 0
    confidence_avg: float = 0.0


class CandidateAggregator:
    """候选聚合器
    
    聚合来自多个数据源的候选结果，实现去重、合并、排序。
    
    常量:
        MIN_CONFIDENCE_THRESHOLD: 最小置信度阈值 (0.3)，低于此值的候选将被过滤
        MAX_CANDIDATES: 最大返回候选数量 (10)
    
    主要方法:
        aggregate(): 主入口，执行完整聚合流程
        get_best_candidate(): 获取最佳候选
    """
    
    MIN_CONFIDENCE_THRESHOLD = 0.3
    MAX_CANDIDATES = 10
    
    def aggregate(self, candidates: List[Candidate]) -> AggregationResult:
        """聚合候选列表 - 主入口方法
        
        执行完整的聚合流程:
            1. 统计参与的数据源数量
            2. 按标准化 key (title + artist) 分组
            3. 合并同组候选（置信度取最大值，来源列表合并）
            4. 过滤低置信度候选 (< MIN_CONFIDENCE_THRESHOLD)
            5. 按置信度 + 来源数量排序
            6. 限制返回数量 (MAX_CANDIDATES)
        
        Args:
            candidates: 来自各数据源的候选列表
        
        Returns:
            AggregationResult: 聚合结果，包含:
                - candidates: 最终候选列表
                - total_sources: 参与的数据源数量
                - merge_count: 合并次数 (原始数量 - 合并后数量)
                - confidence_avg: 平均置信度
        """
        logger.debug(f"CandidateAggregator::aggregate: input {len(candidates)} candidates")
        
        if not candidates:
            return AggregationResult(
                candidates=[],
                total_sources=0,
                merge_count=0,
                confidence_avg=0.0
            )
        
        sources = set()
        for c in candidates:
            sources.add(c.source)
        
        logger.debug(f"[Data] Before merge: {len(candidates)} candidates from {len(sources)} sources")
        merged = self._merge_candidates(candidates)
        logger.debug(f"[Data] After merge: {len(merged)} candidates")
        
        filtered = [c for c in merged if c.confidence >= self.MIN_CONFIDENCE_THRESHOLD]
        logger.debug(f"[Data] After filter (threshold={self.MIN_CONFIDENCE_THRESHOLD}): {len(filtered)} candidates")
        
        sorted_candidates = sorted(
            filtered,
            key=lambda c: (c.confidence, len(c.sources)),
            reverse=True
        )
        
        final_candidates = sorted_candidates[:self.MAX_CANDIDATES]
        
        confidence_avg = 0.0
        if final_candidates:
            confidence_avg = sum(c.confidence for c in final_candidates) / len(final_candidates)
        
        merge_count = len(candidates) - len(merged)
        
        logger.debug(
            f"CandidateAggregator: aggregated {len(candidates)} -> {len(final_candidates)} candidates, "
            f"sources={len(sources)}, merges={merge_count}, avg_conf={confidence_avg:.2f}"
        )
        
        return AggregationResult(
            candidates=final_candidates,
            total_sources=len(sources),
            merge_count=merge_count,
            confidence_avg=confidence_avg
        )
    
    def _merge_candidates(self, candidates: List[Candidate]) -> List[Candidate]:
        """合并相似候选 - 内部方法
        
        按 title + artist 标准化后的 key 分组，同组候选合并为一个。
        
        Args:
            candidates: 候选列表
        
        Returns:
            List[Candidate]: 合并后的候选列表
        """
        groups: Dict[str, List[Candidate]] = {}
        
        for candidate in candidates:
            key = self._get_merge_key(candidate)
            
            if key not in groups:
                groups[key] = []
            groups[key].append(candidate)
        
        merged = []
        for key, group in groups.items():
            if len(group) == 1:
                merged.append(group[0])
            else:
                merged_candidate = self._merge_group(group)
                merged.append(merged_candidate)
        
        return merged
    
    def _get_merge_key(self, candidate: Candidate) -> str:
        """生成合并键 - 内部方法
        
        标准化 title + artist 作为分组依据，确保相似候选能被正确分组。
        
        标准化步骤:
            1. Unicode NFKD 规范化（分解兼容字符）
            2. 转小写
            3. 移除非字母数字字符（保留空格）
            4. 合并连续空格
        
        Args:
            candidate: 候选对象
        
        Returns:
            str: 标准化后的合并键，格式为 "{title}|{artist}"
        """
        def normalize(s: str) -> str:
            if not s:
                return ""
            s = unicodedata.normalize('NFKD', s)
            s = s.lower()
            s = ''.join(c for c in s if c.isalnum() or c.isspace())
            return ' '.join(s.split())
        
        title_key = normalize(candidate.title)
        artist_key = normalize(candidate.artist)
        
        return f"{title_key}|{artist_key}"
    
    def _merge_group(self, group: List[Candidate]) -> Candidate:
        """合并一组相似候选 - 内部方法
        
        将同一分组内的多个候选合并为一个，保留最佳信息。
        
        合并策略:
            - 置信度: 取最大值
            - 来源列表: 合并所有数据源（去重）
            - 匹配分数: 取最大值
            - 字段值: 优先取置信度最高候选的字段
        
        Args:
            group: 相似候选组（已按 title + artist 分组）
        
        Returns:
            Candidate: 合并后的候选
        """
        sorted_group = sorted(group, key=lambda c: c.confidence, reverse=True)
        
        best = sorted_group[0]
        
        all_sources = []
        for c in group:
            if c.source not in all_sources:
                all_sources.append(c.source)
        
        max_confidence = max(c.confidence for c in group)
        
        merged = Candidate(
            title=best.title,
            artist=best.artist,
            album=self._pick_best_field(group, "album"),
            year=self._pick_best_field(group, "year"),
            track_number=self._pick_best_field(group, "track_number"),
            disc_number=self._pick_best_field(group, "disc_number"),
            genre=self._pick_best_field(group, "genre"),
            composer=self._pick_best_field(group, "composer"),
            lyricist=self._pick_best_field(group, "lyricist"),
            label=self._pick_best_field(group, "label"),
            country=self._pick_best_field(group, "country"),
            catalog_number=self._pick_best_field(group, "catalog_number"),
            musicbrainz_id=best.musicbrainz_id,
            source=best.source,
            confidence=max_confidence,
            match_score=max(c.match_score for c in group),
            sources=all_sources,
            raw={"merged_from": [c.raw for c in group if c.raw]}
        )
        
        return merged
    
    def _pick_best_field(self, candidates: List[Candidate], field_name: str) -> str:
        """从候选组中选择最佳字段值 - 内部方法
        
        按置信度从高到低遍历，返回第一个非空的字段值。
        这确保了高置信度候选的字段优先被采用。
        
        Args:
            candidates: 候选组
            field_name: 字段名 (如 "album", "year", "genre" 等)
        
        Returns:
            str: 最佳字段值，若所有候选该字段都为空则返回空字符串
        """
        sorted_candidates = sorted(candidates, key=lambda c: c.confidence, reverse=True)
        
        for candidate in sorted_candidates:
            value = getattr(candidate, field_name, "")
            if value:
                return value
        
        return ""
    
    def get_best_candidate(self, candidates: List[Candidate]) -> Optional[Candidate]:
        """获取最佳候选
        
        从候选列表中返回置信度最高的候选。
        
        Args:
            candidates: 候选列表
        
        Returns:
            Optional[Candidate]: 最佳候选，无候选返回 None
        """
        if not candidates:
            return None
        
        sorted_candidates = sorted(candidates, key=lambda c: c.confidence, reverse=True)
        return sorted_candidates[0]
    
    def filter_by_confidence(self, candidates: List[Candidate],
                              min_confidence: float) -> List[Candidate]:
        """按置信度过滤候选
        
        Args:
            candidates: 候选列表
            min_confidence: 最小置信度
        
        Returns:
            List[Candidate]: 过滤后的候选列表
        """
        return [c for c in candidates if c.confidence >= min_confidence]
    
    def get_candidates_by_source(self, candidates: List[Candidate],
                                  source: DataSourceType) -> List[Candidate]:
        """获取指定数据源的候选
        
        Args:
            candidates: 候选列表
            source: 数据源类型
        
        Returns:
            List[Candidate]: 指定数据源的候选列表
        """
        return [c for c in candidates if c.source == source or source in c.sources]
