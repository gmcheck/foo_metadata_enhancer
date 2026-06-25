#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Discogs Adapter
Provides metadata scraping from Discogs API
"""

import json
import logging
import time
import urllib.parse
import hashlib
import hmac
import secrets
from typing import Dict, Any, Optional, List

from .base import (
    DataSourceType,
    ReleaseInfo,
    DataSourceAdapter,
    QueryInput,
    Candidate,
)

logger = logging.getLogger(__name__)


class DiscogsAdapter(DataSourceAdapter):
    """Discogs 数据源适配器
    
    使用 Discogs API 进行元数据刮削
    API 文档: https://www.discogs.com/developers
    """
    
    API_BASE_URL = "https://api.discogs.com"
    
    def __init__(self, config: Dict[str, Any]):
        """初始化 Discogs 适配器
        
        Args:
            config: 配置字典
        """
        super().__init__(config)
        
        data_sources_config = config.get("data_sources", {})
        discogs_config = data_sources_config.get("discogs", {})
        
        self._api_url = discogs_config.get("api_url", self.API_BASE_URL)
        self._consumer_key = discogs_config.get("consumer_key", "")
        self._consumer_secret = discogs_config.get("consumer_secret", "")
        self._rate_limit_rpm = discogs_config.get("rate_limit_rpm", 60)
        self._enabled = discogs_config.get("enabled", True)
        
        self._last_request_time = 0.0
        self._min_request_interval = 60.0 / self._rate_limit_rpm
        
        self._token = ""
        self._token_secret = ""
    
    @property
    def source_type(self) -> DataSourceType:
        """数据源类型"""
        return DataSourceType.DISCOGS
    
    @property
    def is_enabled(self) -> bool:
        """是否启用"""
        return self._enabled and bool(self._consumer_key and self._consumer_secret)
    
    def _rate_limit(self) -> None:
        """速率限制"""
        elapsed = time.time() - self._last_request_time
        if elapsed < self._min_request_interval:
            time.sleep(self._min_request_interval - elapsed)
        self._last_request_time = time.time()
    
    def _generate_auth_header(self, method: str, url: str) -> str:
        """生成 OAuth 认证头
        
        Args:
            method: HTTP 方法
            url: 请求 URL
        
        Returns:
            str: OAuth 认证头
        """
        timestamp = str(int(time.time()))
        nonce = secrets.token_hex(16)
        
        params = {
            "oauth_consumer_key": self._consumer_key,
            "oauth_nonce": nonce,
            "oauth_signature_method": "HMAC-SHA1",
            "oauth_timestamp": timestamp,
            "oauth_token": self._token,
            "oauth_version": "1.0",
        }
        
        param_string = "&".join(
            f"{urllib.parse.quote(k)}={urllib.parse.quote(v)}"
            for k, v in sorted(params.items())
        )
        
        base_string = f"{method}&{urllib.parse.quote(url, safe='')}&{urllib.parse.quote(param_string)}"
        
        signing_key = f"{urllib.parse.quote(self._consumer_secret)}&{urllib.parse.quote(self._token_secret)}"
        
        signature = hmac.new(
            signing_key.encode(),
            base_string.encode(),
            hashlib.sha1
        ).digest()
        
        import base64
        signature_b64 = base64.b64encode(signature).decode()
        
        params["oauth_signature"] = signature_b64
        
        auth_header = "OAuth " + ", ".join(
            f'{k}="{urllib.parse.quote(v)}"'
            for k, v in sorted(params.items())
        )
        
        return auth_header
    
    def _make_request(self, endpoint: str, params: Dict[str, str] = None) -> Optional[Dict]:
        """发送 API 请求
        
        Args:
            endpoint: API 端点
            params: 请求参数
        
        Returns:
            Dict: JSON 响应，失败返回 None
        """
        import urllib.request
        import urllib.error
        
        self._rate_limit()
        
        url = f"{self._api_url}/{endpoint}"
        
        if params:
            query_string = urllib.parse.urlencode(params)
            url = f"{url}?{query_string}"
        
        headers = {
            "User-Agent": "foo_metadata_enhancer/1.0",
            "Accept": "application/vnd.discogs.v2.discogs+json",
        }
        
        if self._consumer_key and self._consumer_secret:
            headers["Authorization"] = self._generate_auth_header("GET", url)
        
        request = urllib.request.Request(url, headers=headers)
        
        try:
            logger.debug(f"Discogs request: {url}")
            
            with urllib.request.urlopen(request, timeout=30) as response:
                data = response.read().decode('utf-8')
                return json.loads(data)
        
        except urllib.error.HTTPError as e:
            logger.error(f"Discogs HTTP error: {e.code} - {e.reason}")
            return None
        
        except urllib.error.URLError as e:
            logger.error(f"Discogs URL error: {e.reason}")
            return None
        
        except json.JSONDecodeError as e:
            logger.error(f"Discogs JSON decode error: {e}")
            return None
        
        except Exception as e:
            logger.error(f"Discogs request error: {e}")
            return None
    
    def search_candidates(self, query: QueryInput) -> List[Candidate]:
        """搜索并返回候选列表
        
        并发查询接口，返回多个候选结果供后续聚合和决策。
        
        Args:
            query: 查询输入（title, artist, album, duration）
        
        Returns:
            List[Candidate]: 候选列表
        """
        logger.debug(f"DiscogsAdapter::search_candidates: title='{query.title}', artist='{query.artist}'")
        
        if not self.is_enabled:
            logger.warning("Discogs adapter is not enabled")
            return []
        
        search_query = f"{query.title} {query.artist}"
        
        params = {
            "q": search_query,
            "type": "release",
            "per_page": "10"
        }
        
        response = self._make_request("database/search", params)
        
        if not response:
            logger.warning("Discogs API request failed")
            return []
        
        results = response.get("results", [])
        
        if not results:
            logger.debug("Discogs: No results found")
            return []
        
        candidates = self._convert_results_to_candidates(results, query)
        
        logger.debug(f"Discogs: Returning {len(candidates)} candidates")
        return candidates
    
    def _convert_results_to_candidates(self, results: List[Dict],
                                        query: QueryInput) -> List[Candidate]:
        """将 Discogs 搜索结果转换为候选列表
        
        Args:
            results: Discogs 搜索结果列表
            query: 原始查询输入
        
        Returns:
            List[Candidate]: 候选列表
        """
        candidates = []
        
        for result in results:
            title = result.get("title", "")
            
            if " - " in title:
                parts = title.split(" - ", 1)
                result_artist = parts[0]
                result_title = parts[1] if len(parts) > 1 else ""
            else:
                result_artist = ""
                result_title = title
            
            title_sim = self._calculate_string_similarity(result_title, query.title)
            artist_sim = self._calculate_string_similarity(result_artist, query.artist)
            
            base_score = (title_sim * 0.5 + artist_sim * 0.5)
            
            year = str(result.get("year", "")) if result.get("year") else ""
            
            label_data = result.get("label", [])
            label = ""
            if label_data:
                if isinstance(label_data, list):
                    label = label_data[0] if label_data else ""
                else:
                    label = str(label_data)
            
            catno_data = result.get("catno", "")
            catalog_number = ""
            if catno_data:
                if isinstance(catno_data, list):
                    catalog_number = catno_data[0] if catno_data else ""
                else:
                    catalog_number = str(catno_data)
            
            country = result.get("country", "")
            
            resource_url = result.get("resource_url", "")
            discogs_id = ""
            if resource_url:
                discogs_id = resource_url.split("/")[-1]
            
            candidate = Candidate(
                title=result_title,
                artist=result_artist,
                album="",
                year=year,
                track_number="",
                disc_number="",
                genre="",
                composer="",
                lyricist="",
                label=label,
                country=country,
                catalog_number=catalog_number,
                musicbrainz_id="",
                source=DataSourceType.DISCOGS,
                confidence=min(base_score, 0.90),
                match_score=base_score,
                sources=[DataSourceType.DISCOGS],
                raw={"result": result, "discogs_id": discogs_id}
            )
            
            candidates.append(candidate)
        
        candidates.sort(key=lambda c: c.confidence, reverse=True)
        
        return candidates[:5]
    
    def get_release_info(self, release_id: str) -> Optional[ReleaseInfo]:
        """获取发行信息
        
        Args:
            release_id: Discogs 发行 ID
        
        Returns:
            ReleaseInfo: 发行信息
        """
        logger.debug(f"DiscogsAdapter::get_release_info: release_id='{release_id}'")
        
        response = self._make_request(f"releases/{release_id}")
        
        if not response:
            return None
        
        return self._parse_release(response)
    
    def _parse_release(self, release: Dict) -> ReleaseInfo:
        """解析发行数据
        
        Args:
            release: Discogs 发行数据
        
        Returns:
            ReleaseInfo: 发行信息
        """
        release_id = str(release.get("id", ""))
        title = release.get("title", "")
        
        artists = release.get("artists", [])
        artist_name = ""
        if artists:
            artist_name = artists[0].get("name", "")
        
        year = release.get("year", "")
        
        country = release.get("country", "")
        
        labels = release.get("labels", [])
        label = ""
        catalog_number = ""
        if labels:
            first_label = labels[0]
            label = first_label.get("name", "")
            catalog_number = first_label.get("catno", "")
        
        tracklist = release.get("tracklist", [])
        track_count = len(tracklist)
        
        formats = release.get("formats", [])
        format_name = ""
        disc_count = 1
        if formats:
            first_format = formats[0]
            format_name = first_format.get("name", "")
            qty = first_format.get("qty", "1")
            try:
                disc_count = int(qty)
            except ValueError:
                disc_count = 1
        
        tracks = []
        for track in tracklist:
            tracks.append({
                "position": track.get("position", ""),
                "title": track.get("title", ""),
                "duration": track.get("duration", ""),
            })
        
        return ReleaseInfo(
            release_id=release_id,
            title=title,
            artist=artist_name,
            year=str(year) if year else "",
            country=country,
            label=label,
            catalog_number=catalog_number,
            track_count=track_count,
            disc_count=disc_count,
            format=format_name,
            tracks=tracks,
            confidence=0.85,
            source=DataSourceType.DISCOGS
        )
