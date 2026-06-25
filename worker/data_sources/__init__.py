#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Data Sources Module
Provides unified interface for multiple metadata sources
"""

from .base import (
    DataSourceType,
    FallbackLevel,
    QueryInput,
    Candidate,
    FinalResult,
    FallbackContext,
    ReleaseInfo,
    DataSourceAdapter,
    ScrapingOptions,
    EnhancementOptions,
)
from .manager import DataSourceManager
from .musicbrainz_adapter import MusicBrainzAdapter
from .discogs_adapter import DiscogsAdapter

__all__ = [
    "DataSourceType",
    "FallbackLevel",
    "QueryInput",
    "Candidate",
    "FinalResult",
    "FallbackContext",
    "ReleaseInfo",
    "DataSourceAdapter",
    "DataSourceManager",
    "MusicBrainzAdapter",
    "DiscogsAdapter",
    "ScrapingOptions",
    "EnhancementOptions",
]
