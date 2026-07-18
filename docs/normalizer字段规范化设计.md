
---

AI Normalize 接口设计（Artist 第一阶段）
一、目标

实现一个 AI Normalize 服务。

第一阶段支持：

Artist

后续支持：

Album Artist
Album
Genre
Label
Composer
Publisher

Normalize 的职责：

对一批元数据进行实体归一化（Entity Normalization），识别同一实体的不同名称，并推荐 Canonical Name。

它不是 Metadata Scraper。

它不会联网查元数据。

它也不负责补充发行年份、Label 等信息。

二、整体流程
读取歌曲

↓

提取 Artist

↓

SQLite Alias 查询

↓

命中
↓

直接返回

未命中

↓

收集上下文

↓

AI Normalize

↓

返回建议

↓

用户确认

↓

更新 SQLite

↓

修改 Tag
三、为什么先查 SQLite

SQLite 是整个 Normalize 系统的知识库。

例如：

BEYOND

↓

Beyond

以后再次出现：

BEYOND

无需 AI。

因此：

AI 只处理未知 Alias。

四、AI 输入数据

注意：

不是发送一个 Artist。

也不是发送一个 Song。

而是：

发送一批未知 Artist。

例如：

{
  "field": "artist",
  "candidates": [
    {
      "alias": "华仔",
      "examples": [
        {
          "title": "忘情水",
          "album": "忘情水"
        },
        {
          "title": "天意",
          "album": "天意"
        },
        {
          "title": "一起走过的日子",
          "album": "一起走过的日子"
        }
      ]
    },
    {
      "alias": "刘德华",
      "examples": [
        {
          "title": "冰雨",
          "album": "男人的爱"
        }
      ]
    },
    {
      "alias": "BEYOND",
      "examples": [
        {
          "title": "海阔天空",
          "album": "乐与怒"
        }
      ]
    },
    {
      "alias": "Beyond",
      "examples": [
        {
          "title": "长城",
          "album": "乐与怒"
        }
      ]
    }
  ]
}

这里的 examples：

不是为了刮削。

只是为了帮助 AI 判断。

五、AI Prompt

系统 Prompt：

你是一个音乐元数据规范化助手。

请根据 Artist 名称以及代表歌曲，判断哪些 Artist 表示同一个实体。

不要修改不存在对应关系的 Artist。

不确定时返回 uncertain。

不要猜测。

输出 JSON。

六、AI 输出

建议：

{
  "groups": [
    {
      "canonical_name": "刘德华",
      "confidence": 0.99,
      "aliases": [
        "刘德华",
        "华仔"
      ],
      "reason": "华仔是刘德华常见昵称"
    },
    {
      "canonical_name": "Beyond",
      "confidence": 0.99,
      "aliases": [
        "Beyond",
        "BEYOND"
      ],
      "reason": "仅大小写不同"
    }
  ],
  "uncertain": [
    {
      "alias": "Alex",
      "reason": "可能对应多个歌手"
    }
  ]
}
七、插件处理

AI 返回以后。

不要立即修改 Tag。

生成：

建议

刘德华

← 华仔

Beyond

← BEYOND

等待用户确认。

八、SQLite 更新

确认以后：

artist_alias

----------------------

alias_name

canonical_name

source

confirmed

created_time

例如：

alias	canonical
华仔	刘德华
BEYOND	Beyond
九、修改 Tag

例如：

原来：

Artist

BEYOND

修改：

Artist

Beyond
十、为什么发送一批 Artist

原因：

AI 可以利用全局信息。

例如：

发送：

刘德华

华仔

Andy Lau

AI 能知道：

属于同一组

而不是逐条判断。

十一、为什么带 Examples

例如：

Alias

华仔

AI 有时不知道。

但是：

Alias

华仔

Songs

忘情水

一起走过的日子

天意

AI 基本可以确认。

Examples 只是辅助判断。

不是刮削信息。

十二、限制

每个 Alias：

建议最多：

Songs

3 首

Album：

2 张

即可。

无需全部发送。

十三、AI 不允许做的事情

AI 不负责：

修改 Tag
写 SQLite
自动确认
联网刮削
查询 MusicBrainz
查询 Discogs

它只是：

返回 Normalize 建议。

十四、未来扩展

未来：

Artist

↓

Album

↓

Genre

↓

Label

↓

Composer

全部使用同一套接口。

唯一变化：

{
    "field":"artist"
}

改成：

{
    "field":"genre"
}

即可。
