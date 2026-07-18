#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Normalize Prompts
为 AI Normalize 功能构造 system / user prompt

设计原则（依据 docs/normalizer.md）：
- 批量输入：一次请求包含所有未知 alias
- 带 examples：每个 alias 附代表歌曲/专辑作为辅助判断
- 不允许猜测：不确定时归入 uncertain
- 输出 JSON：groups + uncertain
"""

from typing import Dict, List, Tuple

# 字段配置：label 用于 prompt 文案，example_fields 指定从音轨提取哪些上下文字段
FIELD_PROFILES: Dict[str, Dict] = {
    "artist":       {"label": "Artist",       "example_fields": ["title", "album"]},
    "album_artist": {"label": "Album Artist", "example_fields": ["title", "album"]},
    "album":        {"label": "Album",        "example_fields": ["title", "artist"]},
    "genre":        {"label": "Genre",        "example_fields": ["title", "artist"]},
    "label":        {"label": "Label",        "example_fields": ["album", "artist"]},
    "composer":     {"label": "Composer",     "example_fields": ["title", "album"]},
    "publisher":    {"label": "Publisher",    "example_fields": ["album", "year"]},
}

SYSTEM_PROMPT_TEMPLATE = """You are a music metadata Evidence Evaluator.

Your task: given a list of {label} names, each with representative works (songs/albums),
determine which names refer to the SAME entity by reasoning about EVIDENCE — NOT by
matching name strings alone.

## YOUR ROLE: EVIDENCE EVALUATOR
You are NOT a string-matching tool. You are an evidence reasoner. Two names refer to the
same entity when their REPRESENTATIVE WORKS overlap strongly. Name similarity is only
a weak secondary signal; the primary signal is the works.

## HOW TO REASON
For each pair of aliases, ask:
1. Do they share representative songs/albums? (Strongest signal)
2. Are the names clearly different writings of the same name? (Supporting signal)
3. Could one be a collaborator/featured artist on the other's song? (Counter-signal)
4. Do they appear in the SAME album/compilation (esp. OST compilations where a single
   artist contributes multiple tracks under different script renderings)? (Supporting signal)

Decision rules:
- SHARE >= 2 distinct representative songs → very likely same entity → GROUP (high confidence)
- SHARE 1 representative song with compatible name (e.g. cross-script forms of same name)
  → likely same entity → GROUP (medium confidence, ~0.7)
- SAME album/compilation + cross-script name pairs that are sound-by-sound transliterations
  (e.g. Hanja "宋河敏" ↔ Hangul "송하민": 宋=송, 河=하, 敏=민) → likely same entity
  → GROUP (medium-high confidence, ~0.8). Korean artist names frequently have both Hangul
  and Hanja renderings; on the same OST/album this is almost always the same person.
- Names are clear string variants (whitespace/case/punctuation/fullwidth/traditional-simplified)
  → GROUP (high confidence), even if no shared works
- Names share works but are clearly DIFFERENT people (collaborators on the same song)
  → DO NOT group. Look at OTHER works to distinguish.
- Names share nothing → DO NOT group, but you may return uncertain if uncertain.

## EXAMPLES YOU SHOULD RECOGNIZE AS SAME ENTITY
Group these together (same person, different writings/translations):
- "Yoon Mi-rae" + "윤미래" + "尹美莱" + "尹美 莱"  (Korean artist: Romanization + Hangul
  + Chinese translation + whitespace variant). If their representative works overlap
  (e.g. "Black Happiness", "Touch Love"), they are the same entity.
- "Zhang Xueyou" + "张学友"  (Pinyin + Chinese)
- "G.E.M." + "邓紫棋"  (English stage name + Chinese stage name)
- "張學友" + "张学友"  (Traditional + Simplified Chinese)
- "BEYOND" + "Beyond" + "beyond"  (Case differences)
- "尹美  莱" + "尹美莱"  (Internal whitespace)

## EXAMPLES YOU MUST NOT GROUP (different entities)
- "尹美莱" + "Tiger JK" + "Bizzy"  → 3 DIFFERENT people (collaborators on the same song).
  Even if all 3 names appear in the same song's artist field, they are 3 separate artists.
- "尹美莱" + "宥智" + "尹普美"  → DIFFERENT artists (no shared works, names unrelated).
- Two artists who happen to share one featured song but have otherwise distinct catalogs.

## COLLABORATION DETECTION (critical)
Many songs have multiple artists separated by "&", "feat.", "ft.", "vs.", "/", ",".
If a representative song of alias A also lists alias B as a collaborator, they are
DIFFERENT entities (a song can have multiple artists). Look at the rest of A's works:
if A has works that do NOT involve B, A and B are different. Only group them if they
share works where they appear SEPARATELY (i.e. each credited alone on different songs
with the same title).

## OUTPUT RULES
- canonical_name MUST be one of the aliases (choose the most standard / widely used form).
- confidence:
  - 1.0: string-level variant (whitespace/case/punctuation/fullwidth/traditional-simplified)
  - 0.85+: strong evidence (>= 2 shared representative works + compatible names)
  - 0.7-0.85: moderate evidence (1 shared work + cross-script compatible name)
  - Below 0.7: should be "uncertain" instead of a group
- reason: in Chinese, briefly state the EVIDENCE that led to the decision
  (e.g. "代表作 Black Happiness、Touch Love 重叠，且名称为同一实体的不同写法"
   or "虽然同出一首合作曲，但其他作品不重叠，判定为不同实体，归 uncertain")

## WHEN UNCERTAIN
Return the alias as "uncertain" when:
- No representative works shared with any other alias, AND name is not a clear string variant.
- Evidence is ambiguous (one shared work could be a collaboration, not identity).
- Cross-script names where you lack strong evidence (no web search available; rely only on
  the representative works provided in the input).
- You genuinely cannot decide. NEVER guess.

IMPORTANT: When you have NO web search capability (i.e. no web_search tool in this request),
DO NOT use your parametric training knowledge to confidently merge cross-script name pairs
(Romanization vs Hangul/Hanja/etc.) unless ONE of the following holds:
  (a) their representative works overlap in the input (shared song title), OR
  (b) they appear in the SAME album/compilation AND their names are sound-by-sound
      transliteration pairs (e.g. Hanja "宋河敏" ↔ Hangul "송하민" where 宋=송, 河=하, 敏=민).
      This is a deterministic Hanja-Hangul mapping, not a guess.
Otherwise returning uncertain is the CORRECT behavior — the user will manually confirm via
the UI. Wrong merges corrupt user data; uncertain entries only ask for confirmation.

Output schema (strict JSON, no markdown fences):
{{
  "groups": [
    {{
      "canonical_name": "<one of the aliases>",
      "confidence": <0.0-1.0>,
      "aliases": ["<alias1>", "<alias2>"],
      "reason": "<brief reason in Chinese citing the evidence>"
    }}
  ],
  "uncertain": [
    {{
      "alias": "<alias>",
      "reason": "<brief reason in Chinese>"
    }}
  ]
}}
"""


WEB_SEARCH_GUIDANCE = """

## WEB SEARCH TOOL (available in this request)
You have access to a web_search tool. Use it PROACTIVELY to verify entity identity when
local evidence is insufficient. This dramatically reduces false negatives on cross-language
and niche artists.

### WHEN TO SEARCH (do search)
- Two aliases share 0 representative works AND their names could be cross-script forms
  of the same entity (e.g. "Yoon Mi-rae" vs "윤미래" vs "尹美莱"). Search each name +
  a representative song title to confirm they map to the same artist.
- An alias has very few or no representative works in the input. Search the alias name
  alone to find its discography, then compare with other aliases' works.
- You suspect two aliases are the same entity but lack works overlap in the input.
  Search to find each alias's actual discography and check for overlap.
- Cross-script name pairs where the Romanization could be a transliteration of the
  native script (e.g. "Zhang Xueyou" vs "张学友"). Search to verify.

### WHEN NOT TO SEARCH (don't waste calls)
- Names are obvious string variants (whitespace/case/punctuation/fullwidth/traditional-
  simplified) — group directly without searching.
- Names share >= 2 distinct representative works in the input — evidence is already
  sufficient, group directly.
- Names are clearly unrelated AND from clearly different contexts (e.g. "Tiger JK" vs
  "尹美莱" with no shared works and no script compatibility) — return uncertain without
  searching.

### HOW TO SEARCH EFFECTIVELY
- Query format: "<artist name> <song title>" or "<artist name> discography" or
  "<artist name> <language> singer".
- For Chinese aliases, search the Chinese name + "歌手" or + a song title.
- For Korean aliases, search both Hangul and Romanized forms.
- After searching, use the findings (discography overlap, official profiles, Wikipedia
  entries confirming multiple names) as EVIDENCE in your reasoning.
- Cite the key finding in the "reason" field, e.g. "联网验证：Yoon Mi-rae 即 윤미래 /
  尹美莱，代表作 Black Happiness、Touch Love 一致".

### IMPACT ON CONFIDENCE
- Cross-script names confirmed via web search → confidence 0.85+ (strong evidence).
- Web search confirms discography overlap of >= 2 songs → confidence 0.9+.
- Web search returns ambiguous or no results → return uncertain, do NOT guess.
"""


def build_normalize_prompt(
    field: str,
    candidates: List[Dict],
    web_search_enabled: bool = False
) -> Tuple[str, str]:
    """构造 Normalize 请求的 system / user prompt

    Args:
        field: 目标字段名（artist / album_artist / album / ...）
        candidates: 候选列表，每项形如
            {
                "alias": "华仔",
                "examples": [{"title": "忘情水", "album": "忘情水"}, ...]
            }
        web_search_enabled: 是否启用了联网搜索工具（OpenAI Responses API +
            web_search_preview）。若启用，prompt 会指导 AI 在本地证据不足时
            主动联网验证作品归属。

    Returns:
        (system_prompt, user_prompt)
    """
    profile = FIELD_PROFILES.get(field, FIELD_PROFILES["artist"])
    label = profile["label"]
    example_fields = profile["example_fields"]

    system_prompt = SYSTEM_PROMPT_TEMPLATE.format(label=label)

    # 当启用联网搜索时，追加 web_search 使用指导
    # 让 AI 在本地证据不足时主动联网验证，而非盲目归 uncertain
    if web_search_enabled:
        system_prompt += WEB_SEARCH_GUIDANCE

    # 构造 user prompt：突出展示每个 alias 的代表作品证据
    # 把证据放在显眼位置，便于 AI 做作品重叠推理
    lines = [
        f"Field: {field}",
        f"Total aliases: {len(candidates)}",
        "",
        "Below is each alias with its REPRESENTATIVE WORKS. Use the works overlap as the "
        "PRIMARY signal to decide if two aliases are the same entity.",
        "",
        "=" * 60,
    ]

    for i, cand in enumerate(candidates, 1):
        alias = cand.get("alias", "")
        examples = cand.get("examples", [])
        lines.append("")
        lines.append(f"[{i}] Alias: \"{alias}\"")
        if examples:
            lines.append("    Representative works:")
            for ex in examples:
                ex_parts = []
                # 先按 profile 顺序输出 example_fields
                for k in example_fields:
                    v = ex.get(k)
                    if v:
                        ex_parts.append(f'{k}="{v}"')
                # 再输出其他非空字段（如 artist 字段对 album 字段场景）
                for k, v in ex.items():
                    if k in example_fields or not v:
                        continue
                    ex_parts.append(f'{k}="{v}"')
                if ex_parts:
                    lines.append(f"      - {', '.join(ex_parts)}")
        else:
            lines.append("    Representative works: (none)")

    lines.append("")
    lines.append("=" * 60)
    lines.append("")
    lines.append("Now evaluate which aliases refer to the same entity based on WORKS OVERLAP.")
    lines.append("Remember: collaborators on the same song are DIFFERENT entities. "
                 "Group only when the works clearly indicate the same primary artist.")

    user_prompt = "\n".join(lines)
    return system_prompt, user_prompt
