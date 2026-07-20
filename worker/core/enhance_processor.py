#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Stage 2 Processor (Enhancer)
Metadata enhancement: translation only.

V8.2 Architecture:
    - Scrape: fetches external facts (title/artist/album/year/genre/...).
      Genre is now sourced from MusicBrainz in Scrape.
    - Enhancer: derives NEW VALUE from EXISTING metadata.
      Currently this means Chinese translations (title_zh / album_zh / artist_zh).
      Edition identification has been removed (was unreliable from AI inference).
    - Normalize: ensures consistency of existing tags (e.g., artist normalization).

Batch AI optimization: one API call processes multiple tracks.
"""

import json
import logging
from dataclasses import dataclass
from typing import Dict, Any, Optional, List

from data_sources.base import EnhancementOptions
from abort_checker import is_aborted
from common.text_utils import clean_value
from prompts import get_composer
from .types import TrackInput

logger = logging.getLogger(__name__)


@dataclass
class EnhancementResult:
    """增强结果（仅翻译；genre/edition 已移除）"""
    track_id: str
    success: bool = False

    title_zh: str = ""
    album_zh: str = ""
    artist_zh: str = ""
    translation_confidence: float = 0.0

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
            "model": self.model,
            "model_type": self.model_type,
            "tokens_used": self.tokens_used,
            "error": self.error or "",
        }


class EnhanceProcessor:
    """阶段二处理器（Enhancer）

    功能：基于已有元数据生成新价值，不获取新事实。
    当前能力：中文翻译（title_zh / album_zh / artist_zh）。

    核心方法为 enhance，支持批量处理多首歌曲。
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
            logger.info(f"EnhanceProcessor initialized with provider: {self._model_adapter.get_provider_info()}")
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
        logger.info(
            f"EnhanceProcessor::enhance: BEGIN, tracks={len(tracks)}, "
            f"options=translate_title={options.translate_title},"
            f"translate_album={options.translate_album},"
            f"translate_artist={options.translate_artist},"
            f"min_confidence={getattr(options, 'min_translation_confidence', 0.5)}"
        )

        if not tracks:
            logger.warning("EnhanceProcessor::enhance: empty track list, returning []")
            return []

        if is_aborted():
            logger.warning("EnhanceProcessor::enhance: ABORTED by user before AI call")
            return [
                EnhancementResult(
                    track_id=t.track_id,
                    success=False,
                    error="Aborted by user"
                )
                for t in tracks
            ]

        if not self._model_adapter:
            logger.error("EnhanceProcessor::enhance: model_adapter is None, cannot call AI")
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
            logger.info(
                f"EnhanceProcessor::enhance: calling AI, "
                f"system_prompt_len={len(messages[0]['content']) if messages else 0}, "
                f"user_prompt_len={len(messages[1]['content']) if len(messages) > 1 else 0}, "
                f"tracks_payload={len(tracks)}"
            )

            analysis_result = self._model_adapter.analyze(messages)

            if not analysis_result.success:
                logger.error(
                    f"EnhanceProcessor::enhance: AI call FAILED, "
                    f"error={analysis_result.error}, "
                    f"model={getattr(analysis_result, 'model', 'unknown')}"
                )
                return [
                    EnhancementResult(
                        track_id=t.track_id,
                        success=False,
                        error=analysis_result.error
                    )
                    for t in tracks
                ]

            logger.info(
                f"EnhanceProcessor::enhance: AI call OK, "
                f"model={analysis_result.model}, tokens={analysis_result.tokens_used}, "
                f"result_type={type(analysis_result.result).__name__}, "
                f"result_size={len(str(analysis_result.result)) if analysis_result.result else 0}"
            )

            return self._parse_batch_result(analysis_result.result, tracks, analysis_result)

        except Exception as e:
            logger.error(f"EnhanceProcessor::enhance: EXCEPTION {type(e).__name__}: {e}", exc_info=True)
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
        # Enhance 仅做翻译：构建翻译任务子集
        translation_tasks = []
        if options.translate_title:
            translation_tasks.append("title_zh")
        if options.translate_album:
            translation_tasks.append("album_zh")
        if options.translate_artist:
            translation_tasks.append("artist_zh")

        if not translation_tasks:
            # 无翻译任务：直接返回空结果（避免无意义的 API 调用）
            translation_tasks = ["title_zh", "album_zh", "artist_zh"]

        tasks_text = f"- Chinese translations for: {', '.join(translation_tasks)}"

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
- For non-Chinese content, you MUST provide Chinese translations in *_zh fields
- translation_confidence: 0.0-1.0
  - 0.0 means "already Chinese, no translation needed" (RARE case)
  - For non-Chinese content, use 0.5-1.0 based on confidence
- Do NOT leave *_zh fields empty for English/Japanese/Korean content"""

        return [
            {"role": "system", "content": get_composer(self._config).build_enhance_system_prompt()},
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
            logger.warning("EnhanceProcessor::_parse_batch_result: empty AI response")
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
                logger.info(f"EnhanceProcessor::_parse_batch_result: dict with 'results' key, "
                            f"items={len(results_list)}")
            else:
                results_list = [result]
                logger.info("EnhanceProcessor::_parse_batch_result: single dict (no 'results' key), "
                            "wrapping as single-item list")
        elif isinstance(result, list):
            results_list = result
            logger.info(f"EnhanceProcessor::_parse_batch_result: list response, items={len(results_list)}")
        else:
            logger.warning(f"EnhanceProcessor::_parse_batch_result: unexpected result type: {type(result)}")
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
                logger.warning(f"EnhanceProcessor::_parse_batch_result: no matching result for "
                               f"track_id={track_id} (AI returned {len(results_list)} items)")
                final_results.append(EnhancementResult(
                    track_id=track.track_id,
                    success=False,
                    error="No matching result from AI"
                ))

        logger.info(
            f"EnhanceProcessor::_parse_batch_result: DONE, "
            f"parsed={len(final_results)}, success_count="
            f"{sum(1 for r in final_results if r.success)}"
        )
        return final_results

    def _parse_single_result(self, result: Dict[str, Any], track: TrackInput,
                              analysis_result: Any) -> EnhancementResult:
        """解析单个结果（仅翻译字段；忽略 AI 可能返回的 genre/edition）

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

        raw_title_zh = result.get("title_zh", "")
        raw_album_zh = result.get("album_zh", "")
        raw_artist_zh = result.get("artist_zh", "")
        raw_conf = result.get("translation_confidence", 0.0)

        enhancement_result.title_zh = clean_value(raw_title_zh)
        enhancement_result.album_zh = clean_value(raw_album_zh)
        enhancement_result.artist_zh = clean_value(raw_artist_zh)
        enhancement_result.translation_confidence = raw_conf

        # 显式忽略 AI 可能误返回的 genre / edition（已不属于 Enhance 职责）
        # genre 由 Scrape 从 MusicBrainz 抓取；edition 已废弃。

        enhancement_result.model = analysis_result.model
        enhancement_result.model_type = analysis_result.model_type
        enhancement_result.tokens_used = analysis_result.tokens_used

        logger.info(
            f"EnhanceProcessor::_parse_single_result: track_id={track.track_id}, "
            f"title='{track.title[:30]}' -> zh='{enhancement_result.title_zh[:30]}', "
            f"album='{track.album[:30]}' -> zh='{enhancement_result.album_zh[:30]}', "
            f"artist='{track.artist[:30]}' -> zh='{enhancement_result.artist_zh[:30]}', "
            f"confidence={enhancement_result.translation_confidence}"
        )

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
