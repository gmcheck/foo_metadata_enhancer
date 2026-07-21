#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Common Module
共享工具与 IPC 边界模型

V8.2 数据模型统一：
  本包仅导出 IPC 边界 Pydantic 模型（models.py）与共享工具（text_utils 等）。
  运行时 dataclass 模型请从 core/types.py 或 data_sources/base.py 导入。

V8.2 移除：所有 V8.1 孤儿模型（TrackOptions/GenreInfo/EditionInfo/... 等），
          它们要么由 IPC 模型（Scrape*/Enhance*）承担，
          要么由运行时 dataclass（Candidate/FinalResult/QueryInput 等）承担。
"""

from .models import (
    IPCResponse,
    ScrapeResultModel,
    ScrapeFieldModel,
    ScrapeResponseModel,
    EnhanceResultModel,
    EnhanceResponseModel,
    create_scrape_result,
    create_scrape_error_result,
    create_enhance_result,
    create_enhance_error_result,
    STATUS_SUCCESS,
    STATUS_FAILED,
    DEFAULT_PROVIDER_ERROR_CATEGORY,
    ProviderTestDataModel,
    ProviderActionResultModel,
    create_provider_success,
    create_provider_failure,
)

__all__ = [
    "IPCResponse",
    "ScrapeResultModel",
    "ScrapeFieldModel",
    "ScrapeResponseModel",
    "EnhanceResultModel",
    "EnhanceResponseModel",
    "create_scrape_result",
    "create_scrape_error_result",
    "create_enhance_result",
    "create_enhance_error_result",
    "STATUS_SUCCESS",
    "STATUS_FAILED",
    "DEFAULT_PROVIDER_ERROR_CATEGORY",
    "ProviderTestDataModel",
    "ProviderActionResultModel",
    "create_provider_success",
    "create_provider_failure",
]
