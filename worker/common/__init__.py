#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Common Module
共享工具与 IPC 边界模型

V8.2 数据模型统一：
  本包仅导出 IPC 边界 Pydantic 模型（models.py）与共享工具（text_utils 等）。
  运行时 dataclass 模型请从 core/types.py 或 data_sources/base.py 导入。

V8.2 移除：所有 V8.1 孤儿模型（TrackOptions/GenreInfo/EditionInfo/... 等），
          它们要么由 IPC 模型（Stage1Scraping*/Stage2Enhancement*）承担，
          要么由运行时 dataclass（Candidate/FinalResult/QueryInput 等）承担。
"""

from .models import (
    IPCResponse,
    Stage1ScrapingResultModel,
    Stage1ScrapedFieldModel,
    Stage1ScrapingResponseModel,
    Stage2EnhancementResultModel,
    Stage2EnhancementResponseModel,
    create_stage1_scraping_result,
    create_stage1_error_result,
    create_stage2_enhancement_result,
    create_stage2_error_result,
)

__all__ = [
    "IPCResponse",
    "Stage1ScrapingResultModel",
    "Stage1ScrapedFieldModel",
    "Stage1ScrapingResponseModel",
    "Stage2EnhancementResultModel",
    "Stage2EnhancementResponseModel",
    "create_stage1_scraping_result",
    "create_stage1_error_result",
    "create_stage2_enhancement_result",
    "create_stage2_error_result",
]
