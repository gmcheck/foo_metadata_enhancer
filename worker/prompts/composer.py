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
    SCRAPE_OUTPUT_SCHEMA,
    SCRAPE_ENHANCED_OUTPUT_SCHEMA,
    ENHANCE_OUTPUT_SCHEMA,
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
        """当前 Provider 名称/协议（用于 profile；优先 runtime current）"""
        try:
            from ai.provider_runtime import get_provider_runtime

            rt = get_provider_runtime()
            if rt is not None:
                row = rt.get_active_row()
                if row:
                    # profile 按 protocol 取；未知则用 name
                    return str(row.get("protocol") or row.get("name") or "")
        except Exception:
            pass
        return self._config.get("providers", {}).get("default", "")

    @property
    def provider_extra_instructions(self) -> str:
        """Provider/protocol 特定提示（V1 统一默认 + 轻量 protocol 差异）"""
        return get_provider_profile(self.provider_name).get("extra_instructions", "")

    # =========================================================================
    # Stage 2：翻译（仅翻译，不再分类/识别版本）
    # =========================================================================

    def build_enhance_system_prompt(self) -> str:
        """构建 Enhance 系统提示（仅翻译）

        组装顺序：
        Layer 1（角色 + JSON 契约）
          + Provider Profile
          + Layer 2（翻译平台清单）
          + Layer 3（翻译规则 + 自定义 hints）
          + Layer 1（输出 schema，尾部强化）

        注意：自 V8.2 起，genre 已移至 Scrape（MusicBrainz 抓取），
        edition 已移除。Enhance 仅保留翻译能力（基于已有元数据生成新价值）。
        """
        parts: List[str] = [
            # Layer 1：角色 + JSON 契约
            SYSTEM_CORE_ROLE_ENHANCE,
            self._build_enhance_task_description(),
            SYSTEM_CORE_JSON_REQUIREMENTS,
            # Provider Profile
            self.provider_extra_instructions,
            # Layer 2：领域知识（仅保留翻译平台清单）
            self.dk_loader.get_translation_platforms(),
            # Layer 3：用户偏好
            self._build_translation_rules(),
            self._build_custom_hints(),
            # Layer 1：尾部强化输出契约
            ENHANCE_OUTPUT_SCHEMA,
        ]
        return "\n\n".join(p for p in parts if p and p.strip())

    def _build_enhance_task_description(self) -> str:
        """构建 Enhance 任务描述（仅翻译）

        注：具体翻译哪些字段由运行时 EnhancementOptions 在 user message 中指定，
        system prompt 仅描述任务能力，不重复声明字段开关。
        """
        return ("Analyze the provided tracks and return Chinese translations "
                "for title, album, and artist (as requested in the user message).")

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

        # 不确定时仍尝试翻译（避免空结果）
        if prefs.keep_original_when_uncertain:
            rules.append("")
            rules.append("If uncertain about a translation, still provide your best guess rather than")
            rules.append("leaving the field empty. Only leave empty if the original is already Chinese.")

        # 置信度阈值
        min_conf = prefs.min_translation_confidence
        rules.append("")
        rules.append("TRANSLATION IS MANDATORY for non-Chinese content:")
        rules.append("- English/Japanese/Korean/other non-Chinese titles MUST be translated")
        rules.append("- Provide your best Chinese translation for every non-Chinese field")
        rules.append("- Set translation_confidence to 0.5-1.0 based on your confidence")
        rules.append("")
        rules.append("No-translation case (RARE):")
        rules.append("- ONLY leave *_zh empty when the original is ALREADY Chinese characters")
        rules.append("- For already-Chinese content, set translation_confidence to 0.0")
        rules.append("- Do NOT skip translation for English or other non-Chinese content")
        rules.append("")
        rules.append(f"translation_confidence: 0.0-1.0")
        rules.append(f"  - 0.0: only for already-Chinese content (no translation needed)")
        rules.append(f"  - 0.3-0.5: low confidence translation (below {min_conf})")
        rules.append(f"  - 0.5-0.7: moderate confidence")
        rules.append(f"  - 0.7-1.0: high confidence")

        return "\n".join(rules)

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

    def build_scrape_system_prompt(self, enhanced: bool = False) -> str:
        """构建 Scrape 系统提示（候选决策）

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
            task_desc = self._build_scrape_enhanced_task()
            schema = SCRAPE_ENHANCED_OUTPUT_SCHEMA
        else:
            role = SYSTEM_CORE_ROLE_RESOLVE
            task_desc = self._build_scrape_normal_task()
            schema = SCRAPE_OUTPUT_SCHEMA

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
            self._build_scrape_guidelines(enhanced),
            CONFIDENCE_GUIDELINES,
            ANTI_HALLUCINATION_RULES,
            # Layer 1：尾部强化输出契约
            schema,
        ]
        return "\n\n".join(p for p in parts if p and p.strip())

    def _build_scrape_normal_task(self) -> str:
        """Scrape 普通模式任务描述"""
        return """For each track, you will receive:
1. An original query (what the user provided)
2. A list of candidates from different sources (MusicBrainz, Discogs, etc.)

You need to:
1. Select the most accurate information for each track
2. Fill in missing fields when possible
3. Correct any obvious errors
4. Provide a confidence score for each decision"""

    def _build_scrape_enhanced_task(self) -> str:
        """Scrape 增强模式任务描述"""
        return """In addition to selecting from candidates, you may:
1. Infer missing metadata from patterns
2. Correct obvious errors in candidates
3. Fill in genre and other derived fields
4. Make educated guesses when candidates are incomplete

For each track, you will receive:
1. An original query (what the user provided)
2. A list of candidates from different sources (MusicBrainz, Discogs, etc.)"""

    def _build_scrape_guidelines(self, enhanced: bool) -> str:
        """Scrape 决策指南"""
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

def _build_default_scrape_normal_prompt() -> str:
    """使用默认 config 构建 Scrape 普通模式 Prompt"""
    return get_composer({}).build_scrape_system_prompt(enhanced=False)


def _build_default_scrape_enhanced_prompt() -> str:
    """使用默认 config 构建 Scrape 增强模式 Prompt"""
    return get_composer({}).build_scrape_system_prompt(enhanced=True)


def _build_default_enhance_prompt() -> str:
    """使用默认 config 构建 Enhance Prompt"""
    return get_composer({}).build_enhance_system_prompt()


def _build_default_fallback_prompt() -> str:
    """使用默认 config 构建 Fallback Prompt"""
    return get_composer({}).build_fallback_system_prompt()


def _build_default_ai_scrape_prompt() -> str:
    """使用默认 config 构建 AIAdapter Prompt"""
    return get_composer({}).build_ai_scrape_system_prompt()
