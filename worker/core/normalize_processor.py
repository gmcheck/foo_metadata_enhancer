#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Normalize Processor
元数据实体归一化处理器

职责：
- 接收一批未知 alias（含上下文 examples）
- 调用 AI 推断哪些 alias 属于同一实体，推荐 canonical name
- 返回 groups（已分组）+ uncertain（无法判定）

不做的事情（依据 docs/normalizer.md 第十三节）：
- 不修改 Tag
- 不写 SQLite
- 不自动确认
- 不联网刮削
"""

import json
import logging
import unicodedata
from typing import Dict, Any, List, Optional

from prompts.normalize_prompts import build_normalize_prompt
from common.text_utils import clean_value, INVALID_VALUES


logger = logging.getLogger(__name__)


class NormalizeProcessor:
    """元数据归一化处理器

    通过 AI 对一批 alias 进行实体归一化，返回分组建议。
    """

    # 零宽字符集合（不可见，应从 key 中完全移除）
    _ZERO_WIDTH_CHARS = frozenset({'\u200b', '\u200c', '\u200d', '\ufeff', '\u2060'})

    def __init__(self, config: Dict[str, Any], ai_adapter=None):
        """初始化

        Args:
            config: 配置字典
            ai_adapter: ModelAdapter 实例（可选，延迟创建）
        """
        self.config = config
        self._ai_adapter = ai_adapter

    def _get_adapter(self):
        """延迟创建 ModelAdapter"""
        if self._ai_adapter is None:
            from ai.adapter import ModelAdapter
            self._ai_adapter = ModelAdapter(self.config)
        return self._ai_adapter

    @classmethod
    def _normalize_key(cls, s: str) -> str:
        """归一化 key：移除零宽字符 + trim 首尾 Unicode 空白 + 转小写

        Python str.strip() 默认处理所有 Unicode 空白字符，包括：
        - ASCII 空格 \\t \\n \\r \\f \\v
        - 全角空格 U+3000
        - 不间断空格 U+00A0
        - 其他 Unicode 空白字符

        该 key 用于匹配 track 的 field 值与 group 的 alias，避免因 Unicode
        表示差异（如 NFC vs NFD 韩文、尾部全角空格等）导致匹配遗漏。
        """
        if not s:
            return ""
        # 先移除所有零宽字符（它们不影响显示但会导致 key 不同）
        s = ''.join(c for c in s if c not in cls._ZERO_WIDTH_CHARS)
        # NFC 规范化：合并分解形式（NFD）为预组合形式
        # 韩文（Hangul）在 NFC/NFD 下字节序列不同，AI 返回的 JSON 可能是 NFD，
        # 而 C++ 传入的字符串通常是 NFC，导致精确字符串匹配失败
        s = unicodedata.normalize('NFC', s)
        # strip 首尾 Unicode 空白 + 转小写
        return s.strip().lower()

    def process(self, request: Dict) -> Dict:
        """处理 normalize 请求

        Args:
            request: IPC 请求字典，结构：
                {
                    "method": "normalize",
                    "params": {
                        "field": "artist",
                        "candidates": [
                            {"alias": "华仔", "examples": [{"title": "忘情水", "album": "忘情水"}]},
                            ...
                        ]
                    }
                }

        Returns:
            IPC 响应字典，结构：
                {
                    "success": True/False,
                    "result": {
                        "groups": [...],
                        "uncertain": [...]
                    }
                }
        """
        request_id = request.get("id", "")
        task_id = request.get("task_id", "")
        params = request.get("params", {})
        field = params.get("field", "artist")
        candidates = params.get("candidates", [])
        # C++ 端查询 SQLite 命中的 groups（canonical_name + aliases），
        # 交给 Python 与 _clean_candidates 生成的变体 pre_groups 合并。
        # 这样能正确处理 SQLite 存的是 trim 过的 alias，而 track 实际值带尾空格的情况。
        known_groups = params.get("known_groups", [])
        # C++ 端送来的每个 track 当前 field 的所有 values（multi-value 支持）。
        # Python 在生成最终 groups 后，根据 groups 为每个 track 构造目标 values，
        # 一起返回给 C++，避免 C++ 端做字符串匹配（Unicode 表示差异会导致遗漏）。
        # 结构: [{"track_index": 0, "track_id": "...", "values": ["..."]}, ...]
        track_values = params.get("track_values", [])

        logger.info(
            f"NormalizeProcessor::process: field={field}, candidates={len(candidates)}, "
            f"known_groups={len(known_groups)}, track_values={len(track_values)}, "
            f"task_id={task_id}"
        )

        if not candidates and not known_groups:
            # 无 candidates 也无 known_groups：track_updates 全部标记为未匹配
            track_updates = self._build_track_updates([], track_values)
            return {
                "id": request_id,
                "task_id": task_id,
                "success": True,
                "result": {"groups": [], "uncertain": [], "track_updates": track_updates},
            }

        # 边缘情况：candidates 为空但 known_groups 不为空（所有 alias 都被 SQLite 命中）
        # 直接返回 known_groups，无需调用 AI
        if not candidates and known_groups:
            logger.info(
                f"NormalizeProcessor::process: no candidates, returning "
                f"{len(known_groups)} known_groups directly"
            )
            track_updates = self._build_track_updates(known_groups, track_values)
            return {
                "id": request_id,
                "task_id": task_id,
                "success": True,
                "result": {"groups": known_groups, "uncertain": [], "track_updates": track_updates},
            }

        try:
            # 1. 清理输入 + 预归一化（合并仅空白/大小写差异的 alias）
            cleaned, pre_groups = self._clean_candidates(candidates)

            # 2. 构造 prompt
            # 3. 调用 AI
            adapter = self._get_adapter()
            if not adapter.provider:
                return {
                    "id": request_id,
                    "task_id": task_id,
                    "success": False,
                    "error": {"code": "NO_PROVIDER", "message": "No AI provider configured"},
                }

            web_search_enabled = getattr(adapter.provider, "supports_web_search", False)
            system_prompt, user_prompt = build_normalize_prompt(
                field, cleaned, web_search_enabled=web_search_enabled
            )

            messages = [
                {"role": "system", "content": system_prompt},
                {"role": "user", "content": user_prompt},
            ]

            logger.debug(f"NormalizeProcessor: calling AI, messages size={len(system_prompt) + len(user_prompt)}")

            # 优先使用带联网搜索的调用（OpenAI Responses API + web_search_preview）
            # 这样 AI 可联网验证作品归属，显著降低跨语言/小众艺人的误判
            if web_search_enabled:
                logger.info(
                    f"NormalizeProcessor::process: calling AI with web_search, "
                    f"field={field}, candidates={len(cleaned)}, "
                    f"system_prompt_len={len(system_prompt)}, user_prompt_len={len(user_prompt)}"
                )
                response = adapter.provider.chat_completion_json_with_web_search(
                    messages, temperature=0.0
                )
            else:
                logger.info(
                    f"NormalizeProcessor::process: calling AI (no web_search), "
                    f"field={field}, candidates={len(cleaned)}, "
                    f"system_prompt_len={len(system_prompt)}, user_prompt_len={len(user_prompt)}"
                )
                response = adapter.provider.chat_completion_json(messages, temperature=0.0)

            if not response.success:
                logger.error(
                    f"NormalizeProcessor::process: AI call FAILED, "
                    f"error={response.error}, model={getattr(response, 'model', 'unknown')}"
                )
                return {
                    "id": request_id,
                    "task_id": task_id,
                    "success": False,
                    "error": {"code": "AI_ERROR", "message": response.error or "Unknown AI error"},
                }

            logger.info(
                f"NormalizeProcessor::process: AI call OK, "
                f"model={response.model}, tokens={response.tokens_used}, "
                f"content_size={len(response.content) if response.content else 0}"
            )

            # 4. 解析响应
            result = self._parse_response(response.content, field, cleaned)

            # 4.5 把 C++ 送来的 known_groups（SQLite 命中，高置信度）加入 AI groups。
            #     _merge_pre_groups 会把 _clean_candidates 生成的变体 pre_groups 合并到
            #     known_groups（如果 pre_group 的 canonical 或 alias 与 known_group 重叠）。
            #     这样能正确处理 SQLite 存的是 trim 过的 alias，而 track 实际值带尾空格的情况：
            #     pre_group 的 canonical（trim 过的）会命中 known_group 的 alias，从而把
            #     原始变体（带尾空格）加入 known_group 的 aliases。
            if known_groups:
                result["groups"] = list(known_groups) + result["groups"]
                logger.info(
                    f"NormalizeProcessor::process: added {len(known_groups)} known_groups "
                    f"(SQLite hits), total groups before merge={len(result['groups'])}"
                )

            # 4.6 合并重叠的 groups（known_groups 与 AI groups 可能有同一实体但
            #     不同 canonical/alias 的 group）。用 normalize_key 检测重叠，
            #     把 alias 重叠的多个 group 合并为一个，避免同一实体出现多个 group。
            #     场景：SQLite 缓存 group canonical="미도와 파라솔" aliases=["미도와 파라솔"]，
            #     AI 返回 group canonical="Mido and Falasol" aliases=["Mido and Falasol",
            #     "美都与遮阳伞", "미도와 파라솔"]——两个 group 共享 alias "미도와 파라솔"，
            #     合并后 canonical 取 confidence 更高者（known_groups confidence=1.0 通常更高）。
            groups_before_overlap_merge = len(result["groups"])
            result["groups"] = self._merge_overlapping_groups(result["groups"])
            if len(result["groups"]) < groups_before_overlap_merge:
                logger.info(
                    f"NormalizeProcessor::process: merged overlapping groups, "
                    f"before={groups_before_overlap_merge}, after={len(result['groups'])}"
                )

            # 5. 合并预处理 groups（明显格式差异，无需 AI 判断）
            #    注意：必须与 AI groups 去重合并，避免同一实体出现多个 group
            if pre_groups:
                result["groups"] = self._merge_pre_groups(result["groups"], pre_groups)
                logger.info(
                    f"NormalizeProcessor::process: merged {len(pre_groups)} pre-normalized groups, "
                    f"final groups={len(result['groups'])}"
                )

            logger.info(
                f"NormalizeProcessor::process: done, groups={len(result['groups'])}, "
                f"uncertain={len(result['uncertain'])}"
            )
            # 详细日志：输出最终 groups 和 uncertain（用 repr 显示空白字符）
            for g in result["groups"]:
                aliases_repr = [repr(a) for a in g.get("aliases", [])]
                logger.info(
                    f"NormalizeProcessor::process: GROUP canonical={g.get('canonical_name')!r}, "
                    f"confidence={g.get('confidence')}, aliases={aliases_repr}"
                )
            for u in result["uncertain"]:
                logger.info(
                    f"NormalizeProcessor::process: UNCERTAIN alias={u.get('alias')!r}, "
                    f"reason={u.get('reason')!r}"
                )

            # 6. 根据最终 groups 为每个 track 构造目标 values（multi-value 支持）
            #    用 _normalize_key 匹配，避免 C++ 端因 Unicode 表示差异（NFC/NFD、
            #    尾部全角空格等）导致 alias_to_canonical.find(val) 漏匹配。
            #    C++ 端直接用 track_updates 写入，不再解析 groups 做匹配。
            result["track_updates"] = self._build_track_updates(result["groups"], track_values)
            logger.info(
                f"NormalizeProcessor::process: built {len(result['track_updates'])} "
                f"track_updates, matched="
                f"{sum(1 for u in result['track_updates'] if u.get('matched'))}"
            )

            return {
                "id": request_id,
                "task_id": task_id,
                "success": True,
                "result": result,
                "model": response.model,
                "tokens_used": response.tokens_used,
                "provider": response.provider,
            }

        except Exception as e:
            logger.error(f"NormalizeProcessor::process error: {e}", exc_info=True)
            return {
                "id": request_id,
                "task_id": task_id,
                "success": False,
                "error": {"code": "INTERNAL_ERROR", "message": str(e)},
            }

    def _build_track_updates(
        self,
        groups: List[Dict],
        track_values: List[Dict],
    ) -> List[Dict]:
        """根据最终 groups 为每个 track 构造目标 values（multi-value 支持）

        匹配规则：
        - 对每个 track 的每个 value，用 _normalize_key 生成 key
        - 在 groups 中查找：遍历每个 group，对 group 的 canonical_name 和所有 aliases
          用 _normalize_key 生成 key，建立 key -> canonical_name 的映射
        - 如果 track value 的 key 命中映射，则替换为对应的 canonical_name
        - 未命中的 value 保留原值
        - 同一 canonical 在一个 track 内去重（避免 multi-value 重复）
        - 如果 new_values 与 original_values 完全相同（无任何值改变），matched=False

        这样避免 C++ 端用精确字符串匹配（alias_to_canonical.find(val)）导致
        Unicode 表示差异（NFC/NFD 韩文、尾部全角空格等）引发的匹配遗漏。

        Args:
            groups: 最终的归一化分组列表
            track_values: C++ 送来的每个 track 的当前 field values 列表
                [{"track_index": 0, "track_id": "...", "values": ["..."]}, ...]

        Returns:
            track_updates 列表，每个元素：
                {
                    "track_index": int,
                    "track_id": str,
                    "matched": bool,           # 是否有任何 value 被替换
                    "original_values": [...],  # 原始 values
                    "new_values": [...],       # 目标 values（已替换 canonical，去重）
                    "canonical_name": str,     # 命中的 canonical（如有多个取第一个）
                }
        """
        # 构建 normalize_key -> canonical_name 映射
        # canonical_name 本身也加入映射（key = normalize_key(canonical)）
        key_to_canonical: Dict[str, str] = {}
        for g in groups:
            canonical = g.get("canonical_name", "")
            if not isinstance(canonical, str) or not canonical:
                continue
            ckey = self._normalize_key(canonical)
            if ckey and ckey not in key_to_canonical:
                key_to_canonical[ckey] = canonical
            for alias in g.get("aliases", []):
                if not isinstance(alias, str) or not alias:
                    continue
                akey = self._normalize_key(alias)
                if akey and akey not in key_to_canonical:
                    key_to_canonical[akey] = canonical

        updates: List[Dict] = []
        for tv in track_values:
            track_index = tv.get("track_index", -1)
            track_id = tv.get("track_id", "")
            raw_values = tv.get("values", [])
            if not isinstance(raw_values, list):
                raw_values = [raw_values] if raw_values else []

            original_values = [str(v) for v in raw_values if v is not None and str(v)]
            new_values: List[str] = []
            seen_canonicals: set = set()
            matched_canonical = ""
            any_matched = False

            for val in original_values:
                vkey = self._normalize_key(val)
                hit_canonical = key_to_canonical.get(vkey)
                if hit_canonical:
                    # 命中：用 canonical 替换（同一 canonical 去重）
                    if hit_canonical not in seen_canonicals:
                        seen_canonicals.add(hit_canonical)
                        new_values.append(hit_canonical)
                    if not matched_canonical:
                        matched_canonical = hit_canonical
                    any_matched = True
                    logger.debug(
                        f"NormalizeProcessor::_build_track_updates: track_index={track_index}, "
                        f"matched value={val!r} -> canonical={hit_canonical!r}"
                    )
                else:
                    # 未命中：保留原值
                    new_values.append(val)

            # matched=False 的情况：没有任何 value 命中，或者 new_values 与 original 完全相同
            changed = new_values != original_values
            updates.append({
                "track_index": track_index,
                "track_id": track_id,
                "matched": any_matched and changed,
                "original_values": original_values,
                "new_values": new_values,
                "canonical_name": matched_canonical if any_matched else "",
            })

        return updates

    def _clean_candidates(self, candidates: List[Dict]) -> tuple:
        """清理候选列表 + 预归一化合并

        预归一化规则：仅 trim 首尾空白并转小写得到 normalize_key。
        同一 normalize_key 的多个原始写法直接构成一个 pre_group（confidence=1.0），
        不再送 AI 判断。

        注意：
        - 中间的空白必须保留，否则 "A B C" 和 "ABC" 会被误判为同一实体
          （对英文/韩文歌手名尤其重要）。trim 后仍不同则交给 AI 判定。
        - 必须保留原始 alias（含首尾空白变体），否则 C++ 侧 track 的原始值
          无法匹配到 group 的 aliases，导致被误归为 uncertain。
          例：C++ 发来 "미도와 파라솔    "（尾部全角/不间断空格），
          Python 必须把原始写法加回 pre_group.aliases，C++ apply 时才能命中。
        - 处理 Unicode 空白（全角空格 U+3000、不间断空格 U+00A0 等）和
          零宽字符（U+200B/U+200C/U+200D/U+FEFF/U+2060），避免非 ASCII
          空白导致 normalize_key 不同。

        Returns:
            (cleaned, pre_groups):
              cleaned: 每个 normalize_key 的代表 alias（送 AI 判断，已 trim）
              pre_groups: 预合并的 groups（直接加入最终结果，保留所有原始变体）
        """
        seen_keys = {}  # norm_key -> 代表 alias（已 trim，送 AI）
        pre_groups = []  # norm_key -> pre_group
        cleaned = []

        def _is_invalid_value(s: str) -> bool:
            """检查是否是无效占位值（n/a, unknown 等）"""
            cleaned_lower = s.strip().lower()
            return cleaned_lower in INVALID_VALUES

        def _pick_canonical(aliases: List[str]) -> str:
            """从多个写法中选择 canonical：优先首尾无空白、其次最短"""
            return min(aliases, key=lambda a: (
                len(a) - len(a.strip()),  # 首尾空白字符数
                len(a),
            ))

        def _clean_examples(raw_examples) -> List[Dict]:
            result = []
            for ex in raw_examples:
                cleaned_ex = {}
                for k, v in ex.items():
                    cv = clean_value(str(v)) if v else ""
                    if cv:
                        cleaned_ex[k] = cv
                if cleaned_ex:
                    result.append(cleaned_ex)
            return result

        for cand in candidates:
            # 保留原始 alias（含首尾空白变体），用于回填到 pre_group.aliases
            original_alias = str(cand.get("alias", ""))
            if not original_alias or _is_invalid_value(original_alias):
                continue

            nkey = self._normalize_key(original_alias)
            if not nkey:
                continue

            # 代表 alias：trim 首尾空白（保留中间空白），用于送 AI 判断
            representative = original_alias.strip()

            if nkey in seen_keys:
                first_representative = seen_keys[nkey]
                # 找到或创建 pre_group
                pg = next((g for g in pre_groups if g["_key"] == nkey), None)
                if pg is None:
                    # 首次出现时未创建 pre_group（因为 original == representative），
                    # 此处补创建，把首次代表也加入 aliases
                    pg = {
                        "_key": nkey,
                        "canonical_name": first_representative,
                        "aliases": [first_representative],
                        "confidence": 1.0,
                        "reason": "预处理归并：仅空白/大小写差异",
                    }
                    pre_groups.append(pg)
                # 加入原始 alias（去重）：这是关键，保留原始变体以便 C++ 匹配 track
                if original_alias not in pg["aliases"]:
                    pg["aliases"].append(original_alias)
                    # 重新选择 canonical（空白最少的写法）
                    pg["canonical_name"] = _pick_canonical(pg["aliases"])
                # 合并 examples 到 cleaned 中代表 alias 的项
                ex_list = _clean_examples(cand.get("examples", []))
                if ex_list:
                    for c in cleaned:
                        if c["alias"] == first_representative:
                            c["examples"].extend(ex_list)
                            break
            else:
                seen_keys[nkey] = representative
                cleaned.append({
                    "alias": representative,
                    "examples": _clean_examples(cand.get("examples", [])),
                })
                # 如果原始 alias 与代表不同（有首尾空白/零宽字符差异），
                # 立即创建 pre_group，把原始写法也加入 aliases
                if original_alias != representative:
                    pre_groups.append({
                        "_key": nkey,
                        "canonical_name": representative,
                        "aliases": [representative, original_alias],
                        "confidence": 1.0,
                        "reason": "预处理归并：仅空白/大小写差异",
                    })

        # 清理临时字段
        for g in pre_groups:
            g.pop("_key", None)

        if pre_groups:
            logger.info(
                f"NormalizeProcessor::_clean_candidates: pre-merged {len(pre_groups)} groups "
                f"({sum(len(g['aliases']) for g in pre_groups)} aliases), "
                f"{len(cleaned)} unique candidates sent to AI"
            )
            # 详细日志：便于排查 "미도와 파라솔    " 类问题
            for g in pre_groups:
                aliases_repr = [repr(a) for a in g["aliases"]]
                logger.info(
                    f"NormalizeProcessor::_clean_candidates: pre_group "
                    f"canonical={g['canonical_name']!r}, aliases={aliases_repr}"
                )

        return cleaned, pre_groups

    def _parse_response(
        self,
        content: str,
        field: str,
        candidates: List[Dict],
    ) -> Dict:
        """解析 AI 返回的 JSON，做基本合法性校验

        - canonical_name 必须是 aliases 之一
        - aliases 必须都在输入候选列表中
        - uncertain 中的 alias 必须在输入候选列表中
        - 不做字符串相似度后置过滤：AI 是 Evidence Evaluator，应基于作品证据判断，
          强制字符串相似度会把 AI 退化为字符串比对工具
        """
        try:
            data = json.loads(content) if isinstance(content, str) else content
        except json.JSONDecodeError as e:
            logger.error(
                f"NormalizeProcessor::_parse_response: JSON decode failed: {e}, "
                f"content_preview={content[:300] if content else 'empty'}"
            )
            # 解析失败：全部归为 uncertain
            return {
                "groups": [],
                "uncertain": [{"alias": c["alias"], "reason": "AI 响应解析失败"} for c in candidates],
            }

        logger.info(
            f"NormalizeProcessor::_parse_response: parsed JSON, "
            f"top_keys={list(data.keys()) if isinstance(data, dict) else type(data).__name__}, "
            f"groups_in_response={len(data.get('groups', [])) if isinstance(data, dict) else 0}, "
            f"uncertain_in_response={len(data.get('uncertain', [])) if isinstance(data, dict) else 0}"
        )

        input_aliases = {c["alias"] for c in candidates}
        # 建立 normalize_key -> 原始 alias 的映射，用于将 AI 返回的别名（可能
        # 是 NFD 等 Unicode 变体）映射回输入时的原始写法，确保后续匹配一致
        input_alias_map: Dict[str, str] = {}
        for c in candidates:
            k = self._normalize_key(c["alias"])
            if k and k not in input_alias_map:
                input_alias_map[k] = c["alias"]

        groups = []
        for g in data.get("groups", []):
            canonical = g.get("canonical_name", "")
            aliases = g.get("aliases", [])
            # 过滤：aliases 必须是字符串、非空、且在输入候选中
            # （AI 不能凭空添加 alias，只能合并已有的候选）
            # 用 _normalize_key 匹配，避免 NFC/NFD 差异导致误过滤
            filtered_aliases = []
            for a in aliases:
                if not isinstance(a, str) or not a:
                    continue
                akey = self._normalize_key(a)
                if akey in input_alias_map:
                    # 用输入时的原始写法，确保后续匹配一致
                    orig = input_alias_map[akey]
                    if orig not in filtered_aliases:
                        filtered_aliases.append(orig)
            aliases = filtered_aliases
            # canonical 非空校验：空时退化为第一个 alias
            if not isinstance(canonical, str) or not canonical:
                if aliases:
                    canonical = aliases[0]
                else:
                    continue
            if not aliases:
                continue
            # canonical 规范化为 NFC，避免 NFD 形式导致显示/存储不一致
            # （AI 返回的 JSON 可能是 NFD，韩文在 NFD 下字节序列不同）
            canonical = unicodedata.normalize('NFC', canonical)
            # 关键：不再强制 canonical 必须在 aliases 中。
            #   AI 的职责是给出"标准名"（canonical），该标准名可能是一个全新的、
            #   不在输入 candidates 中的统一规范名（例如输入 ["미도와 파라솔 ", "美都与遮阳伞"],
            #   AI 给出 canonical="Mido and Falasol"）。
            #   若强制 canonical = aliases[0]，会丢失 AI 的标准化建议，导致 normalize
            #   后 track 只是被改成"另一个原始写法"（例如 "미도와 파라솔"），而非真正的
            #   标准名（"Mido and Falasol"）。这是用户反馈"normalize 确认后 alias 没有按
            #   canonical 写入"的根本原因。
            #   aliases 过滤仍保留：AI 只能合并已有候选作为 alias，不能添加新 alias。
            groups.append({
                "canonical_name": canonical,
                "confidence": float(g.get("confidence", 0.0)),
                "aliases": aliases,
                "reason": g.get("reason", "") or "",
            })

        uncertain = []
        grouped_keys = set()  # 用 normalize_key 跟踪已分组的 alias
        for g in groups:
            for a in g["aliases"]:
                grouped_keys.add(self._normalize_key(a))
        for u in data.get("uncertain", []):
            alias = u.get("alias", "")
            if not isinstance(alias, str) or not alias:
                continue
            akey = self._normalize_key(alias)
            if akey and akey in input_alias_map and akey not in grouped_keys:
                uncertain.append({
                    "alias": input_alias_map[akey],  # 用原始写法
                    "reason": u.get("reason", ""),
                })
                grouped_keys.add(akey)  # 标记已处理，避免重复加入 uncertain

        # 未出现在 groups/uncertain 中的输入 alias，默认归为 uncertain
        for c in candidates:
            ckey = self._normalize_key(c["alias"])
            if ckey and ckey not in grouped_keys:
                uncertain.append({
                    "alias": c["alias"],
                    "reason": "AI 未给出判定",
                })
                grouped_keys.add(ckey)  # 避免重复

        return {"groups": groups, "uncertain": uncertain}

    def _merge_overlapping_groups(self, groups: List[Dict]) -> List[Dict]:
        """合并 alias/canonical 重叠的 groups（用 normalize_key 检测重叠）

        场景：known_groups（SQLite 缓存）与 AI 返回的 groups 可能描述同一实体
        但用不同 canonical。例如：
          - known_group: canonical="미도와 파라솔", aliases=["미도와 파라솔"]
          - AI_group:    canonical="Mido and Falasol", aliases=["Mido and Falasol",
                         "美都与遮阳伞", "미도와 파라솔"]
        两个 group 共享 alias "미도와 파라솔"（normalize_key 相同），应合并为一个。

        合并策略：
        - 用并查集（Union-Find）把共享 normalize_key 的 group 合并
        - canonical 取 confidence 最高的 group 的 canonical（同分取第一个）
        - aliases 取所有合并 group 的 aliases 去重（用 normalize_key 去重，保留首次出现的原始写法）
        - reason 拼接所有 group 的 reason
        """
        if not groups:
            return []

        n = len(groups)
        # 并查集
        parent = list(range(n))

        def find(x: int) -> int:
            while parent[x] != x:
                parent[x] = parent[parent[x]]
                x = parent[x]
            return x

        def union(x: int, y: int):
            rx, ry = find(x), find(y)
            if rx != ry:
                parent[rx] = ry

        # 建立 normalize_key -> group_index 的索引
        # 一个 key 只记第一个出现的 group，后续相同 key 的 group 通过 union 合并
        key_to_first_idx: Dict[str, int] = {}
        for idx, g in enumerate(groups):
            # canonical 本身也作为检测重叠的 key
            all_keys = []
            ckey = self._normalize_key(g.get("canonical_name", ""))
            if ckey:
                all_keys.append(ckey)
            for a in g.get("aliases", []):
                akey = self._normalize_key(a)
                if akey:
                    all_keys.append(akey)

            for k in all_keys:
                if k in key_to_first_idx:
                    # 当前 group 与 key_to_first_idx[k] 共享 key，合并
                    union(idx, key_to_first_idx[k])
                else:
                    key_to_first_idx[k] = idx

        # 按 root 分组
        clusters: Dict[int, List[int]] = {}
        for i in range(n):
            r = find(i)
            clusters.setdefault(r, []).append(i)

        # 合并每个 cluster
        merged: List[Dict] = []
        for root, indices in clusters.items():
            if len(indices) == 1:
                # 单个 group，直接保留（但仍需去重 aliases）
                g = dict(groups[indices[0]])
                g["aliases"] = list(g.get("aliases", []))
                merged.append(g)
                continue

            # 多个 group 合并
            cluster_groups = [groups[i] for i in indices]
            # canonical: 取 confidence 最高的（同分取第一个），并规范化为 NFC
            best = max(cluster_groups, key=lambda g: g.get("confidence", 0.0))
            canonical = unicodedata.normalize('NFC', best.get("canonical_name", ""))

            # aliases 合并去重（用 normalize_key 去重，保留首次出现的原始写法）
            seen_keys = set()
            all_aliases = []
            # canonical 也算一个 alias，先加入
            ckey = self._normalize_key(canonical)
            if ckey:
                seen_keys.add(ckey)
            for g in cluster_groups:
                for a in g.get("aliases", []):
                    akey = self._normalize_key(a)
                    if a and akey and akey not in seen_keys:
                        seen_keys.add(akey)
                        all_aliases.append(a)

            # reason 拼接
            reasons = [g.get("reason", "") for g in cluster_groups if g.get("reason")]
            reason = " | ".join(reasons) if reasons else ""

            # confidence 取最大
            confidence = max(g.get("confidence", 0.0) for g in cluster_groups)

            logger.debug(
                f"NormalizeProcessor::_merge_overlapping_groups: merged {len(indices)} groups "
                f"into canonical='{canonical}', aliases_count={len(all_aliases)}"
            )

            merged.append({
                "canonical_name": canonical,
                "aliases": all_aliases,
                "confidence": confidence,
                "reason": reason,
            })

        return merged

    def _merge_pre_groups(
        self,
        ai_groups: List[Dict],
        pre_groups: List[Dict],
    ) -> List[Dict]:
        """将预处理 groups 合并到 AI groups 中，避免同一实体出现多个 group

        合并规则：
        1. 对每个 pre_group，检查它的 canonical_name 和所有 aliases 是否已经出现在
           某个 AI group 中（作为 canonical_name 或 alias）。
        2. 若命中，把 pre_group 的所有 aliases 合并进该 AI group（去重），不重复创建 group。
        3. 若没命中，把 pre_group 作为独立 group 加入结果。
        4. 最后对每个 group 的 aliases 去重，保持首次出现顺序。

        注意：所有匹配均通过 _normalize_key 进行，避免 NFC/NFD 韩文、尾部空白
        等 Unicode 表示差异导致合并失败（同一实体出现两个 group）。
        """
        # 建立 normalize_key -> group_index 的索引（基于 AI groups）
        # 注意：一个 key 可能出现在多个 AI group 中（理论上不应该），取第一个
        alias_to_group_idx: Dict[str, int] = {}
        canonical_to_group_idx: Dict[str, int] = {}
        for idx, g in enumerate(ai_groups):
            ckey = self._normalize_key(g["canonical_name"])
            if ckey:
                canonical_to_group_idx[ckey] = idx
            for a in g.get("aliases", []):
                akey = self._normalize_key(a)
                if akey and akey not in alias_to_group_idx:
                    alias_to_group_idx[akey] = idx

        merged_groups = [dict(g) for g in ai_groups]  # 浅拷贝，准备追加
        # 确保 aliases 是 list（避免引用共享）
        for g in merged_groups:
            g["aliases"] = list(g.get("aliases", []))

        for pg in pre_groups:
            pg_canonical = pg.get("canonical_name", "")
            pg_aliases = pg.get("aliases", [])

            # 找目标 group：先匹配 canonical，再匹配任意 alias（均用 normalize_key）
            target_idx = canonical_to_group_idx.get(self._normalize_key(pg_canonical), None)
            if target_idx is None:
                for a in pg_aliases:
                    akey = self._normalize_key(a)
                    if akey and akey in alias_to_group_idx:
                        target_idx = alias_to_group_idx[akey]
                        break

            if target_idx is not None:
                # 合并到现有 AI group
                target = merged_groups[target_idx]
                # 用 normalize_key 去重，避免 NFC/NFD 近重复
                existing_keys = set(self._normalize_key(a) for a in target["aliases"])
                existing_keys.add(self._normalize_key(target["canonical_name"]))
                for a in pg_aliases:
                    akey = self._normalize_key(a)
                    if a and akey and akey not in existing_keys:
                        target["aliases"].append(a)
                        existing_keys.add(akey)
                        # 更新索引
                        alias_to_group_idx[akey] = target_idx
                # 追加 reason（保留预处理来源信息）
                if pg.get("reason"):
                    suffix = f" [pre-merged: {pg['reason']}]"
                    if suffix not in target.get("reason", ""):
                        target["reason"] = (target.get("reason", "") or "") + suffix
                logger.debug(
                    f"NormalizeProcessor::_merge_pre_groups: pre_group "
                    f"canonical='{pg_canonical}' merged into AI group "
                    f"canonical='{target['canonical_name']}'"
                )
            else:
                # 没命中：作为独立 group 加入
                new_g = {
                    "canonical_name": pg_canonical,
                    "aliases": list(pg_aliases),
                    "confidence": pg.get("confidence", 1.0),
                    "reason": pg.get("reason", ""),
                }
                merged_groups.append(new_g)
                ckey = self._normalize_key(pg_canonical)
                if ckey:
                    canonical_to_group_idx[ckey] = len(merged_groups) - 1
                for a in pg_aliases:
                    akey = self._normalize_key(a)
                    if akey and akey not in alias_to_group_idx:
                        alias_to_group_idx[akey] = len(merged_groups) - 1

        # 最终去重（防御性），用 normalize_key 检测近重复
        for g in merged_groups:
            seen_keys = set()
            deduped = []
            # canonical 也要进 seen，避免 aliases 中出现 canonical 的重复写法
            ckey = self._normalize_key(g["canonical_name"])
            seen_keys.add(ckey)
            deduped.append(g["canonical_name"])  # canonical 也算 alias 之一
            for a in g["aliases"]:
                akey = self._normalize_key(a)
                if a and akey and akey not in seen_keys:
                    seen_keys.add(akey)
                    deduped.append(a)
            # 第一个元素是 canonical，剩余作为 aliases
            g["aliases"] = deduped[1:] if len(deduped) > 1 else []
            # 确保 canonical 至少在 aliases 中出现一次
            if self._normalize_key(g["canonical_name"]) not in {
                self._normalize_key(a) for a in g["aliases"]
            }:
                g["aliases"].insert(0, g["canonical_name"])

        return merged_groups
