#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Common Module
Shared utilities and helpers
"""

from .text_utils import (
    clean_value,
    clean_dict_values,
    clean_list_values,
    clean_field_dict,
    is_valid_value,
    INVALID_VALUES,
)

from .models import (
    TrackOptions,
    TrackInput,
    GenreInfo,
    EditionInfo,
    TranslationInfo,
    AIResult,
    AnalysisInfo,
    OriginalMetadata,
    TrackAnalysisResult,
    ErrorInfo,
    BatchResponse,
    IPCRequest,
    IPCResponse,
    ScrapingOptions,
    EnhancementOptionsModel,
    ScrapedFieldModel,
    TrackScrapingResultModel,
    EnhancementResultModel,
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

from .config_manager import (
    ConfigManager,
    setup_logging,
    get_config,
)

__all__ = [
    "clean_value",
    "clean_dict_values",
    "clean_list_values",
    "clean_field_dict",
    "is_valid_value",
    "INVALID_VALUES",
    "TrackOptions",
    "TrackInput",
    "GenreInfo",
    "EditionInfo",
    "TranslationInfo",
    "AIResult",
    "AnalysisInfo",
    "OriginalMetadata",
    "TrackAnalysisResult",
    "ErrorInfo",
    "BatchResponse",
    "IPCRequest",
    "IPCResponse",
    "ScrapingOptions",
    "EnhancementOptionsModel",
    "ScrapedFieldModel",
    "TrackScrapingResultModel",
    "EnhancementResultModel",
    "Stage1ScrapingResultModel",
    "Stage1ScrapedFieldModel",
    "Stage1ScrapingResponseModel",
    "Stage2EnhancementResultModel",
    "Stage2EnhancementResponseModel",
    "create_stage1_scraping_result",
    "create_stage1_error_result",
    "create_stage2_enhancement_result",
    "create_stage2_error_result",
    "ConfigManager",
    "setup_logging",
    "get_config",
]
