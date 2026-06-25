#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Stage 2 Processor
Metadata enhancement including translation, genre classification, and edition identification

V8.1 Architecture:
    批量 AI 处理优化：一次 API 调用处理多首歌曲
"""

import json
import logging
from dataclasses import dataclass
from typing import Dict, Any, Optional, List

from data_sources.base import EnhancementOptions
from abort_checker import is_aborted
from common.text_utils import clean_value
from prompts import BATCH_ENHANCE_SYSTEM_PROMPT

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
            musicbrainz_id=data.get("musicbrainz_id", ""),
        )


@dataclass
class EnhancementResult:
    """增强结果"""
    track_id: str
    success: bool = False
    
    title_zh: str = ""
    album_zh: str = ""
    artist_zh: str = ""
    translation_confidence: float = 0.0
    
    genre_value: str = ""
    genre_confidence: float = 0.0
    
    edition_value: str = ""
    edition_confidence: float = 0.0
    
    model: str = ""
    model_type: str = ""
    tokens_used: int = 0
    
    error: Optional[str] = None
    
    def to_dict(self) -> Dict[str, Any]:
        """转换为字典"""
        return {
            "track_id": self.track_id,
            "success": self.success,
            "title_zh": self.title_zh,
            "album_zh": self.album_zh,
            "artist_zh": self.artist_zh,
            "translation_confidence": self.translation_confidence,
            "genre_value": self.genre_value,
            "genre_confidence": self.genre_confidence,
            "edition_value": self.edition_value,
            "edition_confidence": self.edition_confidence,
            "model": self.model,
            "model_type": self.model_type,
            "tokens_used": self.tokens_used,
            "error": self.error or "",
        }


class Stage2Processor:
    """阶段二处理器
    
    功能：
    1. 翻译元数据（title_zh, album_zh, artist_zh）
    2. 流派分类（genre）
    3. 版本识别（edition）
    
    核心方法为 enhance_batch，支持批量处理多首歌曲。
    单首歌曲处理是批量的特例（N=1）。
    """
    
    def __init__(self, config: Dict[str, Any], backup_db_path: str = None):
        """初始化阶段二处理器
        
        Args:
            config: 配置字典
            backup_db_path: 备份数据库路径（已废弃，保留参数兼容性）
        """
        self._config = config
        self._model_adapter = None
        
        self._init_model_adapter()
    
    def _init_model_adapter(self) -> None:
        """初始化模型适配器"""
        try:
            from ai.adapter import ModelAdapter
            self._model_adapter = ModelAdapter(self._config)
            logger.info(f"Stage2Processor initialized with provider: {self._model_adapter.get_provider_info()}")
        except Exception as e:
            logger.error(f"Failed to initialize model adapter: {e}")
    
    def enhance(self, tracks: List[TrackInput], 
                options: EnhancementOptions) -> List[EnhancementResult]:
        """执行阶段二增强（批量 AI 处理优化版）
        
        核心方法：一次 AI 调用处理多首歌曲
        
        Args:
            tracks: 音轨列表
            options: 增强选项
        
        Returns:
            List[EnhancementResult]: 增强结果列表
        """
        logger.debug(f"Stage2Processor::enhance: processing {len(tracks)} tracks")
        
        if not tracks:
            return []
        
        if is_aborted():
            logger.debug("Stage2Processor::enhance: Abort requested, returning empty results")
            return [
                EnhancementResult(
                    track_id=t.track_id,
                    success=False,
                    error="Aborted by user"
                )
                for t in tracks
            ]
        
        if not self._model_adapter:
            return [
                EnhancementResult(
                    track_id=t.track_id,
                    success=False,
                    error="Model adapter not initialized"
                )
                for t in tracks
            ]
        
        try:
            messages = self._build_batch_enhance_prompt(tracks, options)
            
            analysis_result = self._model_adapter.analyze(messages)
            
            if not analysis_result.success:
                return [
                    EnhancementResult(
                        track_id=t.track_id,
                        success=False,
                        error=analysis_result.error
                    )
                    for t in tracks
                ]
            
            return self._parse_batch_result(analysis_result.result, tracks, analysis_result)
        
        except Exception as e:
            logger.error(f"Batch enhancement failed: {e}")
            return [
                EnhancementResult(
                    track_id=t.track_id,
                    success=False,
                    error=str(e)
                )
                for t in tracks
            ]
    
    def _build_batch_enhance_prompt(self, tracks: List[TrackInput], 
                                     options: EnhancementOptions) -> List[Dict[str, str]]:
        """构建批量增强提示
        
        Args:
            tracks: 音轨列表
            options: 增强选项
        
        Returns:
            List[Dict]: 消息列表
        """
        tasks = []
        
        if options.translate_title or options.translate_album or options.translate_artist:
            tasks.append("- Chinese translations for title, album, and artist")
        
        if options.classify_genre:
            tasks.append("- Genre classification")
        
        if options.identify_edition:
            tasks.append("- Edition identification")
        
        tasks_text = "\n".join(tasks)
        
        tracks_data = []
        for i, track in enumerate(tracks):
            tracks_data.append({
                "track_id": track.track_id or f"track_{i}",
                "title": track.title,
                "artist": track.artist,
                "album": track.album,
                "album_artist": track.album_artist,
                "year": track.year,
                "genre": track.genre,
                "composer": track.composer,
                "label": track.label
            })
        
        user_content = f"""Analyze the following {len(tracks_data)} tracks and provide enhancement for each:

{json.dumps(tracks_data, indent=2, ensure_ascii=False)}

Please analyze and provide:
{tasks_text}

Remember:
- Return a JSON array with exactly {len(tracks_data)} results
- Each result must have a track_id matching the input
- translation_confidence: 0.0-1.0, use 0.0 if no translation needed"""

        return [
            {"role": "system", "content": BATCH_ENHANCE_SYSTEM_PROMPT},
            {"role": "user", "content": user_content}
        ]
    
    def _parse_batch_result(self, result: Any, tracks: List[TrackInput],
                            analysis_result: Any) -> List[EnhancementResult]:
        """解析批量 AI 返回结果
        
        Args:
            result: AI 返回的 JSON
            tracks: 原始音轨列表
            analysis_result: 分析结果对象
        
        Returns:
            List[EnhancementResult]: 增强结果列表
        """
        if not result:
            logger.warning("Empty batch result")
            return [
                EnhancementResult(
                    track_id=t.track_id,
                    success=False,
                    error="Empty AI response"
                )
                for t in tracks
            ]
        
        if isinstance(result, dict):
            if "results" in result:
                results_list = result["results"]
            else:
                results_list = [result]
        elif isinstance(result, list):
            results_list = result
        else:
            logger.warning(f"Unexpected result type: {type(result)}")
            return [
                EnhancementResult(
                    track_id=t.track_id,
                    success=False,
                    error=f"Unexpected result type: {type(result)}"
                )
                for t in tracks
            ]
        
        track_map = {t.track_id or f"track_{i}": t for i, t in enumerate(tracks)}
        
        final_results = []
        for i, track in enumerate(tracks):
            track_id = track.track_id or f"track_{i}"
            matched_result = None
            
            for r in results_list:
                if isinstance(r, dict) and r.get("track_id") == track_id:
                    matched_result = r
                    break
            
            if matched_result:
                final_results.append(self._parse_single_result(matched_result, track, analysis_result))
            else:
                logger.warning(f"No matching result for track_id={track_id}")
                final_results.append(EnhancementResult(
                    track_id=track.track_id,
                    success=False,
                    error="No matching result from AI"
                ))
        
        return final_results
    
    def _parse_single_result(self, result: Dict[str, Any], track: TrackInput,
                              analysis_result: Any) -> EnhancementResult:
        """解析单个结果
        
        Args:
            result: AI 返回的单个结果
            track: 原始音轨
            analysis_result: 分析结果对象
        
        Returns:
            EnhancementResult: 增强结果
        """
        enhancement_result = EnhancementResult(
            track_id=track.track_id,
            success=True
        )
        
        enhancement_result.title_zh = clean_value(result.get("title_zh", ""))
        enhancement_result.album_zh = clean_value(result.get("album_zh", ""))
        enhancement_result.artist_zh = clean_value(result.get("artist_zh", ""))
        enhancement_result.translation_confidence = result.get("translation_confidence", 0.0)
        
        genre_data = result.get("genre", {})
        if isinstance(genre_data, dict):
            enhancement_result.genre_value = clean_value(genre_data.get("value", ""))
            enhancement_result.genre_confidence = genre_data.get("confidence", 0.0)
        elif isinstance(genre_data, str):
            enhancement_result.genre_value = clean_value(genre_data)
            enhancement_result.genre_confidence = 0.8
        
        edition_data = result.get("edition", {})
        if isinstance(edition_data, dict):
            enhancement_result.edition_value = clean_value(edition_data.get("value", ""))
            enhancement_result.edition_confidence = edition_data.get("confidence", 0.0)
        elif isinstance(edition_data, str):
            enhancement_result.edition_value = clean_value(edition_data)
            enhancement_result.edition_confidence = 0.8
        
        enhancement_result.model = analysis_result.model
        enhancement_result.model_type = analysis_result.model_type
        enhancement_result.tokens_used = analysis_result.tokens_used
        
        return enhancement_result
    
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
        enhancement_options = EnhancementOptions.from_dict(options)
        
        results = self.enhance(track_inputs, enhancement_options)
        
        return [r.to_dict() for r in results]
