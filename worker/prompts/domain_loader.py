#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Domain Knowledge Loader (Layer 2 加载器)

从 <profile>/foo_metadata_enhancer/prompts/*.md 加载领域知识文件，
基于 mtime+size 双重检查实现热重载。

已锁定决策：
1. 文件缺失 → 返回代码默认值（不强制文件存在）
2. 文件存在但内容为空 → 返回空字符串（视为用户故意禁用该段）
3. mtime 检查时机 = 每次 get() 调用都触发（stat 成本 <0.1ms）
"""

import logging
from pathlib import Path
from typing import Optional

from .domain_defaults import (
    DEFAULT_GENRE_CATEGORIES,
    DEFAULT_EDITION_TYPES,
    DEFAULT_SOURCE_PRIORITY,
    DEFAULT_TRANSLATION_PLATFORMS,
)

logger = logging.getLogger(__name__)


class DomainKnowledgeLoader:
    """领域知识文件加载器，基于 mtime+size 双重检查实现热重载

    单例，跨调用保留缓存。通过 get_domain_knowledge_loader() 获取实例。
    """

    _instance: Optional["DomainKnowledgeLoader"] = None

    def __init__(self, prompts_dir: Path):
        """初始化加载器

        Args:
            prompts_dir: prompts 目录路径（<profile>/foo_metadata_enhancer/prompts/）
        """
        self._dir = prompts_dir
        # 缓存：filename -> (mtime, size, content)
        self._cache: dict[str, tuple[float, int, str]] = {}

    def get(self, filename: str, default: str) -> str:
        """获取领域知识内容，自动热重载

        Args:
            filename: MD 文件名（相对 prompts_dir）
            default: 文件不存在时使用的默认值

        Returns:
            str: 文件内容、空字符串（空文件）、或默认值（文件不存在/读取失败）
        """
        path = self._dir / filename

        try:
            stat = path.stat()
        except FileNotFoundError:
            # 决策 1：文件不存在 → 默认值
            return default
        except OSError as e:
            # Windows 下文件被独占打开时 stat 可能失败
            logger.warning(f"[DomainKnowledgeLoader] stat failed for {path}: {e}")
            cached = self._cache.get(filename)
            return cached[2] if cached else default

        # mtime + size 双重检查（mtime 在某些 FS 上分辨率粗）
        key = (stat.st_mtime, stat.st_size)
        cached = self._cache.get(filename)
        if cached and (cached[0], cached[1]) == key:
            return cached[2]

        # 读取内容
        try:
            content = path.read_text(encoding='utf-8')
        except OSError as e:
            # 用户用编辑器打开文件时可能独占锁定
            logger.warning(f"[DomainKnowledgeLoader] read failed for {path}: {e}, using cached or default")
            return cached[2] if cached else default
        except UnicodeDecodeError as e:
            logger.error(f"[DomainKnowledgeLoader] UTF-8 decode failed for {path}: {e}, using default")
            return default

        # 决策 2：空文件视为禁用该段，返回空字符串
        if not content.strip():
            logger.info(f"[DomainKnowledgeLoader] {filename} is empty, treating as intentional override")
            self._cache[filename] = (stat.st_mtime, stat.st_size, "")
            return ""

        self._cache[filename] = (stat.st_mtime, stat.st_size, content)
        return content

    def invalidate(self, filename: str = None) -> None:
        """显式失效缓存（预留，正常流程不需要调用）

        Args:
            filename: 指定文件名；None 表示清空所有缓存
        """
        if filename:
            self._cache.pop(filename, None)
        else:
            self._cache.clear()

    # ---- 便捷方法：直接获取各领域知识 ----

    def get_genre_categories(self) -> str:
        """获取流派分类列表"""
        return self.get("genre_categories.md", DEFAULT_GENRE_CATEGORIES)

    def get_edition_types(self) -> str:
        """获取版本类型列表"""
        return self.get("edition_types.md", DEFAULT_EDITION_TYPES)

    def get_source_priority(self) -> str:
        """获取数据源优先级"""
        return self.get("source_priority.md", DEFAULT_SOURCE_PRIORITY)

    def get_translation_platforms(self) -> str:
        """获取翻译平台清单"""
        return self.get("translation_platforms.md", DEFAULT_TRANSLATION_PLATFORMS)


# =============================================================================
# 单例工厂
# =============================================================================

_domain_knowledge_loader: Optional[DomainKnowledgeLoader] = None


def get_domain_knowledge_loader(prompts_dir: Path = None) -> DomainKnowledgeLoader:
    """获取 DomainKnowledgeLoader 单例

    首次调用时必须提供 prompts_dir。后续调用可省略，复用已创建的实例。
    若 prompts_dir 变化（例如 profile 切换），传入新路径会重建单例。

    Args:
        prompts_dir: prompts 目录路径（首次调用必须提供）

    Returns:
        DomainKnowledgeLoader: 加载器单例
    """
    global _domain_knowledge_loader

    if _domain_knowledge_loader is None or (prompts_dir and _domain_knowledge_loader._dir != prompts_dir):
        if prompts_dir is None:
            # 无路径时使用临时空目录，所有 get() 都会返回默认值
            prompts_dir = Path(".")
        _domain_knowledge_loader = DomainKnowledgeLoader(prompts_dir)
        logger.debug(f"[DomainKnowledgeLoader] Initialized with dir: {prompts_dir}")

    return _domain_knowledge_loader


def reset_domain_knowledge_loader() -> None:
    """重置单例（主要用于测试）"""
    global _domain_knowledge_loader
    _domain_knowledge_loader = None
