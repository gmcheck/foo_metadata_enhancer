#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
NormalizeStore — Python 端 SQLite 访问层（管理 normalize_alias 表）

表结构：
    normalize_alias
        id              INTEGER PRIMARY KEY AUTOINCREMENT
        field           TEXT NOT NULL                 -- artist / album_artist / album / genre / composer / publisher
        alias_name      TEXT NOT NULL                 -- 原始写法（保留每条原始字符串）
        alias_key       TEXT NOT NULL                 -- 归一化 key（NFC + strip Unicode 空白 + lower）
        canonical_name  TEXT NOT NULL                 -- 标准名
        source          TEXT DEFAULT 'ai'             -- 来源：ai / manual / imported
        confidence      REAL DEFAULT 1.0              -- AI 置信度
        confirmed       INTEGER DEFAULT 1             -- 是否用户确认
        reason          TEXT                          -- AI 给出的归并理由
        created_time    TEXT NOT NULL                 -- 创建时间（ISO8601）
        updated_time    TEXT NOT NULL                 -- 更新时间（ISO8601）
        UNIQUE(field, alias_name)                    -- 每 (field, 原始 alias) 唯一

查询策略：
    使用 alias_key 做归一化匹配，"Beyond" / "beyond " / "BEYOND" 都归一化为 "beyond"，
    能正确命中同一条记录。

迁移逻辑：
    - C++ 端旧表（无 alias_key 列）：自动 ADD COLUMN + 回填 alias_key
    - 完整性：幂等，可重复执行
"""

import sqlite3
import threading
import unicodedata
import logging
from datetime import datetime, timezone
from typing import Dict, List, Any, Optional


logger = logging.getLogger(__name__)


class NormalizeStore:
    """normalize_alias 表的 SQLite 访问层

    线程安全：内部用 lock 保护，可被多线程调用。
    生命周期：进程内常驻，db 连接在进程退出时关闭。
    """

    # 零宽字符集合（不可见，应从 key 中完全移除）
    _ZERO_WIDTH_CHARS = frozenset({'\u200b', '\u200c', '\u200d', '\ufeff', '\u2060'})

    def __init__(self, db_path: str):
        """初始化

        Args:
            db_path: foo_metadata_enhancer.db 的绝对路径
        """
        self._db_path = db_path
        self._lock = threading.Lock()
        self._conn: Optional[sqlite3.Connection] = None
        self._connect()
        self._init_schema()

    # ------------------------------------------------------------------
    # Public API
    # ------------------------------------------------------------------

    def get_aliases(self, field: str, alias_names: List[str]) -> List[Dict[str, Any]]:
        """批量查询 alias → canonical 映射（用 alias_key 归一化匹配）

        Args:
            field: 目标字段
            alias_names: 待查的原始 alias 字符串列表

        Returns:
            命中的条目列表，每条 dict 包含：
            {
                "field": str,
                "alias_name": str,        # 数据库中存储的原始写法
                "canonical_name": str,
                "source": str,
                "confidence": float,
                "confirmed": bool,
                "reason": str,
            }
            注意：alias_name 是库中存储的原始写法，可能与传入的 alias_names 不同
            （例如传入 "Beyond " 库里是 "Beyond"），调用方需自行做变体合并。
        """
        if not alias_names:
            return []

        # 对传入的 alias_names 做归一化
        keys = [self._normalize_key(a) for a in alias_names]
        keys = [k for k in keys if k]  # 过滤空串
        if not keys:
            return []

        placeholders = ",".join("?" for _ in keys)
        sql = (
            "SELECT field, alias_name, canonical_name, source, confidence, confirmed, reason "
            f"FROM normalize_alias WHERE field = ? AND alias_key IN ({placeholders})"
        )

        with self._lock:
            try:
                cur = self._conn.execute(sql, [field] + keys)
                rows = cur.fetchall()
            except sqlite3.Error as e:
                logger.error(f"NormalizeStore.get_aliases: {e}")
                return []

        result = []
        for row in rows:
            result.append({
                "field": row[0],
                "alias_name": row[1],
                "canonical_name": row[2],
                "source": row[3] or "ai",
                "confidence": float(row[4]) if row[4] is not None else 1.0,
                "confirmed": bool(row[5]),
                "reason": row[6] or "",
            })
        return result

    def upsert_aliases(self, entries: List[Dict[str, Any]]) -> bool:
        """批量写入 alias 映射（事务）

        Args:
            entries: 待写入的条目列表，每条 dict 必须包含：
                - field (str)
                - alias_name (str)        原始写法
                - canonical_name (str)
                可选：
                - source (str)            默认 "ai"
                - confidence (float)      默认 1.0
                - confirmed (bool)        默认 True
                - reason (str)            默认 ""

        Returns:
            全部成功返回 True
        """
        if not entries:
            return True

        now = self._now_iso()
        sql = (
            "INSERT OR REPLACE INTO normalize_alias "
            "(field, alias_name, alias_key, canonical_name, source, confidence, confirmed, reason, created_time, updated_time) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"
        )

        with self._lock:
            try:
                self._conn.execute("BEGIN")
                for e in entries:
                    alias_name = e.get("alias_name", "")
                    if not alias_name:
                        continue
                    params = (
                        e.get("field", ""),
                        alias_name,
                        self._normalize_key(alias_name),
                        e.get("canonical_name", ""),
                        e.get("source", "ai"),
                        float(e.get("confidence", 1.0)),
                        1 if e.get("confirmed", True) else 0,
                        e.get("reason", ""),
                        now,
                        now,
                    )
                    self._conn.execute(sql, params)
                self._conn.commit()
                return True
            except sqlite3.Error as e:
                logger.error(f"NormalizeStore.upsert_aliases: {e}")
                self._conn.rollback()
                return False

    def close(self):
        """关闭数据库连接"""
        with self._lock:
            if self._conn:
                try:
                    self._conn.close()
                except sqlite3.Error:
                    pass
                self._conn = None

    # ------------------------------------------------------------------
    # Internal helpers
    # ------------------------------------------------------------------

    @classmethod
    def _normalize_key(cls, s: str) -> str:
        """归一化 key：移除零宽字符 + NFC + strip Unicode 空白 + 转小写

        必须与 NormalizeProcessor._normalize_key 保持一致。
        """
        if not s:
            return ""
        # 移除零宽字符
        s = ''.join(c for c in s if c not in cls._ZERO_WIDTH_CHARS)
        # NFC 规范化（合并分解形式）
        s = unicodedata.normalize('NFC', s)
        # strip 首尾 Unicode 空白 + 转小写
        return s.strip().lower()

    @staticmethod
    def _now_iso() -> str:
        """当前 UTC 时间 ISO8601 字符串"""
        return datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")

    def _connect(self):
        """打开数据库连接"""
        try:
            # check_same_thread=False：允许跨线程使用（外部加锁保护）
            self._conn = sqlite3.connect(self._db_path, check_same_thread=False)
            self._conn.row_factory = sqlite3.Row
            # 性能优化
            self._conn.execute("PRAGMA journal_mode=WAL")
            self._conn.execute("PRAGMA synchronous=NORMAL")
            self._conn.execute("PRAGMA busy_timeout=5000")
        except sqlite3.Error as e:
            logger.error(f"NormalizeStore._connect: Failed to open db at {self._db_path}: {e}")
            self._conn = None
            raise

    def _init_schema(self):
        """创建表 + 索引 + 旧表迁移（幂等）"""
        if not self._conn:
            return

        create_table_sql = """
        CREATE TABLE IF NOT EXISTS normalize_alias (
            id              INTEGER PRIMARY KEY AUTOINCREMENT, -- 自增主键
            field           TEXT NOT NULL,                     -- 目标字段：artist/album_artist/album/genre/...
            alias_name      TEXT NOT NULL,                     -- 原始写法（保留每条原始字符串）
            alias_key       TEXT NOT NULL,                     -- 归一化 key（NFC + strip + lower）
            canonical_name  TEXT NOT NULL,                     -- 标准名
            source          TEXT DEFAULT 'ai',                 -- 来源：ai / manual / imported
            confidence      REAL DEFAULT 1.0,                  -- AI 置信度
            confirmed       INTEGER DEFAULT 1,                 -- 是否用户确认（0/1）
            reason          TEXT,                              -- AI 给出的归并理由
            created_time    TEXT NOT NULL,                     -- 创建时间（ISO8601 UTC）
            updated_time    TEXT NOT NULL,                     -- 更新时间（ISO8601 UTC）
            UNIQUE(field, alias_name)                          -- 每 (field, 原始 alias) 唯一
        )
        """

        create_index_sql = (
            "CREATE INDEX IF NOT EXISTS idx_normalize_field_alias_key "
            "ON normalize_alias(field, alias_key)"
        )
        create_index_canonical_sql = (
            "CREATE INDEX IF NOT EXISTS idx_normalize_canonical "
            "ON normalize_alias(field, canonical_name)"
        )

        with self._lock:
            try:
                self._conn.execute(create_table_sql)
                self._conn.execute(create_index_sql)
                self._conn.execute(create_index_canonical_sql)
                self._conn.commit()
            except sqlite3.Error as e:
                logger.error(f"NormalizeStore._init_schema: create failed: {e}")
                return

            # 旧表迁移：检查 alias_key 列是否存在
            self._migrate_legacy_table()

    def _migrate_legacy_table(self):
        """迁移旧表（C++ 端遗留的 normalize_alias 表）

        旧表无 alias_key 列。迁移步骤：
        1. ALTER TABLE ADD COLUMN alias_key
        2. UPDATE 回填 alias_key = normalize_key(alias_name)
        3. 重建索引（idx_normalize_field_alias_key）
        """
        # 检查 alias_key 列是否存在
        try:
            cur = self._conn.execute("PRAGMA table_info(normalize_alias)")
            columns = [row[1] for row in cur.fetchall()]
        except sqlite3.Error as e:
            logger.error(f"NormalizeStore._migrate_legacy_table: PRAGMA failed: {e}")
            return

        if "alias_key" in columns:
            # 已经是新版表
            return

        if not columns:
            # 表不存在（不应该，_init_schema 已建表）
            return

        logger.info("NormalizeStore._migrate_legacy_table: adding alias_key column to legacy normalize_alias table")
        try:
            # Step 1: 添加列（先允许 NULL，回填后再设 NOT NULL）
            self._conn.execute(
                "ALTER TABLE normalize_alias ADD COLUMN alias_key TEXT"
            )

            # Step 2: 回填 alias_key（用 Python 端 _normalize_key，确保与查询一致）
            cur = self._conn.execute(
                "SELECT id, alias_name FROM normalize_alias WHERE alias_key IS NULL"
            )
            rows = cur.fetchall()
            for row in rows:
                row_id = row[0]
                alias_name = row[1] or ""
                key = self._normalize_key(alias_name)
                self._conn.execute(
                    "UPDATE normalize_alias SET alias_key = ? WHERE id = ?",
                    (key, row_id)
                )
            self._conn.commit()
            logger.info(f"NormalizeStore._migrate_legacy_table: backfilled {len(rows)} rows with alias_key")

            # Step 3: 重建索引（idx_normalize_field_alias_key 已在 _init_schema 创建，
            #          但旧索引名可能是 idx_normalize_field_alias，需 DROP 旧索引）
            self._conn.execute("DROP INDEX IF EXISTS idx_normalize_field_alias")
            self._conn.commit()
        except sqlite3.Error as e:
            logger.error(f"NormalizeStore._migrate_legacy_table: migration failed: {e}")
            self._conn.rollback()
