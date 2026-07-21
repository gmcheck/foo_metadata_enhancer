# AI Providers 设计（V1 定稿）

> 状态：V1 定稿  
> 范围：多 Provider 实例 + OpenAI Chat / Anthropic Messages  
> 非目标：Gemini / Ollama / web_search / Provider 内多 Model / 自动 fallback

---

## 一、设计目标

将 AI 服务抽象为 **Provider 实例**。插件始终使用 **当前选中的 Provider**，业务不关心具体厂商。

Provider 是一条用户可配置的记录，例如：

- Zhipu 官方（seed 预设，可删）
- DeepSeek（seed 预设，可删）
- 公益站 / SiliconFlow / 公司代理（用户自建）

协议（protocol）决定请求如何发送，而不是厂商类继承。

**V1 原则：**

1. Provider 是 AI 服务的抽象单位  
2. Model 是 Provider 上的可编辑字段，不维护官方模型列表  
3. 业务模块（Scrape / Translate / Normalize 等）只依赖当前 Provider  
4. 优先支持多个 Provider，而不是一个 Provider 多个 Model  
5. Provider 配置与业务配置分离  

---

## 二、V1 范围

### 2.1 做

| 项 | 说明 |
|----|------|
| 多 Provider 实例 | 可增删改、可切换 |
| 协议 | `openai_chat`、`anthropic_messages` |
| 存储 | Python 端 SQLite（`providers` + `app_settings`） |
| Seed 预设 | 表为空时写入；标记 `is_preset`，**可删除** |
| 当前选中 | `app_settings.current_provider_id` |
| 连接测试 | 支持错误分类 |
| 业务接入 | 仅使用当前 Provider 发起调用 |

### 2.2 不做（明确 out of scope）

| 项 | 说明 |
|----|------|
| Gemini / Ollama | 协议差异大，后期按需加 Protocol Client |
| OpenAI Responses / web_search | 多数兼容站不支持，V1 不做 |
| Provider 内多 Model | 一个 Provider 一个 `model` 字段 |
| 自动 fallback | 跨 Provider / 多 model 故障转移暂不做，仅留配置口子 |
| 按厂商继承的 Provider 类 | 删除 Zhipu/DeepSeek/Gemini 等专用类路径 |
| `config.yaml` 固定厂商槽位 | 不再维护 `providers.zhipu` 这类结构作为 SoT |
| `settings.json` 存 provider_configs | 迁移后废弃，不再作为权威来源 |

### 2.3 以后扩展（不实现，只预留方向）

新增能力 = 新增 **Protocol Client**，而不是新增厂商 Provider 类。

可能协议：

- `openai_responses`（含 web_search）
- `gemini_generate`
- `ollama_chat`
- Azure OpenAI / LM Studio / OneAPI 等：多数仍可归入 `openai_chat`，只需不同 base_url

业务逻辑仍只依赖「当前 Provider」，无需修改。

---

## 三、分层架构

```
┌──────────────────────────────────────────────┐
│  Preferences UI (C++)                        │
│  Provider 列表 / 编辑 / 当前选择 / Test       │
└─────────────────────┬────────────────────────┘
                      │ IPC
                      ▼
┌──────────────────────────────────────────────┐
│  ProviderStore (Python)                      │
│  SQLite: providers + app_settings            │
│  CRUD / seed / 迁移 / 当前选中               │
└─────────────────────┬────────────────────────┘
                      │
                      ▼
┌──────────────────────────────────────────────┐
│  ProviderRuntime                             │
│  provider row → Protocol Client              │
└─────────────────────┬────────────────────────┘
                      │
          ┌───────────┴───────────┐
          ▼                       ▼
   openai_chat client      anthropic_messages client
          │                       │
          └───────────┬───────────┘
                      ▼
                    HTTP
```

**职责划分：**

| 层 | 职责 |
|----|------|
| UI | 展示与编辑；不直接拥有 Provider 业务存储 |
| ProviderStore | 唯一权威存储（SoT）；seed、迁移、CRUD |
| ProviderRuntime | 根据 `protocol` 创建 client |
| Protocol Client | URL 拼装、鉴权、请求/响应、错误分类 |
| 业务层 | 只调用当前 Provider 的 `chat_completion` / `chat_completion_json` |

---

## 四、权威存储（Source of Truth）

### 4.1 硬约定

1. **`providers` 与 `current_provider_id` 的唯一权威存储是 Python SQLite**  
2. **C++ Preferences 仅通过 Worker IPC 访问 Provider 数据**  
3. **`settings.json` 不再保存 `provider` / `provider_configs`（迁移完成后）**  
4. **`config.yaml` 不再作为 Provider 配置 SoT**（可保留 worker/logging 等非 Provider 配置）

### 4.2 数据库位置

与现有 normalize 等数据相同库：

```text
<foobar2000 profile>/foo_metadata_enhancer/foo_metadata_enhancer.db
```

Python 侧新增 `ProviderStore`（参考现有 `NormalizeStore` 模式）。

### 4.3 表结构

#### providers

| 字段 | 类型 | 说明 |
|------|------|------|
| id | TEXT PK | UUID |
| name | TEXT NOT NULL | 显示名，用户可改（如 "GLM Official"、"公益 Grok"） |
| protocol | TEXT NOT NULL | `openai_chat` \| `anthropic_messages` |
| base_url | TEXT NOT NULL | API root，见 4.5 URL 约定 |
| api_key | TEXT NOT NULL | API Key（V1 本地明文，与现 settings 同级；加密后置） |
| model | TEXT NOT NULL | 当前使用的模型 ID，用户可随时修改 |
| enabled | INTEGER NOT NULL DEFAULT 1 | 是否启用（预留；V1 UI 可先不暴露禁用） |
| sort_order | INTEGER NOT NULL DEFAULT 0 | 列表排序 |
| is_preset | INTEGER NOT NULL DEFAULT 0 | 是否来自 seed（仅标记，**不限制删除/编辑**） |
| created_at | TEXT NOT NULL | ISO8601 |
| updated_at | TEXT NOT NULL | ISO8601 |

**不设 `provider_type: builtin/custom`。**  
预设与用户自建在运行时是同一类记录；`is_preset` 仅用于 UI 可选展示。

#### app_settings

| 字段 | 类型 | 说明 |
|------|------|------|
| key | TEXT PK | 配置键 |
| value | TEXT NOT NULL | 配置值 |

V1 使用的 key：

| key | value 含义 |
|-----|------------|
| `current_provider_id` | 当前选中的 providers.id |
| `fallback_provider_ids` | 预留，JSON 数组字符串，**V1 读取但忽略**，默认 `[]` |
| `providers_migrated` | 迁移标记，如 `1`，避免重复导入 |

### 4.4 Model 规则

- Model 是 Provider 的字段，不是子表  
- **不在插件内维护模型白名单**  
- 代码/seed 可提供 **默认初值**，用户可改成任意字符串  
- 运行时 **不做** `SUPPORTED_MODELS` 校验  
- 模型升级（glm-4 → glm-5 → glm-5.2）只需改字段，无需升级插件  

### 4.5 Base URL 约定

用户填写 **API root**（不要带具体 path；允许实现做归一化）：

| protocol | 用户填写示例 | 实际请求 |
|----------|--------------|----------|
| `openai_chat` | `https://open.bigmodel.cn/api/paas/v4` | `{base}/chat/completions` |
| `anthropic_messages` | `https://api.anthropic.com` | `{base}/v1/messages` |

规则：

- 若用户已填完整 path（已含 `/chat/completions` 或 `/v1/messages`），实现应识别并避免重复追加  
- seed 与文档示例统一使用 root 形式  

### 4.6 配置归属

| 配置项 | 归属 |
|--------|------|
| providers 列表、api_key、model、protocol、base_url | SQLite `providers` |
| current_provider_id | SQLite `app_settings` |
| fallback_provider_ids（预留） | SQLite `app_settings` |
| worker timeout / batch / log_level | 仍由 `settings.json` 或 `config.yaml` |
| prompts.user_prefs | 仍由 `settings.json` |

---

## 五、Seed 预设

### 5.1 时机

- **仅当 `providers` 表为空时** 写入 seed  
- 或：首次迁移完成后表仍为空时写入  
- **用户删光所有 Provider 后，启动不得自动重新灌入 seed**（否则「可删除」无效）  
- UI 在列表为空时引导用户 [+ Add] 或「恢复默认预设」（显式操作，可选）

### 5.2 预设内容（示例）

| name | protocol | base_url | model 初值 | is_preset |
|------|----------|----------|------------|-----------|
| Zhipu | openai_chat | `https://open.bigmodel.cn/api/paas/v4` | `glm-5` | 1 |
| DeepSeek | openai_chat | `https://api.deepseek.com/v1` | `deepseek-chat` | 1 |
| OpenRouter | openai_chat | `https://openrouter.ai/api/v1` | `openrouter/free` | 1 |

- `api_key` 一律为空  
- 名称、URL、model 写入后与普通记录相同，用户可改可删  

### 5.3 与「不写死 model」的关系

Seed 只提供 **可用的默认初值**，不是白名单。  
运行时与校验逻辑不得依赖「必须是预设模型名」。

---

## 六、为什么多 Provider、不多 Model

### 6.1 一个 Provider 一个 Model

站点侧切换模型只需改 `model` 字段。  
为单个 Provider 维护 Model1/2/3 列表收益小，UI 与存储更复杂。

### 6.2 多个 Provider 的价值

用户常同时拥有：

- GLM 官方  
- 公益站  
- SiliconFlow  
- 公司代理  

需要的是 **切换整套（URL + Key + Model）**，而不是每次重填。  
因此：**多 Provider ≫ 多 Model**。

### 6.3 Fallback 口子（V1 不实现逻辑）

```text
app_settings.current_provider_id   → 生效
app_settings.fallback_provider_ids → 预留，V1 忽略
```

运行时接口保持简单：

```text
get_active_provider() -> Provider
# 将来可选：get_provider_chain() -> [primary, *fallbacks]
```

业务层始终只拿 **一个** Provider 调用；不做 failover 状态机。  
现有 `models[]` + priority / `get_fallback_models()` 在重构中删除，避免两套语义。

---

## 七、UI 设计

### 7.1 AI Providers 页

```text
AI Providers                    [+ Add]

--------------------------------
Name              Model
--------------------------------
GLM Official      glm-5.2
DeepSeek          deepseek-chat
公益 Grok          grok-4.5

[Edit]  [Delete]

Current Provider
▼ GLM Official
Model: <当前 Provider 的 model，可在此快捷修改或进入 Edit>

[Test Connection]
```

### 7.2 Add / Edit 表单

| 字段 | 说明 |
|------|------|
| Name | 必填 |
| Protocol | `OpenAI Chat Completions` / `Anthropic Messages` |
| Base URL | API root |
| API Key | 密码框 |
| Model | 自由文本 |

`[Save]` `[Cancel]`

### 7.3 UI 与数据流

- 列表 / 保存 / 删除 / 设置当前：一律 IPC → Python `ProviderStore`  
- 打开 Preferences 时：若 Worker/DB 未就绪，需有 ensure 路径（启动轻量 Python 访问或保证 DB 可初始化）  
- 变更 current / api_key / model / base_url 后：刷新 Worker 内 Provider 实例（沿用异步重启或等价热加载，避免卡 UI）  

---

## 八、连接测试

### 8.1 行为

`[Test Connection]`：

1. 优先使用 **表单当前值**（未保存也可测），临时构造 client  
2. 验证：Base URL 可达、API Key 有效、Model 可用、能返回正常响应  
3. 不强制先入库  

### 8.2 错误分类

尽量映射为：

| 类别 | 含义 |
|------|------|
| API Key Invalid | 密钥格式/错误 |
| Unauthorized | 401/鉴权失败 |
| Model Not Found | 模型不存在 |
| Timeout | 超时 |
| Network Error | 网络/DNS/连接失败 |
| Rate Limit | 429 / 限流 |

避免只显示笼统的 `Request Failed`。

---

## 九、IPC 契约（C++ ↔ Python）

Provider 相关操作由 Worker 提供命令（名称可在实现时微调，语义固定）：

| 命令 | 方向 | 说明 |
|------|------|------|
| `providers.list` | C++ → Py | 返回全部 providers（api_key 可掩码） |
| `providers.get` | C++ → Py | 按 id 获取 |
| `providers.create` | C++ → Py | 新增 |
| `providers.update` | C++ → Py | 更新 |
| `providers.delete` | C++ → Py | 删除；若删的是当前，需清理/重选 current |
| `providers.set_current` | C++ → Py | 设置 `current_provider_id` |
| `providers.get_current` | C++ → Py | 当前 Provider 详情 |
| `providers.test` | C++ → Py | 连接测试（可带未保存草稿字段） |
| `providers.restore_presets` | C++ → Py | 可选：用户显式恢复 seed（不覆盖已有同名策略由实现定） |

业务任务（scrape/enhance 等）**不再**在请求里传厂商名枚举；Worker 启动或任务执行时从 Store 加载当前 Provider。

---

## 十、运行时与代码形态（目标）

```text
worker/
  db/provider_store.py           # SQLite CRUD、seed、迁移
  ai/protocols/
    base.py                      # Protocol Client 接口
    openai_chat.py
    anthropic_messages.py
  ai/provider_runtime.py         # row → client
  ai/adapter.py                  # 业务适配：只用 get_active_provider()

plugin/
  preferences: Provider 列表 UI
  通过 IPC 管理 providers
```

**删除 / 弱化：**

- `ZhipuProvider` / `DeepSeekProvider` / `GeminiProvider` / `OllamaProvider` 等厂商类  
- `ProviderType` 厂商枚举驱动工厂（改为 protocol 枚举）  
- `SUPPORTED_MODELS` 白名单  
- `config.yaml` 中 `providers.zhipu` 等固定槽位作为运行时配置  
- `settings.json` 的 `provider` / `provider_configs`  
- 业务层 `switch_provider("zhipu")` 等厂商 API  
- `web_search` / `chat_completion_json_with_web_search` 主路径依赖  

**保留并收敛：**

- 统一聊天接口：`chat_completion` / `chat_completion_json`  
- 超时、重试（全局 worker 配置；将来可 per-provider 覆盖）  
- 连接测试与错误分类  

**Prompt profile：**

- 不再按厂商名挂 profile  
- V1 使用统一默认 prompt 参数；若需差异，按 **protocol** 挂少量差异，避免厂商泄漏  

---

## 十一、迁移

从旧配置 **一次性导入** SQLite，完成后写 `providers_migrated=1`。

### 11.1 来源

1. `settings.json`：`provider`、`provider_configs`  
2. 可选补充：`config.yaml` 中尚有而 settings 缺失的 base_url 等  

### 11.2 映射

| 旧 provider 名 | protocol | 说明 |
|----------------|----------|------|
| zhipu / deepseek / openrouter / openai | `openai_chat` | 取 api_key、selected_model→model、base_url |
| custom 且 api_format=openai | `openai_chat` | 同上 |
| custom 且 api_format=anthropic | `anthropic_messages` | 同上 |
| gemini / ollama | **不导入为可用协议** | V1 不支持；可记录日志/UI 提示用户改用 OpenAI 兼容端点 |

### 11.3 当前选中

- 旧 `provider` 能映射到新行 → 设为 `current_provider_id`  
- 否则：选 sort_order 最小的可用行；若无，保持未选中并引导配置  

### 11.4 Seed 与迁移顺序

```text
1. 若已 providers_migrated → 跳过迁移
2. 尝试从 settings.json 导入
3. 若 providers 仍为空 → seed 预设
4. 标记 providers_migrated=1
```

---

## 十二、设计原则（汇总）

1. **Provider 实例** 是 AI 服务的抽象，不是厂商枚举  
2. **Protocol** 是扩展点；新增协议 = 新 Client，不是新业务分支  
3. **Model** 是可编辑字符串，seed 只给初值，无白名单  
4. **多 Provider** 优先于多 Model  
5. **SQLite 为 SoT**；C++ UI 经 IPC 访问  
6. **业务只依赖当前 Provider**  
7. **V1 裁剪**：无 Gemini/Ollama、无 web_search、无自动 fallback  
8. **Preset 可删**：seed 非系统锁死记录  

---

## 十三、实现检查清单（供开发对照）

- [ ] `ProviderStore`：表结构、CRUD、seed、迁移  
- [ ] `openai_chat` / `anthropic_messages` 两个 Protocol Client  
- [ ] Runtime：current_provider_id → client  
- [ ] IPC：list/create/update/delete/set_current/test  
- [ ] Preferences UI 改为列表模型（非固定厂商下拉）  
- [ ] 业务路径去掉厂商 switch  
- [ ] 删除/停用旧厂商 Provider 类与 yaml 槽位依赖  
- [ ] 去掉 web_search 主路径  
- [ ] 旧 settings 迁移 + 空表 seed  
- [ ] Test Connection 错误分类  
- [ ] `fallback_provider_ids` 字段写入默认值但不使用  
