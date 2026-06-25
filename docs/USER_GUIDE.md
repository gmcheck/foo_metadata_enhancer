# foo_ai_metadata 用户指南

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

### 1.1 什么是 foo_ai_metadata？

foo_ai_metadata 是一个 foobar2000 插件，利用人工智能技术自动分析和完善音乐文件的元数据。

### 1.2 核心功能

| 功能 | 描述 |
|------|------|
| Stage 1: 刮削 | 从 MusicBrainz、Discogs、AI 获取基础元数据 |
| Stage 2: 增强 | AI 驱动的翻译、流派分类、版本识别 |
| 回滚 | 恢复到处理前的原始状态 |
| 缓存管理 | 查看和清理缓存数据 |

### 1.3 工作流程

```
选择曲目 → 右键菜单 → AI Metadata → 选择操作 → 预览结果 → 确认写入
```

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

1. 访问 [GitHub Releases](https://github.com/yourusername/foo_ai_metadata/releases) 页面
2. 下载最新版本的 `foo_ai_metadata-x.x.x.zip`
3. 解压缩下载的文件

### 3.2 安装组件

1. 关闭 foobar2000
2. 将 `foo_ai_metadata.dll` 和 `foo_ai_metadata` 文件夹复制到 foobar2000 的 `components` 文件夹
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

- **Stage 1: Scrape Metadata** - 获取基础元数据
- **Stage 2: Enhance Metadata** - AI 增强处理

#### 步骤 4：预览和确认

处理完成后，预览对话框会显示所有获取到的元数据，选择要写入的字段和曲目，点击 **Apply Selected**。

---

## 5. 菜单功能详解

### 5.1 AI Metadata 右键菜单

右键点击选中的曲目，在菜单中找到 **AI Metadata**，包含以下子菜单：

| 菜单项 | 功能说明 |
|--------|----------|
| **Stage 1: Scrape Metadata** | 从 MusicBrainz/Discogs/AI 获取基础元数据 |
| **Stage 2: Enhance Metadata** | AI 增强处理（翻译、流派分类、版本识别） |
| **Rollback to Initial** | 回滚到处理前的原始状态 |
| **Cache Statistics** | 查看缓存统计信息 |
| **Clear Cache** | 清除缓存数据 |

---

### 5.2 Stage 1: Scrape Metadata 详解

#### 5.2.1 功能说明

Stage 1 从多个在线数据源获取基础元数据，按照优先级顺序查询：

```
MusicBrainz (优先级最高) → Discogs (补充) → AI (兜底)
```

#### 5.2.2 选项对话框

点击 Stage 1 后，会弹出选项对话框：

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

---

### 5.3 Stage 2: Enhance Metadata 详解

#### 5.3.1 功能说明

Stage 2 使用 AI 对已有元数据进行增强处理：

- **翻译**：将标题、专辑、艺术家翻译为中文
- **流派分类**：AI 分析并分类音乐流派
- **版本识别**：识别是否为原版、重制版、现场版等

#### 5.3.2 选项对话框

点击 Stage 2 后，会显示选项说明：

```
AI will perform the following enhancements:
- Translate metadata to Chinese (Title, Album, Artist)
- Classify genre
- Identify edition (Original, Remastered, Live, etc.)

Select which fields to write in the confirmation dialog.
```

#### 5.3.3 确认结果对话框

**字段选择区域**：

| 复选框 | 字段 | 说明 |
|--------|------|------|
| Title_ZH | TITLE_ZH | 中文标题 |
| Album_ZH | ALBUM_ZH | 中文专辑名 |
| Artist_ZH | ARTIST_ZH | 中文艺术家名 |
| Genre | GENRE | AI 分类的流派 |
| Edition | EDITION | 版本信息 |

#### 5.3.4 写入的标签

| 标签 | 说明 | 示例值 |
|------|------|--------|
| GENRE | AI 分类的流派 | "Synth-pop; Electronic" |
| EDITION | 版本信息 | "Remastered", "Live", "Deluxe Edition" |
| TITLE_ZH | 中文标题 | "芳华之年" |
| ALBUM_ZH | 中文专辑名 | "百惠传" |
| ARTIST_ZH | 中文艺术家名 | "山口百惠" |

---

### 5.4 Rollback to Initial 详解

#### 5.4.1 功能说明

将选中的曲目恢复到处理前的原始状态。

#### 5.4.2 使用场景

- 处理结果不满意
- 错误处理了曲目
- 想要恢复原始标签

#### 5.4.3 确认对话框

```
Rollback all selected tracks to their initial state?

This will restore the original metadata before any AI processing.
```

点击 **Yes** 确认回滚，点击 **No** 取消。

#### 5.4.4 注意事项

- 只能回滚到最近一次 Stage 处理前的状态
- 如果多次处理，只能回滚到第一次处理前的状态
- 回滚操作不可撤销

---

### 5.5 Cache Statistics 详解

#### 5.5.1 功能说明

显示缓存统计信息，帮助了解缓存使用情况。

#### 5.5.2 统计信息

| 统计项 | 说明 |
|--------|------|
| Total Entries | 缓存条目总数（Stage 1 + Stage 2） |
| Cache Hits | 缓存命中次数 |
| Cache Misses | 缓存未命中次数 |
| Hit Rate | 缓存命中率（%） |
| Database Size | 缓存数据库大小（MB） |
| API Calls Saved | 节省的 API 调用次数 |

---

### 5.6 Clear Cache 详解

#### 5.6.1 功能说明

清除缓存数据，支持两种模式。

#### 5.6.2 清除选项对话框

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

#### 5.6.3 清除模式

| 选项状态 | 行为 |
|---------|------|
| ☐ Clear all cache（默认） | 只清除**选中曲目**的缓存 |
| ☑ Clear all cache | 清除**全部**缓存 |

#### 5.6.4 清除范围

| 操作 | stage1_cache | stage2_cache | cache_statistics |
|------|-------------|-------------|-----------------|
| 清除选中曲目 | ✓ (按 track_id) | ✓ (按 track_id) | ✗ |
| 清除所有缓存 | ✓ (全部) | ✓ (全部) | ✓ (全部) |

---

## 6. 设置面板详解

访问设置：**File** → **Preferences** → **AI Metadata**

设置页面分为三个子页面：

| 页面 | 功能 |
|------|------|
| General | AI Provider、Python、Cache 设置 |
| Data Sources | MusicBrainz、Discogs 配置 |
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
- 位置：`%APPDATA%\foobar2000-v2\foo_ai_metadata\cache.db`
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

### 6.3 Advanced 设置页面

#### 6.3.1 Task Queue 区域

| 设置项 | 说明 | 默认值 |
|--------|------|--------|
| **Batch Size** | Stage 1 每批处理的曲目数 | 50 |

**说明**：
- 较大的批次大小可以提高处理速度
- 但会增加内存使用和单次失败的影响范围
- 建议范围：20-100

#### 6.3.2 AI Batch 区域

| 设置项 | 说明 | 默认值 |
|--------|------|--------|
| **Batch Size** | Stage 2 每批处理的曲目数 | 10 |

**说明**：
- AI 批处理大小取决于模型上下文窗口
- 较大的值可能超出模型限制
- 建议范围：5-20

#### 6.3.3 Timeout 区域

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

---

## 8. 数据源配置

### 8.1 数据源优先级

Stage 1 按以下优先级查询数据源：

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

- **Stage 1 缓存**：基于 TITLE + ARTIST + ALBUM 的查询结果
- **Stage 2 缓存**：基于完整元数据的增强结果

### 9.2 缓存键生成

缓存键通过 SHA256 哈希生成：

```
track_id = SHA256(path + "|" + subsong + "|" + file_size)
```

### 9.3 缓存位置

```
%APPDATA%\foobar2000-v2\foo_ai_metadata\cache.db
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

### 10.1 自动备份

在执行 Stage 1 或 Stage 2 之前，插件会自动备份当前标签：

```
备份位置：%APPDATA%\foobar2000-v2\foo_ai_metadata\backup.db
```

### 10.2 备份内容

备份包含以下信息：

- 曲目 ID（track_id）
- 所有现有标签（键值对）
- 备份时间戳

### 10.3 回滚操作

1. 选择要回滚的曲目
2. 右键 → **AI Metadata** → **Rollback to Initial**
3. 确认回滚操作

### 10.4 回滚限制

- 只能回滚到初始状态（第一次处理前）
- 中间状态无法恢复
- 回滚后无法撤销

---

## 11. 日志系统

### 11.1 日志位置

```
%APPDATA%\foobar2000-v2\foo_ai_metadata\logs\ai_metadata.log
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
1. 确认 `foo_ai_metadata.dll` 在 `components` 文件夹
2. 检查 Help → About 中是否显示插件
3. 升级 foobar2000 到 2.0 或更高版本

### 12.2 处理时提示 "TITLE 或 ARTIST 缺失"

**原因**：Stage 1 需要 TITLE 和 ARTIST 作为查询依据。

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
├── foo_ai_metadata.dll
└── foo_ai_metadata/
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
| 5. 创建 zip | 输出到 `zips/foo_ai_metadata-<version>.zip` |

**生成的 zip 结构**：

```
foo_ai_metadata-1.0.0.zip
├── foo_ai_metadata.dll
└── foo_ai_metadata/
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

1. 解压 `foo_ai_metadata-x.x.x.zip`
2. 将 `foo_ai_metadata.dll` 和 `foo_ai_metadata` 文件夹复制到 foobar2000 的 `components/` 目录
3. 编辑 `components/foo_ai_metadata/worker/config.yaml`，填入你的 API key
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
| 配置文件 | `%APPDATA%\foobar2000-v2\foo_ai_metadata\settings.json` |
| 缓存数据库 | `%APPDATA%\foobar2000-v2\foo_ai_metadata\cache.db` |
| 备份数据库 | `%APPDATA%\foobar2000-v2\foo_ai_metadata\backup.db` |
| 日志文件 | `%APPDATA%\foobar2000-v2\foo_ai_metadata\logs\` |

### C. 性能优化建议

1. **使用缓存**：避免重复处理相同曲目
2. **合理批次大小**：根据网络状况调整
3. **选择快速模型**：如 gpt-4o-mini、gemini-flash
4. **使用本地模型**：Ollama 无网络延迟
5. **安装 External Tags**：对 CUE 分轨使用 SQLite 存储

---

## 更新日志

### v1.0.0

- 初始版本发布
- 支持 Stage 1/Stage 2 处理
- 支持多 AI Provider
- 缓存系统
- 备份与回滚
- External Tags 支持

---

## 联系支持

- **GitHub Issues**: [https://github.com/yourusername/foo_ai_metadata/issues](https://github.com/yourusername/foo_ai_metadata/issues)
- **文档**: [docs/](docs/)
