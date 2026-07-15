#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Prompt Composer（三层 Prompt 组装器）

按"变化频率 + 用户影响面"分层组装 system prompt：
    Layer 1（系统核心，代码常量）
      + Provider Profile（代码内）
      + Layer 2（领域知识，MD 文件热重载）
      + Layer 3（用户偏好，settings.json）
      + Layer 1（输出 schema，尾部强化）

组装顺序的设计原则：
- 头部：角色定义 + JSON 强约束（让 AI 一开始就进入正确模式）
- 中部：Provider 差异 + 领域知识 + 用户偏好（可变内容）
- 尾部：输出 schema（强化输出契约，防止 AI 在长文本中遗忘格式）
"""

import logging
from typing import Optional, List

from .system_core import (
    SYSTEM_CORE_ROLE,
    SYSTEM_CORE_ROLE_RESOLVE,
    SYSTEM_CORE_ROLE_ENHANCED,
    SYSTEM_CORE_ROLE_ENHANCE,
    SYSTEM_CORE_ROLE_INFERENCE,
    SYSTEM_CORE_JSON_REQUIREMENTS,
    TRACK_ID_REQUIREMENTS,
    ANTI_HALLUCINATION_RULES,
    CONFIDENCE_GUIDELINES,
    STAGE1_OUTPUT_SCHEMA,
    STAGE1_ENHANCED_OUTPUT_SCHEMA,
    STAGE2_OUTPUT_SCHEMA,
    FALLBACK_OUTPUT_SCHEMA,
    AI_SCRAPE_OUTPUT_SCHEMA,
    build_batch_example,
)
from .domain_defaults import (
    DEFAULT_GENRE_CATEGORIES,
    DEFAULT_EDITION_TYPES,
    DEFAULT_SOURCE_PRIORITY,
    DEFAULT_TRANSLATION_PLATFORMS,
    PLATFORM_NAMES,
    PLATFORM_DESC,
)
from .user_prefs import PromptPrefs, DEFAULT_USER_PREFS, parse_user_prefs
from .provider_profiles import get_provider_profile
from .domain_loader import DomainKnowledgeLoader, get_domain_knowledge_loader

logger = logging.getLogger(__name__)


class PromptComposer:
    """三层 Prompt 组装器，单次调用产出完整 system prompt

    生命周期：
    - 在 Processor 初始化时构造一次
    - 每次 build_*() 调用读取最新 config（ConfigManager 单例刷新后即生效）
    - Layer 2 的 MD 文件通过 DomainKnowledgeLoader 的 mtime 检查热重载
    """

    def __init__(self, config: dict, dk_loader: Optional[DomainKnowledgeLoader] = None):
        """初始化组装器

        Args:
            config: 配置字典（来自 ConfigManager）
            dk_loader: 领域知识加载器；None 时自动获取单例
        """
        self._config = config
        self._dk = dk_loader

    @property
    def dk_loader(self) -> DomainKnowledgeLoader:
        """获取领域知识加载器（惰性初始化）"""
        if self._dk is None:
            self._dk = get_domain_knowledge_loader()
        return self._dk

    @property
    def user_prefs(self) -> PromptPrefs:
        """从 config 解析用户偏好（每次调用读取最新值）"""
        prefs_dict = self._config.get("prompts", {}).get("user_prefs", {})
        return parse_user_prefs(prefs_dict)

    @property
    def provider_name(self) -> str:
        """当前 Provider 名称"""
        return self._config.get("providers", {}).get("default", "")

    @property
    def provider_extra_instructions(self) -> str:
        """Provider 特定提示"""
        return get_provider_profile(self.provider_name).get("extra_instructions", "")

    # =========================================================================
    # Stage 2：翻译 / 流派 / 版本
    # =========================================================================

    def build_stage2_system_prompt(self) -> str:
        """构建 Stage2 系统提示（翻译/流派/版本）

        组装顺序：
        Layer 1（角色 + JSON 契约）
          + Provider Profile
          + Layer 2（流派表 + 版本表 + 翻译平台清单）
          + Layer 3（翻译规则 + 流派语言 + 自定义 hints）
          + Layer 1（输出 schema，尾部强化）
        """
        parts: List[str] = [
            # Layer 1：角色 + JSON 契约
            SYSTEM_CORE_ROLE_ENHANCE,
            self._build_stage2_task_description(),
            SYSTEM_CORE_JSON_REQUIREMENTS,
            # Provider Profile
            self.provider_extra_instructions,
            # Layer 2：领域知识（MD 文件热重载，缺失时用默认值）
            self.dk_loader.get_genre_categories(),
            self.dk_loader.get_edition_types(),
            # Layer 3：用户偏好
            self._build_translation_rules(),
            self._build_genre_language_rule(),
            self._build_custom_hints(),
            # Layer 1：尾部强化输出契约
            STAGE2_OUTPUT_SCHEMA,
        ]
        return "\n\n".join(p for p in parts if p and p.strip())

    def _build_stage2_task_description(self) -> str:
        """构建 Stage2 任务描述

        注：具体翻译哪些字段由运行时 EnhancementOptions 在 user message 中指定，
        system prompt 仅描述任务能力，不重复声明字段开关。
        """
        tasks = [
            "1. Chinese translations for title, album, and artist (as requested in the user message)",
            "2. Genre classification with confidence",
            '3. Edition identification (e.g., "Original Release", "Remastered", "Live", "Demo", etc.)',
        ]
        return "Analyze the provided tracks and return for each:\n" + "\n".join(tasks)

    def _build_translation_rules(self) -> str:
        """构建翻译规则（基于用户偏好动态生成）"""
        prefs = self.user_prefs
        rules: List[str] = ["Translation Rules (CRITICAL):"]

        style = prefs.translation_style
        if style == "official":
            platforms = prefs.translation_platform_priority
            rules.append("")
            rules.append("STEP 1 - MANDATORY: Search for official translations from these platforms (in order of priority):")
            for i, p in enumerate(platforms, 1):
                name = PLATFORM_NAMES.get(p, p)
                desc = PLATFORM_DESC.get(p, "")
                rules.append(f"  {i}. {name} - {desc}")
            rules.append("")
            rules.append("STEP 2 - If found on ANY platform above, USE THAT EXACT translation. DO NOT modify it.")
            rules.append("STEP 3 - Only translate yourself if NO official translation exists on ANY platform.")
        elif style == "literal":
            rules.append("Translate literally, preserving original meaning word-by-word.")
        elif style == "semantic":
            rules.append("Translate by meaning, prioritizing natural Chinese expression over literal word-by-word.")
        else:
            rules.append(f"Translation style: {style}")

        # 字段开关由运行时 EnhancementOptions 控制，system prompt 不重复声明

        # 不确定时保留原文
        if prefs.keep_original_when_uncertain:
            rules.append("")
            rules.append("If uncertain about a translation, leave the field empty rather than guessing.")

        # 置信度阈值
        min_conf = prefs.min_translation_confidence
        rules.append("")
        rules.append(f"translation_confidence: 0.0-1.0. Use 0.0 if no translation needed (already Chinese).")
        rules.append(f"Treat confidence below {min_conf} as low confidence.")

        return "\n".join(rules)

    def _build_genre_language_rule(self) -> str:
        """构建流派语言规则"""
        lang = self.user_prefs.genre_language
        if lang == "chinese":
            return "Genre Language Rule:\nReturn genre in Chinese (e.g., 摇滚, 流行, 古典, 爵士)."
        if lang == "bilingual":
            return "Genre Language Rule:\nReturn genre as 'English (中文)' format (e.g., 'Rock (摇滚)', 'Pop (流行)')."
        return "Genre Language Rule:\nReturn genre in English (e.g., Rock, Pop, Classical, Jazz)."

    def _build_custom_hints(self) -> str:
        """构建自定义翻译 hints 与附加指令"""
        prefs = self.user_prefs
        hints = prefs.custom_translation_hints.strip()
        extra = prefs.custom_instructions.strip()

        if not hints and not extra:
            return ""

        lines: List[str] = []
        if hints:
            lines.append("Custom Translation Hints (user-provided, apply when matching original name):")
            for line in hints.split("\n"):
                line = line.strip()
                if line and "=" in line:
                    lines.append(f"  - {line}")
            lines.append("")

        if extra:
            lines.append("Additional user instructions:")
            lines.append(extra)

        return "\n".join(lines)

    # =========================================================================
    # Stage 1：候选决策
    # =========================================================================

    def build_stage1_system_prompt(self, enhanced: bool = False) -> str:
        """构建 Stage1 系统提示（候选决策）

        Args:
            enhanced: 是否增强模式（候选不足时启用推断）

        组装顺序：
        Layer 1（角色 + JSON 契约）
          + Provider Profile
          + Layer 2（数据源优先级）
          + 任务描述 + 决策指南
          + Layer 1（输出 schema，尾部强化）
        """
        if enhanced:
            role = SYSTEM_CORE_ROLE_ENHANCED
            task_desc = self._build_stage1_enhanced_task()
            schema = STAGE1_ENHANCED_OUTPUT_SCHEMA
        else:
            role = SYSTEM_CORE_ROLE_RESOLVE
            task_desc = self._build_stage1_normal_task()
            schema = STAGE1_OUTPUT_SCHEMA

        parts: List[str] = [
            # Layer 1：角色
            role,
            task_desc,
            # Layer 1：JSON 契约
            SYSTEM_CORE_JSON_REQUIREMENTS,
            # Provider Profile
            self.provider_extra_instructions,
            # Layer 2：数据源优先级
            self.dk_loader.get_source_priority(),
            # 决策指南
            self._build_stage1_guidelines(enhanced),
            CONFIDENCE_GUIDELINES,
            ANTI_HALLUCINATION_RULES,
            # Layer 1：尾部强化输出契约
            schema,
        ]
        return "\n\n".join(p for p in parts if p and p.strip())

    def _build_stage1_normal_task(self) -> str:
        """Stage1 普通模式任务描述"""
        return """For each track, you will receive:
1. An original query (what the user provided)
2. A list of candidates from different sources (MusicBrainz, Discogs, etc.)

You need to:
1. Select the most accurate information for each track
2. Fill in missing fields when possible
3. Correct any obvious errors
4. Provide a confidence score for each decision"""

    def _build_stage1_enhanced_task(self) -> str:
        """Stage1 增强模式任务描述"""
        return """In addition to selecting from candidates, you may:
1. Infer missing metadata from patterns
2. Correct obvious errors in candidates
3. Fill in genre and other derived fields
4. Make educated guesses when candidates are incomplete

For each track, you will receive:
1. An original query (what the user provided)
2. A list of candidates from different sources (MusicBrainz, Discogs, etc.)"""

    def _build_stage1_guidelines(self, enhanced: bool) -> str:
        """Stage1 决策指南"""
        guidelines = """Guidelines:
- Prefer candidates from authoritative sources (MusicBrainz > Discogs > AI)
- Higher candidate confidence should generally be trusted more
- If candidates disagree, use your knowledge to make the best choice
- Only include fields you can confidently determine
- confidence should reflect how certain you are about the final result"""

        if enhanced:
            guidelines += """
- Be more creative in filling gaps
- Use musical knowledge to infer genre, era, etc.
- Mark lower confidence for inferred fields
- Still prefer concrete candidate data when available"""

        return guidelines

    # =========================================================================
    # Fallback：AI 推断（所有数据源失败时）
    # =========================================================================

    def build_fallback_system_prompt(self) -> str:
        """构建 Fallback 系统提示（AI 推断）

        组装顺序：
        Layer 1（角色 + JSON 契约 + 输出 schema）
          + Provider Profile
          + 防幻觉 + 置信度指南
        """
        parts: List[str] = [
            SYSTEM_CORE_ROLE_INFERENCE,
            SYSTEM_CORE_JSON_REQUIREMENTS,
            # Provider Profile
            self.provider_extra_instructions,
            # 行为规则
            FALLBACK_OUTPUT_SCHEMA,
            """Guidelines:
- Only provide information you can reasonably infer
- Set confidence to 0.0-0.5 for uncertain inferences
- Leave fields empty if you cannot infer them
- This is a fallback when all data sources failed""",
            ANTI_HALLUCINATION_RULES,
        ]
        return "\n\n".join(p for p in parts if p and p.strip())

    # =========================================================================
    # AIAdapter：作为数据源时的系统提示
    # =========================================================================

    def build_ai_scrape_system_prompt(self) -> str:
        """构建 AIAdapter 系统提示（AI 作为数据源刮削）

        组装顺序：
        Layer 1（角色 + JSON 契约）
          + Provider Profile
          + 领域知识（流派表）
          + 输出 schema
        """
        parts: List[str] = [
            SYSTEM_CORE_ROLE,
            "Given a song title and artist, provide accurate metadata information. Return all available fields with confidence scores.",
            SYSTEM_CORE_JSON_REQUIREMENTS,
            # Provider Profile
            self.provider_extra_instructions,
            # Layer 2：流派表
            self.dk_loader.get_genre_categories(),
            # Layer 1：输出 schema
            AI_SCRAPE_OUTPUT_SCHEMA,
            """Guidelines:
- confidence: 0.0-1.0, where 1.0 means absolutely certain
- Only include fields you can confidently identify
- If uncertain about a field, omit it or use low confidence
- For classical music, composer is very important
- For pop/rock, label and year are often available""",
        ]
        return "\n\n".join(p for p in parts if p and p.strip())


# =============================================================================
# 单例工厂与向后兼容入口
# =============================================================================

_composer_instance: Optional[PromptComposer] = None


def get_composer(config: dict = None, dk_loader: Optional[DomainKnowledgeLoader] = None) -> PromptComposer:
    """获取 PromptComposer 单例

    首次调用时传入 config 创建实例。后续调用如果传入新 config，会更新引用，
    使 Layer 3（用户偏好）的修改实时生效。

    Args:
        config: 配置字典（首次调用必须提供；后续可选）
        dk_loader: 领域知识加载器（可选，None 时使用单例）

    Returns:
        PromptComposer: 组装器单例
    """
    global _composer_instance

    if _composer_instance is None:
        if config is None:
            config = {}
        _composer_instance = PromptComposer(config, dk_loader)
    else:
        # 更新 config 引用，让用户偏好实时生效
        if config is not None:
            _composer_instance._config = config
        if dk_loader is not None:
            _composer_instance._dk = dk_loader

    return _composer_instance


def reset_composer() -> None:
    """重置单例（主要用于测试）"""
    global _composer_instance
    _composer_instance = None


# =============================================================================
# 向后兼容：旧常量别名
# 调用方仍可 import BATCH_*_SYSTEM_PROMPT，内部走 Composer（默认 config）
# =============================================================================

def _build_default_stage1_normal_prompt() -> str:
    """使用默认 config 构建 Stage1 普通模式 Prompt"""
    return get_composer({}).build_stage1_system_prompt(enhanced=False)


def _build_default_stage1_enhanced_prompt() -> str:
    """使用默认 config 构建 Stage1 增强模式 Prompt"""
    return get_composer({}).build_stage1_system_prompt(enhanced=True)


def _build_default_stage2_prompt() -> str:
    """使用默认 config 构建 Stage2 Prompt"""
    return get_composer({}).build_stage2_system_prompt()


def _build_default_fallback_prompt() -> str:
    """使用默认 config 构建 Fallback Prompt"""
    return get_composer({}).build_fallback_system_prompt()


def _build_default_ai_scrape_prompt() -> str:
    """使用默认 config 构建 AIAdapter Prompt"""
    return get_composer({}).build_ai_scrape_system_prompt()
