#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Core Types Module
统一的运行时数据类型定义（单一来源）

V8.2 数据模型统一：
  本模块是 Stage1/Stage2 处理器共享的 TrackInput 唯一定义点。
  - 不在 stage1_processor.py / stage2_processor.py 中重复定义 TrackInput。
  - 不在 common/models.py 中重复定义 dataclass 版 TrackInput（Pydantic 版已删除）。
  - IPC 边界（ai_worker.py ↔ C++）的 JSON 校验使用 common/models.py 中的
    Stage1Scraping* / Stage2Enhancement* Pydantic 模型，与本模块互不重叠。

字段集采用 Stage1 的超集（含 conductor/performer/lyricist 等），
Stage2 仅使用其中的 title/artist/album/album_artist/year/genre/track_number/
disc_number/duration_sec/comment/label/composer/musicbrainz_id 子集，多余字段忽略即可。
"""

from dataclasses import dataclass
from typing import Any, Dict, TYPE_CHECKING

if TYPE_CHECKING:
    from data_sources.base import QueryInput


@dataclass
class TrackInput:
    """音轨输入数据（Stage1/Stage2 共享，单一来源）

    Attributes:
        track_id: 音轨 ID（C++ 端传入，用于回传对应结果）
        title: 标题
        artist: 艺术家
        album: 专辑
        album_artist: 专辑艺术家
        year: 发行年份
        genre: 流派（已有或由 Stage1 刮削填充）
        track_number: 音轨号
        disc_number: 光盘号
        duration_sec: 时长（秒）
        comment: 注释
        label: 厂牌
        composer: 作曲
        lyricist: 作词
        conductor: 指挥
        performer: 演奏者
        musicbrainz_id: MusicBrainz ID
    """
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

    def to_query_input(self) -> "QueryInput":
        """转换为 QueryInput（用于数据源查询）

        Returns:
            QueryInput: 数据源查询输入
        """
        from data_sources.base import QueryInput  # 延迟导入避免循环
        return QueryInput(
            track_id=self.track_id,
            title=self.title,
            artist=self.artist,
            album=self.album,
            duration=self.duration_sec,
            raw_data=self.to_dict()
        )
