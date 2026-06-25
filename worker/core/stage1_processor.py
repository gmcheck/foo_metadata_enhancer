#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Stage 1 Processor
Basic metadata processing and correction

V8.1 Architecture:
    Cache → DataSourceManager(并发) → Aggregator → AIResolver
"""

import logging
import json
from dataclasses import dataclass
from typing import Dict, Any, Optional, List

from data_sources import (
    DataSourceManager,
    DataSourceType,
    ScrapingOptions,
    QueryInput,
    Candidate,
)
from .aggregator import CandidateAggregator
from .resolver import AIResolver, FinalResult
from common.result_formatter import ResultFormatter
from abort_checker import is_aborted
from common.models import (
    Stage1ScrapingResultModel,
    create_stage1_scraping_result,
    create_stage1_error_result
)

logger = logging.getLogger(__name__)


@dataclass
class TrackInput:
    """音轨输入数据"""
    track_id: str
    title: str = ""
    artist: str = ""
    album: str = ""
    album_artist: str = ""
    year: str = ""
    genre: str = ""
    track_number: int = 0
    disc_number: int = 0
    duration_sec: int = 0
    comment: str = ""
    label: str = ""
    composer: str = ""
    lyricist: str = ""
    conductor: str = ""
    performer: str = ""
    musicbrainz_id: str = ""
    
    def to_dict(self) -> Dict[str, Any]:
        """转换为字典"""
        return {
            "track_id": self.track_id,
            "title": self.title,
            "artist": self.artist,
            "album": self.album,
            "album_artist": self.album_artist,
            "year": self.year,
            "genre": self.genre,
            "track_number": self.track_number,
            "disc_number": self.disc_number,
            "duration_sec": self.duration_sec,
            "comment": self.comment,
            "label": self.label,
            "composer": self.composer,
            "lyricist": self.lyricist,
            "conductor": self.conductor,
            "performer": self.performer,
            "musicbrainz_id": self.musicbrainz_id,
        }
    
    @classmethod
    def from_dict(cls, data: Dict[str, Any]) -> "TrackInput":
        """从字典创建"""
        return cls(
            track_id=data.get("track_id", ""),
            title=data.get("title", ""),
            artist=data.get("artist", ""),
            album=data.get("album", ""),
            album_artist=data.get("album_artist", ""),
            year=data.get("year", ""),
            genre=data.get("genre", ""),
            track_number=data.get("track_number", 0),
            disc_number=data.get("disc_number", 0),
            duration_sec=data.get("duration_sec", 0),
            comment=data.get("comment", ""),
            label=data.get("label", ""),
            composer=data.get("composer", ""),
            lyricist=data.get("lyricist", ""),
            conductor=data.get("conductor", ""),
            performer=data.get("performer", ""),
            musicbrainz_id=data.get("musicbrainz_id", ""),
        )
    
    def to_query_input(self) -> QueryInput:
        """转换为 QueryInput"""
        return QueryInput(
            track_id=self.track_id,
            title=self.title,
            artist=self.artist,
            album=self.album,
            duration=self.duration_sec,
            raw_data=self.to_dict()
        )


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
            error_result = create_stage1_error_result(self.track_id, self.error or "Unknown error")
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
        
        pydantic_result = create_stage1_scraping_result(
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


class Stage1Processor:
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
        
        logger.info("Stage1Processor initialized with V8.1 architecture")
    
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
        logger.debug(f"Stage1Processor::scrape: processing {len(tracks)} tracks")
        
        missing = self.validate_tracks(tracks)
        if missing:
            logger.warning(f"Found {len(missing)} tracks with missing required fields")
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
                logger.debug(f"Abort requested, stopping at track {i}/{len(tracks)}")
                break
            
            result = self._scrape_single_prepare(track, options)
            
            if result is not None:
                results[i] = result
            else:
                query = track.to_query_input()
                logger.debug(f"[Data] Before fetch_all: track_id={track.track_id}, title='{query.title}'")
                raw_candidates = self._data_source_manager.fetch_all(query, options)
                logger.debug(f"[Data] After fetch_all: track_id={track.track_id}, raw_candidates={len(raw_candidates)}")
                aggregation_result = self._aggregator.aggregate(raw_candidates)
                candidates = aggregation_result.candidates
                logger.debug(f"[Data] After aggregate: track_id={track.track_id}, candidates={len(candidates)}")
                
                if len(candidates) >= 3:
                    pending_normal.append((track, query, candidates))
                    pending_normal_indices.append(i)
                else:
                    pending_enhanced.append((track, query, candidates))
                    pending_enhanced_indices.append(i)
        
        if pending_normal:
            logger.debug(f"Batch normal resolve: {len(pending_normal)} tracks")
            queries = [item[1] for item in pending_normal]
            candidates_list = [item[2] for item in pending_normal]
            
            logger.debug(f"[Data] Before resolve_batch (normal): {len(queries)} queries")
            final_results = self._resolver.resolve_batch(queries, candidates_list, enhanced=False)
            logger.debug(f"[Data] After resolve_batch (normal): {len(final_results)} results")
            
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
            logger.debug(f"Batch enhanced resolve: {len(pending_enhanced)} tracks")
            queries = [item[1] for item in pending_enhanced]
            candidates_list = [item[2] for item in pending_enhanced]
            
            logger.debug(f"[Data] Before resolve_batch (enhanced): {len(queries)} queries")
            final_results = self._resolver.resolve_batch(queries, candidates_list, enhanced=True)
            logger.debug(f"[Data] After resolve_batch (enhanced): {len(final_results)} results")
            
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
        logger.info("Stage1Processor closed")
