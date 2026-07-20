AI Providers 设计
一、设计目标

将 AI 服务抽象为 Provider，插件始终使用当前选中的 Provider，而不是关心具体厂商。

Provider 可以是：

内置 Provider（Zhipu、DeepSeek）
Custom Provider（OpenAI Chat Completions）
Custom Provider（Anthropic Messages）

以后新增协议或厂商，不需要修改业务逻辑。

二、数据库设计
providers
字段	说明
id	主键
name	Provider 名称（用户可修改，例如 "GLM Official"、"公益 Grok"）
provider_type	builtin / custom
protocol	openai_chat / anthropic_messages
base_url	API 地址
api_key	API Key（建议加密存储）
model	默认 Model（用户可修改）
enabled	是否启用
sort_order	排序
created_at	创建时间
updated_at	更新时间

说明：

model 为可编辑字段。
不要写死任何模型名称。
模型升级（glm-4 → glm-5 → glm-5.2）无需升级插件。



三、Provider 配置

一个 Provider 包含：

Name

Protocol

Base URL

API Key

Model

例如：

Name:
GLM Official

Protocol:
OpenAI Chat Completions

Base URL:
https://open.bigmodel.cn/api/paas/v4

API Key:
********

Model:
glm-5.2


四、Model 设计

Model 是 Provider 的默认配置。

要求：

用户可随时修改。
不在插件内维护模型列表。
不写死任何模型。

原因：

模型更新非常频繁：

glm-4
↓

glm-5

↓

glm-5.2

↓

glm-6

插件无需升级。

五、UI 设计
AI Providers 页面

管理所有 Provider。

AI Providers     [+ Add]

--------------------------------
Name              Model
--------------------------------
GLM Official      glm-5.2
DeepSeek          deepseek-chat
公益 Grok          grok-4.5

[Edit] [Delete] 

Current Provider
▼ GLM Official
Model: providers 表中的默认 model，用户可随时修改。

[Test Connection]




点击Add 或，选中具体 provider点击 Edit：

Name
Protocol
Base URL
API Key
Model

[Save] [Cancel]



六、连接测试
[Test Connection]

测试内容：

Base URL 是否可访问
API Key 是否有效
Model 是否存在
是否返回正常响应

错误尽量分类：

API Key Invalid
Unauthorized
Model Not Found
Timeout
Network Error
Rate Limit

而不是统一显示：

Request Failed


七、为什么不支持多个 Model

一个 Provider 一次只使用一个 Model。

站点本身已经支持：

glm-5.2

↓

grok-4.5

↓

deepseek-v4-flash

切换只需要修改 Model 字段即可。

没有必要设计：

Provider

Model1

Model2

Model3

收益很小。

八、为什么支持多个 Provider

很多用户同时拥有多个站点：

GLM 官方

公益站

SiliconFlow

公司代理

他们需要：

切换 Provider

而不是：

重新填写：

Base URL
API Key
Model

因此：

支持多个 Provider 的价值远高于支持多个 Model。

九、扩展性

以后增加：

OpenAI Responses API
Gemini
Azure OpenAI
Ollama
LM Studio
自建 OneAPI

仅新增：

protocol

业务逻辑无需修改。

Provider 仍然只是：

Name

Protocol

Base URL

API Key

Model
十、设计原则
Provider 是 AI 服务的抽象。
Model 是 Provider 的默认配置，可运行时修改。
插件始终只使用当前选中的 Provider。
业务模块（Scrape、Translate、Normalize 等）不关心厂商，只依赖 Provider。
优先支持多个 Provider，而不是多个 Model。
Provider 管理与业务配置分离，降低后续维护成本。