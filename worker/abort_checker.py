#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Abort Checker
Check abort flag files for task cancellation
"""

import os
import logging
from pathlib import Path
from typing import Optional

logger = logging.getLogger(__name__)


class AbortChecker:
    """中断检查器
    
    检查 C++ 端创建的中断标志文件，实现跨进程中断通信。
    """
    
    def __init__(self, abort_dir: Optional[str] = None, task_id: Optional[str] = None):
        """初始化中断检查器
        
        Args:
            abort_dir: 中断标志文件目录
            task_id: 任务ID
        """
        self._abort_dir = abort_dir
        self._task_id = task_id
    
    def set_task(self, task_id: str, abort_dir: str) -> None:
        """设置当前任务
        
        Args:
            task_id: 任务ID
            abort_dir: 中断标志文件目录
        """
        self._task_id = task_id
        self._abort_dir = abort_dir
        logger.debug(f"AbortChecker: Set task_id={task_id}, abort_dir={abort_dir}")
    
    def is_aborted(self) -> bool:
        """检查是否请求了中断
        
        Returns:
            bool: 已请求中断返回 True
        """
        if not self._abort_dir or not self._task_id:
            return False
        
        abort_file = Path(self._abort_dir) / f"abort_{self._task_id}.flag"
        
        if abort_file.exists():
            logger.info(f"AbortChecker: Abort flag detected: {abort_file}")
            return True
        
        return False
    
    def clear(self) -> None:
        """清除当前任务设置"""
        self._task_id = None
        self._abort_dir = None


_abort_checker: Optional[AbortChecker] = None


def get_abort_checker() -> AbortChecker:
    """获取全局中断检查器实例
    
    Returns:
        AbortChecker: 中断检查器实例
    """
    global _abort_checker
    if _abort_checker is None:
        _abort_checker = AbortChecker()
    return _abort_checker


def is_aborted() -> bool:
    """检查是否请求了中断（便捷函数）
    
    Returns:
        bool: 已请求中断返回 True
    """
    return get_abort_checker().is_aborted()


def set_abort_task(task_id: str, abort_dir: str) -> None:
    """设置当前任务（便捷函数）
    
    Args:
        task_id: 任务ID
        abort_dir: 中断标志文件目录
    """
    get_abort_checker().set_task(task_id, abort_dir)


def clear_abort_task() -> None:
    """清除当前任务设置（便捷函数）"""
    get_abort_checker().clear()
