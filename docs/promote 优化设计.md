# foo_metadata_enhancer Prompt 分层设计

> 目标：让 Prompt 可被**实时微调**，同时保护正确性关键的规则不被破坏。
>
> 分层依据：**变化频率 + 用户影响面**，而非按功能拆分。

---

## 一、现状诊断

### 1.1 当前 Prompt 全部硬编码在 Python 源码

| 位置 | 内容 | 调用方 |
|------|------|--------|
| `worker/prompts/base.py` | JSON 格式要求、track_id 规则、置信度指南、数据源优先级、流派分类表、版本类型表 | 所有 Stage 共享 |
| `worker/prompts/stage1_prompts.py` | `BATCH_RESOLVE_SYSTEM_PROMPT`、`BATCH_ENHANCED_SYSTEM_PROMPT` | `AIResolver` |
| `worker/prompts/stage2_prompts.py` | `BATCH_ENHANCE_SYSTEM_PROMPT`（含翻译规则、平台优先级、流派表、版本表、输出 schema） | `Stage2Processor` |
| `worker/prompts/fallback_prompts.py` | `INFERENCE_SYSTEM_PROMPT`、`build_inference_prompt()` | Fallback Controller |
| `worker/ai/ai_data_source.py` | `AIAdapter.SCRAPE_SYSTEM_PROMPT`（类内常量） | `AIAdapter` |

### 1.2 核心问题

1. **无法实时微调**：改任意 Prompt 文本需编辑 `.py` 源码、重新打包、重启 Worker。
2. **职责混杂**：Stage2 的系统 Prompt 把"翻译平台优先级""流派分类表""JSON 输出契约""AI 角色"四类不同变化频率的内容糅在一个字符串里。
3. **用户偏好无入口**：用户无法表达"官方译名优先""保留英文艺术家名""网易云优先于 QQ 音乐""流派用中文"等偏好，只能接受硬编码结果。
4. **Provider 差异无承载**：不同模型对 JSON 稳定性、温度、上下文长度要求不同，目前只能改 `extra_params.temperature`，Prompt 层差异无处安放。
5. **正确性规则无保护**：JSON 输出格式、track_id 完整性、批量结果数量对齐等"一旦破坏即故障"的规则，与可调内容混在同一常量里，用户若自行修改极易导致解析失败。
6. **设计文档与实现脱节**：本文档前版提议的 `prompts/system.md` + `user/preference.md` + `providers/glm.md` 全文件方案未落地，且本身未回答"哪些归用户、哪些归系统、用户偏好为何用文件而非 UI"。

### 1.3 现有可复用机制

| 通道 | 文件 | 用途 | 已贯通 C++↔Python |
|------|------|------|-------------------|
| C++ Settings | `<profile>/foo_metadata_enhancer/settings.json` | UI 持久化（Provider、API Key、批量大小、MB 调优等） | 是（`_merge_cpp_settings`） |
| YAML Config | `worker/config.yaml` | Worker 静态默认值，被 settings.json 覆盖 | 否（Python 独占） |
| Preferences UI | `plugin/preferences_page.cpp` | General / DataSources / Advanced 三子页 | 是 |

**新设计复用这套机制，不引入新通道。**

---

## 二、设计目标

1. **实时微调**：用户在 Preferences 调整偏好 → 点 Apply → 下一次 AI 调用立即生效，无需重启 foobar2000、无需重启 Worker。
2. **职责分层**：按"变化频率 + 用户影响面"分层，不按功能拆分，避免 Token 重复。
3. **保护稳定性**：JSON 契约、track_id 规则等不可被用户改动。
4. **零额外依赖**：不引入 Jinja 等模板引擎，纯 Python 字符串拼装。
5. **向后兼容**：保留 `BATCH_*_SYSTEM_PROMPT` 等入口符号，内部重构为组装器，调用方可零改动迁移。

---

## 三、三层分层模型

```
┌─────────────────────────────────────────────────────────────┐
│  Layer 3：用户偏好层（User Preference Layer）                 │
│  ─────────────────────────────────────────────────────────  │
│  来源：Preferences UI → settings.json → ConfigManager       │
│  内容：翻译风格、字段开关、平台优先级、流派语言、自定义指令    │
│  变化频率：高（用户随时改）                                  │
│  可见性：UI 暴露                                            │
└─────────────────────────────────────────────────────────────┘
                          ↓ 注入
┌─────────────────────────────────────────────────────────────┐
│  Layer 2：领域知识层（Domain Knowledge Layer）                │
│  ─────────────────────────────────────────────────────────  │
│  来源：<profile>/foo_metadata_enhancer/prompts/*.md（热加载）│
│  内容：流派分类表、版本类型表、数据源优先级、翻译平台清单      │
│  变化频率：低（专家偶尔改）                                  │
│  可见性：文件可见，UI 不暴露                                 │
└─────────────────────────────────────────────────────────────┘
                          ↓ 拼装
┌─────────────────────────────────────────────────────────────┐
│  Layer 1：系统核心层（System Core Layer）                    │
│  ─────────────────────────────────────────────────────────  │
│  来源：Python 代码内常量（不可修改）                         │
│  内容：AI 角色、JSON 输出契约、track_id 规则、防幻觉约束     │
│  变化频率：极低（随版本发布）                                │
│  可见性：源码可见，对用户透明                                │
└─────────────────────────────────────────────────────────────┘
                          +
┌─────────────────────────────────────────────────────────────┐
│  Provider Profile（薄层，代码内）                            │
│  内容：JSON 稳定性提示、默认温度、上下文约束                  │
│  理由：与 retry / JSON 修复等代码逻辑紧耦合，外置会割裂      │
└─────────────────────────────────────────────────────────────┘
```

### 3.1 Layer 1：系统核心层（代码常量，不可改）

**保留为 Python 模块常量，不外置、不开放修改。**

理由：这些规则一旦被破坏，会导致 JSON 解析失败、track_id 错乱、批量结果错位等不可恢复故障，必须与代码版本绑定。

包含：
- AI 角色定义（"You are a music metadata expert..."）
- JSON 输出强约束（`CRITICAL REQUIREMENTS`：结果数对齐、track_id 原样复制、纯 JSON 无 markdown）
- 防幻觉原则（不确定时保留原文、禁止虚构）
- 批量结果数量对齐规则

**位置**：`worker/prompts/system_core.py`（新文件，从 `base.py` 抽取真正"不可改"的部分）

### 3.2 Layer 2：领域知识层（外部 MD 文件，可热加载）

**从 `<profile>/foo_metadata_enhancer/prompts/` 加载 MD 文件；文件缺失时使用代码内默认值兜底。**

理由：流派分类表、版本类型表、平台清单会随时间演化（新流派、新平台），但普通用户不需要在 UI 调。专家用户直接编辑文件，Worker 调用前检查 mtime 实现热重载。

包含文件：
- `genre_categories.md`：标准流派分类列表
- `edition_types.md`：常见版本类型
- `source_priority.md`：数据源优先级（MusicBrainz > Discogs > AI）
- `translation_platforms.md`：中文译名查询平台清单与描述

#### 3.2.1 路径解析

复用 `ConfigManager._find_foobar_profile()` 已有的多路径回退逻辑（便携版/安装版/LOCALAPPDATA/APPDATA）。

- `ConfigManager` 新增 `get_prompts_dir() -> Path`，返回 `_find_foobar_profile() / "prompts"`，首次调用时 `mkdir(parents=True, exist_ok=True)` 确保目录存在
- `DomainKnowledgeLoader` 通过 `ConfigManager` 拿到路径，不自行做路径查找，避免逻辑重复

#### 3.2.2 默认值来源

MD 文件缺失时回退到代码内默认值，集中放在 `worker/prompts/domain_defaults.py`：

| MD 文件 | 默认值来源（现有代码抽取） |
|---------|--------------------------|
| `genre_categories.md` | `base.py::format_genre_categories()` |
| `edition_types.md` | `base.py::format_edition_types()` |
| `source_priority.md` | `base.py::format_source_priority()` |
| `translation_platforms.md` | 当前嵌在 `stage2_prompts.py::BATCH_ENHANCE_SYSTEM_PROMPT` 的 STEP 1 平台清单 |

`base.py` 旧函数改为调用 `domain_defaults` 常量的薄包装，保持调用方零改动。

#### 3.2.3 加载机制（已锁定决策）

**决策 1**：文件缺失 → 回退到代码默认值（不强制文件存在）
**决策 2**：文件存在但内容为空 → 返回空字符串（视为用户故意禁用该段领域知识，例如清空 `genre_categories.md` 让 AI 自由发挥流派分类）
**决策 3**：mtime 检查时机 = 每次 `PromptComposer.build_*()` 调用都触发（stat 成本 <0.1ms，可忽略）

```python
class DomainKnowledgeLoader:
    """领域知识文件加载器，基于 mtime+size 双重检查实现热重载

    单例，跨调用保留缓存。
    """
    _instance = None

    def __init__(self, prompts_dir: Path):
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
            logger.warning(f"stat failed for {path}: {e}")
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
            logger.warning(f"read failed for {path}: {e}, using cached or default")
            return cached[2] if cached else default
        except UnicodeDecodeError as e:
            logger.error(f"UTF-8 decode failed for {path}: {e}, using default")
            return default

        # 决策 2：空文件视为禁用该段，返回空字符串
        if not content.strip():
            logger.info(f"{filename} is empty, treating as intentional override")
            self._cache[filename] = (stat.st_mtime, stat.st_size, "")
            return ""

        self._cache[filename] = (stat.st_mtime, stat.st_size, content)
        return content
```

#### 3.2.4 关键设计点

- **mtime + size 双重检查**：单 mtime 在 FAT32/某些 Windows FS 上分辨率为 2 秒，叠加 size 检查避免"同秒内编辑"漏检
- **读取失败回退缓存**：用户编辑文件时编辑器可能独占锁定，`read_text` 失败时返回上次缓存内容而非默认值，避免编辑过程中行为回退
- **不使用 `_missing_set`**：`stat()` 对不存在文件抛 `FileNotFoundError` 也很快，且去掉了"用户创建文件后需手动失效缓存"的隐患
- **BOM/CRLF**：`read_text(encoding='utf-8')` 在 Python 3 自动处理 UTF-8 BOM；CRLF 保留不影响 AI 理解，无需转换

#### 3.2.5 模板导出（UI 一键导出）

首次运行 profile 目录不自动创建 MD 文件（保持干净）。用户需要定制时：

- Preferences 的 "AI Prompt 偏好" 子页加"导出默认 Prompt 模板"按钮
- 点击后将 `worker/prompts/templates/` 中的 4 份示例 MD 复制到 `<profile>/foo_metadata_enhancer/prompts/`
- 用户在此基础上编辑，下次 AI 调用即生效

`worker/prompts/templates/` 仅作参考，运行时不读取此目录。

### 3.3 Layer 3：用户偏好层（UI → settings.json → Python 配置）

**通过 Preferences UI 编辑，持久化到 settings.json，经 `ConfigManager._merge_cpp_settings()` 合并到 `config["prompts"]["user_prefs"]`。**

理由：这些是真正影响翻译结果且用户会反复调整的偏好，必须 UI 暴露；用结构化字段而非自由文本，避免用户写出破坏 Prompt 的内容。

| 字段 | 类型 | 选项 / 取值 | 默认值 |
|------|------|------------|--------|
| `translation_style` | enum | `official`（官方译名优先）/ `literal`（字面直译）/ `semantic`（意译） | `official` |
| `translate_title` | bool | 开/关 | `true` |
| `translate_album` | bool | 开/关 | `true` |
| `translate_artist` | bool | 开/关 | `false`（默认保留英文艺术家名） |
| `genre_language` | enum | `english` / `chinese` / `bilingual` | `english` |
| `keep_original_when_uncertain` | bool | 开/关 | `true` |
| `min_translation_confidence` | float | 0.0–1.0 | `0.5` |
| `translation_platform_priority` | list | `["netease", "qq", "spotify", "applemusic", "melon", "oricon"]` 可拖拽排序 | 网易云 → QQ → Spotify → Apple Music |
| `custom_translation_hints` | text | 多行，每行 `原文名 = 译名` | 空 |
| `custom_instructions` | text | 多行自由文本（高级用户附加指令） | 空 |

**UI 入口**：Preferences 新增子页 `AIPreferencePagePrompts`（"AI Prompt 偏好"），与现有 General / DataSources / Advanced 平级。

**C++ 端**：`PluginSettings` 新增 `PromptPrefs` 子结构；`SettingsManager::save/load` 序列化到 settings.json 的 `prompts.user_prefs` 段。

**Python 端**：`ConfigManager._merge_cpp_settings()` 已有合并机制，新增字段直接走同一路径。

---

## 四、Prompt 组装流程

### 4.1 组装器（PromptComposer）

新增 `worker/prompts/composer.py`：

```python
class PromptComposer:
    """三层 Prompt 组装器，单次调用产出完整 system prompt"""

    def __init__(self, config: dict, dk_loader: DomainKnowledgeLoader):
        self._user_prefs = config.get("prompts", {}).get("user_prefs", {})
        self._provider = config.get("providers", {}).get("default", "")
        self._dk = dk_loader

    # ---- Stage 2：翻译 / 流派 / 版本 ----
    def build_stage2_system_prompt(self) -> str:
        parts = [
            # Layer 1
            SYSTEM_CORE_ROLE,
            SYSTEM_CORE_JSON_REQUIREMENTS,
            # Provider Profile
            PROVIDER_PROFILES.get(self._provider, {}).get("extra_instructions", ""),
            # Layer 2
            self._dk.get("genre_categories.md", DEFAULT_GENRE_CATEGORIES),
            self._dk.get("edition_types.md", DEFAULT_EDITION_TYPES),
            # Layer 3
            self._build_translation_rules(),
            self._build_genre_language_rule(),
            self._build_custom_hints(),
            # Layer 1（尾部强化输出契约）
            STAGE2_OUTPUT_SCHEMA,
        ]
        return "\n\n".join(p for p in parts if p.strip())

    def _build_translation_rules(self) -> str:
        style = self._user_prefs.get("translation_style", "official")
        platforms = self._user_prefs.get("translation_platform_priority",
                                         ["netease", "qq", "spotify", "applemusic"])
        rules = []
        if style == "official":
            rules.append("STEP 1 - MANDATORY: Search official translations from:")
            for i, p in enumerate(platforms, 1):
                rules.append(f"  {i}. {PLATFORM_NAMES[p]} - {PLATFORM_DESC[p]}")
            rules.append("STEP 2 - If found on ANY platform, USE THAT EXACT translation.")
            rules.append("STEP 3 - Only translate yourself if NO official translation exists.")
        elif style == "literal":
            rules.append("Translate literally, preserving original meaning word-by-word.")
        elif style == "semantic":
            rules.append("Translate by meaning, prioritizing natural Chinese expression.")

        fields = []
        if self._user_prefs.get("translate_title", True):  fields.append("title_zh")
        if self._user_prefs.get("translate_album", True):  fields.append("album_zh")
        if self._user_prefs.get("translate_artist", False): fields.append("artist_zh")
        rules.append(f"Translate ONLY these fields: {', '.join(fields) or 'NONE'}")

        if self._user_prefs.get("keep_original_when_uncertain", True):
            rules.append("If uncertain, leave the field empty rather than guessing.")

        min_conf = self._user_prefs.get("min_translation_confidence", 0.5)
        rules.append(f"translation_confidence below {min_conf} means low confidence.")
        return "\n".join(rules)

    def _build_genre_language_rule(self) -> str:
        lang = self._user_prefs.get("genre_language", "english")
        if lang == "chinese":
            return "Genre: Return in Chinese (e.g., 摇滚, 流行, 古典)."
        if lang == "bilingual":
            return "Genre: Return as 'English (中文)' (e.g., 'Rock (摇滚)')."
        return "Genre: Return in English (e.g., Rock, Pop, Classical)."

    def _build_custom_hints(self) -> str:
        hints = self._user_prefs.get("custom_translation_hints", "").strip()
        extra = self._user_prefs.get("custom_instructions", "").strip()
        if not hints and not extra:
            return ""
        lines = []
        if hints:
            lines.append("Custom Translation Hints (user-provided, apply when matching):")
            for line in hints.split("\n"):
                line = line.strip()
                if line and "=" in line:
                    lines.append(f"  - {line}")
        if extra:
            lines.append("Additional user instructions:")
            lines.append(extra)
        return "\n".join(lines)

    # ---- Stage 1：候选决策（normal / enhanced）----
    def build_stage1_system_prompt(self, enhanced: bool) -> str: ...

    # ---- Fallback：AI 推断 ----
    def build_fallback_system_prompt(self) -> str: ...

    # ---- AIAdapter：作为数据源时 ----
    def build_ai_scrape_system_prompt(self) -> str: ...
```

### 4.2 调用方改造

`Stage2Processor._build_batch_enhance_prompt()`：

```python
# 旧
from prompts import BATCH_ENHANCE_SYSTEM_PROMPT
return [{"role": "system", "content": BATCH_ENHANCE_SYSTEM_PROMPT}, ...]

# 新
composer = PromptComposer(self._config, self._dk_loader)
return [{"role": "system", "content": composer.build_stage2_system_prompt()}, ...]
```

`AIResolver`、`AIAdapter`、Fallback Controller 同理。`Composer` 在 Processor 初始化时构造一次，每次调用读取最新 `self._config`（ConfigManager 单例刷新后即生效）。

### 4.3 实时生效路径

```
用户在 Preferences 修改 → 点击 Apply
    ↓
SettingsManager::save() 写 settings.json
    ↓
C++ 下一次 IPC 请求附带最新 settings 快照
    ↓
ConfigManager 单例刷新 _config["prompts"]["user_prefs"]
    ↓
PromptComposer 下次组装读取最新 _config
    ↓
下一次 AI 调用使用新 Prompt
```

**无需重启 Worker，无需重启 foobar2000。** Layer 2 的 MD 文件通过 mtime 检查热重载，同样无需重启。

---

## 五、Provider 定制（薄层，代码内）

不同 Provider 的差异（JSON 稳定性提示、默认温度、上下文约束）通过代码内 Profile 处理，**不放外部文件**：

```python
# worker/prompts/provider_profiles.py
PROVIDER_PROFILES = {
    "zhipu": {
        "extra_instructions": "Ensure JSON is valid. Avoid markdown wrapping.",
        "default_temperature": 0.3,
    },
    "gemini": {
        "extra_instructions": "Return raw JSON without code fences.",
        "default_temperature": 0.2,
    },
    "openrouter": {
        "extra_instructions": "",
        "default_temperature": 0.3,
    },
    "ollama": {
        "extra_instructions": "Be concise. Local model token budget is limited.",
        "default_temperature": 0.1,
    },
}
```

组装顺序：

```
Layer 1（角色 + JSON 契约）
  + Provider Profile（extra_instructions）
  + Layer 2（领域知识）
  + Layer 3（用户偏好）
  + Layer 1（输出 schema，尾部强化）
```

不放外部文件的理由：Provider 差异与代码逻辑（retry 策略、JSON 修复、温度参数）紧耦合，外置反而割裂维护。

---

## 六、目录结构

```
foo_metadata_enhancer/
├── worker/
│   ├── prompts/
│   │   ├── __init__.py              # 导出 Composer 与默认常量
│   │   ├── system_core.py           # Layer 1：代码常量（不可改）
│   │   ├── domain_defaults.py       # Layer 2：默认值（MD 文件缺失时兜底）
│   │   ├── domain_loader.py         # Layer 2：MD 文件加载器（热重载）
│   │   ├── user_prefs.py            # Layer 3：用户偏好 schema 与默认值
│   │   ├── provider_profiles.py     # Provider 差异 Profile
│   │   └── composer.py              # 三层组装器
│   └── ...
├── plugin/
│   └── preferences_page.cpp         # 新增 "AI Prompt 偏好" 子页
└── <profile>/foo_metadata_enhancer/
    ├── settings.json                # 已有，扩展 prompts.user_prefs 段
    └── prompts/                     # 新增：用户可编辑的领域知识文件
        ├── genre_categories.md
        ├── edition_types.md
        ├── source_priority.md
        └── translation_platforms.md
```

---

## 七、迁移路径（向后兼容，分阶段发布）

1. **阶段一**：新增 `composer.py` + `system_core.py` + `domain_defaults.py`；旧 `BATCH_*_SYSTEM_PROMPT` 改为 `composer.build_*()` 的别名，调用方零改动。
2. **阶段二**：新增 `domain_loader.py` + `worker/prompts/templates/` 示例 MD 文件；`ConfigManager.get_prompts_dir()` 落地；Composer 内部 `build_*()` 调用 `DomainKnowledgeLoader.get()` 走"文件优先 → 默认值兜底"路径。本阶段不涉及 UI，专家用户可手动复制 templates 到 profile 目录测试。
3. **阶段三**：C++ 端扩展 `PluginSettings` 增加 `PromptPrefs`；新增 Preferences 子页 `AIPreferencePagePrompts`（含"导出默认 Prompt 模板"按钮，触发 templates → profile/prompts/ 复制）；`ConfigManager._merge_cpp_settings()` 接收 `prompts.user_prefs` 字段。
4. **阶段四**：Stage1 / Stage2 / Fallback / AIAdapter 调用方切换到 Composer，移除旧常量。

每阶段独立可发布，不破坏现有功能。

---

## 八、与原设计文档（前版）的差异

| 维度 | 前版（本文件历史版本） | 本设计 |
|------|----------------------|--------|
| 分层依据 | 按文件功能（system / output / user / provider） | 按**变化频率 + 用户影响面**（系统核心 / 领域知识 / 用户偏好） |
| 用户偏好承载 | `user/preference.md`（文件） | Preferences UI → settings.json（结构化字段） |
| 实时微调 | 未明确机制 | settings.json 合并 + Composer 单例刷新 + MD 文件 mtime 热重载 |
| Provider 定制 | `providers/glm.md`（文件） | 代码内 `PROVIDER_PROFILES`（与逻辑耦合） |
| 系统核心保护 | 未区分可改/不可改 | Layer 1 明确不可改，防止用户破坏 JSON 契约 |
| 领域知识 | 与系统 Prompt 混在一起 | 独立 Layer 2，支持热重载 |
| 过度拆分风险 | 提议按任务拆（translate_album.md 等） | 不拆任务，仅按层拆，避免 Token 重复 |

---

## 九、核心原则

> Prompt 负责定义 AI 行为规则；代码负责执行业务流程。
>
> 按变化频率 + 用户影响面分层，而不是按功能拆分。
>
> **用户能改的暴露到 UI；专家能改的放到文件；谁都不该改的留在代码里。**
