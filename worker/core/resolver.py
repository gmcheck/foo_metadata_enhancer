#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
AI Resolver
AI-powered decision layer for metadata selection from candidates
"""

import json
import logging
import threading
from typing import List, Dict, Any, Optional
from dataclasses import dataclass

from data_sources.base import Candidate, QueryInput, DataSourceType, FinalResult
from common.text_utils import clean_value
from prompts import get_composer

logger = logging.getLogger(__name__)


@dataclass
class ResolveContext:
    """决策上下文
    
    Attributes:
        query: 原始查询输入
        candidates: 候选列表
        enhanced_mode: 是否启用增强模式
    """
    query: QueryInput
    candidates: List[Candidate]
    enhanced_mode: bool = False


class AIResolver:
    """AI 决策层
    
    职责：
    1. 从多个候选中选择最佳结果
    2. 补全缺失字段
    3. 纠正错误信息
    4. 生成最终元数据
    
    核心方法为 resolve_batch，支持批量处理多首歌曲。
    单首歌曲处理是批量的特例（N=1）。
    """
    
    def __init__(self, config: Dict[str, Any]):
        """初始化 AI 决策层
        
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
            logger.info(f"AIResolver initialized with provider: {self._model_adapter.get_provider_info()}")
        except Exception as e:
            logger.error(f"Failed to initialize AIResolver: {e}")
            self._model_adapter = None
    
    @property
    def is_available(self) -> bool:
        """AI 决策层是否可用"""
        return self._model_adapter is not None
    
    def resolve(self, query: QueryInput, candidates: List[Candidate]) -> FinalResult:
        """从候选中选择最佳结果（单首歌曲）
        
        内部调用 resolve_batch，是批量的特例（N=1）
        
        Args:
            query: 原始查询输入
            candidates: 候选列表
        
        Returns:
            FinalResult: 最终元数据结果
        """
        results = self.resolve_batch([query], [candidates], enhanced=False)
        return results[0] if results else self._create_empty_result(query, query.raw_data)
    
    def resolve_enhanced(self, query: QueryInput, candidates: List[Candidate]) -> FinalResult:
        """增强模式决策（单首歌曲）
        
        内部调用 resolve_batch，是批量的特例（N=1）
        
        Args:
            query: 原始查询输入
            candidates: 候选列表
        
        Returns:
            FinalResult: 最终元数据结果
        """
        results = self.resolve_batch([query], [candidates], enhanced=True)
        return results[0] if results else self._create_empty_result(query, query.raw_data)
    
    def resolve_batch(self, 
                      queries: List[QueryInput], 
                      candidates_list: List[List[Candidate]],
                      enhanced: bool = False) -> List[FinalResult]:
        """批量从候选中选择最佳结果
        
        核心方法：一次 AI 调用处理多首歌曲
        
        Args:
            queries: 原始查询输入列表
            candidates_list: 每首歌曲对应的候选列表
            enhanced: 是否增强模式
        
        Returns:
            List[FinalResult]: 最终元数据结果列表
        """
        import time as _time
        _t0 = _time.time()
        logger.info(f"AIResolver::resolve_batch: BEGIN, {len(queries)} tracks, enhanced={enhanced}")

        if not queries:
            return []

        for i, (query, candidates) in enumerate(zip(queries, candidates_list)):
            if not candidates:
                candidates_list[i] = []

        if not self.is_available:
            logger.warning("AI resolver not available, using best candidate fallback")
            return [self._select_best_candidate(q, c) for q, c in zip(queries, candidates_list)]

        try:
            messages = self._build_batch_resolve_prompt(queries, candidates_list, enhanced)

            logger.info(f"AIResolver::resolve_batch: Calling model_adapter.analyze, messages={len(messages)}")

            # 启动心跳线程：AI 调用是阻塞的，期间定期向 C++ 端发 progress，
            # 避免触发 max_silence_time_ms 误判导致 worker 被强制重启。
            heartbeat_stop = threading.Event()
            heartbeat_interval_s = 10.0  # 每 10 秒发一次

            def _heartbeat():
                seq = 0
                while not heartbeat_stop.wait(heartbeat_interval_s):
                    seq += 1
                    try:
                        from ipc_utils import write_progress as _wp
                        _wp(
                            0.8,
                            f"AI calling (heartbeat #{seq}, elapsed={int(_time.time()-_t0)}s, do not restart)"
                        )
                    except Exception:
                        pass

            hb_thread = threading.Thread(target=_heartbeat, daemon=True, name="ai-heartbeat")
            hb_thread.start()

            try:
                result = self._model_adapter.analyze(messages)
            finally:
                heartbeat_stop.set()
                hb_thread.join(timeout=2.0)

            logger.info(
                f"AIResolver::resolve_batch: model_adapter.analyze returned, "
                f"success={result.success}, "
                f"tokens: total={result.tokens_used} prompt={result.prompt_tokens} "
                f"completion={result.completion_tokens} reasoning={result.reasoning_tokens}, "
                f"tracks={len(queries)}, enhanced={enhanced}, "
                f"elapsed_ms={int((_time.time()-_t0)*1000)}"
            )
            # 仅当 reasoning_tokens 异常大时才告警（>5000，约 3x prompt）。
            # 像 deepseek-v4-flash 这类带轻度推理的模型通常 <500 token，属正常范围。
            # 真正需要警惕的是 R1/o1 系，单次 reasoning 可达 5w-10w。
            if result.success and result.reasoning_tokens > 5000:
                logger.warning(
                    f"AIResolver::resolve_batch: reasoning_tokens={result.reasoning_tokens} "
                    f"detected (model={result.model}). Reasoning model used for scrape decision "
                    f"may cause excessive token consumption. Consider switching to a non-reasoning model."
                )

            if not result.success:
                logger.warning(f"AI resolve_batch failed: {result.error}")
                return [self._select_best_candidate(q, c) for q, c in zip(queries, candidates_list)]

            parsed_results = self._parse_batch_resolve_result(result.result, queries, candidates_list)
            logger.info(f"AIResolver::resolve_batch: END, {len(parsed_results)}/{len(queries)} results, total_elapsed_ms={int((_time.time()-_t0)*1000)}")
            return parsed_results

        except Exception as e:
            logger.error(f"AIResolver resolve_batch error: {e}")
            return [self._select_best_candidate(q, c) for q, c in zip(queries, candidates_list)]
    
    def _build_batch_resolve_prompt(self, 
                                     queries: List[QueryInput], 
                                     candidates_list: List[List[Candidate]],
                                     enhanced: bool = False) -> List[Dict[str, str]]:
        """构建批量决策提示
        
        Args:
            queries: 原始查询输入列表
            candidates_list: 每首歌曲对应的候选列表
            enhanced: 是否增强模式
        
        Returns:
            List[Dict]: 消息列表
        """
        system_prompt = get_composer(self._config).build_scrape_system_prompt(enhanced=enhanced)
        
        tracks_data = []
        for i, (query, candidates) in enumerate(zip(queries, candidates_list)):
            query_info = {
                "track_id": query.track_id or f"track_{i}",
                "title": query.title,
                "artist": query.artist,
                "album": query.album,
                "duration": query.duration
            }
            
            candidates_info = []
            for j, c in enumerate(candidates[:5]):
                candidates_info.append({
                    "index": j + 1,
                    "title": c.title,
                    "artist": c.artist,
                    "album": c.album,
                    "year": c.year,
                    "genre": c.genre,
                    "track_number": c.track_number,
                    "disc_number": c.disc_number,
                    "composer": c.composer,
                    "lyricist": c.lyricist,
                    "label": c.label,
                    "country": c.country,
                    "musicbrainz_id": c.musicbrainz_id,
                    "source": c.source.value if isinstance(c.source, DataSourceType) else c.source,
                    "confidence": c.confidence,
                    "sources": [s.value if isinstance(s, DataSourceType) else s for s in c.sources]
                })
            
            tracks_data.append({
                "track_id": query_info["track_id"],
                "query": query_info,
                "candidates": candidates_info,
                "total_candidates": len(candidates)
            })
        
        user_content = f"""Process the following {len(tracks_data)} tracks and provide metadata for each:

{json.dumps(tracks_data, indent=2, ensure_ascii=False)}

Remember:
- Return a JSON array with exactly {len(tracks_data)} results
- Each result must have a track_id matching the input
- Provide confidence score (0.0-1.0) for each track"""
        
        return [
            {"role": "system", "content": system_prompt},
            {"role": "user", "content": user_content}
        ]
    
    def _parse_batch_resolve_result(self, 
                                     result: Any,
                                     queries: List[QueryInput], 
                                     candidates_list: List[List[Candidate]]) -> List[FinalResult]:
        """解析批量 AI 决策结果
        
        Args:
            result: AI 返回的 JSON 结果
            queries: 原始查询输入列表
            candidates_list: 每首歌曲对应的候选列表
        
        Returns:
            List[FinalResult]: 最终元数据结果列表
        """
        if not result:
            logger.warning("Empty batch result, using fallback")
            return [self._select_best_candidate(q, c) for q, c in zip(queries, candidates_list)]
        
        if isinstance(result, dict):
            if "results" in result:
                results_list = result["results"]
            else:
                results_list = [result]
        elif isinstance(result, list):
            results_list = result
        else:
            logger.warning(f"Unexpected result type: {type(result)}, using fallback")
            return [self._select_best_candidate(q, c) for q, c in zip(queries, candidates_list)]
        
        query_map = {q.track_id or f"track_{i}": (q, c) for i, (q, c) in enumerate(zip(queries, candidates_list))}
        
        final_results = []
        for i, (query, candidates) in enumerate(zip(queries, candidates_list)):
            track_id = query.track_id or f"track_{i}"
            matched_result = None
            
            for r in results_list:
                if isinstance(r, dict) and r.get("track_id") == track_id:
                    matched_result = r
                    break
            
            if matched_result:
                final_results.append(self._parse_single_result(matched_result, query, candidates))
            else:
                logger.warning(f"No matching result for track_id={track_id}, using fallback")
                final_results.append(self._select_best_candidate(query, candidates))
        
        return final_results
    
    def _parse_single_result(self, 
                             result: Dict[str, Any],
                             query: QueryInput, 
                             candidates: List[Candidate]) -> FinalResult:
        """解析单个结果
        
        Args:
            result: AI 返回的单个结果
            query: 原始查询输入
            candidates: 候选列表
        
        Returns:
            FinalResult: 最终元数据结果
        """
        def get_value(field: str, default: str = "") -> str:
            value = result.get(field, default)
            cleaned = clean_value(value)
            return cleaned if cleaned else default
        
        confidence = result.get("confidence", 0.7)
        if isinstance(confidence, str):
            try:
                confidence = float(confidence)
            except ValueError:
                confidence = 0.7
        
        sources = list(set(c.source for c in candidates if c.source)) if candidates else []
        
        best_candidate = None
        if candidates:
            sorted_candidates = sorted(candidates, key=lambda c: c.confidence, reverse=True)
            best_candidate = sorted_candidates[0]
        
        def get_value_with_source(field: str) -> tuple:
            """获取字段值及其来源
            
            如果 AI 返回的值与候选值匹配，则认为来源是候选的 source
            
            Returns:
                tuple: (value, source) - 值和来源类型
            """
            ai_value = get_value(field, "")
            candidate_value = ""
            candidate_source = DataSourceType.AI
            
            if best_candidate:
                candidate_value = getattr(best_candidate, field, "")
                if candidate_value:
                    candidate_value = str(candidate_value)
                    candidate_source = best_candidate.source
            
            if ai_value and candidate_value:
                ai_normalized = ai_value.lower().strip()
                candidate_normalized = candidate_value.lower().strip()
                if ai_normalized == candidate_normalized:
                    return (ai_value, candidate_source)
                return (ai_value, DataSourceType.AI)
            
            if ai_value:
                return (ai_value, DataSourceType.AI)
            
            if candidate_value:
                return (candidate_value, candidate_source)
            
            return ("", DataSourceType.AI)
        
        title, title_source = get_value_with_source("title")
        artist, artist_source = get_value_with_source("artist")
        album, album_source = get_value_with_source("album")
        year, year_source = get_value_with_source("year")
        track_number, _ = get_value_with_source("track_number")
        disc_number, _ = get_value_with_source("disc_number")
        composer, _ = get_value_with_source("composer")
        lyricist, _ = get_value_with_source("lyricist")
        label, _ = get_value_with_source("label")
        catalog_number, _ = get_value_with_source("catalog_number")
        
        mb_id, mb_source = get_value_with_source("musicbrainz_id")
        if not mb_id and best_candidate and best_candidate.musicbrainz_id:
            mb_id = best_candidate.musicbrainz_id
            mb_source = best_candidate.source
        
        genre = get_value("genre")
        country = get_value("country")
        
        primary_source = DataSourceType.AI
        if best_candidate and best_candidate.source != DataSourceType.AI:
            core_fields_from_candidate = 0
            for src in [title_source, artist_source, album_source, year_source]:
                if src != DataSourceType.AI:
                    core_fields_from_candidate += 1
            
            if core_fields_from_candidate >= 2:
                primary_source = best_candidate.source
        
        final_result = FinalResult(
            title=title or query.title,
            artist=artist or query.artist,
            album=album,
            year=year,
            track_number=track_number,
            disc_number=disc_number,
            genre=genre,
            composer=composer,
            lyricist=lyricist,
            label=label,
            country=country,
            catalog_number=catalog_number,
            musicbrainz_id=mb_id,
            confidence=min(confidence, 0.95),
            source=primary_source.value if isinstance(primary_source, DataSourceType) else str(primary_source),
            sources=[s.value if isinstance(s, DataSourceType) else str(s) for s in sources],
            is_fallback=False
        )
        
        reasoning = result.get("reasoning", "")
        logger.info(f"AIResolver decision: track_id={query.track_id}, confidence={confidence:.2f}, "
                   f"source={primary_source.value}, mb_id={mb_id[:20] if mb_id else 'N/A'}...")
        
        return final_result
    
    def _select_best_candidate(self, query: QueryInput, candidates: List[Candidate]) -> FinalResult:
        """选择最佳候选作为结果
        
        当 AI 不可用时的降级策略
        
        Args:
            query: 原始查询输入
            candidates: 候选列表
        
        Returns:
            FinalResult: 最终元数据结果
        """
        if not candidates:
            return self._create_empty_result(query, query.raw_data)
        
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
            source=best.source.value if isinstance(best.source, DataSourceType) else str(best.source),
            sources=[s.value if isinstance(s, DataSourceType) else str(s) for s in sources],
            is_fallback=False
        )
    
    def _create_empty_result(self, query: QueryInput, track_data: Optional[Dict[str, Any]] = None) -> FinalResult:
        """创建空结果
        
        Args:
            query: 原始查询输入
            track_data: 原始音轨数据（用于保留更多字段）
        
        Returns:
            FinalResult: 空结果
        """
        result = FinalResult(
            title=query.title,
            artist=query.artist,
            album=query.album,
            confidence=0.0,
            source=DataSourceType.AI.value,
            sources=[],
            is_fallback=True
        )
        
        if track_data:
            if track_data.get("year"):
                result.year = str(track_data.get("year", ""))
            if track_data.get("genre"):
                result.genre = str(track_data.get("genre", ""))
            if track_data.get("composer"):
                result.composer = str(track_data.get("composer", ""))
            if track_data.get("lyricist"):
                result.lyricist = str(track_data.get("lyricist", ""))
            if track_data.get("label"):
                result.label = str(track_data.get("label", ""))
            if track_data.get("track_number"):
                result.track_number = str(track_data.get("track_number", ""))
            if track_data.get("disc_number"):
                result.disc_number = str(track_data.get("disc_number", ""))
            if track_data.get("musicbrainz_id"):
                result.musicbrainz_id = str(track_data.get("musicbrainz_id", ""))
        
        return result
