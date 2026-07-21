#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Providers IPC handlers (V1).

method: providers
params.action:
  list | get | create | update | delete | set_current | get_current | test | restore_presets

响应 results[0] 统一由 common.models.ProviderActionResultModel 生成：
  success -> { status: "success", action, data }
  failed  -> { status: "failed", action, error, error_category, data }
"""

from __future__ import annotations

import logging
from typing import Any, Dict, Optional

from common.models import (
    DEFAULT_PROVIDER_ERROR_CATEGORY,
    STATUS_FAILED,
    STATUS_SUCCESS,
    ProviderActionResultModel,
    ProviderTestDataModel,
    create_provider_failure,
    create_provider_success,
)

logger = logging.getLogger(__name__)

# 兼容旧 import / 单测
DEFAULT_ERROR_CATEGORY = DEFAULT_PROVIDER_ERROR_CATEGORY


def _respond_ok(create_response, request_id: str, action: str, data: Optional[Any] = None):
    return create_response(
        request_id,
        success=True,
        results=[create_provider_success(action, data)],
    )


def _respond_fail(
    create_response,
    request_id: str,
    action: str,
    error: str,
    *,
    error_category: str = DEFAULT_PROVIDER_ERROR_CATEGORY,
    data: Optional[Any] = None,
):
    return create_response(
        request_id,
        success=False,
        results=[
            create_provider_failure(
                action,
                error,
                error_category=error_category,
                data=data,
            )
        ],
    )


def _mask_provider(row: Optional[Dict[str, Any]], *, include_api_key: bool = False) -> Optional[Dict[str, Any]]:
    if row is None:
        return None
    out = dict(row)
    key = out.get("api_key") or ""
    if include_api_key:
        out["api_key"] = key
    else:
        out["api_key_set"] = bool(key)
        out["api_key"] = ""
    return out


def _runtime_or_error(get_provider_runtime):
    rt = get_provider_runtime()
    if rt is None:
        return None, "Provider runtime not available"
    return rt, None


def process_providers(request: Dict, *, create_response, get_provider_runtime) -> Dict:
    """处理 providers 管理请求。"""
    request_id = request.get("id", "")
    params = request.get("params") or {}
    if not isinstance(params, dict):
        params = {}

    action = str(params.get("action") or "").strip().lower()
    if not action:
        return _respond_fail(
            create_response,
            request_id,
            "unknown",
            "params.action is required",
            error_category="Invalid Configuration",
        )

    try:
        if action == "list":
            return _action_list(request_id, params, create_response, get_provider_runtime)
        if action == "get":
            return _action_get(request_id, params, create_response, get_provider_runtime)
        if action == "create":
            return _action_create(request_id, params, create_response, get_provider_runtime)
        if action == "update":
            return _action_update(request_id, params, create_response, get_provider_runtime)
        if action == "delete":
            return _action_delete(request_id, params, create_response, get_provider_runtime)
        if action == "set_current":
            return _action_set_current(request_id, params, create_response, get_provider_runtime)
        if action == "get_current":
            return _action_get_current(request_id, params, create_response, get_provider_runtime)
        if action == "test":
            return _action_test(request_id, params, create_response, get_provider_runtime)
        if action == "restore_presets":
            return _action_restore_presets(request_id, params, create_response, get_provider_runtime)

        return _respond_fail(
            create_response,
            request_id,
            action,
            f"Unknown providers action: {action}",
            error_category="Invalid Configuration",
        )
    except ValueError as e:
        logger.warning("providers action=%s validation error: %s", action, e)
        return _respond_fail(
            create_response,
            request_id,
            action,
            str(e),
            error_category="Invalid Configuration",
        )
    except Exception as e:
        logger.error("providers action=%s failed: %s", action, e, exc_info=True)
        return _respond_fail(
            create_response,
            request_id,
            action,
            str(e),
            error_category=DEFAULT_PROVIDER_ERROR_CATEGORY,
        )


def _action_list(request_id, params, create_response, get_provider_runtime):
    rt, err = _runtime_or_error(get_provider_runtime)
    if err:
        return _respond_fail(create_response, request_id, "list", err)

    include_disabled = bool(params.get("include_disabled", True))
    include_api_key = bool(params.get("include_api_key", False))
    rows = rt.store.list_providers(include_disabled=include_disabled)
    payload = [_mask_provider(r, include_api_key=include_api_key) for r in rows]
    return _respond_ok(
        create_response,
        request_id,
        "list",
        {
            "providers": payload,
            "current_provider_id": rt.store.get_current_provider_id() or "",
            "count": len(payload),
        },
    )


def _action_get(request_id, params, create_response, get_provider_runtime):
    rt, err = _runtime_or_error(get_provider_runtime)
    if err:
        return _respond_fail(create_response, request_id, "get", err)

    provider_id = str(params.get("id") or "").strip()
    if not provider_id:
        raise ValueError("params.id is required")

    include_api_key = bool(params.get("include_api_key", True))
    row = rt.store.get_provider(provider_id)
    if row is None:
        return _respond_fail(
            create_response,
            request_id,
            "get",
            f"Provider not found: {provider_id}",
            error_category="Invalid Configuration",
        )
    return _respond_ok(
        create_response,
        request_id,
        "get",
        {
            "provider": _mask_provider(row, include_api_key=include_api_key),
            "current_provider_id": rt.store.get_current_provider_id() or "",
        },
    )


def _action_create(request_id, params, create_response, get_provider_runtime):
    rt, err = _runtime_or_error(get_provider_runtime)
    if err:
        return _respond_fail(create_response, request_id, "create", err)

    row = rt.store.create_provider(
        name=str(params.get("name") or ""),
        protocol=str(params.get("protocol") or "openai_chat"),
        base_url=str(params.get("base_url") or ""),
        api_key=str(params.get("api_key") or ""),
        model=str(params.get("model") or ""),
        enabled=bool(params.get("enabled", True)),
        is_preset=bool(params.get("is_preset", False)),
    )
    if not rt.store.get_current_provider_id():
        rt.store.set_current_provider_id(row["id"])
    rt.invalidate()
    return _respond_ok(
        create_response,
        request_id,
        "create",
        {
            "provider": _mask_provider(row, include_api_key=True),
            "current_provider_id": rt.store.get_current_provider_id() or "",
        },
    )


def _action_update(request_id, params, create_response, get_provider_runtime):
    rt, err = _runtime_or_error(get_provider_runtime)
    if err:
        return _respond_fail(create_response, request_id, "update", err)

    provider_id = str(params.get("id") or "").strip()
    if not provider_id:
        raise ValueError("params.id is required")

    fields = {}
    for key in ("name", "protocol", "base_url", "api_key", "model", "enabled", "sort_order", "is_preset"):
        if key in params and params[key] is not None:
            fields[key] = params[key]

    if not fields:
        raise ValueError("No updatable fields provided")

    row = rt.store.update_provider(provider_id, **fields)
    rt.invalidate()
    return _respond_ok(
        create_response,
        request_id,
        "update",
        {
            "provider": _mask_provider(row, include_api_key=True),
            "current_provider_id": rt.store.get_current_provider_id() or "",
        },
    )


def _action_delete(request_id, params, create_response, get_provider_runtime):
    rt, err = _runtime_or_error(get_provider_runtime)
    if err:
        return _respond_fail(create_response, request_id, "delete", err)

    provider_id = str(params.get("id") or "").strip()
    if not provider_id:
        raise ValueError("params.id is required")

    deleted = rt.store.delete_provider(provider_id)
    if not deleted:
        return _respond_fail(
            create_response,
            request_id,
            "delete",
            f"Provider not found: {provider_id}",
            error_category="Invalid Configuration",
        )
    rt.invalidate()
    return _respond_ok(
        create_response,
        request_id,
        "delete",
        {
            "deleted_id": provider_id,
            "current_provider_id": rt.store.get_current_provider_id() or "",
            "count": rt.store.count_providers(),
        },
    )


def _action_set_current(request_id, params, create_response, get_provider_runtime):
    rt, err = _runtime_or_error(get_provider_runtime)
    if err:
        return _respond_fail(create_response, request_id, "set_current", err)

    provider_id = str(params.get("id") or "").strip()
    if not provider_id:
        raise ValueError("params.id is required")

    rt.store.set_current_provider_id(provider_id)
    rt.invalidate()
    row = rt.store.get_current_provider()
    return _respond_ok(
        create_response,
        request_id,
        "set_current",
        {
            "provider": _mask_provider(row, include_api_key=False),
            "current_provider_id": provider_id,
        },
    )


def _action_get_current(request_id, params, create_response, get_provider_runtime):
    rt, err = _runtime_or_error(get_provider_runtime)
    if err:
        return _respond_fail(create_response, request_id, "get_current", err)

    include_api_key = bool(params.get("include_api_key", False))
    row = rt.store.get_current_provider()
    return _respond_ok(
        create_response,
        request_id,
        "get_current",
        {
            "provider": _mask_provider(row, include_api_key=include_api_key),
            "current_provider_id": rt.store.get_current_provider_id() or "",
        },
    )


def _action_test(request_id, params, create_response, get_provider_runtime):
    rt, err = _runtime_or_error(get_provider_runtime)
    if err:
        return _respond_fail(create_response, request_id, "test", err)

    draft = params.get("draft")
    provider_id = params.get("id")
    if draft is not None and not isinstance(draft, dict):
        raise ValueError("params.draft must be an object")

    # 允许顶层草稿字段（无 draft 包装）
    if draft is None and any(k in params for k in ("protocol", "base_url", "api_key", "model", "name")):
        draft = {
            "name": params.get("name"),
            "protocol": params.get("protocol"),
            "base_url": params.get("base_url"),
            "api_key": params.get("api_key"),
            "model": params.get("model"),
            "timeout_ms": params.get("timeout_ms"),
        }

    raw = rt.test_connection(
        provider_id=str(provider_id) if provider_id else None,
        draft=draft,
    ) or {}

    data = ProviderTestDataModel.from_runtime_result(
        raw,
        provider_id=str(provider_id) if provider_id else "",
    )

    if bool(raw.get("success")):
        return _respond_ok(create_response, request_id, "test", data)

    return _respond_fail(
        create_response,
        request_id,
        "test",
        str(raw.get("error") or "Connection test failed"),
        error_category=str(raw.get("error_category") or DEFAULT_PROVIDER_ERROR_CATEGORY),
        data=data,
    )


def _action_restore_presets(request_id, params, create_response, get_provider_runtime):
    rt, err = _runtime_or_error(get_provider_runtime)
    if err:
        return _respond_fail(create_response, request_id, "restore_presets", err)

    overwrite = bool(params.get("overwrite_existing_names", False))
    created = rt.store.restore_presets(overwrite_existing_names=overwrite)
    if not rt.store.get_current_provider_id() and rt.store.count_providers() > 0:
        first = rt.store.list_providers()[0]
        rt.store.set_current_provider_id(first["id"])
    rt.invalidate()
    rows = rt.store.list_providers()
    return _respond_ok(
        create_response,
        request_id,
        "restore_presets",
        {
            "created": created,
            "providers": [_mask_provider(r) for r in rows],
            "current_provider_id": rt.store.get_current_provider_id() or "",
            "count": len(rows),
        },
    )


__all__ = [
    "STATUS_SUCCESS",
    "STATUS_FAILED",
    "DEFAULT_ERROR_CATEGORY",
    "process_providers",
    "ProviderActionResultModel",
    "ProviderTestDataModel",
]
