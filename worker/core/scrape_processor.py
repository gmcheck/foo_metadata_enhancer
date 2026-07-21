#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Stage 1 Processor (Scrape Layer)
从外部数据源获取本地没有的数据（事实获取）。

V8.2 三功能边界：
  - 本处理器属于 Scrape 层。
  - 数据源：MusicBrainz / Discogs / AI 降级。
  - 产物：title / artist / album / year / genre / composer / ... / musicbrainz_id。
  - V8.2 变更：genre 改由本层从 MusicBrainz recording 详情获取。
  - 不做：翻译（归 Enhancer）、归一化（归 Normalize）。

V8.1 Architecture:
    Cache → DataSourceManager(并发) → Aggregator → AIResolver
"""

import logging
import json
from dataclasses import dataclass
from typing import Dict, Any, Optional, List

# 进度上报（避免 C++ 端 max_silence_time_ms 误判）
# 必须从 ipc_utils 导入：ai_worker.py 作为 __main__ 运行，无法被 import
from ipc_utils import write_progress as _write_progress

from data_sources import (
    DataSourceManager,
    DataSourceType,
    ScrapingOptions,
    QueryInput,
    Candidate,
)
from .aggregator import CandidateAggregator
from .resolver import AIResolver, FinalResult
from .types import TrackInput
from common.result_formatter import ResultFormatter
from abort_checker import is_aborted
from common.models import (
    ScrapeResultModel,
    create_scrape_result,
    create_scrape_error_result
)

logger = logging.getLogger(__name__)


@dataclass
class MissingFieldInfo:
    """缺失字段信息"""
    track_id: str
    missing_fields: List[str]


@dataclass
class ScrapingResult:
    """刮削结果（V8.1新结构）"""
    track_id: str
    success: bool
    final: Optional[FinalResult] = None
    candidates: List[Candidate] = None
    fallback_level: str = "normal"
    fallback_used: bool = False
    cache_hit: bool = False
    error: Optional[str] = None
    
    def __post_init__(self):
        if self.candidates is None:
            self.candidates = []
    
    def to_dict(self) -> Dict[str, Any]:
        """转换为字典 - 使用 Pydantic 验证格式"""
        if not self.success or not self.final:
            error_result = create_scrape_error_result(self.track_id, self.error or "Unknown error")
            return error_result.to_cpp_dict()
        
        scraped_fields = {}
        for field_name in ["title", "artist", "album", "year", "genre", 
                           "composer", "lyricist", "label", "country",
                           "track_number", "disc_number", "catalog_number",
                           "musicbrainz_id", "conductor", "performer",
                           "mood", "bpm"]:
            value = getattr(self.final, field_name, "")
            if value:
                source = self.final.source.value if isinstance(self.final.source, DataSourceType) else self.final.source
                scraped_fields[field_name] = {
                    "value": str(value),
                    "confidence": self.final.confidence,
                    "source": source
                }
        
        release_source = self.final.source.value if isinstance(self.final.source, DataSourceType) else self.final.source
        
        pydantic_result = create_scrape_result(
            track_id=self.track_id,
            success=True,
            scraped_fields=scraped_fields,
            release_source=release_source
        )
        
        result = pydantic_result.to_cpp_dict()
        
        result["final"] = self.final.to_dict()
        result["candidates"] = [
            {
                "title": c.title,
                "artist": c.artist,
                "album": c.album,
                "year": c.year,
                "source": c.source.value if isinstance(c.source, DataSourceType) else c.source,
                "confidence": c.confidence
            }
            for c in self.candidates[:5]
        ]
        result["fallback_context"] = {
            "level": self.fallback_level,
            "fallback_used": self.fallback_used,
            "candidate_count": len(self.candidates)
        }
        result["cache_hit"] = self.cache_hit
        
        if self.error:
            result["error"] = self.error
        
        logger.debug(f"ScrapingResult::to_dict: track_id={self.track_id}, "
                    f"scraped_fields={json.dumps(scraped_fields, ensure_ascii=False)}, "
                    f"release_source={release_source}")
        
        return result


class ScrapeProcessor:
    """阶段一处理器

    V8.1 数据流架构：
    Cache → DataSourceManager(并发) → Aggregator → AIResolver

    功能：
    1. 前置检查：TITLE 和 ARTIST 必须存在
    2. 缓存优先查询
    3. 并发数据源查询
    4. 候选聚合
    5. AI 决策
    """

    REQUIRED_FIELDS = ["title", "artist"]

    # 高质量匹配阈值：与 DataSourceManager.HIGH_QUALITY_CONFIDENCE 一致。
    # 候选中已有 confidence >= 此值时，直接选用最佳候选，跳过 AIResolver。
    HIGH_QUALITY_CONFIDENCE = 0.85
    
    def __init__(self, config: Dict[str, Any], backup_db_path: str = None):
        """初始化阶段一处理器
        
        Args:
            config: 配置字典
            backup_db_path: 备份数据库路径（已废弃，保留参数兼容性）
        """
        self._config = config
        
        self._data_source_manager = DataSourceManager(config)
        self._data_source_manager.set_abort_checker(is_aborted)
        self._aggregator = CandidateAggregator()
        self._resolver = AIResolver(config)
        self._result_formatter = ResultFormatter(config)
        
        logger.info("ScrapeProcessor initialized with V8.1 architecture")
    
    def validate_tracks(self, tracks: List[TrackInput]) -> List[MissingFieldInfo]:
        """验证音轨是否满足前置条件
        
        Args:
            tracks: 音轨列表
        
        Returns:
            List[MissingFieldInfo]: 缺失字段的音轨列表
        """
        missing_list = []
        
        for track in tracks:
            missing_fields = []
            
            if not track.title or not track.title.strip():
                missing_fields.append("title")
            
            if not track.artist or not track.artist.strip():
                missing_fields.append("artist")
            
            if missing_fields:
                missing_list.append(MissingFieldInfo(
                    track_id=track.track_id,
                    missing_fields=missing_fields
                ))
        
        return missing_list
    
    def scrape(self, tracks: List[TrackInput], 
               options: ScrapingOptions) -> List[ScrapingResult]:
        """执行阶段一处理（批量 AI 决策优化版）
        
        优化策略：
        1. 逐首处理 Cache、DataSourceManager、Aggregator（快速操作）
        2. 根据候选数量分组：
           - 候选 >= 3: 正常模式批量 AI 决策
           - 候选 1-2: 增强模式批量 AI 决策
           - 候选 0: AI 推断（单独处理或合并到增强模式）
        3. 批量调用 AIResolver.resolve_batch（减少 API 调用次数）
        
        Args:
            tracks: 音轨列表
            options: 刮削选项
        
        Returns:
            List[ScrapingResult]: 刮削结果列表
        """
        logger.info(
            f"ScrapeProcessor::scrape: BEGIN, tracks={len(tracks)}, "
            f"data_sources={options.data_sources if hasattr(options, 'data_sources') else 'all'}"
        )

        missing = self.validate_tracks(tracks)
        if missing:
            logger.warning(
                f"ScrapeProcessor::scrape: {len(missing)}/{len(tracks)} tracks have missing required fields, "
                f"first_missing={missing[0].missing_fields if missing else []}"
            )
            return [
                ScrapingResult(
                    track_id=m.track_id,
                    success=False,
                    error=f"Missing required fields: {', '.join(m.missing_fields)}"
                )
                for m in missing
            ]

        results = [None] * len(tracks)

        pending_normal = []
        pending_normal_indices = []
        pending_enhanced = []
        pending_enhanced_indices = []

        for i, track in enumerate(tracks):
            if is_aborted():
                logger.warning(f"ScrapeProcessor::scrape: ABORTED at track {i+1}/{len(tracks)}")
                break

            result = self._scrape_single_prepare(track, options)

            if result is not None:
                results[i] = result
            else:
                query = track.to_query_input()
                logger.info(
                    f"ScrapeProcessor::scrape: track {i+1}/{len(tracks)} "
                    f"track_id={track.track_id[:16]}, query title='{query.title[:40]}', "
                    f"artist='{query.artist[:40]}', album='{query.album[:40]}'"
                )
                _write_progress(
                    float(i) / len(tracks) * 0.5,
                    f"track {i+1}/{len(tracks)}: fetching data sources"
                )
                raw_candidates = self._data_source_manager.fetch_all(query, options)
                logger.info(
                    f"ScrapeProcessor::scrape: track {i+1} fetch_all done, "
                    f"raw_candidates={len(raw_candidates)}"
                )
                aggregation_result = self._aggregator.aggregate(raw_candidates)
                candidates = aggregation_result.candidates
                logger.info(
                    f"ScrapeProcessor::scrape: track {i+1} aggregate done, "
                    f"candidates={len(candidates)}"
                )

                # 已有高质量匹配时（如 MB 通过 MatchDecision 早停），直接选用最佳候选，
                # 跳过 AIResolver。避免 MB 已给出可信结果仍调用 AI 的浪费。
                if candidates and max(c.confidence for c in candidates) >= self.HIGH_QUALITY_CONFIDENCE:
                    logger.info(
                        f"ScrapeProcessor::scrape: track {i+1} high-quality match found "
                        f"(best_conf={max(c.confidence for c in candidates):.2f}), "
                        f"selecting best candidate directly without AI"
                    )
                    results[i] = self._build_direct_result(track, query, candidates)
                elif len(candidates) >= 3:
                    pending_normal.append((track, query, candidates))
                    pending_normal_indices.append(i)
                else:
                    pending_enhanced.append((track, query, candidates))
                    pending_enhanced_indices.append(i)

        if pending_normal:
            logger.info(
                f"ScrapeProcessor::scrape: batch NORMAL resolve, "
                f"tracks={len(pending_normal)}, indices={pending_normal_indices}"
            )
            queries = [item[1] for item in pending_normal]
            candidates_list = [item[2] for item in pending_normal]

            _write_progress(
                0.5,
                f"AI resolve (normal): {len(pending_normal)} tracks, may take 1-3 min"
            )
            final_results = self._resolver.resolve_batch(queries, candidates_list, enhanced=False)
            _write_progress(
                0.7,
                f"AI resolve (normal) done: {len(final_results)} results"
            )
            logger.info(
                f"ScrapeProcessor::scrape: normal resolve_batch returned "
                f"{len(final_results)} results"
            )

            for idx, (final_result, (track, query, candidates)) in enumerate(zip(final_results, pending_normal)):
                i = pending_normal_indices[idx]

                results[i] = ScrapingResult(
                    track_id=track.track_id,
                    success=True,
                    final=final_result,
                    candidates=candidates,
                    fallback_level="normal",
                    fallback_used=False,
                    cache_hit=False
                )

        if pending_enhanced:
            logger.info(
                f"ScrapeProcessor::scrape: batch ENHANCED resolve, "
                f"tracks={len(pending_enhanced)}, indices={pending_enhanced_indices}"
            )
            queries = [item[1] for item in pending_enhanced]
            candidates_list = [item[2] for item in pending_enhanced]

            _write_progress(
                0.7,
                f"AI resolve (enhanced): {len(pending_enhanced)} tracks, may take 1-3 min"
            )
            final_results = self._resolver.resolve_batch(queries, candidates_list, enhanced=True)
            _write_progress(
                0.95,
                f"AI resolve (enhanced) done: {len(final_results)} results"
            )
            logger.info(
                f"ScrapeProcessor::scrape: enhanced resolve_batch returned "
                f"{len(final_results)} results"
            )
            
            for idx, (final_result, (track, query, candidates)) in enumerate(zip(final_results, pending_enhanced)):
                i = pending_enhanced_indices[idx]
                
                if len(candidates) >= 1:
                    fallback_level = "enhanced"
                    fallback_used = final_result.is_fallback
                else:
                    fallback_level = "ai_infer"
                    fallback_used = True
                
                results[i] = ScrapingResult(
                    track_id=track.track_id,
                    success=True,
                    final=final_result,
                    candidates=candidates,
                    fallback_level=fallback_level,
                    fallback_used=fallback_used,
                    cache_hit=False
                )
        
        return results
    
    def _build_direct_result(self, track: TrackInput, query: QueryInput,
                             candidates: List[Candidate]) -> ScrapingResult:
        """已有高质量匹配时，直接选最佳候选构造结果（不调用 AI）

        MB 已通过 MatchDecision 判定找到高质量匹配时，无需再触发 AIResolver 决策。
        逻辑等价于 AIResolver._select_best_candidate，但避免一次 AI 调用。

        Args:
            track: 音轨输入
            query: 查询输入
            candidates: 候选列表（已含 confidence >= HIGH_QUALITY_CONFIDENCE 的项）

        Returns:
            ScrapingResult: 直接从最佳候选构造的结果
        """
        if not candidates:
            return ScrapingResult(
                track_id=track.track_id,
                success=False,
                error="No candidates available"
            )

        sorted_candidates = sorted(candidates, key=lambda c: c.confidence, reverse=True)
        best = sorted_candidates[0]

        sources = best.sources if best.sources else [best.source]

        final_result = FinalResult(
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
            source=best.source.value if isinstance(best.source, DataSourceType) else str(best.source),
            sources=[s.value if isinstance(s, DataSourceType) else str(s) for s in sources],
            is_fallback=False
        )

        logger.info(
            f"ScrapeProcessor::_build_direct_result: track_id={track.track_id}, "
            f"source={best.source.value if isinstance(best.source, DataSourceType) else best.source}, "
            f"confidence={best.confidence:.2f}, skipped AI call"
        )

        return ScrapingResult(
            track_id=track.track_id,
            success=True,
            final=final_result,
            candidates=candidates,
            fallback_level="direct",
            fallback_used=False,
            cache_hit=False
        )

    def _scrape_single_prepare(self, track: TrackInput,
                                options: ScrapingOptions) -> Optional[ScrapingResult]:
        """处理准备阶段（Python端缓存已移除，始终返回None）
        
        Args:
            track: 音轨输入
            options: 刮削选项
        
        Returns:
            Optional[ScrapingResult]: 始终返回 None，需要进一步处理
        """
        return None
    
    def get_source_status(self) -> Dict[str, Dict[str, Any]]:
        """获取数据源状态
        
        Returns:
            Dict: 数据源状态
        """
        return self._data_source_manager.get_source_status()
    
    def process_batch(self, tracks: List[Dict[str, Any]], 
                      options: Dict[str, Any]) -> List[Dict[str, Any]]:
        """批量处理音轨（IPC接口）
        
        Args:
            tracks: 音轨字典列表
            options: 选项字典
        
        Returns:
            List[Dict]: 结果字典列表
        """
        track_inputs = [TrackInput.from_dict(t) for t in tracks]
        scraping_options = ScrapingOptions.from_dict(options)
        
        results = self.scrape(track_inputs, scraping_options)
        
        return [r.to_dict() for r in results]
    
    def close(self):
        """关闭资源"""
        logger.info("ScrapeProcessor closed")
