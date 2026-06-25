#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Configuration Manager for AI Metadata Plugin
Loads and manages configuration from config.yaml
Also merges settings from C++ saved settings.json
"""

import os
import sys
import logging
import logging.handlers
import json
from pathlib import Path
from typing import Dict, Any, Optional
from datetime import datetime

import yaml

logger = logging.getLogger(__name__)


def _find_foobar_profile() -> Optional[Path]:
    """查找 foo_metadata_enhancer 目录（包含 settings.json）
    
    搜索顺序：
    1. 从当前脚本目录向上查找 settings.json 或 foo_metadata_enhancer/settings.json
    2. 从 components 目录查找（foobar2000 便携版 profile 子目录）
    3. 从标准 foobar2000 profile 路径查找
    
    Returns:
        Optional[Path]: foo_metadata_enhancer 目录路径，未找到返回 None
    """
    script_dir = Path(__file__).parent.resolve()
    
    current = script_dir
    for _ in range(10):
        # 检查当前目录是否有 settings.json
        settings_file = current / "settings.json"
        if settings_file.exists():
            return current
        
        parent = current.parent
        if parent == current:
            break
        
        # 检查父目录下是否有 foo_metadata_enhancer/settings.json
        foo_metadata_enhancer_dir = parent / "foo_metadata_enhancer"
        if foo_metadata_enhancer_dir.exists():
            settings_file = foo_metadata_enhancer_dir / "settings.json"
            if settings_file.exists():
                return foo_metadata_enhancer_dir
        
        # 检查父目录下是否有 profile/foo_metadata_enhancer/settings.json（便携版）
        profile_dir = parent / "profile" / "foo_metadata_enhancer"
        if profile_dir.exists():
            settings_file = profile_dir / "settings.json"
            if settings_file.exists():
                return profile_dir
        
        current = parent
    
    # 从脚本目录向上查找 components 目录
    # 脚本路径: .../components/foo_metadata_enhancer/worker/common/config_manager.py
    dll_candidates = []
    current = script_dir
    for _ in range(10):
        if current.name == "components":
            # 便携版: components 的父目录是 foobar2000 安装目录
            fb2k_root = current.parent
            dll_candidates.append(fb2k_root / "profile" / "foo_metadata_enhancer")
            dll_candidates.append(fb2k_root / "foo_metadata_enhancer")
            break
        parent = current.parent
        if parent == current:
            break
        current = parent
    
    for candidate in dll_candidates:
        if candidate.exists():
            settings_file = candidate / "settings.json"
            if settings_file.exists():
                return candidate
    
    candidates = []
    
    local_app_data = os.environ.get('LOCALAPPDATA', '')
    if local_app_data:
        candidates.append(Path(local_app_data) / "foobar2000")
        candidates.append(Path(local_app_data) / "foobar2000_v2")
    
    app_data = os.environ.get('APPDATA', '')
    if app_data:
        candidates.append(Path(app_data) / "foobar2000")
        candidates.append(Path(app_data) / "foobar2000_v2")
    
    program_files = os.environ.get('ProgramFiles', 'C:\\Program Files')
    candidates.append(Path(program_files) / "foobar2000" / "profile")
    candidates.append(Path(program_files) / "foobar2000_v2" / "profile")
    
    program_files_x86 = os.environ.get('ProgramFiles(x86)', 'C:\\Program Files (x86)')
    candidates.append(Path(program_files_x86) / "foobar2000" / "profile")
    candidates.append(Path(program_files_x86) / "foobar2000_v2" / "profile")
    
    for candidate in candidates:
        settings_dir = candidate / "foo_metadata_enhancer"
        settings_file = settings_dir / "settings.json"
        if settings_file.exists():
            return settings_dir
    
    return None


def _get_expected_settings_path() -> Optional[Path]:
    """获取预期的 settings.json 路径（用于创建）
    
    Returns:
        Optional[Path]: 预期的 settings.json 路径
    """
    script_dir = Path(__file__).parent.resolve()
    
    # 从脚本目录向上查找 components 目录
    # 脚本路径: .../components/foo_metadata_enhancer/worker/common/config_manager.py
    current = script_dir
    for _ in range(10):
        if current.name == "components":
            fb2k_root = current.parent
            profile_dir = fb2k_root / "profile"
            if profile_dir.exists():
                return profile_dir / "foo_metadata_enhancer" / "settings.json"
            return fb2k_root / "foo_metadata_enhancer" / "settings.json"
        parent = current.parent
        if parent == current:
            break
        current = parent
    
    return None


def _load_cpp_settings() -> Dict[str, Any]:
    """加载 C++ 保存的 settings.json
    
    Returns:
        Dict[str, Any]: settings.json 内容，未找到返回空字典
    """
    settings_dir = _find_foobar_profile()
    settings_file = None
    
    logger.debug("[config_manager.py::_load_cpp_settings] Searching for settings.json")
    
    if settings_dir:
        settings_file = settings_dir / "settings.json"
        logger.debug(f"[config_manager.py::_load_cpp_settings] Found settings_dir: {settings_dir}")
    else:
        expected_path = _get_expected_settings_path()
        if expected_path:
            settings_file = expected_path
            logger.debug(f"[config_manager.py::_load_cpp_settings] Using expected path: {expected_path}")
    
    if not settings_file:
        logger.debug("[config_manager.py::_load_cpp_settings] Could not determine settings.json path")
        return {}
    
    if not settings_file.exists():
        logger.debug(f"[config_manager.py::_load_cpp_settings] settings.json not found at {settings_file}")
        return {}
    
    try:
        with open(settings_file, 'r', encoding='utf-8') as f:
            data = json.load(f)
            logger.debug(f"[config_manager.py::_load_cpp_settings] Loaded settings.json from {settings_file}")
            logger.debug(f"[config_manager.py::_load_cpp_settings] provider = {data.get('provider', 'N/A')}")
            provider_configs = data.get('provider_configs', {})
            for prov_name, prov_config in provider_configs.items():
                logger.debug(f"[config_manager.py::_load_cpp_settings] {prov_name} selected_model = {prov_config.get('selected_model', 'N/A')}")
            return data
    except Exception as e:
        logger.debug(f"[config_manager.py::_load_cpp_settings] Failed to load settings.json: {e}")
        return {}


class ConfigManager:
    """配置管理器类，管理AI Worker的配置
    
    单例模式实现，从config.yaml加载配置，并合并C++保存的settings.json。
    
    Attributes:
        _instance: 单例实例
        _config: 配置字典
    """
    
    _instance = None
    _config = None
    
    def __new__(cls):
        """创建或返回单例实例
        
        Returns:
            ConfigManager: 配置管理器实例
        """
        if cls._instance is None:
            cls._instance = super().__new__(cls)
        return cls._instance
    
    def __init__(self):
        """初始化配置管理器，加载配置文件"""
        if self._config is None:
            self._load_config()
    
    def _load_config(self) -> None:
        """加载配置文件
        
        从config.yaml加载配置，然后合并C++保存的settings.json。
        """
        config_path = Path(__file__).parent.parent / "config.yaml"
        
        self._config = {
            "providers": {
                "default": "",
                "priority_order": ["openrouter", "zhipu", "gemini", "ollama"]
            },
            "worker": {
                "pool_size": 3,
                "auto_restart": True,
                "max_retries": 3,
                "batch_size": 30,
                "base_timeout_ms": 120000,
                "per_track_timeout_ms": 60000,
                "api_timeout_ms": 180000
            },
            "logging": {
                "level": "INFO",
                "max_file_size": 10485760,
                "backup_count": 5
            }
        }
        
        if config_path.exists():
            try:
                with open(config_path, 'r', encoding='utf-8') as f:
                    loaded = yaml.safe_load(f)
                    if loaded:
                        self._deep_update(self._config, loaded)
            except Exception as e:
                print(f"Warning: Failed to load config: {e}", file=sys.stderr)
        
        cpp_settings = _load_cpp_settings()
        if cpp_settings:
            self._merge_cpp_settings(cpp_settings)
    
    def _merge_cpp_settings(self, cpp_settings: Dict[str, Any]) -> None:
        """合并 C++ 保存的设置到配置
        
        重点合并 provider_configs 中的 selected_model 和 api_key。
        
        Args:
            cpp_settings: C++ 保存的设置字典
        """
        provider = cpp_settings.get("provider", "")
        if provider:
            self._config["providers"]["default"] = provider
            logger.debug(f"[config_manager.py::_merge_cpp_settings] C++ provider = {provider}")
        
        provider_configs = cpp_settings.get("provider_configs", {})
        if provider_configs:
            logger.debug("[config_manager.py::_merge_cpp_settings] Processing provider_configs")
            for prov_name, prov_config in provider_configs.items():
                logger.debug(f"[config_manager.py::_merge_cpp_settings] Checking provider: {prov_name}")
                if prov_name in self._config.get("providers", {}):
                    selected_model = prov_config.get("selected_model", "")
                    api_key = prov_config.get("api_key", "")
                    
                    logger.debug(f"[config_manager.py::_merge_cpp_settings] {prov_name} selected_model from C++ = '{selected_model}'")
                    
                    if selected_model:
                        self._config["providers"][prov_name]["selected_model"] = selected_model
                        logger.debug(f"[config_manager.py::_merge_cpp_settings] Set {prov_name} selected_model = {selected_model}")
                    
                    if api_key:
                        self._config["providers"][prov_name]["api_key"] = api_key
                        logger.debug(f"[config_manager.py::_merge_cpp_settings] {prov_name} api_key updated")
                else:
                    logger.debug(f"[config_manager.py::_merge_cpp_settings] Provider {prov_name} not found in config")
        
        ai_batch_size = cpp_settings.get("ai_batch_size")
        if ai_batch_size is not None:
            self._config["worker"]["batch_size"] = ai_batch_size
            logger.debug(f"[config_manager.py::_merge_cpp_settings] C++ ai_batch_size = {ai_batch_size}")
        
        taskqueue_batch_size = cpp_settings.get("taskqueue_batch_size")
        if taskqueue_batch_size is not None:
            self._config["worker"]["taskqueue_batch_size"] = taskqueue_batch_size
            logger.debug(f"[config_manager.py::_merge_cpp_settings] C++ taskqueue_batch_size = {taskqueue_batch_size}")
    
    def _deep_update(self, base: dict, override: dict) -> None:
        """深度更新字典
        
        递归地更新基础字典，保留嵌套结构。
        
        Args:
            base: 基础字典（将被修改）
            override: 覆盖字典
        """
        for key, value in override.items():
            if key in base and isinstance(base[key], dict) and isinstance(value, dict):
                self._deep_update(base[key], value)
            else:
                base[key] = value
    
    @property
    def config(self) -> Dict[str, Any]:
        """获取完整配置字典
        
        Returns:
            Dict[str, Any]: 配置字典
        """
        return self._config
    
    def get(self, key: str, default: Any = None) -> Any:
        """获取配置值（支持点分隔的键路径）
        
        Args:
            key: 配置键，支持点分隔（如"logging.level"）
            default: 默认值
        
        Returns:
            Any: 配置值或默认值
        """
        keys = key.split(".")
        value = self._config
        for k in keys:
            if isinstance(value, dict):
                value = value.get(k)
            else:
                return default
            if value is None:
                return default
        return value
    
    def get_worker_config(self) -> Dict[str, Any]:
        """获取Worker配置
        
        Returns:
            Dict[str, Any]: Worker配置字典
        """
        return self._config.get("worker", {})
    
    def get_logging_config(self) -> Dict[str, Any]:
        """获取日志配置
        
        Returns:
            Dict[str, Any]: 日志配置字典
        """
        return self._config.get("logging", {})
    
    def get_providers_config(self) -> Dict[str, Any]:
        """获取提供商配置
        
        Returns:
            Dict[str, Any]: 提供商配置字典
        """
        return self._config.get("providers", {})


def setup_logging(config: Optional[Dict[str, Any]] = None) -> logging.Logger:
    """设置日志系统
    
    配置文件和终端日志处理器。
    
    Args:
        config: 可选的配置字典
    
    Returns:
        logging.Logger: 配置好的日志器
    """
    log_config = config.get("logging", {}) if config else {}
    
    level_name = log_config.get("level", "INFO")
    level = getattr(logging, level_name.upper(), logging.INFO)
    
    max_file_size = log_config.get("max_file_size", 10 * 1024 * 1024)
    backup_count = log_config.get("backup_count", 5)
    
    script_dir = Path(__file__).parent.resolve()
    log_dir = script_dir.parent.parent / "logs"
    
    try:
        log_dir.mkdir(parents=True, exist_ok=True)
    except Exception as e:
        print(f"Warning: Failed to create log directory {log_dir}: {e}", file=sys.stderr)
        log_dir = Path.cwd() / "logs"
        try:
            log_dir.mkdir(parents=True, exist_ok=True)
        except Exception as e2:
            print(f"Warning: Failed to create fallback log directory {log_dir}: {e2}", file=sys.stderr)
    
    log_file = log_dir / "worker.log"
    
    print(f"AI Worker: Log file = {log_file}", file=sys.stderr)
    print(f"AI Worker: Log directory = {log_dir}", file=sys.stderr)
    
    root_logger = logging.getLogger()
    root_logger.setLevel(level)
    
    for handler in root_logger.handlers[:]:
        root_logger.removeHandler(handler)
    
    try:
        file_handler = logging.handlers.RotatingFileHandler(
            log_file,
            maxBytes=max_file_size,
            backupCount=backup_count,
            encoding='utf-8'
        )
        file_handler.setLevel(level)
        file_handler.setFormatter(logging.Formatter(
            '%(asctime)s [%(levelname)s] [Python] [%(filename)s::%(funcName)s] %(message)s'
        ))
        root_logger.addHandler(file_handler)
        print(f"AI Worker: File handler added successfully", file=sys.stderr)
    except Exception as e:
        print(f"Warning: Failed to create file handler: {e}", file=sys.stderr)
    
    console_handler = logging.StreamHandler(sys.stderr)
    console_handler.setLevel(level)
    console_handler.setFormatter(logging.Formatter(
        '%(asctime)s [%(levelname)s] [Python] [%(filename)s::%(funcName)s] %(message)s'
    ))
    root_logger.addHandler(console_handler)
    print(f"AI Worker: Console handler added successfully", file=sys.stderr)
    
    return logging.getLogger(__name__)


def get_config() -> ConfigManager:
    """获取配置管理器实例
    
    Returns:
        ConfigManager: 配置管理器实例
    """
    return ConfigManager()
