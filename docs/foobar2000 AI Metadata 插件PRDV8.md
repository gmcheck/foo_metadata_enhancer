# foobar2000 AI Metadata 插件

# Product Requirements Document (PRD) v8.0

---

## 目录

1. [项目概述](#1-项目概述)
2. [系统架构](#2-系统架构)
3. [开发环境要求](#3-开发环境要求)
4. [编译与打包](#4-编译与打包)
5. [目录结构](#5-目录结构)
6. [IPC通信协议](#6-ipc通信协议)
7. [数据结构定义](#7-数据结构定义)
8. [错误处理](#8-错误处理)
9. [Worker生命周期与Pool](#9-worker生命周期与pool)
10. [任务队列设计](#10-任务队列设计)
11. [AI Engine设计](#11-ai-engine设计)
12. [Prompt Template System](#12-prompt-template-system)
13. [缓存层设计](#13-缓存层设计)
14. [AI Provider公共层](#14-ai-provider公共层)
15. [标签写入与回滚策略](#15-标签写入与回滚策略)
16. [UI交互设计](#16-ui交互设计)
17. [日志系统](#17-日志系统)
18. [安全设计](#18-安全设计)
19. [性能预期](#19-性能预期)
20. [配置系统](#20-配置系统)
21. [V8核心数据流架构](#21-v8核心数据流架构)
22. [未来扩展](#22-未来扩展)
23. [版本历史](#23-版本历史)

---

## 1. 项目概述

开发一个 **foobar2000 AI Metadata 插件**，用于自动分析音乐元数据并写入标签。

### 1.1 核心目标

| 功能 | 说明 |
|------|------|
| AI 自动分类音乐风格 | Genre Classification |
| AI 识别音乐版本 | Edition Identification |
| AI 翻译元数据 | Metadata Translation |
| **V8新增：AI 元数据刮削与补全** | Metadata Scraping |
| **V8新增：多数据源支持** | MusicBrainz/Discogs/AI |
| 批量分析音乐库 | Batch Processing |
| 自动写入标签 | GENRE / EDITION / TITLE_ZH / ALBUM_ZH / ARTIST_ZH |
| 支持 AI 缓存减少重复调用 | Cache Layer |
| 提供稳定 Worker 进程架构 | Worker Process |

AI 使用 **OpenRouter API** 或本地模型（如 Ollama），也支持智谱AI、Google Gemini等多种AI Provider。

### 1.2 V8核心功能

| 功能 | 说明 |
|------|------|
| **阶段一：基础刮削** | 从MusicBrainz/Discogs/AI获取并补全基础元数据 |
| **阶段二：增强处理** | 翻译、流派分类、版本识别等增强功能 |
| **多数据源支持** | MusicBrainz (权威) → Discogs (补充) → AI (兜底) |
| **备份与回滚** | 支持多版本备份，可选择任意版本回滚 |

### 1.3 前置条件

- **TITLE 和 ARTIST 必须存在**：刮削和增强处理的前提条件
- 缺失时弹窗提示用户补充
- 未来支持从文件名提取信息

---

## 2. 系统架构

### 2.1 整体架构图

```text
Foobar2000 主进程 (UI & Tagging Thread)
     │
foo_ai_metadata.dll (C++ Plugin) ───[ metadb_io_v3 API 交互 ]
     │
AI Core (C++) ──[ contextmenu_item_v2 触发 ]
     │
WorkerManager (C++ 进程管理器)
     │
Cache Layer (SQLite Database, C++ 层拦截)
     │
  [ IPC: Anonymous Pipes / stdin & stdout framing ]
     │
ai_worker.exe (Python 子进程)
     │
Python AI Engine
     │
┌─────────────────────────────────────────────────────────────────────────┐
│                                                                         │
│  ┌───────────────────────────────────────────────────────────────────┐ │
│  │                     数据源准备阶段                                 │ │
│  │                     (FallbackController)                          │ │
│  ├───────────────────────────────────────────────────────────────────┤ │
│  │                                                                   │ │
│  │  ┌─────────────────────────────────────────────────────────────┐ │ │
│  │  │                   DataSourceManager                         │ │ │
│  │  │         (并发查询，优先级: MB > Discogs > AI)                │ │ │
│  │  │  ┌─────────────┐ ┌─────────────┐ ┌─────────────┐           │ │ │
│  │  │  │MusicBrainz  │ │  Discogs    │ │ AI Adapter  │           │ │ │
│  │  │  │  Adapter    │ │  Adapter    │ │   (兜底)    │           │ │ │
│  │  │  └─────────────┘ └─────────────┘ └─────────────┘           │ │ │
│  │  └─────────────────────────────────────────────────────────────┘ │ │
│  │                              │                                    │ │
│  │                              ▼                                    │ │
│  │  ┌─────────────────────────────────────────────────────────────┐ │ │
│  │  │                 CandidateAggregator                         │ │ │
│  │  │                 (多数据源候选聚合器)                          │ │ │
│  │  └─────────────────────────────────────────────────────────────┘ │ │
│  │                              │                                    │ │
│  │                              ▼                                    │ │
│  │                        候选数量判断                                │ │
│  │                              │                                    │ │
│  │               ┌──────────────┼──────────────┐                    │ │
│  │               ▼              ▼              ▼                    │ │
│  │             ≥3个           1-2个           0个                   │ │
│  │               │              │              │                    │ │
│  │               │              │              ▼                    │ │
│  │               │              │      ┌───────────────┐            │ │
│  │               │              │      │  查询重写      │            │ │
│  │               │              │      │ (规则+AI变体)  │            │ │
│  │               │              │      └───────┬───────┘            │ │
│  │               │              │              │                    │ │
│  │               │              │              ▼                    │ │
│  │               │              │      重新查询数据源                │ │
│  │               │              │              │                    │ │
│  │               │              │        ┌─────┴─────┐              │ │
│  │               │              │        ▼           ▼              │ │
│  │               │              │     有候选      无候选            │ │
│  │               │              │        │           │              │ │
│  │               │              │        │           ▼              │ │
│  │               │              │        │    ┌───────────┐          │ │
│  │               │              │        │    │ AI纯推断  │          │ │
│  │               │              │        │    │(最后手段)  │          │ │
│  │               │              │        │    └─────┬─────┘          │ │
│  │               │              │        │          │                │ │
│  │               │              │        │          ▼                │ │
│  │               │              │        │    ┌───────────┐          │ │
│  │               │              │        │    │ 直接返回  │          │ │
│  │               │              │        │    │ FinalResult│          │ │
│  │               │              │        │    └───────────┘          │ │
│  │               ▼              ▼        ▼                           │ │
│  │         ┌─────────────────────────────────────────────┐          │ │
│  │         │              候选数据就绪                    │          │ │
│  │         └─────────────────────────────────────────────┘          │ │
│  │                              │                                    │ │
│  └──────────────────────────────┼────────────────────────────────────┘ │
│                                 │                                      │
│                                 ▼                                      │
│  ┌───────────────────────────────────────────────────────────────────┐ │
│  │                       AI决策阶段                                   │ │
│  │                       (AIResolver)                                │ │
│  ├───────────────────────────────────────────────────────────────────┤ │
│  │                                                                   │ │
│  │  输入：候选列表 + 原始查询                                         │ │
│  │                                                                   │ │
│  │  ┌─────────────────────────────────────────────────────────────┐ │ │
│  │  │  正常决策：从≥3个候选中选择最佳                               │ │ │
│  │  │  增强决策：从1-2个候选中选择+补全（promote不同）                   │ │ │
│  │  └─────────────────────────────────────────────────────────────┘ │ │
│  │                                                                   │ │
│  │  输出：FinalResult (title, artist, album, year, confidence...)   │ │
│  │                                                                   │ │
│  └───────────────────────────────────────────────────────────────────┘ │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                     AI Provider Layer                           │
│                        (统一接口)                                │
│  ┌─────────┐ ┌─────────┐ ┌─────────┐ ┌─────────┐              │
│  │OpenRouter│ │ Zhipu  │ │ Gemini  │ │ Ollama  │              │
│  │(远程API) │ │(智谱AI) │ │(Google) │ │ (本地)   │              │
│  └─────────┘ └─────────┘ └─────────┘ └─────────┘              │
└─────────────────────────────────────────────────────────────────┘
         ▲                    ▲              ▲
         │                    │              │
   AI Adapter调用       查询重写AI调用   AIResolver调用
   (数据源兜底)         (生成变体)       (最终决策)
```

### 2.2 设计原则

| 原则 | 说明 | 实现约束 |
| --- | --- | --- |
| **稳定性** | Worker crash / AI异常不影响主程序 | C++ 端监听子进程句柄，崩溃自动重启，保证主 UI 线程无阻塞 |
| **扩展性** | AI逻辑可独立升级，支持本地或远程模型 | 接口数据完全解耦，Python 端可随时替换底层调用链路 |
| **性能** | Worker 进程异步处理，批量队列 | 采用 `std::jthread` 异步调度，减少 IO 挂起时间 |
| **接口** | JSON RPC / 二进制 framing | 采用 4 字节头长度的高效 IPC 协议，稳定映射 C++/Python 数据 |
| **AI 友好** | 面向 AI 优化的提示词和数据结构 | 提供详细的类型定义和接口规范，便于 AI 代码生成 |

### 2.3 两阶段处理架构

```
┌─────────────────────────────────────────────────────────────────────────┐
│                    两阶段处理架构                                        │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  ┌─────────────────────────────────────────────────────────────────┐   │
│  │                    阶段一：基础刮削                               │   │
│  │                                                                   │   │
│  │  输入：TITLE + ARTIST (必须存在)                                 │   │
│  │                                                                   │   │
│  │  数据源优先级：                                                   │   │
│  │    MusicBrainz (权威) → Discogs (补充) → AI (兜底)              │   │
│  │                                                                   │   │
│  │  输出字段：                                                       │   │
│  │    • 基础字段：title, artist, album, year, track_number, etc.   │   │
│  │    • 人员字段：composer, lyricist, conductor, performer, etc.   │   │
│  │    • 元数据：label, country, catalog_number, etc.               │   │
│  │    • 标识符：musicbrainz_id, isrc                               │   │
│  │                                                                   │   │
│  │  处理流程：                                                       │   │
│  │    1. 前置检查 → 2. 数据源查询 → 3. 结果确认 → 4. 写入标签       │   │
│  └───────────────────────────────────────────────────────────────────┘   │
│                                                                         │
│                              ↓ 备份                                      │
│                                                                         │
│  ┌─────────────────────────────────────────────────────────────────┐   │
│  │                    阶段二：增强处理                               │   │
│  │                                                                   │   │
│  │  输入：阶段一的输出结果                                          │   │
│  │                                                                   │   │
│  │  处理内容：                                                       │   │
│  │    • 翻译：title_zh, album_zh, artist_zh                        │   │
│  │    • 分类：genre (流派分类)                                      │   │
│  │    • 识别：edition (版本识别)                                    │   │
│  │                                                                   │   │
│  │  处理流程：                                                       │   │
│  │    1. 读取阶段一结果 → 2. AI增强 → 3. 结果确认 → 4. 写入标签     │   │
│  └───────────────────────────────────────────────────────────────────┘   │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

---

## 3. 开发环境要求

| 组件 | 版本/说明 | 核心依赖要求 |
| --- | --- | --- |
| **OS** | Windows 11+ | 使用 Windows API (`CreateProcess`, 匿名管道) |
| **Foobar2000** | 2.25.6 Asion 汉化版 | 仅支持 64-bit 架构 (x64) |
| **SDK** | foobar2000 SDK 2025-03-07 | 必须使用 `metadb_io_v3` 和 `service_ptr_t` |
| **Python** | 3.11+ | 标准库为主 |
| **Visual Studio** | 2026 Community | 启用 C++20 标准 (`/std:c++20`) |
| **SQLite** | 3.52.0 | 用于缓存存储，header-only 集成 |
| **Python依赖** | requests, json, logging, pathlib, time, random, uuid, pyyaml, pydantic, typing | 采用 pydantic 保证 JSON 返回结构的绝对一致性 |

---

## 4. 编译与打包

### 4.1 C++插件编译

#### 4.1.1 构建环境

| 组件 | 要求 |
| --- | --- |
| Visual Studio | 2022 with C++ workload |
| CMake | 3.25+ |
| foobar2000 SDK | 2024 |
| vcpkg | 安装 nlohmann-json |

#### 4.1.2 CMake 可配置变量

| 变量 | 说明 | 默认值 |
| --- | --- | --- |
| `VCPKG_PATH` | vcpkg 安装路径 | `D:/Programs/vcpkg` |
| `FOOBAR2000_SDK_PATH` | foobar2000 SDK 路径 | `D:/Programs/foobar2000_sdk` |
| `FOOBAR_DEV_DIR` | 本地 foobar2000 目录（留空则不自动部署） | 空 |

#### 4.1.3 编译命令

```powershell
# 本地开发（自动部署到 foobar2000）
cmake -B out/build -G "Visual Studio 17 2022" -A x64 ^
    -DFOOBAR_DEV_DIR="C:/path/to/foobar2000"
cmake --build out/build --config Release -- /m

# 仅编译（不部署，用于打包）
cmake -B out/build -G "Visual Studio 17 2022" -A x64
cmake --build out/build --config Release -- /m
```

*构建配置说明：*
- *C++ 标准：C++20*
- *项目名称：foo_ai_metadata*
- *注意：C++ 工程必须配置 `.rc` 资源文件，包含 `FileVersion`, `FileDescription` 等元数据，否则 foobar2000 将拒绝加载组件。*

### 4.2 配置文件管理

项目包含两套配置文件：

| 文件 | 用途 | Git |
| --- | --- | --- |
| `worker/config.yaml.template` | 模板文件，API key 留空 | 提交 |
| `worker/config.yaml` | 本地开发用，含真实 API key | 排除 (.gitignore) |

**首次使用**：复制模板并填入 API key：

```bash
copy worker\config.yaml.template worker\config.yaml
```

### 4.3 打包发布

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
| --- | --- |
| 1. 编译（可选） | 使用 `-BuildFirst` 参数启用 |
| 2. 检查文件 | 验证 DLL 和 worker 目录存在 |
| 3. 准备文件 | 复制 DLL、worker 脚本，排除 `__pycache__` 和 `.pyc` |
| 4. 替换配置 | 删除 `config.yaml`，用 `config.yaml.template` 替代 |
| 5. 创建 zip | 输出到 `zips/foo_ai_metadata-<version>.zip` |

### 4.4 发布包结构

```
foo_ai_metadata-1.0.0.zip
├── foo_ai_metadata.dll              # C++插件
└── foo_ai_metadata/                 # 插件数据目录
    ├── worker/                      # Python Worker脚本
    │   ├── ai_worker.py
    │   ├── config.yaml              # 从 template 复制，API key 为空
    │   ├── ai/
    │   │   ├── adapter.py
    │   │   └── providers/
    │   ├── core/
    │   ├── common/
    │   └── ...
    ├── cache/                       # 缓存目录（空）
    └── logs/                        # 日志目录（空）
```

### 4.5 安装部署

1. 解压 `foo_ai_metadata-x.x.x.zip`
2. 将 `foo_ai_metadata.dll` 和 `foo_ai_metadata` 文件夹复制到 foobar2000 的 `components/` 目录
3. 编辑 `components/foo_ai_metadata/worker/config.yaml`，填入 API key
4. 启动 foobar2000

### 4.6 升级策略

| 文件/目录 | 操作 | 说明 |
| --- | --- | --- |
| `foo_ai_metadata.dll` | 覆盖 | 插件主体 |
| `foo_ai_metadata/worker/` | 覆盖 | Python Worker脚本 |
| `foo_ai_metadata/worker/config.yaml` | 保留 | 用户配置，不覆盖 |
| `foo_ai_metadata/cache/` | 保留 | 缓存数据 |
| `foo_ai_metadata/logs/` | 保留 | 历史日志 |

### 4.7 运行环境依赖

| 组件 | 要求 |
| --- | --- |
| 操作系统 | Windows 10 / 11 (x64) |
| foobar2000 | 2.x 或更高版本 |
| Python | 3.11+ |
| VC++ Runtime | Visual C++ 2022 Redistributable |

---

## 5. 目录结构

```text
foo_ai_metadata_v8/

plugin/                      # foobar2000 插件 (SHARED)
   main.cpp                  # 组件入口与描述
   menu_handler.cpp          # contextmenu_item_v2 子菜单实现
   dialogs.cpp               # 对话框实现
   confirm_dialog.cpp        # V8新增：确认对话框实现
   preferences_page.cpp      # 设置面板实现
   backup_manager.cpp        # V8新增：备份管理器
   foo_ai_metadata.rc        # 版本资源文件
   resource.h                # 资源ID定义

core/                        # 核心库 (STATIC)
   ai_core.cpp               # 核心业务串联
   ai_core.h                 # 核心业务头文件
   worker_manager.cpp        # 子进程生命周期与管道通信
   worker_manager.h          # 子进程管理头文件
   task_queue.cpp            # 异步任务分发
   task_queue.h              # 任务队列头文件
   cache_layer.cpp           # Hash 缓存计算与存储
   cache_layer.h             # 缓存层头文件
   logger.cpp                # 日志系统实现
   logger.h                  # 日志系统头文件

include/                     # 公共头文件
   types.h                   # 公共类型定义
   constants.h               # 常量定义
   third_party/
      nlohmann/json.hpp      # JSON 库 (nlohmann/json)
      sqlite/
         sqlite3.h           # SQLite3 头文件
      sqlite/sqlite3.c       # SQLite3 源文件

worker/                      # Python Worker
   ai_worker.py              # CLI 入口与标准输入输出循环
   config.yaml               # API 密钥与并发配置
   
   # 核心处理模块
   core/
      __init__.py            # 模块导出
      stage1_processor.py    # 阶段一：基础刮削处理器
      stage2_processor.py    # 阶段二：增强处理器
      aggregator.py          # 候选聚合器
      resolver.py            # AI决策层
   
   # AI 相关模块
   ai/
      __init__.py            # 模块导出
      adapter.py             # 模型适配器
      ai_data_source.py      # AI 数据源适配器
      providers/
         __init__.py         # 模块导出
         factory.py          # 工厂类
         base.py             # 基类定义
         openrouter_provider.py
         zhipu_provider.py   # 智谱AI Provider
         gemini_provider.py
         ollama_provider.py
   
   # 外部数据源
   data_sources/
      __init__.py            # 模块导出
      base.py                # 数据源适配器基类
      manager.py             # 数据源管理器
      musicbrainz_adapter.py
      discogs_adapter.py
   
   # 降级处理
   fallback/
      __init__.py            # 模块导出
      controller.py          # 降级控制器
      query_rewriter.py      # 查询重写器
      ai_inferencer.py       # AI推断器
   
   # 提示词管理
   prompts/
      __init__.py            # 模块导出
      base.py                # 基础 prompt 工具函数
      stage1_prompts.py      # Stage 1 相关 prompt
      stage2_prompts.py      # Stage 2 相关 prompt
      fallback_prompts.py    # 降级处理 prompt
   
   # 通用工具
   common/
      __init__.py            # 模块导出
      models.py              # Pydantic 数据模型
      config_manager.py      # 配置管理
      text_utils.py          # 文本处理工具
      result_formatter.py    # 结果格式化工具

tests/
   python/test_*.py          # Python单元测试
   cpp/test_*.cpp            # C++单元测试

docs/
   foobar2000 AI Metadata 插件PRDV8.md  # 本文档
```

---

## 6. IPC通信协议

### 6.1 消息 Framing

* 每条消息前4字节为 **消息长度（big endian 的 32 位无符号整数）**
* 解决 Python 输出 stdout 与 C++ 管道读取之间的粘包/半包问题

内存表现示例：

```text
[0x00, 0x00, 0x01, 0x2A] {"version":1,"id":"uuid",...}
```

### 6.2 IPC消息类型定义

#### 6.2.1 分析请求消息

```json
{
    "version": 1,
    "id": "uuid-string",
    "type": "analyze_batch",
    "payload": {
        "tracks": [...],
        "options": {
            "classify_genre": true,
            "identify_edition": true,
            "translate_title": true
        }
    }
}
```

#### 6.2.2 V8新增：刮削请求消息 (scrape_stage1)

```json
{
    "version": 1,
    "id": "uuid-string",
    "type": "scrape_stage1",
    "payload": {
        "tracks": [
            {"track_id": "...", "title": "Song Title", "artist": "Artist Name"}
        ],
        "options": {
            "scrape_title": true,
            "scrape_artist": true,
            "enable_musicbrainz": true,
            "enable_discogs": true,
            "enable_ai": true
        }
    }
}
```

#### 6.2.3 V8新增：增强请求消息 (enhance_stage2)

```json
{
    "version": 1,
    "id": "uuid-string",
    "type": "enhance_stage2",
    "payload": {
        "tracks": [...],
        "options": {
            "translate_title": true,
            "classify_genre": true
        }
    }
}
```

#### 6.2.4 V8新增：回滚请求消息

```json
{
    "version": 1,
    "id": "uuid-string",
    "type": "rollback",
    "payload": {
        "track_ids": ["track-id-1", "track-id-2"]
    }
}
```

#### 6.2.5 响应消息结构

```json
{
    "version": 1,
    "id": "uuid-string",
    "success": true,
    "type": "scrape_stage1_response",
    "count": 1,
    "results": [
        {
            "track_id": "unique-track-id",
            "success": true,
            "scraped_fields": {
                "title": {"value": "Moonlight", "confidence": 0.98, "source": "musicbrainz"},
                "artist": {"value": "Claude Debussy", "confidence": 0.99, "source": "musicbrainz"}
            }
        }
    ]
}
```

---

## 7. 数据结构定义

### 7.1 C++ 数据结构

#### 7.1.1 TrackInput 结构

```cpp
struct TrackInput {
    std::string track_id;          ///< 音轨唯一标识符
    std::string title;             ///< 标题
    std::string album;             ///< 专辑名
    std::string artist;            ///< 艺术家
    std::string album_artist;      ///< 专辑艺术家
    std::string musicbrainz_id;    ///< MusicBrainz ID
    uint32_t duration_sec = 0;     ///< 时长（秒）
    uint32_t track_number = 0;     ///< 音轨号
    uint32_t disc_number = 0;      ///< 光盘号
    uint32_t subsong_index = 0;    ///< 子音轨索引
    std::string year;              ///< 年份
    std::string genre_existing;    ///< 现有流派
    std::string label;             ///< 厂牌
    std::string language_hint;     ///< 语言提示
};
```

#### 7.1.2 V8新增：ScrapingOptions 结构

```cpp
struct ScrapingOptions {
    bool scrape_title = true;
    bool scrape_artist = true;
    bool scrape_album = true;
    bool scrape_year = true;
    bool enable_musicbrainz = true;
    bool enable_discogs = true;
    bool enable_ai = true;
    float auto_accept_threshold = 0.9f;
};
```

#### 7.1.3 V8新增：EnhancementOptions 结构

```cpp
struct EnhancementOptions {
    bool translate_title = true;
    bool translate_album = true;
    bool translate_artist = true;
    bool classify_genre = true;
    bool identify_edition = true;
};
```

#### 7.1.4 V8新增：ScrapedField 结构

```cpp
struct ScrapedField {
    std::string value;           ///< 刮削值
    float confidence = 0.0f;     ///< 置信度
    std::string source;          ///< 数据来源
};
```

#### 7.1.5 V8新增：失败追踪结构

```cpp
enum class FailureReason {
    None,
    Timeout,
    WorkerCrash,
    NetworkError,
    NoCandidates,
    AIDecisionFailed,
    Unknown
};

struct FailedTrackInfo {
    std::string track_id;
    FailureReason reason = FailureReason::Unknown;
    std::string error_message;
    int retry_count = 0;
};
```

### 7.2 Python 数据模型

#### 7.2.1 DataSourceType 枚举

```python
class DataSourceType(str, Enum):
    MUSICBRAINZ = "musicbrainz"
    DISCOGS = "discogs"
    AI = "ai"
```

#### 7.2.2 Candidate 候选模型

```python
@dataclass
class Candidate:
    """候选元数据 - 多源融合的基础单元"""
    title: str = ""
    artist: str = ""
    album: str = ""
    year: str = ""
    composer: str = ""
    label: str = ""
    source: DataSourceType = DataSourceType.AI
    confidence: float = 0.0
    match_score: float = 0.0
    sources: List[DataSourceType] = field(default_factory=list)
```

#### 7.2.3 FinalResult 最终结果模型

```python
@dataclass
class FinalResult:
    """最终决策结果"""
    title: str = ""
    artist: str = ""
    album: str = ""
    year: str = ""
    genre: str = ""
    confidence: float = 0.0
    source: str = ""  # "musicbrainz" | "ai" | "ai_fallback"
    selected_candidate_index: int = -1
    reasoning: str = ""
```

---

## 8. 错误处理

### 8.1 错误码定义

| 错误码 | 说明 | 处理建议 |
| --- | --- | --- |
| `E001` | Worker进程启动失败 | 检查Python环境和Worker路径 |
| `E002` | IPC通信超时 | 检查Worker进程状态 |
| `E003` | JSON解析失败 | 检查消息格式 |
| `E004` | AI API调用失败 | 检查API Key和网络 |
| `E005` | 缓存读写失败 | 检查数据库文件 |
| `E006` | 标签写入失败 | 检查文件权限 |
| `E007` | V8新增：缺少必要字段 | TITLE和ARTIST必须存在 |
| `E008` | V8新增：数据源不可用 | 检查数据源配置 |
| `E009` | V8新增：回滚失败 | 检查快照数据 |
| `E010` | V8新增：快照不存在 | 该音轨没有保存过快照 |

---

## 9. Worker生命周期与Pool

### 9.1 Worker进程管理

```cpp
class WorkerManager {
public:
    void start();
    void stop();
    void restart();
    bool is_running() const;
    void send_request(const std::string& request, 
                      ResultCallback on_result,
                      ErrorCallback on_error);
private:
    void worker_read_loop();
    void worker_watchdog();
    bool start_worker();
    void stop_worker();
};
```

### 9.2 Worker进程重启策略

| 场景 | 处理方式 |
| --- | --- |
| Worker崩溃 | 自动重启，丢失的请求返回错误 |
| Worker无响应 | 超时后重启 |
| 内存超限 | 重启并记录日志 |

---

## 10. 任务队列设计

### 10.1 Task 结构体

```cpp
struct Task {
    std::string id;              // 任务ID
    std::string method;          // 方法名 (stage1_scrape/stage2_enhance)
    std::vector<TrackInput> tracks;
    uint32_t priority = 5;       // 优先级
    uint32_t timeout_ms = 30000; // 超时时间
    size_t batch_index = 0;      // 批次索引
    size_t total_batches = 1;    // 总批次数
};
```

### 10.2 V8.1 分批处理架构

```
用户选中 500 首歌曲
       │
       ▼
AICore::stage1_scrape_sync()
       │
       ├─ 1. 缓存检查 (全部 500 首)
       │     ├─ 命中缓存 → 直接返回结果
       │     └─ 未命中 → 进入分批处理
       │
       ├─ 2. 分批处理未缓存歌曲
       │     ├─ Batch 1 (tracks 1-50)   → 进度 10%
       │     ├─ Batch 2 (tracks 51-100) → 进度 20%
       │     └─ ...
       │
       └─ 3. 合并结果返回
```

### 10.3 两层 batch_size 说明

| 层级 | 配置位置 | 默认值 | 用途 |
| --- | --- | --- | --- |
| C++ 端 | foobar2000 设置界面 | 50 | 向 Python Worker 发送请求时的分批 |
| Python 端 | config.yaml | 30 | AI 分析阶段的内部批处理 |

---

## 11. AI Engine设计

### 11.1 Python AI Engine

```python
class AIEngine:
    def __init__(self, config: Dict[str, Any]):
        self.model_adapter = ModelAdapter(config)
        self.prompt_template = PromptTemplate()
    
    def analyze_batch(self, tracks: List[TrackInput], 
                      options: Dict[str, bool]) -> List[TrackAnalysisResult]:
        messages = self.prompt_template.build_batch_analysis_prompt(tracks, options)
        result = self.model_adapter.analyze(messages)
        return self._parse_results(result.result, tracks)
```

### 11.2 V8新增：刮削引擎

```python
class ScrapingEngine:
    def __init__(self, config: Dict[str, Any]):
        self.data_source_manager = DataSourceManager(config)
    
    def scrape_stage1(self, tracks: List[TrackInput],
                      options: ScrapingOptions) -> List[TrackScrapingResult]:
        results = []
        for track in tracks:
            if not track.title or not track.artist:
                results.append(TrackScrapingResult(
                    track_id=track.track_id,
                    success=False,
                    error="Missing required fields: TITLE and ARTIST"
                ))
                continue
            result = self.data_source_manager.scrape(track.title, track.artist, options)
            results.append(result)
        return results
```

---

## 12. Prompt Template System

### 12.1 基础Prompt模板

```python
class PromptTemplate:
    BATCH_SYSTEM_PROMPT = """You are a music metadata expert. Analyze the provided tracks and return:
1. Genre classification (with confidence 0.0-1.0)
2. Edition identification (with confidence)
3. Chinese translations for title, album, artist (with confidence)

Return JSON format only."""
```

### 12.2 V8新增：刮削Prompt模板

```python
class ScrapingPromptTemplate:
    SCRAPE_SYSTEM_PROMPT = """You are a music metadata expert. Given a song title and artist, 
provide accurate metadata information. Return all available fields with confidence scores."""
```

---

## 13. 缓存层设计

### 13.1 表间关联图

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                              cache.db                                        │
├─────────────────────────────────────────────────────────────────────────────┤
│  ┌─────────────────┐     track_id      ┌─────────────────┐                  │
│  │  stage1_cache   │ ──────────────────│  stage2_cache   │                  │
│  └─────────────────┘                   └─────────────────┘                  │
│           │                                     │                            │
│           │ track_id                            │ track_id                   │
│           ▼                                     ▼                            │
│  ┌─────────────────────────────────────────────────────────────────────┐    │
│  │                         backups.db                                   │    │
│  │  ┌─────────────────┐                                                 │    │
│  │  │metadata_snapshots│                                                 │    │
│  │  └─────────────────┘                                                 │    │
│  └─────────────────────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 13.2 缓存数据库结构

#### 13.2.1 stage1_cache 表

```sql
CREATE TABLE IF NOT EXISTS stage1_cache (
    cache_key TEXT PRIMARY KEY NOT NULL,
    track_id TEXT,
    title TEXT NOT NULL,
    artist TEXT NOT NULL,
    album TEXT,
    scraped_fields_json TEXT,
    source TEXT DEFAULT 'ai',
    success INTEGER DEFAULT 1,
    cache_hit_count INTEGER DEFAULT 0,
    created_at TEXT NOT NULL,
    updated_at TEXT NOT NULL
);
```

#### 13.2.2 stage2_cache 表

```sql
CREATE TABLE IF NOT EXISTS stage2_cache (
    cache_key TEXT PRIMARY KEY NOT NULL,
    track_id TEXT,
    title TEXT NOT NULL,
    artist TEXT NOT NULL,
    title_zh TEXT,
    album_zh TEXT,
    artist_zh TEXT,
    translation_confidence REAL DEFAULT 0.0,
    genre_value TEXT,
    genre_confidence REAL DEFAULT 0.0,
    success INTEGER DEFAULT 1,
    created_at TEXT NOT NULL,
    updated_at TEXT NOT NULL
);
```

### 13.3 快照数据库结构

```sql
CREATE TABLE IF NOT EXISTS metadata_snapshots (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    track_id TEXT NOT NULL UNIQUE,
    snapshot_data TEXT NOT NULL,
    field_count INTEGER DEFAULT 0,
    created_at TEXT NOT NULL,
    updated_at TEXT NOT NULL
);
```

### 13.4 TTL 策略

| 结果来源 | TTL |
|----------|-----|
| ai_fallback | 1 天 |
| confidence >= 0.9 | 90 天 |
| confidence >= 0.7 | 30 天 |
| 其他 | 7 天 |

### 13.5 缓存清除功能

#### 13.5.1 功能概述

Clear Cache 功能支持两种清除模式：

| 模式 | 说明 |
|------|------|
| 清除选中曲目缓存 | 仅清除当前选中曲目的缓存记录 |
| 清除所有缓存 | 清除数据库中的全部缓存记录 |

#### 13.5.2 用户界面

```
┌─────────────────────────────────────────────┐
│  Clear Cache                          [X]   │
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

#### 13.5.3 实现逻辑

```cpp
// track_id 生成方式（与缓存存储时一致）
std::string track_id = CacheLayer::generate_track_uid(path, subsong, file_size);

// 清除选中曲目缓存
int deleted = cache_layer->clear_by_track_ids(track_ids);

// 清除所有缓存
cache_layer->clear_all();
```

#### 13.5.4 track_id 生成算法

track_id 是通过 SHA256 哈希生成的唯一标识：

```cpp
std::string CacheLayer::generate_track_uid(const std::string& path, uint32_t subsong, uint64_t file_size) {
    std::string combined = path + "|" + std::to_string(subsong) + "|" + std::to_string(file_size);
    std::transform(combined.begin(), combined.end(), combined.begin(), ::tolower);
    return sha256_hash(combined);  // 返回完整哈希值
}
```

**关键参数**：
- `path`: 文件完整路径
- `subsong`: 子歌曲索引（用于 CUE 分轨）
- `file_size`: 文件大小（字节）

#### 13.5.5 清除范围

| 操作 | stage1_cache | stage2_cache | cache_statistics |
|------|-------------|-------------|-----------------|
| 清除选中曲目 | ✓ (按track_id) | ✓ (按track_id) | ✗ |
| 清除所有缓存 | ✓ (全部) | ✓ (全部) | ✓ (全部) |

---

## 14. AI Provider公共层

### 14.1 支持的Provider

| Provider | 类型 | 说明 |
|----------|------|------|
| OpenRouter | 远程API | 支持多种模型 |
| Zhipu | 远程API | 智谱AI |
| Gemini | 远程API | Google Gemini |
| Ollama | 本地 | 本地模型 |

---

## 15. 标签写入与回滚策略

### 15.1 设计理念

采用**全量快照 + 黑名单**机制，确保回滚时不会遗漏任何字段。

### 15.2 元数据库写入（支持CUE分轨）

V8采用 foobar2000 元数据库方式写入标签，而非直接修改文件：

```cpp
// 使用 metadb_hint_list_v3 批量写入
auto hint_list = metadb_index_manager::get()->create_hint_list();
hint_list->add_hint_forced(handle, info);  // 强制更新
hint_list->on_done();  // 提交更改
```

**优势**：
- 支持 CUE 分轨歌曲：元数据写入 foobar2000 数据库，CUE 虚拟轨道也能正确显示
- 不修改原文件：保持音频文件完整性
- 批量写入高效：使用 hint_list 一次性提交多个更改

**注意事项**：
- 元数据仅存在于 foobar2000 数据库中
- 重新扫描媒体库或更换设备后需重新刮削
- 如需永久写入文件，可使用 foobar2000 的 "Write to file" 功能

### 15.3 黑名单机制

排除技术性字段，保留所有元数据字段：

```cpp
static const std::set<std::string> METADATA_BLACKLIST = {
    // 技术信息
    "TECH", "ENCODER", "ENCODER_SETTINGS",
    // 音量增益
    "REPLAYGAIN_ALBUM_GAIN", "REPLAYGAIN_TRACK_GAIN", ...
    // foobar2000 内部字段
    "FOOBAR2000_VERSION", "__tool", ...
    // 播放统计
    "_PLAYCOUNT", "_RATING", "_LAST_PLAYED", ...
    // 文件信息
    "_BITRATE", "_CODEC", "_FILENAME", "_PATH", ...
};
```

### 15.4 BackupManager API

```cpp
class BackupManager {
public:
    bool has_snapshot(const std::string& track_id);
    bool save_snapshot(const std::string& track_id,
                       const std::map<std::string, std::string>& snapshot);
    bool ensure_snapshot(const std::string& track_id,
                         const std::map<std::string, std::string>& snapshot);
    std::optional<std::map<std::string, std::string>> rollback(const std::string& track_id);
    static bool is_field_blacklisted(const std::string& field_name);
};
```

### 15.5 回滚示例

```
T1: 原始文件
    TITLE: "月光", ARTIST: "未知", CUSTOM_FIELD: "value"
              ↓
T2: Stage 1 刮削 - 保存快照
    快照: {TITLE: "月光", ARTIST: "未知", CUSTOM_FIELD: "value"}
    处理后: TITLE: "Moonlight", ARTIST: "Debussy", CUSTOM_FIELD: "value"
              ↓
T3: Stage 2 增强 - 快照已存在，跳过保存
    处理后: TITLE: "Moonlight", TITLE_ZH: "月光", GENRE: "Classical"
              ↓
T4: 用户回滚
    恢复快照: TITLE: "月光", ARTIST: "未知", CUSTOM_FIELD: "value"
```

---

## 16. UI交互设计

### 16.1 右键菜单结构

所有命令组织在 "AI Metadata" 子菜单下，避免菜单臃肿：

```
右键菜单
└── AI Metadata >
    ├── Stage 1: Scrape Metadata
    ├── Stage 2: Enhance Metadata
    ├── ───────────────────────
    ├── Rollback to Initial
    ├── ───────────────────────
    ├── Cache Statistics
    └── Clear Cache
```

**实现方式**：
- 使用 `contextmenu_item_v2` + `contextmenu_item_node_root_popup` 实现子菜单
- `instantiate_item()` 动态生成菜单节点树
- 菜单项状态根据选中曲目数量自动禁用/启用

### 16.2 前置检查弹窗

当TITLE或ARTIST缺失时显示，提示用户补充必要字段。

### 16.3 Stage 1 启动对话框

用户执行 Stage 1 刮削时显示，简化设计，只包含数据源和置信度设置：

```
┌─────────────────────────────────────────────────────────────────┐
│  Stage 1: Scraping Options                                      │
├─────────────────────────────────────────────────────────────────┤
│  Data Sources (Priority Order)                                  │
│  [☑] MusicBrainz    [☑] Discogs    [☑] AI (Fallback)           │
│                                                                 │
│  Confidence Thresholds                                          │
│  Auto Accept: [0.90]    Confirm Below: [0.70]                  │
│                                                                 │
│  AI will fetch all available fields. Select which fields to     │
│  write in the confirmation dialog.                              │
│                                                                 │
│                              [Start Scraping] [Cancel]          │
└─────────────────────────────────────────────────────────────────┘
```

**设计说明**：
- 不再让用户在启动时选择字段
- AI 会获取所有可用字段
- 字段选择移至确认对话框

### 16.4 Stage 1 确认对话框

AI 处理完成后显示，包含字段选择功能：

```
┌──────────────────────────────────────────────────────────────────────────┐
│  Confirm Scraping Results                                                │
├──────────────────────────────────────────────────────────────────────────┤
│  Select Fields to Write                                                  │
│  [☑] Title [☑] Artist [☑] Album [☑] Year [☑] Track# [☑] Disc#          │
│  [☑] Composer [☑] Lyricist [ ] Conductor [ ] Performer [☑] Label        │
│                                                                          │
│  [Select All] [Select None] [Select Success]                            │
│                                                                          │
│  ☑ Track ID          │ Title    │ Artist  │ Album  │ Year │ ...        │
│  ☑ abc123...         │ 芳华之年  │ 山口百惠 │ 百惠传  │ 1983 │ ...        │
│  ☐ def456...         │ (empty)  │ Various │ (empty)│      │ ...        │
│                                                                          │
│                              [Apply Selected] [Cancel]                   │
└──────────────────────────────────────────────────────────────────────────┘
```

**设计说明**：
- 顶部新增字段选择区域，用户勾选要写入的字段
- 列表显示所有 AI 返回的结果
- 双击可编辑单个字段值
- 只有勾选的歌曲和字段才会被写入

### 16.5 Stage 2 启动对话框

用户执行 Stage 2 增强时显示，简化设计：

```
┌─────────────────────────────────────────────────────────────────┐
│  Stage 2: Enhancement Options                                   │
├─────────────────────────────────────────────────────────────────┤
│  AI will perform the following enhancements:                    │
│                                                                 │
│  • Translate metadata to Chinese (Title, Album, Artist)         │
│  • Classify genre                                               │
│  • Identify edition (Original, Remastered, Live, etc.)          │
│                                                                 │
│  Select which fields to write in the confirmation dialog.       │
│                                                                 │
│                     [Start Enhancement] [Cancel]                │
└─────────────────────────────────────────────────────────────────┘
```

**设计说明**：
- 不再让用户选择翻译/分类选项
- AI 自动执行所有增强功能
- 字段选择移至确认对话框

### 16.6 Stage 2 确认对话框

AI 处理完成后显示，包含字段选择功能：

```
┌──────────────────────────────────────────────────────────────────────────┐
│  Confirm Enhancement Results                                             │
├──────────────────────────────────────────────────────────────────────────┤
│  Select Fields to Write                                                  │
│  [☑] Title_ZH [☑] Album_ZH [☑] Artist_ZH [☑] Genre [☑] Edition         │
│                                                                          │
│  [Select All] [Select None] [Select Success]                            │
│                                                                          │
│  ☑ Track ID     │ Title_ZH │ Album_ZH │ Artist_ZH │ Genre │ Edition │...│
│  ☑ abc123...    │ 芳华之年  │ 百惠传   │ 山口百惠   │ Pop   │ Original│...│
│  ☐ def456...    │ (empty)  │ (empty)  │ (empty)   │       │         │...│
│                                                                          │
│                              [Apply Selected] [Cancel]                   │
└──────────────────────────────────────────────────────────────────────────┘
```

**设计说明**：
- 顶部新增字段选择区域
- 用户可选择写入哪些增强字段
- 双击可编辑单个字段值

### 16.7 字段编辑弹窗

双击列表项时弹出，显示原始值与刮削值对比，支持手动编辑。

### 16.8 工作流程总结

**Stage 1 和 Stage 2 统一流程**：

1. 用户执行刮削/增强
2. 启动对话框只显示说明信息（不选择字段）
3. AI 获取所有可用字段
4. 确认对话框显示结果，用户选择：
   - 哪些歌曲写入（勾选歌曲）
   - 哪些字段写入（勾选字段）
5. 只写入用户选中的字段

**优势**：
- 避免用户在执行前不知道 AI 会返回什么
- 用户看到结果后再决定写入哪些字段
- 减少误操作

---

## 17. 日志系统

### 17.1 日志级别

| 级别 | 说明 |
| --- | --- |
| DEBUG | 详细调试信息 |
| INFO | 常规操作信息 |
| WARNING | 警告信息 |
| ERROR | 错误信息 |

### 17.2 日志文件

- C++日志：`{profile}/ai_metadata/logs/plugin.log`
- Python日志：`{profile}/ai_metadata/logs/worker.log`

---

## 18. 安全设计

### 18.1 API密钥存储

- 存储在 `config.yaml` 中
- 用户目录权限保护
- 不记录到日志

### 18.2 数据验证

- 所有输入进行JSON Schema验证
- 防止注入攻击

---

## 19. 性能预期

| 指标 | 目标值 |
| --- | --- |
| 单次AI调用延迟 | < 5s |
| 批量处理吞吐量 | > 100 tracks/min |
| 缓存命中率 | > 80% |
| Worker启动时间 | < 2s |

---

## 20. 配置系统

### 20.1 配置页面结构

```
AI Metadata (根节点)
├── General      - API Settings, Python, Cache, Logging
├── Data Sources - MusicBrainz, Discogs, AI, AI Batch
└── Advanced     - Worker, TaskQueue, Context Menu
```

### 20.2 配置文件结构

```yaml
worker:
  max_workers: 3
  timeout_ms: 30000

providers:
  default: openrouter
  openrouter:
    api_key: ""
    model: "openai/gpt-4o-mini"
  zhipu:
    api_key: ""
    model: "glm-4-flash"

cache:
  enabled: true
  ttl_days: 30

data_sources:
  musicbrainz:
    enabled: true
    rate_limit_rpm: 60
  discogs:
    enabled: true
  ai:
    enabled: true

scraping:
  auto_accept_threshold: 0.9
  confirm_threshold: 0.7
```

---

## 21. V8核心数据流架构

### 21.1 完整数据流

```
┌─────────────────────────────────────────────────────────────┐
│                    ai_worker.py (IPC入口)                    │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│                    cache_layer.py (缓存层)                   │
└─────────────────────────────────────────────────────────────┘
                              │ Cache Miss
                              ▼
┌─────────────────────────────────────────────────────────────┐
│              data_sources/manager.py (并发查询)              │
│         MusicBrainzAdapter + DiscogsAdapter + AIAdapter     │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│                 aggregator.py (候选聚合)                     │
│              去重、合并、按置信度排序                         │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│              fallback/controller.py (降级控制)               │
│    候选≥3 → 正常 │ 候选1-2 → 增强 │ 无候选 → 重写/推断       │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│                  resolver.py (AI决策层)                      │
│              从候选中选择最佳结果、补全字段                   │
└─────────────────────────────────────────────────────────────┘
```

### 21.2 Fallback Pipeline

| 场景 | 候选数量 | 处理方式 |
|------|----------|----------|
| 🟢 正常 | ≥3 | AIResolver 正常决策 |
| 🟡 不足 | 1~2 | AIResolver 增强模式（允许补全） |
| 🔴 无候选 | 0 | Fallback Pipeline（查询重写 → AI推断） |

### 21.3 置信度体系

| 数据源 | 置信度范围 | 说明 |
|--------|-----------|------|
| MusicBrainz | 0.80 ~ 0.95 | 权威数据源 |
| Discogs | 0.70 ~ 0.90 | 补充数据源 |
| AI 决策（正常） | 0.85 ~ 0.95 | 多候选融合决策 |
| AI 决策（增强） | 0.70 ~ 0.85 | 候选不足时补全 |
| AI Fallback（推断） | 0.40 ~ 0.70 | 最后手段 |

---

## 22. 未来扩展

### 22.1 短期计划

- [ ] 从文件名提取元数据
- [ ] 更多数据源支持（Last.fm, Spotify）
- [ ] 批量回滚功能

### 22.2 长期计划

- [ ] 自动化刮削（监控音乐库变化）
- [ ] 云端同步缓存
- [ ] 多语言支持

---

## 23. 版本历史

| 版本 | 日期 | 变更说明 |
| --- | --- | --- |
| v1.0 | 2024-01-01 | 初始版本，基础AI分析功能 |
| v2.0 | 2024-02-01 | 添加缓存层 |
| v3.0 | 2024-03-01 | 添加多Provider支持 |
| v4.0 | 2024-04-01 | 添加批量处理 |
| v5.0 | 2024-05-01 | 添加Worker进程管理 |
| v6.0 | 2024-06-01 | 添加设置面板 |
| v7.0 | 2024-07-01 | 整理版，完善文档 |
| **v8.0** | 2024-08-01 | **新增：AI元数据刮削系统、多数据源支持、备份与回滚** |

---

**文档结束**

这一版完全满足 **工业级插件开发**，可直接进入 **开发阶段**。
