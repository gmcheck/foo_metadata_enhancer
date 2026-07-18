# foo_metadata_enhancer 用户指南

本文档详细介绍 foobar2000 AI Metadata 插件的安装、配置和使用方法。

---

## 目录

1. [简介](#1-简介)
2. [系统要求](#2-系统要求)
3. [安装指南](#3-安装指南)
4. [快速入门](#4-快速入门)
5. [菜单功能详解](#5-菜单功能详解)
6. [设置面板详解](#6-设置面板详解)
7. [AI Provider 配置](#7-ai-provider-配置)
8. [数据源配置](#8-数据源配置)
9. [缓存系统](#9-缓存系统)
10. [备份与回滚](#10-备份与回滚)
11. [日志系统](#11-日志系统)
12. [常见问题](#12-常见问题)
13. [从源码构建](#13-从源码构建)

---

## 1. 简介

### 1.1 什么是 foo_metadata_enhancer？

foo_metadata_enhancer 是一个 foobar2000 插件，利用人工智能技术自动分析和完善音乐文件的元数据。

### 1.2 核心功能

V8.2 起按三个互不重叠的功能层组织（见 [三功能边界](#三功能边界-v82)）：

| 功能 | 描述 |
|------|------|
| Scrape（刮削） | 从 MusicBrainz、Discogs、AI 获取本地没有的数据（事实获取）。V8.2 起 genre 由本层从 MusicBrainz recording 详情获取 |
| Enhancer（增强） | 基于已有元数据生成新价值（不获取新事实）。当前能力：中文翻译。V8.2 移除 edition 识别 |
| Normalize（规范化） | 已有 Tag → 标准 Tag（一致性归一化）。当前能力：歌手名规范化 |
| 一键处理 | 自动串联刮削 + 增强，跳过中间确认 |
| 多操作回滚 | 每种操作独立快照（Scrape / Translate / Normalize），可单独回滚某类操作 |
| 别名去重 | 编辑后 + 应用前双重去重，防止别名重复 |
| 缓存管理 | 查看和清理缓存数据 |
| 翻译风格配置 | 三种翻译策略：官方译名优先 / 字面直译 / 意译 |
| 不确定结果人工确认 | 当 web search 不可用（如 Zhipu Chat）时，保存不确定结果并提示用户确认 |

#### 三功能边界（V8.2）

| 层 | 职责 | 数据来源 | 产物 | 不做的事 |
|----|------|----------|------|----------|
| Scrape | 获取本地没有的数据（事实获取） | MusicBrainz / Discogs / AI 降级 | title / artist / album / year / genre / composer / musicbrainz_id | 翻译、归一化 |
| Enhancer | 基于已有元数据生成新价值 | 已有 Tag | title_zh / album_zh / artist_zh | 获取新事实、归一化 |
| Normalize | 已有 Tag → 标准 Tag | 已有 Tag | ARTIST / ALBUM ARTIST（规范化后） | 获取新事实、翻译 |

### 1.3 工作流程

```
选择曲目 → 右键菜单 → AI Metadata → 选择操作 → 预览结果 → 确认写入
```

**两种工作模式**：

- **分步模式**：先运行 "Scrape Metadata" 获取基础信息并确认，再运行 "Enhance Metadata" 做翻译增强。适合需要人工审核刮削结果的用户。
- **一键模式**：直接运行 "Scrape & Enhance (Auto)"，插件自动完成刮削后立即启动增强，中间不弹确认框，仅在增强完成后弹一次确认框。适合批量处理。

---

## 2. 系统要求

### 2.1 最低要求

| 组件 | 要求 |
|------|------|
| 操作系统 | Windows 10 或更高版本 |
| foobar2000 | 2.0 或更高版本 |
| 内存 | 4 GB RAM |
| 磁盘空间 | 100 MB（包括缓存） |

### 2.2 推荐配置

| 组件 | 推荐 |
|------|------|
| 操作系统 | Windows 11 |
| 内存 | 8 GB RAM 或更高 |
| 磁盘空间 | 500 MB（用于缓存大型音乐库） |
| 网络 | 稳定的互联网连接（使用在线 AI 服务时） |

### 2.3 Python 环境

插件需要 Python 3.11 或更高版本。

#### 安装 Python

1. 访问 [Python 官网](https://www.python.org/downloads/) 下载最新版本
2. 安装时勾选 **"Add Python to PATH"**
3. 重启 foobar2000

#### 验证安装

```bash
python --version
# 应显示 Python 3.11.x 或更高
```

#### 首次运行

插件首次运行时会自动安装所需的 Python 依赖包。后续运行可在设置中取消选项 `Auto-Install-Packages`。

### 2.4 External Tags 插件（推荐）

如果你使用 **CUE 分轨** 音乐文件，强烈建议安装 [External Tags](https://www.foobar2000.org/components) 插件。

**为什么需要 External Tags？**

| 文件类型 | 无 External Tags | 有 External Tags |
|---------|-----------------|-----------------|
| 普通文件 (MP3/FLAC) | ✅ 正常写入标签 | ✅ 正常写入标签 |
| CUE 分轨 | ❌ 无法写入标签 | ✅ 写入 .etag 文件 |

**安装方法**：
1. 从 foobar2000 组件页面下载 External Tags
2. 解压到 foobar2000 的 `components` 文件夹
3. 重启 foobar2000

**配置方法**：
1. 打开 `File` → `Preferences` → `Advanced` → `Tagging` → `External Tags`
2. 配置如下：
   - ◉ **Use only SQLite (fastest)** - 使用 SQLite 数据库存储，性能最佳
   - ☑ **Open properties dialog after external tag creation** - 创建外部标签后打开属性对话框
   - ☑ **Enable art support in external tags** - 支持在外部标签中存储封面
   - ☑ **Take over all tagging** - 接管所有标签写入（推荐）

---

## 3. 安装指南

### 3.1 下载插件

1. 访问 [GitHub Releases](https://github.com/yourusername/foo_metadata_enhancer/releases) 页面
2. 下载最新版本的 `foo_metadata_enhancer-x.x.x.zip`
3. 解压缩下载的文件

### 3.2 安装组件

1. 关闭 foobar2000
2. 将 `foo_metadata_enhancer.dll` 和 `foo_metadata_enhancer` 文件夹复制到 foobar2000 的 `components` 文件夹
   - 默认路径：`C:\Program Files\foobar2000\components\`
   - 或便携版路径：`foobar2000\components\`
3. 启动 foobar2000

### 3.3 验证安装

1. 打开 foobar2000
2. 点击 **Help** → **About**
3. 在组件列表中查找 "AI Metadata Analysis"
4. 如果看到该组件，说明安装成功

---

## 4. 快速入门

### 4.1 配置 AI Provider

在使用插件之前，需要配置 AI 服务提供商：

1. 点击 **File** → **Preferences**（或按 `Ctrl+P`）
2. 导航到 **AI Metadata** → **General**
3. 选择 AI Provider（推荐 OpenRouter）
4. 输入 API Key
5. 选择模型（推荐 `openai/gpt-4o-mini`）
6. 点击 **Apply** → **OK**

### 4.2 基本使用流程

#### 步骤 1：选择曲目

在 foobar2000 播放列表中选择一个或多个曲目。

> **注意**：曲目必须包含 TITLE 和 ARTIST 标签，否则刮削功能将无法正常工作。

#### 步骤 2：打开 AI Metadata 菜单

右键点击选中的曲目，在弹出菜单中找到 **AI Metadata**。

#### 步骤 3：选择操作

- **Scrape Metadata** - 从外部数据源获取本地没有的数据（分步模式，处理后需确认）
- **Enhance Metadata** - AI 翻译增强（标题/专辑/艺术家译为中文）
- **Scrape & Enhance (Auto)** - 一键模式，自动串联两步，中间不弹确认框

#### 步骤 4：预览和确认

处理完成后，预览对话框会显示所有获取到的元数据，选择要写入的字段和曲目，点击 **Apply Selected**。

> **一键模式提示**：Scrape & Enhance (Auto) 仅在 Enhance 阶段结束时弹一次确认框，Scrape 阶段的所有成功结果会自动应用。

---

## 5. 菜单功能详解

### 5.1 AI Metadata 右键菜单

右键点击选中的曲目，在菜单中找到 **AI Metadata**，菜单按功能分为三组（用分隔线隔开）：

**主操作组**：

| 菜单项 | 功能说明 |
|--------|----------|
| **Scrape Metadata** | 从 MusicBrainz/Discogs/AI 获取本地没有的数据（V8.2 起 genre 也由本层获取） |
| **Enhance Metadata** | AI 翻译增强（标题/专辑/艺术家译为中文）。V8.2 移除 genre 分类与 edition 识别 |
| **Scrape & Enhance (Auto)** | 一键模式：自动串联刮削 + 增强，跳过中间确认 |
| **Normalize Artist** | 歌手名规范化（alias → canonical） |

**回滚组**：

| 菜单项 | 功能说明 |
|--------|----------|
| **Rollback** | 弹窗选择回滚哪种操作：Scrape / Translate / Normalize（每类操作独立快照） |

**工具组**：

| 菜单项 | 功能说明 |
|--------|----------|
| **Cache Statistics** | 查看缓存统计信息 |
| **Clear Cache** | 清除缓存数据 |

---

### 5.2 Scrape Metadata 详解

#### 5.2.1 功能说明

Scrape Metadata 从多个在线数据源获取基础元数据，按照优先级顺序查询：

```
MusicBrainz (优先级最高) → Discogs (补充) → AI (兜底)
```

#### 5.2.2 选项对话框

点击 Scrape Metadata 后，会弹出选项对话框：

**数据源设置**：

| 选项 | 说明 | 默认值 |
|------|------|--------|
| MusicBrainz | 启用 MusicBrainz 数据源 | ☑ 启用 |
| Discogs | 启用 Discogs 数据源 | ☑ 启用 |
| AI (Fallback) | 启用 AI 兜底 | ☑ 启用 |

**置信度阈值**：

| 参数 | 说明 | 默认值 |
|------|------|--------|
| Auto Accept Threshold | 自动接受的置信度阈值 | 0.90 |
| Confirm Threshold | 需要确认的置信度阈值 | 0.70 |

#### 5.2.3 确认结果对话框

处理完成后，会显示确认结果对话框：

**字段选择区域**：

| 复选框 | 字段 | 说明 |
|--------|------|------|
| Title | TITLE | 曲目标题 |
| Artist | ARTIST | 艺术家 |
| Album | ALBUM | 专辑名称 |
| Year | YEAR | 发行年份 |
| Track# | TRACKNUMBER | 曲目编号 |
| Disc# | DISCNUMBER | 光盘编号 |
| Composer | COMPOSER | 作曲家 |
| Lyricist | LYRICIST | 作词家 |
| Conductor | CONDUCTOR | 指挥 |
| Performer | PERFORMER | 演奏者 |
| Label | LABEL | 唱片公司 |

**按钮功能**：

| 按钮 | 功能 |
|------|------|
| Select All | 选择所有曲目 |
| Select None | 取消选择所有曲目 |
| Select Success | 仅选择处理成功的曲目 |
| Edit Item | 编辑选中曲目的元数据 |
| Apply Selected | 应用选中的更改 |
| Cancel | 取消操作 |

#### 5.2.4 可获取的字段

| 字段 | 说明 | 来源 |
|------|------|------|
| TITLE | 曲目标题 | MB/Discogs/AI |
| ARTIST | 艺术家 | MB/Discogs/AI |
| ALBUM | 专辑名称 | MB/Discogs/AI |
| YEAR | 发行年份 | MB/Discogs/AI |
| TRACKNUMBER | 曲目编号 | MB/Discogs |
| DISCNUMBER | 光盘编号 | MB/Discogs |
| COMPOSER | 作曲家 | MB/Discogs |
| LYRICIST | 作词家 | MB/Discogs |
| CONDUCTOR | 指挥 | MB/Discogs |
| PERFORMER | 演奏者 | MB/Discogs |
| LABEL | 唱片公司 | MB/Discogs |
| GENRE | 流派（V8.2 起由本层从 MusicBrainz recording 详情获取） | MB |

---

### 5.3 Enhance Metadata 详解

#### 5.3.1 功能说明

Enhance Metadata 属于 Enhancer 层 —— 基于已有元数据生成新价值，不获取新事实：

- **翻译**：将标题、专辑、艺术家翻译为中文

> V8.2 变更：
> - 移除 **流派分类**（genre 改由 Scrape 层从 MusicBrainz recording 详情获取，见 5.2）
> - 移除 **版本识别**（AI 推断不可靠）

#### 5.3.2 选项对话框

点击 Enhance 后，会显示选项说明：

```
AI will perform the following enhancements:
- Translate metadata to Chinese (Title, Album, Artist)

Select which fields to write in the confirmation dialog.
```

#### 5.3.3 确认结果对话框

**字段选择区域**：

| 复选框 | 字段 | 说明 |
|--------|------|------|
| Title_ZH | TITLE_ZH | 中文标题 |
| Album_ZH | ALBUM_ZH | 中文专辑名 |
| Artist_ZH | ARTIST_ZH | 中文艺术家名 |

> V8.2 起确认对话框列数为 6 列：Track ID / Title ZH / Album ZH / Artist ZH / Confidence / Success（移除了 Genre / Edition 列）。

**已是中文内容的显示行为**：当原始元数据本身就是中文（无需翻译）时，AI 会返回空的 `*_zh` 字段。此时确认对话框会回退显示原始值（Title / Album / Artist），而不是空白单元格，便于用户辨认曲目。同时：

- `Confidence` 列显示 `N/A (Chinese)`
- `Success` 列显示 `Skipped`
- 该行默认**不勾选**，不会写入任何 `*_ZH` 标签（避免用原值覆盖已有标签）
- 如需强制写入，可手动勾选该行

#### 5.3.4 写入的标签

| 标签 | 说明 | 示例值 |
|------|------|--------|
| TITLE_ZH | 中文标题 | "芳华之年" |
| ALBUM_ZH | 中文专辑名 | "百惠传" |
| ARTIST_ZH | 中文艺术家名 | "山口百惠" |

#### 5.3.5 翻译风格配置（Translation Style）

Enhance Metadata 的翻译行为受 **Translation Style** 设置控制，可在 `Preferences → AI Metadata → AI Prompt` 页面配置。共三个级别，对 AI 实际收到的 system prompt 有显著影响：

| 级别 | 名称 | 行为说明 | 适用场景 |
|------|------|----------|----------|
| `official` | 官方译名优先（默认） | 强制要求 AI 先从网易云/QQ/Spotify/Apple Music 搜索官方译名，找不到才自行翻译 | 华语/日韩音乐，希望使用约定俗成的译名 |
| `literal` | 字面直译 | 逐字直译，保留原意 | 古典音乐、纯音乐、需要严格对应原名 |
| `semantic` | 意译 | 按意义翻译，优先自然中文表达 | 西方流行/摇滚，希望译名更符合中文习惯 |

**实际生成的 Prompt 差异**（以 `official` 为例）：

```
Translation Rules (CRITICAL):

STEP 1 - MANDATORY: Search for official translations from these platforms (in order of priority):
  1. 网易云音乐 (music.163.com) - PRIMARY source for Asian music
  2. QQ音乐 - Official Chinese translations

STEP 2 - If found on ANY platform above, USE THAT EXACT translation. DO NOT modify it.
STEP 3 - Only translate yourself if NO official translation exists on ANY platform.
```

**配套设置**（同一页面）：

| 设置 | 说明 | 默认值 |
|------|------|--------|
| Translation Style | 翻译风格（见上表） | official |
| Genre Language | 流派返回语言（english / chinese / bilingual） | english |
| Keep Original When Uncertain | 不确定时保留原文而非猜测 | ☑ |
| Min Translation Confidence | 最低翻译置信度阈值 | 0.5 |
| Translation Platform Priority | 翻译平台优先级（拖拽排序） | netease → qq → spotify → applemusic |
| Custom Translation Hints | 自定义翻译对照（每行 `原文名 = 译名`），支持中文输入 | （空） |
| Custom Instructions | 高级用户附加指令，支持中文自由文本输入 | （空） |

> **中文输入支持**：Custom Translation Hints 和 Custom Instructions 编辑框已全面支持 UTF-8 中文输入，可放心填写中文译名对照和指令。

---

### 5.4 Scrape & Enhance (Auto) 一键模式详解

#### 5.4.1 功能说明

将 Scrape Metadata 和 Enhance Metadata 两步操作自动串联，适合批量处理。流程如下：

```
Scrape Metadata（自动应用所有成功结果）→ Enhance Metadata（弹确认框）→ 用户确认 → 写入
```

#### 5.4.2 与分步模式的差异

| 对比项 | 分步模式 | 一键模式 (Auto) |
|--------|----------|-----------------|
| Scrape 后是否弹确认框 | ✅ 弹出，用户选择字段 | ❌ 不弹，自动应用所有成功结果 |
| Enhance 后是否弹确认框 | ✅ 弹出 | ✅ 弹出（保留人工审核） |
| 选项对话框 | 各弹一次 | 两个选项对话框连续弹出（先 Scrape 选项，再 Enhance 选项） |
| 适用场景 | 需要人工审核刮削结果 | 批量处理、信任刮削结果 |

#### 5.4.3 使用建议

- **首次使用**或处理**珍贵音乐库**时，建议用分步模式审核 Scrape 结果
- **大批量处理**熟悉曲目时，用一键模式可节省大量点击时间
- 一键模式下若 Scrape 阶段无任何成功结果，会直接结束不会启动 Enhance

---

### 5.5 Rollback 详解

#### 5.5.1 功能说明

V8.2 起支持**多操作回滚**：将选中的曲目按操作类型独立回滚。每种操作（Scrape / Translate / Normalize）维护各自的快照，回滚时仅恢复该操作影响的字段，不影响其他操作的结果。

#### 5.5.2 使用场景

- 翻译结果不满意 → 仅回滚 Translate，保留 Scrape 结果
- 刮削结果错误 → 仅回滚 Scrape，保留翻译
- 歌手规范化错误 → 仅回滚 Normalize
- 全部重来 → 同时勾选所有操作类型

#### 5.5.3 回滚类型选择对话框

点击 **Rollback** 后弹出选择对话框，仅显示存在快照的操作类型及对应曲目数：

```
Select rollback operations:

☑ Scrape (12 tracks)
☑ Translate (10 tracks)
☐ Normalize (0 tracks)   ← 无快照时不显示

[OK] [Cancel]
```

勾选要回滚的操作类型后点击 **OK**，再次确认后执行。

#### 5.5.4 影响字段

| 操作类型 | 影响字段 |
|----------|----------|
| Scrape | 全量（删除所有非黑名单字段后重设） |
| Translate | TITLE_ZH / ALBUM_ZH / ARTIST_ZH |
| Normalize | ARTIST / ALBUM ARTIST / COMPOSER / PERFORMER / ALBUMARTIST |

#### 5.5.5 注意事项

- 仅能回滚存在快照的操作类型
- 每个操作类型仅保留最近一次快照
- 回滚操作不可撤销

---

### 5.6 Cache Statistics 详解

#### 5.6.1 功能说明

显示缓存统计信息，帮助了解缓存使用情况。

#### 5.6.2 统计信息

| 统计项 | 说明 |
|--------|------|
| Total Entries | 缓存条目总数（Stage 1 + Stage 2） |
| Cache Hits | 缓存命中次数 |
| Cache Misses | 缓存未命中次数 |
| Hit Rate | 缓存命中率（%） |
| Database Size | 缓存数据库大小（MB） |
| API Calls Saved | 节省的 API 调用次数 |

---

### 5.7 Clear Cache 详解

#### 5.7.1 功能说明

清除缓存数据，支持两种模式。

#### 5.7.2 清除选项对话框

```
┌─────────────────────────────────────────────┐
│  Clear Cache                                │
├─────────────────────────────────────────────┤
│  Clear cache for selected tracks or all...  │
│                                             │
│  [ ] Clear all cache (not just selected)    │
│                                             │
│  Warning: This action cannot be undone.     │
│                                             │
│                    [Clear]    [Cancel]      │
└─────────────────────────────────────────────┘
```

#### 5.7.3 清除模式

| 选项状态 | 行为 |
|---------|------|
| ☐ Clear all cache（默认） | 只清除**选中曲目**的缓存 |
| ☑ Clear all cache | 清除**全部**缓存 |

#### 5.7.4 清除范围

| 操作 | stage1_cache | stage2_cache | cache_statistics |
|------|-------------|-------------|-----------------|
| 清除选中曲目 | ✓ (按 track_id) | ✓ (按 track_id) | ✗ |
| 清除所有缓存 | ✓ (全部) | ✓ (全部) | ✓ (全部) |

---

## 6. 设置面板详解

访问设置：**File** → **Preferences** → **AI Metadata**

设置页面分为四个子页面：

| 页面 | 功能 |
|------|------|
| General | AI Provider、Python、Cache 设置 |
| Data Sources | MusicBrainz、Discogs 配置 |
| AI Prompt | 翻译风格、流派语言、自定义翻译对照、高级指令 |
| Advanced | 批次大小、超时等高级设置 |

---

### 6.1 General 设置页面

#### 6.1.1 API Settings 区域

| 设置项 | 说明 | 示例值 |
|--------|------|--------|
| **Provider** | AI 服务提供商 | OpenRouter / Zhipu / Gemini / Ollama |
| **API Key** | API 密钥 | sk-or-v1-xxxxxxxx |
| **Use Env Var** | 使用环境变量中的 API Key | ☐ |
| **Model** | AI 模型选择 | openai/gpt-4o-mini |
| **Test** | 测试 API 连接 | 按钮 |

**Provider 说明**：

| Provider | 类型 | 特点 |
|----------|------|------|
| OpenRouter | 云端 | 支持多种模型，按使用付费 |
| Zhipu AI | 云端 | 智谱清言，中文优化 |
| Gemini | 云端 | Google AI，有免费额度 |
| Ollama | 本地 | 完全离线，需要本地 GPU |

**推荐模型**：

| Provider | 推荐模型 | 说明 |
|----------|----------|------|
| OpenRouter | openai/gpt-4o-mini | 性价比高，速度快 |
| OpenRouter | anthropic/claude-3-haiku | 高质量输出 |
| Zhipu AI | glm-4-flash | 中文优化，有免费额度 |
| Gemini | gemini-1.5-flash | 免费，速度快 |
| Ollama | llama3.1:8b | 本地运行，需要 8GB+ VRAM |

#### 6.1.2 Python 区域

| 设置项 | 说明 | 默认值 |
|--------|------|--------|
| **Path** | Python 可执行文件路径 | 自动检测 |
| **Browse** | 浏览选择 Python 路径 | 按钮 |
| **Auto-install packages** | 自动安装 Python 依赖包 | ☑ |
| **状态显示** | 显示 Python 环境状态 | 文本 |

**Python 状态说明**：

| 状态 | 说明 |
|------|------|
| Python OK | Python 环境正常 |
| Python not found | 未找到 Python，需要手动配置路径 |
| Packages missing | 缺少依赖包，启用 Auto-install 或手动安装 |

#### 6.1.3 Cache 区域

| 设置项 | 说明 | 默认值 |
|--------|------|--------|
| **Enable** | 启用缓存 | ☑ |
| **Expiration (days)** | 缓存过期天数 | 365 |
| **Max (MB)** | 最大缓存大小（MB） | 500 |
| **Auto Cleanup** | 自动清理过期缓存 | ☑ |
| **Clear Cache** | 清除缓存按钮 | 按钮 |

**缓存说明**：

- 缓存存储在 SQLite 数据库中
- 位置：`%APPDATA%\foobar2000-v2\foo_metadata_enhancer\cache.db`
- 启用缓存可大幅减少 API 调用，节省费用

#### 6.1.4 Logging 区域

| 设置项 | 说明 | 默认值 |
|--------|------|--------|
| **Level** | 日志级别 | INFO |
| **Max Size (MB)** | 单个日志文件最大大小 | 10 |
| **Open Log Folder** | 打开日志文件夹 | 按钮 |

**日志级别说明**：

| 级别 | 说明 |
|------|------|
| DEBUG | 详细调试信息，用于问题排查 |
| INFO | 一般信息，记录正常操作 |
| WARNING | 警告信息，记录潜在问题 |
| ERROR | 错误信息，记录错误情况 |

---

### 6.2 Data Sources 设置页面

#### 6.2.1 MusicBrainz 区域

| 设置项 | 说明 | 默认值 |
|--------|------|--------|
| **Timeout** | 请求超时时间（秒） | 30 |
| **Retries** | 重试次数 | 3 |
| **Page Size** | 每页结果数 | 25 |
| **Max Pages** | 最大查询页数 | 5 |
| **Score Threshold** | 匹配分数阈值 | 80 |
| **Score Margin** | 分数容差 | 10 |
| **Rate Limit** | 请求速率限制（请求/秒） | 1 |

**MusicBrainz 说明**：

- MusicBrainz 是一个开放的音乐百科全书
- 免费使用，数据质量高
- 有 API 速率限制（每秒 1 次请求）

#### 6.2.2 Discogs 区域

| 设置项 | 说明 | 默认值 |
|--------|------|--------|
| **Timeout** | 请求超时时间（秒） | 30 |
| **Retries** | 重试次数 | 3 |
| **Page Size** | 每页结果数 | 50 |
| **Max Pages** | 最大查询页数 | 3 |
| **Score Threshold** | 匹配分数阈值 | 70 |

**Discogs 说明**：

- Discogs 是一个音乐数据库和市场
- 涵盖大量独立音乐和稀有发行
- 包含详细的版本信息

---

### 6.3 AI Prompt 设置页面

此页面控制 Enhance Metadata 阶段的 AI 翻译/分类行为，所有设置实时生效（下次请求即应用，无需重启 worker）。

#### 6.3.1 Translation 区域

| 设置项 | 说明 | 选项 / 默认值 |
|--------|------|---------------|
| **Translation Style** | 翻译风格 | Official（默认）/ Literal / Semantic |
| **Genre Language** | 流派返回语言 | English（默认）/ Chinese / Bilingual |
| **Keep Original When Uncertain** | 不确定时保留原文 | ☑ 启用（默认） |
| **Min Translation Confidence** | 最低翻译置信度阈值 | 0.5 |
| **Translation Platform Priority** | 翻译平台优先级 | netease → qq → spotify → applemusic |

**Translation Style 三级别说明**：

| 级别 | 实际效果 |
|------|----------|
| Official | AI 必须先从配置的平台（网易云/QQ 等）搜索官方译名，找到则原样使用，找不到才自行翻译 |
| Literal | 直接逐字直译，不查平台 |
| Semantic | 按意义意译，优先自然中文表达 |

#### 6.3.2 Custom Translation Hints 编辑框

自定义翻译对照表，每行一条，格式 `原文名 = 译名`。AI 在翻译时会优先应用这些对照。

**示例**（支持中文输入）：
```
Taylor Swift = 泰勒·斯威夫特
The Beatles = 披头士乐队
Bohemian Rhapsody = 波西米亚狂想曲
```

#### 6.3.3 Custom Instructions 编辑框（Advanced）

高级用户自由文本指令，会原样附加到 AI 的 system prompt 末尾。可用于微调 AI 行为，例如：

```
优先使用港台译名风格，如"席琳·狄翁"而非"席琳·迪翁"。
古典音乐作品名保留原文，不翻译。
```

> **UTF-8 支持**：两个编辑框均使用 Wide API + UTF-8 转换，完整支持中文输入与保存，保存到 `settings.json` 时以 UTF-8 编码存储。

---

### 6.4 Advanced 设置页面

#### 6.4.1 Task Queue 区域

| 设置项 | 说明 | 默认值 |
|--------|------|--------|
| **Batch Size** | Scrape Metadata 每批处理的曲目数 | 50 |

**说明**：
- 较大的批次大小可以提高处理速度
- 但会增加内存使用和单次失败的影响范围
- 建议范围：20-100

#### 6.4.2 AI Batch 区域

| 设置项 | 说明 | 默认值 |
|--------|------|--------|
| **Batch Size** | Enhance Metadata 每批处理的曲目数 | 10 |

**说明**：
- AI 批处理大小取决于模型上下文窗口
- 较大的值可能超出模型限制
- 建议范围：5-20

#### 6.4.3 Timeout 区域

| 设置项 | 说明 | 默认值 |
|--------|------|--------|
| **Per-Track Timeout (sec)** | 每首曲目的超时时间 | 60 |

**说明**：
- 总超时 = 基础超时 + (曲目数 × Per-Track Timeout)
- 网络较慢时可适当增加

---

## 7. AI Provider 配置

### 7.1 OpenRouter

#### 注册和获取 API Key

1. 访问 [OpenRouter](https://openrouter.ai/)
2. 注册账号
3. 进入 [Keys](https://openrouter.ai/keys) 页面
4. 点击 "Create Key" 创建 API Key

#### 配置步骤

```
Preferences → AI Metadata → General
  Provider: OpenRouter
  API Key: sk-or-v1-xxxxxxxx
  Model: openai/gpt-4o-mini
```

#### 推荐模型

| 模型 | 价格 | 特点 |
|------|------|------|
| openai/gpt-4o-mini | $0.15/1M tokens | 性价比最高 |
| anthropic/claude-3-haiku | $0.25/1M tokens | 质量更好 |
| google/gemini-flash-1.5 | $0.075/1M tokens | 最便宜 |

### 7.2 Zhipu AI (智谱清言)

#### 注册和获取 API Key

1. 访问 [智谱 AI 开放平台](https://open.bigmodel.cn/)
2. 注册账号
3. 进入控制台 → API Keys
4. 创建 API Key

#### 配置步骤

```
Preferences → AI Metadata → General
  Provider: Zhipu AI
  API Key: xxxxxxxxxxxxxxxx
  Model: glm-4-flash
```

#### 推荐模型

| 模型 | 特点 |
|------|------|
| glm-4-flash | 快速，有免费额度 |
| glm-4 | 高质量，付费 |

### 7.3 Google Gemini

#### 注册和获取 API Key

1. 访问 [Google AI Studio](https://aistudio.google.com/)
2. 登录 Google 账号
3. 点击 "Get API Key"
4. 创建 API Key

#### 配置步骤

```
Preferences → AI Metadata → General
  Provider: Gemini
  API Key: AIzaSyxxxxxxxxxxxx
  Model: gemini-1.5-flash
```

#### 免费额度

Gemini 提供免费额度，适合轻度使用：
- 15 RPM（每分钟请求数）
- 1,500 RPD（每日请求数）

### 7.4 Ollama (本地)

#### 安装 Ollama

1. 访问 [Ollama](https://ollama.ai/)
2. 下载并安装
3. 运行 `ollama pull llama3.1` 下载模型

#### 配置步骤

```
Preferences → AI Metadata → General
  Provider: Ollama
  Model: llama3.1:8b
```

#### 硬件要求

| 模型 | 最小显存 | 推荐显存 |
|------|----------|----------|
| llama3.1:8b | 6 GB | 8 GB |
| llama3.1:70b | 40 GB | 48 GB |
| mistral:7b | 5 GB | 8 GB |

### 7.5 DeepSeek

#### 注册和获取 API Key

1. 访问 [DeepSeek 开放平台](https://platform.deepseek.com/)
2. 注册账号
3. 进入 API Keys 页面创建 Key

#### 配置步骤

```
Preferences → AI Metadata → General
  Provider: DeepSeek
  API Key: sk-xxxxxxxxxxxxxxxx
  Model: deepseek-chat
```

#### 推荐模型

| 模型 | 特点 |
|------|------|
| deepseek-chat | 通用对话模型，速度快，价格低 |
| deepseek-reasoner | 推理增强，适合复杂分类任务 |

**说明**：DeepSeek 使用 OpenAI 兼容接口，与 Zhipu AI 共享 OpenAI Provider 基类，配置方式类似。

---

## 8. 数据源配置

### 8.1 数据源优先级

Scrape Metadata 按以下优先级查询数据源：

```
1. MusicBrainz (权威，数据质量高)
2. Discogs (补充，涵盖独立音乐)
3. AI (兜底，处理其他源无法找到的情况)
```

### 8.2 匹配算法

1. 使用 TITLE + ARTIST 作为查询条件
2. 如果提供了 ALBUM，用于提高匹配精度
3. 计算匹配分数，选择最佳匹配
4. 如果分数低于阈值，尝试下一个数据源

### 8.3 置信度说明

每个字段都有置信度评分（0-1）：

| 置信度范围 | 说明 | 处理方式 |
|-----------|------|----------|
| ≥ 0.90 | 高置信度 | 自动接受 |
| 0.70 - 0.90 | 中等置信度 | 建议确认 |
| < 0.70 | 低置信度 | 需要人工审核 |

---

## 9. 缓存系统

### 9.1 缓存机制

插件使用 SQLite 数据库缓存处理结果：

- **Scrape 缓存**：基于 TITLE + ARTIST + ALBUM 的查询结果
- **Enhance 缓存**：基于完整元数据的增强结果

### 9.2 缓存键生成

缓存键通过 SHA256 哈希生成：

```
track_id = SHA256(path + "|" + subsong + "|" + file_size)
```

### 9.3 缓存位置

```
%APPDATA%\foobar2000-v2\foo_metadata_enhancer\cache.db
```

### 9.4 缓存统计

| 统计项 | 计算方式 |
|--------|----------|
| Total Entries | stage1_cache 条目数 + stage2_cache 条目数 |
| Cache Hits | 所有条目的 cache_hit_count 总和 |
| Hit Rate | Hits / (Hits + Misses) × 100% |
| API Calls Saved | 等于 Cache Hits |

---

## 10. 备份与回滚

### 10.1 自动备份（多操作快照）

V8.2 起，每次执行 Scrape / Enhance / Normalize 之前，插件会按**操作类型**独立快照当前标签。同一曲目可同时存在多条不同类型的快照：

```
备份位置：%APPDATA%\foobar2000-v2\foo_metadata_enhancer.db
表：metadata_snapshots
唯一约束：(track_id, operation_type)
```

操作类型（`OperationType`）：

| 类型 | 标识 | 影响字段（回滚时仅恢复这些） |
|------|------|-------------------------------|
| Scrape | `scrape` | 全量（删除所有非黑名单字段后重设） |
| Translate | `translate` | TITLE_ZH / ALBUM_ZH / ARTIST_ZH |
| Normalize | `normalize` | ARTIST / ALBUM ARTIST / COMPOSER / PERFORMER / ALBUMARTIST |

> 回滚数据采用 JSON 保存，不同的回滚使用不同的 `operation_type` 标识。每条数据对应具体操作最开始的状态。

### 10.2 备份内容

每条快照包含：

- 曲目 ID（track_id）
- 操作类型（operation_type）
- 所有现有标签（JSON）
- 备份时间戳

### 10.3 回滚操作

1. 选择要回滚的曲目
2. 右键 → **AI Metadata** → **Rollback**
3. 弹窗中勾选要回滚的操作类型（仅显示存在快照的操作，并显示曲目数）
4. 确认回滚

回滚时仅修改该操作类型影响的字段，其他操作的结果保持不变（部分快照应用）。

### 10.4 回滚限制

- 仅能回滚存在快照的操作类型
- 每个操作类型仅保留最近一次快照（覆盖式）
- 回滚操作不可撤销

---

## 11. 日志系统

### 11.1 日志位置

```
%APPDATA%\foobar2000-v2\foo_metadata_enhancer\logs\ai_metadata.log
```

### 11.2 日志级别

| 级别 | 说明 | 使用场景 |
|------|------|----------|
| DEBUG | 详细调试信息 | 问题排查 |
| INFO | 一般信息 | 正常使用 |
| WARNING | 警告信息 | 潜在问题 |
| ERROR | 错误信息 | 错误情况 |

### 11.3 日志轮转

日志文件自动轮转：

- 单文件最大 10 MB
- 保留最近 5 个日志文件

---

## 12. 常见问题

### 12.1 菜单中看不到 AI Metadata

**原因**：
- 插件未正确安装
- foobar2000 版本过低

**解决方案**：
1. 确认 `foo_metadata_enhancer.dll` 在 `components` 文件夹
2. 检查 Help → About 中是否显示插件
3. 升级 foobar2000 到 2.0 或更高版本

### 12.2 处理时提示 "TITLE 或 ARTIST 缺失"

**原因**：Scrape Metadata 需要 TITLE 和 ARTIST 作为查询依据。

**解决方案**：
1. 手动填写缺失的标签
2. 使用其他工具（如 MusicBrainz Picard）先获取基础信息

### 12.3 AI 处理失败

**可能原因**：
- API Key 无效
- 网络连接问题
- API 配额用尽
- 模型不可用

**解决方案**：
1. 检查 API Key 是否正确
2. 测试网络连接
3. 检查 API 账户余额
4. 尝试其他模型

### 12.4 处理速度很慢

**可能原因**：
- 批次大小过小
- 网络延迟
- API 限流

**解决方案**：
1. 增加批次大小（Advanced 设置）
2. 使用更快的 AI 模型
3. 使用本地 Ollama 模型

### 12.5 CUE 分轨无法写入标签

**原因**：CUE 文件无法直接修改。

**解决方案**：
1. 安装 External Tags 插件
2. 按照本文档 2.4 节配置

### 12.6 Worker 进程崩溃

**可能原因**：
- Python 环境问题
- 内存不足
- 依赖包缺失

**解决方案**：
1. 重启 foobar2000
2. 检查 Python 版本（需要 3.11+）
3. 启用 Auto-install packages

---

## 13. 从源码构建

### 13.1 构建环境

| 组件 | 要求 |
|------|------|
| Visual Studio | 2022 with C++ workload |
| CMake | 3.20+ |
| foobar2000 SDK | 2024 |
| vcpkg | 安装 nlohmann-json |
| Python | 3.11+ (运行时需要) |

### 13.2 配置文件

项目包含两套配置文件：

| 文件 | 用途 | Git |
|------|------|-----|
| `worker/config.yaml.template` | 模板文件，API key 留空 | 提交 |
| `worker/config.yaml` | 本地开发用，含真实 API key | 排除 (.gitignore) |

**首次使用**：复制模板并填入你的 API key：

```bash
copy worker\config.yaml.template worker\config.yaml
```

### 13.3 编译

#### 本地开发（自动部署到 foobar2000）

```bash
# Configure - 指定本地 foobar2000 路径，编译后自动部署
cmake -B out/build -G "Visual Studio 17 2022" -A x64 ^
    -DFOOBAR_DEV_DIR="C:/path/to/foobar2000"

# Build
cmake --build out/build --config Release -- /m
```

编译完成后，DLL 和 worker 脚本会自动复制到：
```
<FOOBAR_DEV_DIR>/components/
├── foo_metadata_enhancer.dll
└── foo_metadata_enhancer/
    ├── cache/
    ├── logs/
    └── worker/
```

#### 仅编译（不部署，用于打包）

```bash
cmake -B out/build -G "Visual Studio 17 2022" -A x64
cmake --build out/build --config Release -- /m
```

### 13.4 打包发布

使用 `tools/pack.ps1` 脚本打包：

```powershell
# 使用 .rc 文件中的版本号（自动读取）
.\tools\pack.ps1

# 指定版本号
.\tools\pack.ps1 -Version 1.0.1

# 先编译再打包（一步到位）
.\tools\pack.ps1 -BuildFirst
```

**打包脚本处理内容**：

| 步骤 | 说明 |
|------|------|
| 1. 编译（可选） | 使用 `-BuildFirst` 参数启用 |
| 2. 检查文件 | 验证 DLL 和 worker 目录存在 |
| 3. 准备文件 | 复制 DLL、worker 脚本，排除 `__pycache__` 和 `.pyc` |
| 4. 替换配置 | 删除 `config.yaml`，用 `config.yaml.template` 替代 |
| 5. 创建 zip | 输出到 `zips/foo_metadata_enhancer-<version>.zip` |

**生成的 zip 结构**：

```
foo_metadata_enhancer-1.0.0.zip
├── foo_metadata_enhancer.dll
└── foo_metadata_enhancer/
    ├── cache/              (空目录)
    ├── logs/               (空目录)
    └── worker/             (Python 脚本)
        ├── config.yaml     (从 template 复制，API key 为空)
        ├── ai_worker.py
        ├── requirements.txt
        └── ...
```

### 13.5 CMake 可配置变量

| 变量 | 说明 | 默认值 |
|------|------|--------|
| `VCPKG_PATH` | vcpkg 安装路径 | `D:/Programs/vcpkg` |
| `FOOBAR2000_SDK_PATH` | foobar2000 SDK 路径 | `D:/Programs/foobar2000_sdk` |
| `FOOBAR_DEV_DIR` | 本地 foobar2000 目录（留空则不自动部署） | 空 |

使用示例：

```bash
cmake -B out/build ^
    -DVCPKG_PATH="C:/vcpkg" ^
    -DFOOBAR2000_SDK_PATH="C:/foobar2000_sdk" ^
    -DFOOBAR_DEV_DIR="C:/foobar2000"
```

### 13.6 安装发布包

1. 解压 `foo_metadata_enhancer-x.x.x.zip`
2. 将 `foo_metadata_enhancer.dll` 和 `foo_metadata_enhancer` 文件夹复制到 foobar2000 的 `components/` 目录
3. 编辑 `components/foo_metadata_enhancer/worker/config.yaml`，填入你的 API key
4. 重启 foobar2000

---

## 附录

### A. 支持的音频格式

| 格式 | 读取 | 写入 |
|------|------|------|
| MP3 (ID3v2) | ✓ | ✓ |
| FLAC (Vorbis) | ✓ | ✓ |
| M4A/MP4 | ✓ | ✓ |
| APE (APEv2) | ✓ | ✓ |
| OGG (Vorbis) | ✓ | ✓ |
| WAV (ID3v2) | ✓ | ✓ |
| CUE 分轨 | ✓ | 需要 External Tags |

### B. 文件位置汇总

| 文件 | 位置 |
|------|------|
| 配置文件 | `%APPDATA%\foobar2000-v2\foo_metadata_enhancer\settings.json` |
| 缓存/备份数据库 | `%APPDATA%\foobar2000-v2\foo_metadata_enhancer.db` |
| 日志文件 | `%APPDATA%\foobar2000-v2\foo_metadata_enhancer\logs\` |

### C. 性能优化建议

1. **使用缓存**：避免重复处理相同曲目
2. **合理批次大小**：根据网络状况调整
3. **选择快速模型**：如 gpt-4o-mini、gemini-flash
4. **使用本地模型**：Ollama 无网络延迟
5. **安装 External Tags**：对 CUE 分轨使用 SQLite 存储

---

## 更新日志

### v1.2.0 (V8.2 三功能分界)

- **三功能边界明确**：代码与文档按 Scrape / Enhancer / Normalize 三个互不重叠的层重组（见 [三功能边界](#三功能边界-v82)）
- **Genre 移至 Scrape 层**：从 MusicBrainz recording 详情获取（事实获取），不再由 Enhancer 层 AI 分类
- **Stage2 移除 edition 识别**：AI 推断不可靠，已删除 EDITION 字段及相关 UI/缓存/回滚逻辑
- **多操作回滚**：扩展快照系统支持按操作类型独立快照（Scrape / Translate / Normalize），回滚弹窗选择要回滚的操作，仅恢复该操作影响的字段
- **别名去重**：编辑后 + 应用前双重去重，防止别名列表中出现重复项
- **不确定结果人工确认**：当 web search 不可用（如 Zhipu Chat）时，保存不确定结果并提示用户手动确认
- **数据模型统一**：消除 Python worker 与 C++ 端的重复模型定义，单一定义多处调用
- **确认对话框**：列数从 8 列收缩为 6 列（移除 Genre / Edition 列）

### v1.1.0

- **菜单重构**：去除技术术语（Stage 1/2），按功能分组（主操作 / 回滚 / 工具）
- **一键模式**：新增 "Scrape & Enhance (Auto)"，自动串联刮削 + 增强
- **翻译风格配置**：新增 Translation Style 三级别（Official / Literal / Semantic），实测对 AI 生成的 prompt 有显著差异
- **AI Prompt 设置页**：新增独立设置页，集中管理翻译风格、流派语言、自定义翻译对照、高级指令
- **中文输入支持**：Custom Translation Hints 和 Custom Instructions 编辑框全面支持 UTF-8 中文输入
- **DeepSeek Provider**：新增 DeepSeek AI 支持，与 Zhipu AI 共享 OpenAI 兼容基类

### v1.0.0

- 初始版本发布
- 支持 Scrape / Enhance 两阶段处理
- 支持多 AI Provider
- 缓存系统
- 备份与回滚
- External Tags 支持

---

## 联系支持

- **GitHub Issues**: [https://github.com/yourusername/foo_metadata_enhancer/issues](https://github.com/yourusername/foo_metadata_enhancer/issues)
- **文档**: [docs/](docs/)
