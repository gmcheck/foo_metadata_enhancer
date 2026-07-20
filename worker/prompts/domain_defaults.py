#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Domain Knowledge Defaults (Layer 2 默认值)

当 <profile>/foo_metadata_enhancer/prompts/*.md 文件缺失时使用的兜底内容。
从原 base.py 和 enhance_prompts.py 抽取，保持现有行为不变。

这些内容会随时间演化（新流派、新平台），专家用户可通过编辑 MD 文件覆盖。
代码内默认值随版本发布更新。
"""

# =============================================================================
# 标准流派分类列表
# 对应 MD 文件：genre_categories.md
# =============================================================================

DEFAULT_GENRE_CATEGORIES = """Standard Genre Categories:
Rock, Pop, Classical, Jazz, Electronic, Hip-Hop, R&B, Country, Folk, Blues,
Metal, Punk, Reggae, Latin, World, Soul, Funk, Disco, Techno, House,
Ambient, Experimental, Indie, Alternative, Soundtrack, Musical, Opera,
Gospel, Christian, New Age, Comedy, Spoken Word, Podcast"""

# =============================================================================
# 常见版本类型
# 对应 MD 文件：edition_types.md
# =============================================================================

DEFAULT_EDITION_TYPES = """Common Edition Types:
- Original Release
- Remastered
- Deluxe Edition
- Anniversary Edition
- Live
- Demo
- Acoustic
- Instrumental
- Remix
- Compilation
- Soundtrack
- Single
- EP
- Album"""

# =============================================================================
# 数据源优先级
# 对应 MD 文件：source_priority.md
# =============================================================================

DEFAULT_SOURCE_PRIORITY = """Source Priority (highest to lowest):
1. MusicBrainz - authoritative music database
2. Discogs - comprehensive release database
3. AI inference - when other sources unavailable"""

# =============================================================================
# 中文译名查询平台清单
# 对应 MD 文件：translation_platforms.md
# 从原 enhance_prompts.py::BATCH_ENHANCE_SYSTEM_PROMPT 的 STEP 1 抽取
# =============================================================================

DEFAULT_TRANSLATION_PLATFORMS = """Translation Platform Reference (for official Chinese translations):
- 网易云音乐 (music.163.com) - PRIMARY source for Asian music
- QQ音乐 - Official Chinese translations
- Spotify Chinese version
- Apple Music Chinese version
- Melon/Genie (Korean) - for K-pop official Chinese titles
- Oricon (Japanese) - for J-pop official Chinese titles"""

# =============================================================================
# 平台元数据（用于 Composer 渲染用户自定义优先级）
# key 与 user_prefs.translation_platform_priority 中的元素对应
# =============================================================================

PLATFORM_NAMES = {
    "netease": "网易云音乐 (music.163.com)",
    "qq": "QQ音乐",
    "spotify": "Spotify Chinese version",
    "applemusic": "Apple Music Chinese version",
    "melon": "Melon/Genie (Korean)",
    "oricon": "Oricon (Japanese)",
}

PLATFORM_DESC = {
    "netease": "PRIMARY source for Asian music",
    "qq": "Official Chinese translations",
    "spotify": "Official Chinese translations on Spotify",
    "applemusic": "Official Chinese translations on Apple Music",
    "melon": "For K-pop official Chinese titles",
    "oricon": "For J-pop official Chinese titles",
}
