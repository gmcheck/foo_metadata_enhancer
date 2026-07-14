下面是给 AI 编码助手的简要设计说明，偏工程需求描述：

---

# foo_metadata_enhancer Prompt 配置化设计需求

## 一、背景

当前插件功能：

1. 音乐元数据刮削；
2. AI 辅助翻译和规范化：

   * Artist
   * Album
   * Title
   * Genre 等；
3. 部分任务需要联网搜索，用于：

   * 查找官方中文译名；
   * 验证元数据；
   * 提高翻译准确性。

当前 Prompt 硬编码在代码中，需要改造成配置化。

---

# 二、设计目标

1. Prompt 与业务代码解耦；
2. 修改 Prompt 不需要重新编译插件；
3. 支持不同 AI Provider 使用不同 Prompt 微调；
4. 减少重复 Prompt，降低 Token 消耗；
5. 保持结构简单，不进行过度拆分。

---

# 三、Prompt 分层设计

## 1. Common Prompt（核心规则）

文件：

```
prompts/system.md
```

内容：

* AI 角色定义；
* 音乐元数据处理原则；
* 翻译规则；
* 官方译名优先；
* 不确定时保留英文；
* 禁止虚构；
* 通用行为约束。

所有模型共享。

---

## 2. Search Prompt（联网搜索规则）

文件：

```
prompts/search.md
```

内容：

* 搜索策略；
* 数据来源优先级；
* 官方来源优先；
* 如何处理多个译名；
* 如何判断可信度。

---

## 3. Output Prompt（输出格式）

文件：

```
prompts/output.md
```

内容：

* JSON 输出要求；
* 字段定义；
* 格式约束。

例如：

```json
{
  "artist_cn":"",
  "album_cn":"",
  "title_cn":"",
  "source":"",
  "confidence":""
}
```

---

# 四、Provider 定制（可选）

目录：

```
providers/
    glm.md
    deepseek.md
    qwen.md
```

用途：

只保存模型差异化要求。

例如：

* JSON 输出稳定性要求；
* 格式约束；
* 特殊提示。

不要复制完整 Prompt。

加载方式：

```
Common Prompt
      +
Provider Override
      +
Task Data
```

---

# 五、不配置化内容

以下保持代码实现：

* API 调用；
* 网络请求；
* Token 管理；
* Retry；
* Cache；
* JSON Parser；
* foobar2000 元数据读写；
* 字段映射；
* 工作流程控制。

---

# 六、避免过度拆分

不要设计：

```
translate_album.md
translate_track.md
translate_artist.md
translate_genre.md
```

原因：

* 内容高度重复；
* 增加维护成本；
* 增加 Token 消耗。

任务类型由代码参数传递：

例如：

```json
{
 "task":"metadata_enhance",
 "artist":"xxx",
 "album":"xxx",
 "title":"xxx"
}
```

AI 根据输入字段处理。

---

# 七、Prompt 组合方式

最终 Prompt：

```
system.md

+

search.md（需要联网时）

+

provider.md（存在时）

+

output.md

+

用户数据
```

---

# 八、核心原则

> Prompt 负责定义 AI 行为规则；
> 代码负责执行业务流程。

> 按变化频率拆分，而不是按功能拆分。

> 保持少量通用模板，通过参数控制任务差异。

---

目标结构：

```
foo_metadata_enhancer/

    prompts/
        system.md
        search.md
        output.md

    providers/
        glm.md
        deepseek.md
```

该设计兼顾：

* 可维护性；
* Token 成本；
* 多模型扩展；
* 后续 Prompt 优化。
