"""SQLite 访问层（Python 端）

模块职责：
- 管理 normalize_alias 表（含 alias_key 归一化列）
- 管理 providers / app_settings（AI Provider V1）
- 提供 alias → canonical 的归一化查询接口

设计说明：
- 本模块独立访问 foo_metadata_enhancer.db（与 C++ 端 CacheLayer 共享同一个 db 文件）
- normalize_alias / providers 表的 schema 由本模块创建和维护
"""

from .normalize_store import NormalizeStore
from .provider_store import ProviderStore

__all__ = [
    "NormalizeStore",
    "ProviderStore",
]
