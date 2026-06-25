#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Data Source Manager
Coordinates between multiple metadata sources with concurrent query support
"""

import logging
from concurrent.futures import ThreadPoolExecutor, as_completed
from typing import Dict, Any, Optional, List

from .base import (
    DataSourceType,
    ReleaseInfo,
    DataSourceAdapter,
    QueryInput,
    Candidate,
)

logger = logging.getLogger(__name__)


class DataSourceManager:
    """数据源管理器
    
    两阶段查询策略：
    1. 第一阶段：并发查询权威数据源（MusicBrainz, Discogs）
    2. 第二阶段：如果候选数量 < threshold，调用 AI Adapter 补充
    """
    
    DEFAULT_PRIORITY = [
        DataSourceType.MUSICBRAINZ,
        DataSourceType.DISCOGS,
        DataSourceType.AI,
    ]
    
    DEFAULT_MAX_CONCURRENT = 3
    DEFAULT_CANDIDATE_THRESHOLD = 3
    
    # 权威数据源（第一阶段查询）
    AUTHORITATIVE_SOURCES = {
        DataSourceType.MUSICBRAINZ,
        DataSourceType.DISCOGS,
    }
    
    def __init__(self, config: Dict[str, Any]):
        """初始化数据源管理器
        
        Args:
            config: 配置字典
        """
        self._config = config
        self._adapters: Dict[DataSourceType, DataSourceAdapter] = {}
        self._priority = self.DEFAULT_PRIORITY.copy()
        self._max_workers = self._load_max_concurrent()
        self._candidate_threshold = self._load_candidate_threshold()
        self._abort_checker = None
        
        self._init_adapters()
        self._load_priority()
    
    def set_abort_checker(self, checker):
        """设置中断检查器
        
        Args:
            checker: 中断检查函数，返回 True 表示需要中断
        """
        self._abort_checker = checker
        for adapter in self._adapters.values():
            if hasattr(adapter, 'set_abort_checker'):
                adapter.set_abort_checker(checker)
    
    def _load_max_concurrent(self) -> int:
        """从配置加载最大并发数"""
        data_sources_config = self._config.get("data_sources", {})
        return data_sources_config.get("max_concurrent", self.DEFAULT_MAX_CONCURRENT)
    
    def _load_candidate_threshold(self) -> int:
        """从配置加载候选阈值"""
        data_sources_config = self._config.get("data_sources", {})
        return data_sources_config.get("candidate_threshold", self.DEFAULT_CANDIDATE_THRESHOLD)
    
    def _init_adapters(self) -> None:
        """初始化所有适配器"""
        from .musicbrainz_adapter import MusicBrainzAdapter
        from .discogs_adapter import DiscogsAdapter
        from ai.ai_data_source import AIAdapter
        
        try:
            self._adapters[DataSourceType.MUSICBRAINZ] = MusicBrainzAdapter(self._config)
            logger.info("MusicBrainz adapter initialized")
        except Exception as e:
            logger.warning(f"Failed to initialize MusicBrainz adapter: {e}")
        
        try:
            self._adapters[DataSourceType.DISCOGS] = DiscogsAdapter(self._config)
            logger.info("Discogs adapter initialized")
        except Exception as e:
            logger.warning(f"Failed to initialize Discogs adapter: {e}")
        
        try:
            self._adapters[DataSourceType.AI] = AIAdapter(self._config)
            logger.info("AI adapter initialized")
        except Exception as e:
            logger.warning(f"Failed to initialize AI adapter: {e}")
    
    def _load_priority(self) -> None:
        """从配置加载优先级"""
        data_sources_config = self._config.get("data_sources", {})
        
        priority = data_sources_config.get("priority_order")
        if priority:
            self._priority = [DataSourceType(p) for p in priority if DataSourceType(p) in self._adapters]
            logger.info(f"Loaded priority order: {[p.value for p in self._priority]}")
    
    def get_adapter(self, source_type: DataSourceType) -> Optional[DataSourceAdapter]:
        """获取指定类型的适配器
        
        Args:
            source_type: 数据源类型
        
        Returns:
            DataSourceAdapter: 适配器实例，不存在返回None
        """
        return self._adapters.get(source_type)
    
    def fetch_all(self, query: QueryInput, 
                  options: Optional[Any] = None) -> List[Candidate]:
        """两阶段查询数据源，返回候选列表
        
        第一阶段：并发查询权威数据源（MusicBrainz, Discogs）
        第二阶段：如果候选数量 < threshold，调用 AI Adapter 补充
        
        数据源启用优先级：
        1. options 中的 enable_* 字段（用户配置）
        2. config.yaml 中的 enabled 字段（默认值）
        
        Args:
            query: 查询输入（title, artist, album, duration）
            options: 刮削选项（可选，用于覆盖配置文件默认值）
        
        Returns:
            List[Candidate]: 所有数据源的候选列表
        """
        logger.debug(f"DataSourceManager::fetch_all: title='{query.title}', artist='{query.artist}'")
        
        def is_source_enabled(source_type: DataSourceType, option_field: str) -> bool:
            """判断数据源是否启用（用户配置优先于配置文件）"""
            if options and hasattr(options, option_field):
                return getattr(options, option_field)
            adapter = self._adapters.get(source_type)
            return adapter.is_enabled if adapter else False
        
        candidates: List[Candidate] = []
        timeout = self._config.get("data_sources", {}).get("fetch_timeout", 120)
        
        # 第一阶段：并发查询权威数据源
        with ThreadPoolExecutor(max_workers=self._max_workers) as executor:
            futures = {}
            
            for source_type, adapter in self._adapters.items():
                if source_type in self.AUTHORITATIVE_SOURCES:
                    option_field = {
                        DataSourceType.MUSICBRAINZ: "enable_musicbrainz",
                        DataSourceType.DISCOGS: "enable_discogs",
                    }.get(source_type)
                    
                    if is_source_enabled(source_type, option_field):
                        future = executor.submit(
                            self._safe_search_candidates,
                            adapter,
                            query
                        )
                        futures[future] = source_type
            
            for future in as_completed(futures, timeout=timeout):
                source_type = futures[future]
                try:
                    result = future.result(timeout=timeout)
                    if result:
                        candidates.extend(result)
                        logger.debug(f"{source_type.value} returned {len(result)} candidates")
                except Exception as e:
                    logger.error(f"{source_type.value} search failed: {e}")
        
        logger.debug(f"Phase 1: Collected {len(candidates)} candidates from authoritative sources")
        
        if len(candidates) < self._candidate_threshold:
            ai_adapter = self._adapters.get(DataSourceType.AI)
            ai_config = self._config.get("data_sources", {}).get("ai", {})
            
            ai_enabled = is_source_enabled(DataSourceType.AI, "enable_ai")
            
            if ai_adapter and ai_enabled and ai_config.get("fallback_only", True):
                logger.debug(f"Candidates ({len(candidates)}) < threshold ({self._candidate_threshold}), calling AI Adapter as fallback")
                try:
                    ai_candidates = ai_adapter.search_candidates(query)
                    if ai_candidates:
                        candidates.extend(ai_candidates)
                        logger.debug(f"AI Adapter returned {len(ai_candidates)} candidates")
                except Exception as e:
                    logger.error(f"AI Adapter search failed: {e}")
        
        logger.debug(f"Total candidates collected: {len(candidates)}")
        return candidates
    
    def _safe_search_candidates(self, adapter: DataSourceAdapter,
                                 query: QueryInput) -> List[Candidate]:
        """安全调用适配器的 search_candidates 方法
        
        Args:
            adapter: 数据源适配器
            query: 查询输入
        
        Returns:
            List[Candidate]: 候选列表，失败返回空列表
        """
        try:
            return adapter.search_candidates(query)
        except Exception as e:
            logger.error(f"Adapter {adapter.source_type.value} error: {e}")
            return []
    
    def get_release_info(self, release_id: str, 
                         source_type: DataSourceType) -> Optional[ReleaseInfo]:
        """获取发行信息
        
        Args:
            release_id: 发行ID
            source_type: 数据源类型
        
        Returns:
            ReleaseInfo: 发行信息
        """
        adapter = self._adapters.get(source_type)
        
        if adapter is None or not adapter.is_enabled:
            logger.warning(f"Adapter for {source_type.value} not available")
            return None
        
        try:
            return adapter.get_release_info(release_id)
        except Exception as e:
            logger.error(f"Error getting release info: {e}")
            return None
    
    def get_available_sources(self) -> List[DataSourceType]:
        """获取可用的数据源列表
        
        Returns:
            List[DataSourceType]: 可用的数据源类型列表
        """
        return [
            source_type 
            for source_type, adapter in self._adapters.items() 
            if adapter.is_enabled
        ]
    
    def get_source_status(self) -> Dict[str, Dict[str, Any]]:
        """获取所有数据源的状态
        
        Returns:
            Dict: 数据源状态字典
        """
        status = {}
        
        for source_type, adapter in self._adapters.items():
            status[source_type.value] = {
                "enabled": adapter.is_enabled,
                "available": adapter is not None,
            }
        
        return status
    
    def set_source_enabled(self, source_type: DataSourceType, enabled: bool) -> None:
        """设置数据源是否启用
        
        Args:
            source_type: 数据源类型
            enabled: 是否启用
        """
        if source_type in self._adapters:
            adapter = self._adapters[source_type]
            if hasattr(adapter, '_config'):
                source_key = f"enable_{source_type.value}"
                adapter._config[source_key] = enabled
                logger.info(f"Set {source_type.value} enabled: {enabled}")
