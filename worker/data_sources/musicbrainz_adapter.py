#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
MusicBrainz Adapter (HTTP Implementation)
Provides metadata scraping from MusicBrainz API using direct HTTP requests

优化特性:
    - HTTP 可控：使用 requests 库，完全控制超时和重试
    - 分页查询：控制每次请求的数据量
    - 重试机制：每页独立重试，指数退避
    - 中断检查：在请求期间响应中断
    - 评分系统：计算候选匹配分数
    - 提前终止：找到高质量匹配后停止查询
    - Normalize Pipeline：字符串归一化提高匹配准确度
┌─────────────────────────────────────────────────────────────────────────────┐
│                      MusicBrainzAdapter (HTTP Mode)                         │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  1. Normalize Pipeline（字符串归一化）                                       │
│     ├── _unicode_normalize()    # Unicode NFKC 归一化                       │
│     ├── _remove_brackets()      # 移除括号内容                              │
│     ├── _remove_feat()          # 移除 feat/ft/featuring                    │
│     ├── _remove_version_noise() # 移除 remix/live/demo 等                   │
│     ├── _normalize_symbols()    # 符号规范化 (& → and)                      │
│     ├── _normalize_numbers()    # 数字规范化 (ii → 2)                       │
│     └── _normalize_whitespace() # 空白规范化                                │
│                                                                             │
│  2. 多策略查询                                                              │
│     ├── _build_strict_query()   # 标题 + 艺术家 + 专辑                      │
│     ├── _build_medium_query()   # 标题 + 艺术家                             │
│     └── _build_loose_query()    # 仅标题                                    │
│                                                                             │
│  3. HTTP 可控                                                               │
│     ├── timeout 参数直接生效                                                │
│     ├── 速率限制自动等待                                                    │
│     └── 重试 + 指数退避                                                     │
│                                                                             │
│  4. 评分系统（使用 normalize）                                              │
│     ├── 标题相似度: 0~50 分                                                 │
│     ├── 艺术家相似度: 0~30 分                                               │
│     └── 时长匹配: 0~20 分                                                   │
│                                                                             │
│  5. 提前终止                                                                │
│     └── 找到高质量匹配后停止查询                                            │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘    
"""

import logging
import re
import time
import unicodedata
from difflib import SequenceMatcher
from typing import Dict, Any, Optional, List
from urllib.parse import quote_plus

import requests
from requests.exceptions import RequestException, Timeout, ConnectionError

from .base import (
    DataSourceType,
    ReleaseInfo,
    DataSourceAdapter,
    QueryInput,
    Candidate,
)
from .match_decision import MatchDecision, DecisionConfig

logger = logging.getLogger(__name__)


class MusicBrainzError(Exception):
    """MusicBrainz 错误基类"""
    pass


class MusicBrainzConnectionError(MusicBrainzError):
    """连接错误"""
    pass


class MusicBrainzRateLimitError(MusicBrainzError):
    """速率限制错误"""
    pass


class MusicBrainzNotFoundError(MusicBrainzError):
    """资源未找到错误"""
    pass


class MusicBrainzAdapter(DataSourceAdapter):
    """MusicBrainz 数据源适配器
    
    使用 HTTP requests 直接访问 MusicBrainz API
    API 文档: https://musicbrainz.org/doc/Development/XML_Web_Service/Version_2
    """
    
    BASE_URL = "https://musicbrainz.org/ws/2/recording"
    
    def __init__(self, config: Dict[str, Any]):
        """初始化 MusicBrainz 适配器
        
        Args:
            config: 配置字典
        """
        super().__init__(config)
        
        data_sources_config = config.get("data_sources", {})
        mb_config = data_sources_config.get("musicbrainz", {})
        
        self._api_url = mb_config.get("api_url", self.BASE_URL)
        self._user_agent = f"{mb_config.get('user_agent_name', 'foo_ai_metadata')}/{mb_config.get('user_agent_version', '1.0')}"
        self._user_agent_contact = mb_config.get("user_agent_contact", "https://github.com/user/foo_ai_metadata")
        self._rate_limit_rpm = mb_config.get("rate_limit_rpm", 50)
        self._enabled = mb_config.get("enabled", True)
        self._timeout = mb_config.get("timeout", 30)
        self._retries = mb_config.get("retries", 3)
        self._page_size = mb_config.get("page_size", 10)
        self._max_pages = mb_config.get("max_pages", 2)
        self._score_threshold = mb_config.get("score_threshold", 85)
        self._score_margin = mb_config.get("score_margin", 10)
        
        decision_config = DecisionConfig(
            score_threshold=self._score_threshold,
            score_margin=self._score_margin,
        )
        self._match_decision = MatchDecision(decision_config)
        
        self._initialized = False
        self._abort_checker = None
        self._last_request_time = 0.0
        self._min_request_interval = 60.0 / self._rate_limit_rpm if self._rate_limit_rpm > 0 else 1.0
        
        self._session = requests.Session()
        self._session.headers.update({
            "User-Agent": f"{self._user_agent} ( {self._user_agent_contact} )",
            "Accept": "application/json"
        })
        
        self._initialized = True
        logger.info(f"MusicBrainz adapter initialized (HTTP mode): user_agent={self._user_agent}")
    
    def set_abort_checker(self, checker):
        """设置中断检查器
        
        Args:
            checker: 中断检查函数，返回 True 表示需要中断
        """
        self._abort_checker = checker
    
    def _is_aborted(self) -> bool:
        """检查是否需要中断"""
        if self._abort_checker:
            return self._abort_checker()
        return False
    
    def _rate_limit_wait(self) -> None:
        """等待以满足速率限制"""
        if self._last_request_time > 0:
            elapsed = time.time() - self._last_request_time
            if elapsed < self._min_request_interval:
                wait_time = self._min_request_interval - elapsed
                time.sleep(wait_time)
    
    @property
    def source_type(self) -> DataSourceType:
        """数据源类型"""
        return DataSourceType.MUSICBRAINZ
    
    @property
    def is_enabled(self) -> bool:
        """是否启用"""
        return self._enabled and self._initialized
    
    # =========================================================================
    # Normalize Pipeline - 字符串归一化处理
    # =========================================================================
    
    def _normalize(self, text: str) -> str:
        """字符串归一化管道
        
        处理步骤：
        1. Unicode NFKC 归一化
        2. 转小写
        3. 去括号内容
        4. 去 feat/ft/featuring
        5. 去版本信息（remix, live 等）
        6. 符号规范化
        7. 数字规范化
        8. 空白规范化
        
        Args:
            text: 原始字符串
        
        Returns:
            str: 归一化后的字符串
        """
        if not text:
            return ""
        
        try:
            text = self._unicode_normalize(text)
            text = text.lower()
            text = self._remove_brackets(text)
            text = self._remove_feat(text)
            text = self._remove_version_noise(text)
            text = self._normalize_symbols(text)
            text = self._normalize_numbers(text)
            text = self._normalize_whitespace(text)
            
            return text.strip()
        except Exception as e:
            logger.warning(f"MusicBrainz: Normalize error for '{text[:50]}...': {e}")
            return text.lower().strip()
    
    def _unicode_normalize(self, text: str) -> str:
        """Unicode 归一化 (NFKC)"""
        return unicodedata.normalize("NFKC", text)
    
    def _remove_brackets(self, text: str) -> str:
        """移除括号及其内容"""
        patterns = [
            r"\(.*?\)",
            r"\[.*?\]",
            r"（.*?）",
            r"【.*?】",
            r"\{.*?\}",
        ]
        for p in patterns:
            text = re.sub(p, "", text)
        return text
    
    def _remove_feat(self, text: str) -> str:
        """移除 feat/ft/featuring 及其后内容"""
        patterns = [
            r"\bfeat\.?.*",
            r"\bft\.?.*",
            r"\bfeaturing.*",
            r"\bwith.*",
        ]
        for p in patterns:
            text = re.sub(p, "", text, flags=re.IGNORECASE)
        return text
    
    def _remove_version_noise(self, text: str) -> str:
        """移除版本信息关键词"""
        keywords = [
            "remix", "mix", "edit", "version",
            "live", "demo", "instrumental",
            "karaoke", "acoustic", "radio",
            "extended", "original", "bonus"
        ]
        for kw in keywords:
            text = re.sub(rf"\b{kw}\b", "", text, flags=re.IGNORECASE)
        return text
    
    def _normalize_symbols(self, text: str) -> str:
        """符号规范化"""
        text = text.replace("&", " and ")
        text = text.replace("+", " and ")
        text = re.sub(r"[\/\-_]+", " ", text)
        text = re.sub(r"[^\w\s]", "", text)
        return text
    
    def _normalize_numbers(self, text: str) -> str:
        """数字规范化"""
        text = re.sub(r"\bii\b", "2", text)
        text = re.sub(r"\biii\b", "3", text)
        text = re.sub(r"\biv\b", "4", text)
        text = re.sub(r"\bi\b", "1", text)
        return text
    
    def _normalize_whitespace(self, text: str) -> str:
        """空白规范化"""
        return re.sub(r"\s+", " ", text)
    
    # =========================================================================
    # 查询构建 - 多策略查询
    # =========================================================================
    
    def _build_strict_query(self, query: QueryInput) -> str:
        """构建严格查询（标题+艺术家+专辑）"""
        parts = []
        
        if query.title:
            title_clean = self._normalize(query.title)
            parts.append(f'recording:"{title_clean}"')
        
        if query.artist:
            artist_clean = self._normalize(query.artist)
            parts.append(f'artist:"{artist_clean}"')
        
        if query.album:
            album_clean = self._normalize(query.album)
            parts.append(f'release:"{album_clean}"')
        
        return " AND ".join(parts) if parts else ""
    
    def _build_medium_query(self, query: QueryInput) -> str:
        """构建中等查询（标题+艺术家）"""
        parts = []
        
        if query.title:
            title_clean = self._normalize(query.title)
            parts.append(f'recording:"{title_clean}"')
        
        if query.artist:
            artist_clean = self._normalize(query.artist)
            parts.append(f'artist:"{artist_clean}"')
        
        return " AND ".join(parts) if parts else ""
    
    def _build_loose_query(self, query: QueryInput) -> str:
        """构建宽松查询（仅标题）"""
        if query.title:
            title_clean = self._normalize(query.title)
            return f'recording:"{title_clean}"'
        return ""
    
    # =========================================================================
    # HTTP 请求
    # =========================================================================
    
    def _http_search(self, query_str: str, offset: int) -> Optional[List[Dict]]:
        """执行 HTTP 搜索请求
        
        Args:
            query_str: 查询字符串
            offset: 偏移量
        
        Returns:
            Optional[List[Dict]]: 录音列表，失败返回 None
        """
        if not query_str:
            return []
        
        params = {
            "query": query_str,
            "fmt": "json",
            "limit": self._page_size,
            "offset": offset
        }
        
        self._rate_limit_wait()
        
        try:
            self._last_request_time = time.time()
            
            response = self._session.get(
                self._api_url,
                params=params,
                timeout=self._timeout
            )
            
            if response.status_code == 503:
                raise MusicBrainzRateLimitError(f"Rate limited (503)")
            
            if response.status_code == 404:
                return []
            
            if response.status_code != 200:
                raise MusicBrainzError(f"HTTP {response.status_code}: {response.text[:200]}")
            
            data = response.json()
            return data.get("recordings", [])
            
        except Timeout:
            raise MusicBrainzConnectionError(f"Request timeout after {self._timeout}s")
        except ConnectionError as e:
            raise MusicBrainzConnectionError(f"Connection error: {e}")
        except RequestException as e:
            raise MusicBrainzError(f"Request error: {e}")
    
    # =========================================================================
    # 评分系统
    # =========================================================================
    
    def _string_similarity(self, a: str, b: str) -> float:
        """计算字符串相似度
        
        使用归一化后的字符串进行比较
        
        Args:
            a: 字符串1
            b: 字符串2
        
        Returns:
            float: 相似度 (0.0 ~ 1.0)
        """
        if not a or not b:
            return 0.0
        
        norm_a = self._normalize(a)
        norm_b = self._normalize(b)
        
        if not norm_a or not norm_b:
            return 0.0
        
        return SequenceMatcher(None, norm_a, norm_b).ratio()
    
    def _extract_artist_name(self, artist_credit: List) -> str:
        """从 artist-credit 结构中提取艺术家名称
        
        Args:
            artist_credit: MusicBrainz artist-credit 列表
        
        Returns:
            str: 艺术家名称
        """
        if not artist_credit:
            return ""
        
        names = []
        for item in artist_credit:
            if isinstance(item, dict):
                name = item.get("name", "")
                if not name:
                    artist = item.get("artist", {})
                    name = artist.get("name", "")
                if name:
                    names.append(name)
            elif isinstance(item, str):
                names.append(item)
        
        return " ".join(names)
    
    def _score_candidate(self, query: QueryInput, recording: Dict) -> Dict[str, Any]:
        """计算候选匹配分数（含详细评分信息）
        
        评分规则（满分 100）：
        - 标题相似度：0~50 分
        - 艺术家相似度：0~30 分
        - 时长匹配：0~20 分（差值≤2秒得20分，≤5秒得10分）
        
        Args:
            query: 查询输入
            recording: MusicBrainz 录音数据
        
        Returns:
            Dict[str, Any]: 包含 base_score, title_sim, artist_sim, duration_score 的字典
        """
        title = recording.get("title", "")
        title_sim = self._string_similarity(query.title, title)
        
        artist_credit = recording.get("artist-credit", [])
        artist_name = self._extract_artist_name(artist_credit)
        artist_sim = self._string_similarity(query.artist, artist_name)
        
        duration_score = 0.0
        if query.duration:
            length = recording.get("length")
            if length:
                diff = abs(query.duration * 1000 - length) / 1000
                if diff <= 2:
                    duration_score = 20
                elif diff <= 5:
                    duration_score = 10
        
        base_score = title_sim * 50 + artist_sim * 30 + duration_score
        
        return {
            "base_score": base_score,
            "title_sim": title_sim,
            "artist_sim": artist_sim,
            "duration_score": duration_score,
        }
    
    def _has_good_match(self, candidates: List[Dict]) -> bool:
        """判断是否已找到高质量匹配
        
        使用 MatchDecision 进行分权判定，综合 score 和 title_sim 等多维度指标。
        
        Args:
            candidates: 候选列表（包含 final_score, title_sim, title_level 等字段）
        
        Returns:
            bool: 是否找到高质量匹配，True 表示可以提前终止查询
        """
        if not candidates or self._is_aborted():
            return False
        
        return self._match_decision.is_best_match(candidates)
    
    # =========================================================================
    # 分页搜索 + 重试 + 提前终止
    # =========================================================================
    
    def _search_with_paging(self, query: QueryInput) -> List[Dict]:
        """分页搜索（带重试和提前终止）
        
        Args:
            query: 查询输入
        
        Returns:
            List[Dict]: 所有候选录音列表
        """
        strategies = [
            ("strict", self._build_strict_query(query)),
            ("medium", self._build_medium_query(query)),
            ("loose", self._build_loose_query(query))
        ]
        
        all_recordings = []
        
        for strategy_name, query_str in strategies:
            if not query_str:
                continue
            
            if self._is_aborted():
                logger.debug(f"MusicBrainz: Abort before strategy '{strategy_name}'")
                break
            
            logger.debug(f"MusicBrainz: Trying strategy '{strategy_name}'")
            
            for page in range(self._max_pages):
                if self._is_aborted():
                    logger.debug("MusicBrainz: Abort during page request")
                    break
                
                offset = page * self._page_size
                page_results = None
                
                for attempt in range(self._retries):
                    if self._is_aborted():
                        break
                    
                    try:
                        page_results = self._http_search(query_str, offset)
                        break
                    except MusicBrainzRateLimitError:
                        wait_time = 2 + attempt
                        logger.warning(f"MusicBrainz rate limited, waiting {wait_time}s...")
                        time.sleep(wait_time)
                    except MusicBrainzConnectionError as e:
                        logger.warning(f"MusicBrainz connection error (attempt {attempt+1}): {e}")
                        if attempt < self._retries - 1:
                            time.sleep(1 + attempt)
                    except MusicBrainzError as e:
                        logger.error(f"MusicBrainz error: {e}")
                        break
                
                if page_results is None:
                    logger.warning(f"MusicBrainz: Page {page+1} failed after {self._retries} retries")
                    break
                
                if not page_results:
                    logger.debug(f"MusicBrainz: Page {page+1} returned empty results")
                    break
                
                for r in page_results:
                    try:
                        score_info = self._score_candidate(query, r)
                        match_score = self._match_decision.compute_final_score(
                            base_score=score_info["base_score"],
                            title_sim=score_info["title_sim"],
                            artist_sim=score_info["artist_sim"],
                            duration_score=score_info["duration_score"],
                            raw=r
                        )
                        r["score"] = match_score.base_score
                        r["final_score"] = match_score.final_score
                        r["title_sim"] = match_score.title_sim
                        r["title_level"] = match_score.title_level
                        r["artist_sim"] = match_score.artist_sim
                        r["duration_score"] = match_score.duration_score
                        all_recordings.append(r)
                    except Exception as e:
                        logger.error(f"MusicBrainz: Error scoring recording: {e}")
                        continue
                
                if self._has_good_match(all_recordings):
                    logger.info(f"MusicBrainz: Early stop - good match found (strategy={strategy_name}, page={page+1})")
                    return all_recordings
            
            if self._has_good_match(all_recordings):
                break
        
        return all_recordings
    
    def search_candidates(self, query: QueryInput) -> List[Candidate]:
        """搜索并返回候选列表
        
        实现分页查询、重试机制、中断检查、评分系统和提前终止。
        
        Args:
            query: 查询输入（title, artist, album, duration）
        
        Returns:
            List[Candidate]: 候选列表
        """
        logger.debug(f"MusicBrainzAdapter::search_candidates: title='{query.title}', artist='{query.artist}'")
        
        if not self.is_enabled:
            logger.warning("MusicBrainz adapter is not enabled")
            return []
        
        if not query.title:
            logger.warning("MusicBrainz: Empty title, skipping")
            return []
        
        try:
            recordings = self._search_with_paging(query)
            
            if not recordings:
                return []
            
            recordings.sort(key=lambda x: x.get("final_score", 0), reverse=True)
            
            candidates = []
            seen_ids = set()
            
            for r in recordings:
                try:
                    mbid = r.get("id", "")
                    if mbid in seen_ids:
                        continue
                    seen_ids.add(mbid)
                    
                    title = r.get("title", "")
                    artist_credit = r.get("artist-credit", [])
                    artist_name = self._extract_artist_name(artist_credit)
                    
                    releases = r.get("release-list", [])
                    album = ""
                    year = ""
                    track_number = ""
                    disc_number = ""
                    
                    if releases:
                        first_release = releases[0]
                        album = first_release.get("title", "")
                        date = first_release.get("date", "")
                        if date:
                            year = date.split("-")[0]
                        
                        medium_list = first_release.get("medium-list", [])
                        if medium_list:
                            first_medium = medium_list[0]
                            track_list = first_medium.get("track-list", [])
                            if track_list:
                                first_track = track_list[0]
                                track_number = first_track.get("number", "")
                            disc_number = first_medium.get("position", "")
                            if disc_number:
                                disc_number = str(disc_number)
                    
                    final_score = r.get("final_score", 0) / 100.0
                    candidate = Candidate(
                        title=title,
                        artist=artist_name,
                        album=album,
                        year=year,
                        track_number=track_number,
                        disc_number=disc_number,
                        musicbrainz_id=mbid,
                        match_score=final_score,
                        confidence=final_score,
                        source=DataSourceType.MUSICBRAINZ,
                        raw=r
                    )
                    candidates.append(candidate)
                except Exception as e:
                    logger.error(f"MusicBrainz: Error parsing recording: {e}")
                    continue
            
            logger.debug(f"MusicBrainz: Returning {len(candidates)} candidates")
            return candidates
            
        except Exception as e:
            logger.error(f"MusicBrainz: search_candidates error: {e}")
            return []
    
    def get_release_info(self, release_id: str) -> Optional[ReleaseInfo]:
        """获取发行信息
        
        Args:
            release_id: MusicBrainz 发行 ID
        
        Returns:
            Optional[ReleaseInfo]: 发行信息
        """
        if not self.is_enabled:
            logger.warning("MusicBrainz adapter is not enabled")
            return None
        
        release_url = f"https://musicbrainz.org/ws/2/release/{release_id}"
        params = {
            "fmt": "json",
            "inc": "recordings+artist-credits+labels"
        }
        
        self._rate_limit_wait()
        
        try:
            self._last_request_time = time.time()
            
            response = self._session.get(
                release_url,
                params=params,
                timeout=self._timeout
            )
            
            if response.status_code != 200:
                logger.error(f"MusicBrainz: Failed to get release {release_id}: HTTP {response.status_code}")
                return None
            
            data = response.json()
            
            media = data.get("media", [])
            track_count = media[0].get("track-count", 0) if media else 0
            disc_count = len(media) if media else 1
            
            return ReleaseInfo(
                release_id=data.get("id", ""),
                title=data.get("title", ""),
                artist=self._extract_artist_name(data.get("artist-credit", [])),
                year=data.get("date", "").split("-")[0] if data.get("date") else "",
                country=data.get("country", ""),
                label=self._extract_label(data.get("label-info-list", [])),
                track_count=track_count,
                disc_count=disc_count
            )
            
        except Exception as e:
            logger.error(f"MusicBrainz: Error getting release {release_id}: {e}")
            return None
    
    def _extract_label(self, label_info_list: List) -> str:
        """从 label-info-list 中提取厂牌名称"""
        if not label_info_list:
            return ""
        
        for info in label_info_list:
            label = info.get("label", {})
            if label:
                return label.get("name", "")
        
        return ""
    
    def close(self):
        """关闭会话"""
        if self._session:
            self._session.close()
            self._session = None
