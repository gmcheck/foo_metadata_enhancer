#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Prompts Package
分层 Prompt 管理：Layer 1 系统核心 + Layer 2 领域知识 + Layer 3 用户偏好

模块结构：
- system_core.py      Layer 1：不可改的代码常量（角色、JSON 契约、输出 schema）
- domain_defaults.py  Layer 2：领域知识默认值（流派表、版本表、源优先级、平台清单）
- domain_loader.py    Layer 2：MD 文件热重载加载器
- user_prefs.py       Layer 3：用户偏好 schema 与默认值
- provider_profiles.py Provider 差异 Profile
- composer.py         三层组装器（核心入口）
- base.py             旧函数薄包装（向后兼容）
- scrape_prompts.py   Scrape 层常量别名（向后兼容）
- enhance_prompts.py  Enhance 层常量别名（向后兼容）
- fallback_prompts.py 旧常量别名 + build_inference_prompt（向后兼容）
"""

# ===== 新分层 API（推荐使用）=====
from .composer import (
    PromptComposer,
    get_composer,
    reset_composer,
)
from .domain_loader import (
    DomainKnowledgeLoader,
    get_domain_knowledge_loader,
    reset_domain_knowledge_loader,
)
from .user_prefs import (
    PromptPrefs,
    DEFAULT_USER_PREFS,
    DEFAULT_USER_PREFS_DICT,
    parse_user_prefs,
)
from .provider_profiles import (
    PROVIDER_PROFILES,
    get_provider_profile,
)
from .system_core import (
    SYSTEM_CORE_ROLE,
    SYSTEM_CORE_JSON_REQUIREMENTS,
)
from .domain_defaults import (
    DEFAULT_GENRE_CATEGORIES,
    DEFAULT_EDITION_TYPES,
    DEFAULT_SOURCE_PRIORITY,
    DEFAULT_TRANSLATION_PLATFORMS,
)

# ===== 向后兼容（旧 API，保持调用方零改动）=====
from .scrape_prompts import (
    BATCH_RESOLVE_SYSTEM_PROMPT,
    BATCH_ENHANCED_SYSTEM_PROMPT,
)
from .enhance_prompts import (
    BATCH_ENHANCE_SYSTEM_PROMPT,
)
from .fallback_prompts import (
    build_inference_prompt,
    INFERENCE_SYSTEM_PROMPT,
)
from .base import (
    format_json_output_requirements,
    format_track_id_requirements,
)

__all__ = [
    # 新分层 API
    "PromptComposer",
    "get_composer",
    "reset_composer",
    "DomainKnowledgeLoader",
    "get_domain_knowledge_loader",
    "reset_domain_knowledge_loader",
    "PromptPrefs",
    "DEFAULT_USER_PREFS",
    "DEFAULT_USER_PREFS_DICT",
    "parse_user_prefs",
    "PROVIDER_PROFILES",
    "get_provider_profile",
    "SYSTEM_CORE_ROLE",
    "SYSTEM_CORE_JSON_REQUIREMENTS",
    "DEFAULT_GENRE_CATEGORIES",
    "DEFAULT_EDITION_TYPES",
    "DEFAULT_SOURCE_PRIORITY",
    "DEFAULT_TRANSLATION_PLATFORMS",
    # 向后兼容
    "BATCH_RESOLVE_SYSTEM_PROMPT",
    "BATCH_ENHANCED_SYSTEM_PROMPT",
    "BATCH_ENHANCE_SYSTEM_PROMPT",
    "build_inference_prompt",
    "INFERENCE_SYSTEM_PROMPT",
    "format_json_output_requirements",
    "format_track_id_requirements",
]
