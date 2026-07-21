#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
ProviderStore — Python 端 SQLite 访问层（管理 providers / app_settings）

V1 定稿：
- providers 表：多 Provider 实例
- app_settings：current_provider_id / fallback_provider_ids / providers_migrated
- seed：仅表为空时写入
- 迁移：从 settings.json 一次性导入
"""

from __future__ import annotations

import json
import logging
import sqlite3
import threading
import uuid
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Dict, List, Optional

logger = logging.getLogger(__name__)

PROTOCOL_OPENAI_CHAT = "openai_chat"
PROTOCOL_ANTHROPIC_MESSAGES = "anthropic_messages"
SUPPORTED_PROTOCOLS = frozenset({PROTOCOL_OPENAI_CHAT, PROTOCOL_ANTHROPIC_MESSAGES})

SETTING_CURRENT_PROVIDER_ID = "current_provider_id"
SETTING_FALLBACK_PROVIDER_IDS = "fallback_provider_ids"
SETTING_PROVIDERS_MIGRATED = "providers_migrated"

# seed 仅在 providers 表为空时写入；api_key 一律为空
SEED_PRESETS: List[Dict[str, str]] = [
    {
        "name": "Zhipu",
        "protocol": PROTOCOL_OPENAI_CHAT,
        "base_url": "https://open.bigmodel.cn/api/paas/v4",
        "model": "glm-5",
    },
    {
        "name": "DeepSeek",
        "protocol": PROTOCOL_OPENAI_CHAT,
        "base_url": "https://api.deepseek.com/v1",
        "model": "deepseek-chat",
    },
    {
        "name": "OpenRouter",
        "protocol": PROTOCOL_OPENAI_CHAT,
        "base_url": "https://openrouter.ai/api/v1",
        "model": "openrouter/free",
    },
]

# 旧厂商名 → 默认 base_url（settings 缺失时回退）
_LEGACY_DEFAULT_BASE_URLS = {
    "zhipu": "https://open.bigmodel.cn/api/paas/v4",
    "deepseek": "https://api.deepseek.com/v1",
    "openrouter": "https://openrouter.ai/api/v1",
    "openai": "https://api.openai.com/v1",
}

_LEGACY_SKIP = frozenset({"gemini", "ollama"})


def _now_iso() -> str:
    return datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def _new_id() -> str:
    return str(uuid.uuid4())


def normalize_base_url(base_url: str) -> str:
    """去掉首尾空白与引号。"""
    return (base_url or "").strip().strip(" `'\"")


def strip_protocol_path(base_url: str, protocol: str) -> str:
    """若用户填了完整 endpoint path，归一化为 API root。"""
    url = normalize_base_url(base_url).rstrip("/")
    if not url:
        return ""
    if protocol == PROTOCOL_OPENAI_CHAT:
        for suffix in ("/chat/completions", "/completions"):
            if url.endswith(suffix):
                return url[: -len(suffix)].rstrip("/")
    elif protocol == PROTOCOL_ANTHROPIC_MESSAGES:
        if url.endswith("/v1/messages"):
            return url[: -len("/v1/messages")].rstrip("/")
        if url.endswith("/messages"):
            return url[: -len("/messages")].rstrip("/")
    return url


class ProviderStore:
    """providers / app_settings 的 SQLite 访问层。

    线程安全：内部 lock 保护。
    """

    def __init__(self, db_path: str):
        self._db_path = db_path
        self._lock = threading.Lock()
        self._conn: Optional[sqlite3.Connection] = None
        self._connect()
        self._init_schema()

    # ------------------------------------------------------------------
    # Connection / schema
    # ------------------------------------------------------------------

    def _connect(self) -> None:
        self._conn = sqlite3.connect(self._db_path, check_same_thread=False)
        self._conn.row_factory = sqlite3.Row
        self._conn.execute("PRAGMA journal_mode=WAL")
        self._conn.execute("PRAGMA synchronous=NORMAL")
        self._conn.execute("PRAGMA busy_timeout=5000")

    def _init_schema(self) -> None:
        assert self._conn is not None
        with self._lock:
            self._conn.executescript(
                """
                CREATE TABLE IF NOT EXISTS providers (
                    id            TEXT PRIMARY KEY,
                    name          TEXT NOT NULL,
                    protocol      TEXT NOT NULL,
                    base_url      TEXT NOT NULL DEFAULT '',
                    api_key       TEXT NOT NULL DEFAULT '',
                    model         TEXT NOT NULL DEFAULT '',
                    enabled       INTEGER NOT NULL DEFAULT 1,
                    sort_order    INTEGER NOT NULL DEFAULT 0,
                    is_preset     INTEGER NOT NULL DEFAULT 0,
                    created_at    TEXT NOT NULL,
                    updated_at    TEXT NOT NULL
                );

                CREATE INDEX IF NOT EXISTS idx_providers_sort
                    ON providers(sort_order, name);

                CREATE TABLE IF NOT EXISTS app_settings (
                    key   TEXT PRIMARY KEY,
                    value TEXT NOT NULL
                );
                """
            )
            self._conn.commit()
            # 预留 fallback 默认值（仅写入一次）
            if self._get_setting_unlocked(SETTING_FALLBACK_PROVIDER_IDS) is None:
                self._set_setting_unlocked(SETTING_FALLBACK_PROVIDER_IDS, "[]")

    def close(self) -> None:
        with self._lock:
            if self._conn is not None:
                self._conn.close()
                self._conn = None

    # ------------------------------------------------------------------
    # Row helpers
    # ------------------------------------------------------------------

    @staticmethod
    def _row_to_dict(row: sqlite3.Row) -> Dict[str, Any]:
        return {
            "id": row["id"],
            "name": row["name"],
            "protocol": row["protocol"],
            "base_url": row["base_url"],
            "api_key": row["api_key"],
            "model": row["model"],
            "enabled": bool(row["enabled"]),
            "sort_order": int(row["sort_order"]),
            "is_preset": bool(row["is_preset"]),
            "created_at": row["created_at"],
            "updated_at": row["updated_at"],
        }

    def _validate_protocol(self, protocol: str) -> str:
        p = (protocol or "").strip().lower()
        if p not in SUPPORTED_PROTOCOLS:
            raise ValueError(
                f"Unsupported protocol: {protocol!r}. "
                f"V1 supports: {', '.join(sorted(SUPPORTED_PROTOCOLS))}"
            )
        return p

    # ------------------------------------------------------------------
    # app_settings
    # ------------------------------------------------------------------

    def _get_setting_unlocked(self, key: str) -> Optional[str]:
        assert self._conn is not None
        cur = self._conn.execute(
            "SELECT value FROM app_settings WHERE key = ?", (key,)
        )
        row = cur.fetchone()
        return None if row is None else str(row["value"])

    def _set_setting_unlocked(self, key: str, value: str) -> None:
        assert self._conn is not None
        self._conn.execute(
            "INSERT INTO app_settings(key, value) VALUES(?, ?) "
            "ON CONFLICT(key) DO UPDATE SET value = excluded.value",
            (key, value),
        )
        self._conn.commit()

    def get_setting(self, key: str, default: Optional[str] = None) -> Optional[str]:
        with self._lock:
            val = self._get_setting_unlocked(key)
            return default if val is None else val

    def set_setting(self, key: str, value: str) -> None:
        with self._lock:
            self._set_setting_unlocked(key, value)

    def get_current_provider_id(self) -> Optional[str]:
        val = self.get_setting(SETTING_CURRENT_PROVIDER_ID)
        return val or None

    def set_current_provider_id(self, provider_id: Optional[str]) -> None:
        with self._lock:
            if not provider_id:
                self._conn.execute(
                    "DELETE FROM app_settings WHERE key = ?",
                    (SETTING_CURRENT_PROVIDER_ID,),
                )
                self._conn.commit()
                return
            # 校验存在
            cur = self._conn.execute(
                "SELECT 1 FROM providers WHERE id = ?", (provider_id,)
            )
            if cur.fetchone() is None:
                raise ValueError(f"Provider not found: {provider_id}")
            self._set_setting_unlocked(SETTING_CURRENT_PROVIDER_ID, provider_id)

    # ------------------------------------------------------------------
    # CRUD
    # ------------------------------------------------------------------

    def list_providers(self, include_disabled: bool = True) -> List[Dict[str, Any]]:
        with self._lock:
            assert self._conn is not None
            if include_disabled:
                cur = self._conn.execute(
                    "SELECT * FROM providers ORDER BY sort_order ASC, name ASC"
                )
            else:
                cur = self._conn.execute(
                    "SELECT * FROM providers WHERE enabled = 1 "
                    "ORDER BY sort_order ASC, name ASC"
                )
            return [self._row_to_dict(r) for r in cur.fetchall()]

    def get_provider(self, provider_id: str) -> Optional[Dict[str, Any]]:
        with self._lock:
            assert self._conn is not None
            cur = self._conn.execute(
                "SELECT * FROM providers WHERE id = ?", (provider_id,)
            )
            row = cur.fetchone()
            return None if row is None else self._row_to_dict(row)

    def get_current_provider(self) -> Optional[Dict[str, Any]]:
        pid = self.get_current_provider_id()
        if not pid:
            return None
        return self.get_provider(pid)

    def count_providers(self) -> int:
        with self._lock:
            assert self._conn is not None
            cur = self._conn.execute("SELECT COUNT(*) AS c FROM providers")
            return int(cur.fetchone()["c"])

    def create_provider(
        self,
        *,
        name: str,
        protocol: str,
        base_url: str = "",
        api_key: str = "",
        model: str = "",
        enabled: bool = True,
        sort_order: Optional[int] = None,
        is_preset: bool = False,
        provider_id: Optional[str] = None,
    ) -> Dict[str, Any]:
        name = (name or "").strip()
        if not name:
            raise ValueError("Provider name is required")

        protocol = self._validate_protocol(protocol)
        base_url = strip_protocol_path(base_url, protocol)
        pid = provider_id or _new_id()
        now = _now_iso()

        with self._lock:
            assert self._conn is not None
            if sort_order is None:
                cur = self._conn.execute(
                    "SELECT COALESCE(MAX(sort_order), -1) AS m FROM providers"
                )
                sort_order = int(cur.fetchone()["m"]) + 1

            self._conn.execute(
                """
                INSERT INTO providers(
                    id, name, protocol, base_url, api_key, model,
                    enabled, sort_order, is_preset, created_at, updated_at
                ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
                """,
                (
                    pid,
                    name,
                    protocol,
                    base_url,
                    api_key or "",
                    model or "",
                    1 if enabled else 0,
                    int(sort_order),
                    1 if is_preset else 0,
                    now,
                    now,
                ),
            )
            self._conn.commit()

        row = self.get_provider(pid)
        assert row is not None
        return row

    def update_provider(
        self, provider_id: str, **fields: Any
    ) -> Dict[str, Any]:
        allowed = {
            "name",
            "protocol",
            "base_url",
            "api_key",
            "model",
            "enabled",
            "sort_order",
            "is_preset",
        }
        updates = {k: v for k, v in fields.items() if k in allowed and v is not None}
        if not updates:
            row = self.get_provider(provider_id)
            if row is None:
                raise ValueError(f"Provider not found: {provider_id}")
            return row

        if "name" in updates:
            updates["name"] = str(updates["name"]).strip()
            if not updates["name"]:
                raise ValueError("Provider name is required")

        if "protocol" in updates:
            updates["protocol"] = self._validate_protocol(str(updates["protocol"]))

        protocol_for_url = updates.get("protocol")
        if "base_url" in updates:
            # 需要当前 protocol 才能归一化
            if protocol_for_url is None:
                existing = self.get_provider(provider_id)
                if existing is None:
                    raise ValueError(f"Provider not found: {provider_id}")
                protocol_for_url = existing["protocol"]
            updates["base_url"] = strip_protocol_path(
                str(updates["base_url"]), str(protocol_for_url)
            )

        if "enabled" in updates:
            updates["enabled"] = 1 if updates["enabled"] else 0
        if "is_preset" in updates:
            updates["is_preset"] = 1 if updates["is_preset"] else 0
        if "sort_order" in updates:
            updates["sort_order"] = int(updates["sort_order"])

        updates["updated_at"] = _now_iso()
        cols = ", ".join(f"{k} = ?" for k in updates.keys())
        values = list(updates.values()) + [provider_id]

        with self._lock:
            assert self._conn is not None
            cur = self._conn.execute(
                f"UPDATE providers SET {cols} WHERE id = ?", values
            )
            self._conn.commit()
            if cur.rowcount == 0:
                raise ValueError(f"Provider not found: {provider_id}")

        row = self.get_provider(provider_id)
        assert row is not None
        return row

    def delete_provider(self, provider_id: str) -> bool:
        """删除 Provider；若是当前选中，自动切换到列表中下一个。"""
        with self._lock:
            assert self._conn is not None
            cur = self._conn.execute(
                "DELETE FROM providers WHERE id = ?", (provider_id,)
            )
            deleted = cur.rowcount > 0
            if not deleted:
                self._conn.commit()
                return False

            current = self._get_setting_unlocked(SETTING_CURRENT_PROVIDER_ID)
            if current == provider_id:
                nxt = self._conn.execute(
                    "SELECT id FROM providers ORDER BY sort_order ASC, name ASC LIMIT 1"
                ).fetchone()
                if nxt is None:
                    self._conn.execute(
                        "DELETE FROM app_settings WHERE key = ?",
                        (SETTING_CURRENT_PROVIDER_ID,),
                    )
                else:
                    self._set_setting_unlocked(
                        SETTING_CURRENT_PROVIDER_ID, str(nxt["id"])
                    )
            self._conn.commit()
            return True

    # ------------------------------------------------------------------
    # Seed
    # ------------------------------------------------------------------

    def seed_presets_if_empty(self) -> int:
        """仅当 providers 表为空时写入 seed。返回写入条数。"""
        if self.count_providers() > 0:
            return 0

        created = 0
        first_id: Optional[str] = None
        for idx, preset in enumerate(SEED_PRESETS):
            row = self.create_provider(
                name=preset["name"],
                protocol=preset["protocol"],
                base_url=preset["base_url"],
                api_key="",
                model=preset.get("model", ""),
                enabled=True,
                sort_order=idx,
                is_preset=True,
            )
            if first_id is None:
                first_id = row["id"]
            created += 1

        if first_id and not self.get_current_provider_id():
            self.set_current_provider_id(first_id)

        logger.info("ProviderStore.seed_presets_if_empty: seeded %s presets", created)
        return created

    def restore_presets(self, overwrite_existing_names: bool = False) -> int:
        """用户显式恢复预设。默认不覆盖同名；返回新建条数。"""
        existing_names = {p["name"] for p in self.list_providers()}
        created = 0
        base_order = self.count_providers()
        for idx, preset in enumerate(SEED_PRESETS):
            name = preset["name"]
            if name in existing_names:
                if not overwrite_existing_names:
                    continue
                # 更新同名预设的默认 URL/protocol/model（不覆盖 api_key）
                for p in self.list_providers():
                    if p["name"] == name:
                        self.update_provider(
                            p["id"],
                            protocol=preset["protocol"],
                            base_url=preset["base_url"],
                            model=preset.get("model", p["model"]),
                            is_preset=True,
                        )
                        break
                continue

            self.create_provider(
                name=name,
                protocol=preset["protocol"],
                base_url=preset["base_url"],
                api_key="",
                model=preset.get("model", ""),
                enabled=True,
                sort_order=base_order + idx,
                is_preset=True,
            )
            created += 1
        return created

    # ------------------------------------------------------------------
    # Migration from settings.json
    # ------------------------------------------------------------------

    def is_migrated(self) -> bool:
        return self.get_setting(SETTING_PROVIDERS_MIGRATED) == "1"

    def mark_migrated(self) -> None:
        self.set_setting(SETTING_PROVIDERS_MIGRATED, "1")

    def migrate_from_settings(
        self,
        settings: Optional[Dict[str, Any]] = None,
        settings_path: Optional[Path] = None,
        yaml_providers: Optional[Dict[str, Any]] = None,
    ) -> int:
        """从旧 settings.json 一次性导入。

        Returns:
            导入条数（0 表示跳过或无可导入项）
        """
        if self.is_migrated():
            logger.debug("ProviderStore.migrate_from_settings: already migrated")
            return 0

        if settings is None and settings_path is not None:
            settings = self._load_json(settings_path)
        settings = settings or {}

        provider_configs = settings.get("provider_configs") or {}
        if not isinstance(provider_configs, dict):
            provider_configs = {}

        old_current = str(settings.get("provider") or "").strip().lower()
        imported: List[Dict[str, Any]] = []
        name_to_id: Dict[str, str] = {}
        sort_idx = 0

        for legacy_name, cfg in provider_configs.items():
            legacy_key = str(legacy_name).strip().lower()
            if legacy_key in _LEGACY_SKIP:
                logger.info(
                    "ProviderStore.migrate: skip unsupported legacy provider %s",
                    legacy_key,
                )
                continue
            if not isinstance(cfg, dict):
                continue

            mapped = self._map_legacy_provider(
                legacy_key, cfg, yaml_providers or {}
            )
            if mapped is None:
                continue

            # 空配置（无 key 且无 model 且无自定义 url）仍导入，方便用户补 key
            row = self.create_provider(
                name=mapped["name"],
                protocol=mapped["protocol"],
                base_url=mapped["base_url"],
                api_key=mapped["api_key"],
                model=mapped["model"],
                enabled=True,
                sort_order=sort_idx,
                is_preset=legacy_key in _LEGACY_DEFAULT_BASE_URLS,
            )
            name_to_id[legacy_key] = row["id"]
            imported.append(row)
            sort_idx += 1

        # 设置 current
        if imported:
            if old_current in name_to_id:
                self.set_current_provider_id(name_to_id[old_current])
            elif not self.get_current_provider_id():
                self.set_current_provider_id(imported[0]["id"])

        self.mark_migrated()
        logger.info(
            "ProviderStore.migrate_from_settings: imported %s providers",
            len(imported),
        )
        return len(imported)

    def _map_legacy_provider(
        self,
        legacy_key: str,
        cfg: Dict[str, Any],
        yaml_providers: Dict[str, Any],
    ) -> Optional[Dict[str, str]]:
        yaml_cfg = yaml_providers.get(legacy_key) or {}
        if not isinstance(yaml_cfg, dict):
            yaml_cfg = {}

        api_format = str(
            cfg.get("api_format")
            or (yaml_cfg.get("extra_params") or {}).get("api_format")
            or "openai"
        ).strip().lower()

        if legacy_key == "custom" and api_format == "anthropic":
            protocol = PROTOCOL_ANTHROPIC_MESSAGES
        else:
            # zhipu/deepseek/openrouter/openai/custom(openai) → openai_chat
            if legacy_key not in (
                "zhipu",
                "deepseek",
                "openrouter",
                "openai",
                "custom",
            ) and legacy_key not in _LEGACY_DEFAULT_BASE_URLS:
                # 未知名字：若有 base_url/api_key 仍按 openai_chat 尝试
                if not (cfg.get("base_url") or cfg.get("api_key") or cfg.get("selected_model")):
                    return None
            protocol = PROTOCOL_OPENAI_CHAT

        base_url = normalize_base_url(
            str(cfg.get("base_url") or yaml_cfg.get("base_url") or "")
        )
        if not base_url:
            base_url = _LEGACY_DEFAULT_BASE_URLS.get(legacy_key, "")
        base_url = strip_protocol_path(base_url, protocol)

        model = str(
            cfg.get("selected_model")
            or cfg.get("model")
            or ""
        ).strip()
        if not model:
            models = yaml_cfg.get("models") or []
            if isinstance(models, list) and models:
                first = models[0]
                if isinstance(first, dict):
                    model = str(first.get("name") or "")
                else:
                    model = str(first)

        display_name = {
            "zhipu": "Zhipu",
            "deepseek": "DeepSeek",
            "openrouter": "OpenRouter",
            "openai": "OpenAI",
            "custom": "Custom",
        }.get(legacy_key, legacy_key.capitalize() or "Provider")

        return {
            "name": display_name,
            "protocol": protocol,
            "base_url": base_url,
            "api_key": str(cfg.get("api_key") or ""),
            "model": model,
        }

    @staticmethod
    def _load_json(path: Path) -> Dict[str, Any]:
        try:
            if not path or not Path(path).exists():
                return {}
            with open(path, "r", encoding="utf-8") as f:
                data = json.load(f)
            return data if isinstance(data, dict) else {}
        except Exception as e:
            logger.warning("ProviderStore._load_json failed for %s: %s", path, e)
            return {}

    # ------------------------------------------------------------------
    # Bootstrap
    # ------------------------------------------------------------------

    def bootstrap(
        self,
        settings_path: Optional[Path] = None,
        settings: Optional[Dict[str, Any]] = None,
        yaml_providers: Optional[Dict[str, Any]] = None,
    ) -> Dict[str, Any]:
        """启动入口：migrate only（不再自动 seed）。

        设计变更：不再在 bootstrap 时自动 seed 预设。
        - 新用户首次启动：列表为空，用户可手动点 'Restore Presets' 加载内置预设
        - 避免用户添加 1 个 provider 后列表突现 3 个预设的困惑
        - 用户主动删光后不会被自动恢复
        """
        migrated_before = self.is_migrated()
        imported = 0
        if not migrated_before:
            imported = self.migrate_from_settings(
                settings=settings,
                settings_path=settings_path,
                yaml_providers=yaml_providers,
            )

        # 若从未标记迁移（异常路径），补标记
        if not self.is_migrated():
            self.mark_migrated()

        # 不再自动 seed。用户需要预设时通过 IPC action=restore_presets 主动触发。
        seeded = 0

        return {
            "imported": imported,
            "seeded": seeded,
            "count": self.count_providers(),
            "current_provider_id": self.get_current_provider_id(),
        }
