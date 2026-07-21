#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
ProviderRuntime — 从 ProviderStore 加载当前 Provider 并创建 Protocol Client。

V1：业务通过 get_active_client() 获取唯一当前客户端。
"""

from __future__ import annotations

import logging
import threading
from pathlib import Path
from typing import Any, Dict, Optional

from ai.protocols import ProtocolClient, ProtocolConfig, ProtocolType, create_protocol_client
from db.provider_store import ProviderStore

logger = logging.getLogger(__name__)


class ProviderRuntime:
    """持有 ProviderStore，并缓存当前 ProtocolClient。"""

    def __init__(
        self,
        store: ProviderStore,
        *,
        timeout_ms: int = 180000,
        max_retries: int = 3,
        retry_delay_ms: int = 1000,
    ):
        self.store = store
        self.timeout_ms = timeout_ms
        self.max_retries = max_retries
        self.retry_delay_ms = retry_delay_ms
        self._lock = threading.Lock()
        self._client: Optional[ProtocolClient] = None
        self._client_provider_id: Optional[str] = None

    def invalidate(self) -> None:
        with self._lock:
            self._client = None
            self._client_provider_id = None

    def get_active_row(self) -> Optional[Dict[str, Any]]:
        return self.store.get_current_provider()

    def create_client_from_row(self, row: Dict[str, Any]) -> ProtocolClient:
        return create_protocol_client(
            row,
            timeout_ms=self.timeout_ms,
            max_retries=self.max_retries,
            retry_delay_ms=self.retry_delay_ms,
        )

    def create_client_from_draft(self, draft: Dict[str, Any]) -> ProtocolClient:
        """用未保存表单字段构造 client（Test Connection）。"""
        protocol = str(draft.get("protocol") or ProtocolType.OPENAI_CHAT.value)
        row = {
            "id": draft.get("id") or "",
            "name": draft.get("name") or "draft",
            "protocol": protocol,
            "base_url": draft.get("base_url") or "",
            "api_key": draft.get("api_key") or "",
            "model": draft.get("model") or "",
        }
        timeout_ms = int(draft.get("timeout_ms") or self.timeout_ms)
        return create_protocol_client(
            row,
            timeout_ms=timeout_ms,
            max_retries=int(draft.get("max_retries") or self.max_retries),
            retry_delay_ms=self.retry_delay_ms,
        )

    def get_active_client(self, force_reload: bool = False) -> Optional[ProtocolClient]:
        row = self.get_active_row()
        if row is None:
            with self._lock:
                self._client = None
                self._client_provider_id = None
            return None

        pid = row["id"]
        with self._lock:
            if (
                not force_reload
                and self._client is not None
                and self._client_provider_id == pid
            ):
                return self._client
            client = self.create_client_from_row(row)
            self._client = client
            self._client_provider_id = pid
            logger.info(
                "ProviderRuntime: active client name=%s protocol=%s model=%s",
                row.get("name"),
                row.get("protocol"),
                row.get("model"),
            )
            return client

    def test_connection(
        self,
        *,
        provider_id: Optional[str] = None,
        draft: Optional[Dict[str, Any]] = None,
    ) -> Dict[str, Any]:
        if draft is not None:
            client = self.create_client_from_draft(draft)
            return client.test_connection()

        if provider_id:
            row = self.store.get_provider(provider_id)
            if row is None:
                return {
                    "success": False,
                    "error": f"Provider not found: {provider_id}",
                    "error_category": "Invalid Configuration",
                }
            return self.create_client_from_row(row).test_connection()

        client = self.get_active_client()
        if client is None:
            return {
                "success": False,
                "error": "No current provider selected",
                "error_category": "Invalid Configuration",
            }
        return client.test_connection()


# ---------------------------------------------------------------------------
# Process-level singleton helpers
# ---------------------------------------------------------------------------

_runtime: Optional[ProviderRuntime] = None
_runtime_lock = threading.Lock()


def get_provider_store_db_path() -> Path:
    """解析与 NormalizeStore 相同的 db 路径。"""
    from common.config_manager import _find_foobar_profile, _get_expected_settings_path

    profile_dir = _find_foobar_profile()
    if profile_dir is not None:
        db_dir = profile_dir
    else:
        expected = _get_expected_settings_path()
        if expected is not None:
            db_dir = expected.parent
        else:
            db_dir = Path(__file__).resolve().parent.parent
    db_dir.mkdir(parents=True, exist_ok=True)
    return db_dir / "foo_metadata_enhancer.db"


def init_provider_runtime(
    *,
    db_path: Optional[str] = None,
    timeout_ms: int = 180000,
    max_retries: int = 3,
    retry_delay_ms: int = 1000,
    settings_path: Optional[Path] = None,
    settings: Optional[Dict[str, Any]] = None,
    yaml_providers: Optional[Dict[str, Any]] = None,
    bootstrap: bool = True,
) -> ProviderRuntime:
    """初始化全局 ProviderRuntime（幂等可 force 重建）。"""
    global _runtime
    with _runtime_lock:
        path = Path(db_path) if db_path else get_provider_store_db_path()
        store = ProviderStore(str(path))
        if bootstrap:
            if settings_path is None and settings is None:
                # 默认尝试 profile/settings.json
                from common.config_manager import _find_foobar_profile

                profile = _find_foobar_profile()
                if profile is not None:
                    settings_path = profile / "settings.json"
            result = store.bootstrap(
                settings_path=settings_path,
                settings=settings,
                yaml_providers=yaml_providers,
            )
            logger.info("init_provider_runtime bootstrap: %s", result)

        _runtime = ProviderRuntime(
            store,
            timeout_ms=timeout_ms,
            max_retries=max_retries,
            retry_delay_ms=retry_delay_ms,
        )
        return _runtime


def get_provider_runtime() -> Optional[ProviderRuntime]:
    return _runtime


def get_active_provider_client() -> Optional[ProtocolClient]:
    rt = _runtime
    if rt is None:
        return None
    return rt.get_active_client()
