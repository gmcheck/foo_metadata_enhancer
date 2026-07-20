#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Stage 2 Prompts（薄包装层）

原硬编码 Prompt 已迁移到 PromptComposer 分层组装。
本文件保留旧常量名作为向后兼容别名，内部走 Composer（默认 config）。

调用方如需动态 Prompt（含用户偏好），请直接使用：
    from prompts.composer import get_composer
    prompt = get_composer(config).build_enhance_system_prompt()
"""

from .composer import _build_default_enhance_prompt

# 向后兼容别名：模块加载时用默认 config 构建一次，内容固定
# 如需动态 Prompt，请使用 get_composer(config).build_enhance_system_prompt()
BATCH_ENHANCE_SYSTEM_PROMPT = _build_default_enhance_prompt()
