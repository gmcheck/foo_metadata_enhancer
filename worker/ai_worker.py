#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
AI Worker - IPC interface for foobar2000 AI Metadata Plugin
Implements 4-byte header framing protocol for stdin/stdout communication
"""

import sys
import os
import json
import struct
import time
import logging
import threading
import uuid
from datetime import datetime, timezone
from pathlib import Path
from typing import Dict, List, Optional, Any

SCRIPT_DIR = Path(__file__).parent.resolve()

# 进度上报与 stdout 互斥锁统一放到 ipc_utils 模块，
# 避免模块名 __main__ 导致其它模块无法 import。
from ipc_utils import (
    _stdout_write_lock,
    set_current_request_id,
    get_current_request_id,
    write_progress,
)

print("AI Worker: Starting import...", file=sys.stderr)
print(f"AI Worker: SCRIPT_DIR = {SCRIPT_DIR}", file=sys.stderr)
print(f"AI Worker: sys.path = {sys.path}", file=sys.stderr)

try:
    from common.config_manager import get_config, setup_logging
    print("AI Worker: config_manager imported successfully", file=sys.stderr)
except Exception as e:
    print(f"AI Worker: Failed to import config_manager: {e}", file=sys.stderr)
    import traceback
    traceback.print_exc(file=sys.stderr)
    sys.exit(1)

try:
    config = get_config()
    print("AI Worker: config loaded successfully", file=sys.stderr)
except Exception as e:
    print(f"AI Worker: Failed to load config: {e}", file=sys.stderr)
    import traceback
    traceback.print_exc(file=sys.stderr)
    sys.exit(1)

try:
    logger = setup_logging(config.config)
    print("AI Worker: logger setup successfully", file=sys.stderr)
except Exception as e:
    print(f"AI Worker: Failed to setup logger: {e}", file=sys.stderr)
    import traceback
    traceback.print_exc(file=sys.stderr)
    sys.exit(1)

# 全局 NormalizeStore 单例（延迟初始化）
# 管理 normalize_alias 表的 SQLite 访问（含 alias_key 归一化查询）
_normalize_store = None

def get_normalize_store():
    """延迟初始化并返回 NormalizeStore 单例

    db_path 与 C++ 端 CacheLayer 共享同一个 db 文件。
    路径: {fb2k_profile}/foo_metadata_enhancer/foo_metadata_enhancer.db
    注意: _find_foobar_profile() 返回的已经是 foo_metadata_enhancer 目录
    （包含 settings.json），所以直接在其下放 db 文件，不要再拼子目录。
    """
    global _normalize_store
    if _normalize_store is not None:
        return _normalize_store

    try:
        from common.config_manager import _find_foobar_profile, _get_expected_settings_path
        from db.normalize_store import NormalizeStore

        # _find_foobar_profile() 返回 foo_metadata_enhancer 目录（含 settings.json）
        profile_dir = _find_foobar_profile()
        if profile_dir is not None:
            db_dir = profile_dir
        else:
            # 回退：从 settings.json 预期路径推导
            expected = _get_expected_settings_path()
            if expected is not None:
                db_dir = expected.parent
            else:
                db_dir = SCRIPT_DIR

        db_dir.mkdir(parents=True, exist_ok=True)
        db_path = db_dir / "foo_metadata_enhancer.db"

        _normalize_store = NormalizeStore(str(db_path))
        logger.info(f"get_normalize_store: opened db at {db_path}")
        return _normalize_store
    except Exception as e:
        logger.error(f"get_normalize_store: failed to init NormalizeStore: {e}", exc_info=True)
        return None

try:
    from common.models import (
        IPCResponse,
        ScrapeResponseModel,
        ScrapeResultModel,
        ScrapeFieldModel,
        create_scrape_result,
        create_scrape_error_result,
        EnhanceResponseModel,
        EnhanceResultModel,
        create_enhance_result,
        create_enhance_error_result
    )
    print("AI Worker: Pydantic models imported successfully", file=sys.stderr)
except Exception as e:
    print(f"AI Worker: Failed to import Pydantic models: {e}", file=sys.stderr)
    import traceback
    traceback.print_exc(file=sys.stderr)


def read_message() -> Optional[Dict]:
    """从stdin读取帧格式消息（4字节大端序长度头）
    
    实现IPC协议的消息读取，包含详细的调试日志。
    
    Returns:
        Optional[Dict]: 解析后的JSON字典，EOF时返回None
    """
    try:
        logger.debug("=" * 80)
        logger.debug("STAGE 1: Python receiving message from C++")
        logger.debug("=" * 80)
        logger.debug("read_message: Waiting for message from stdin...")
        
        header = sys.stdin.buffer.read(4)
        if len(header) < 4:
            logger.info("read_message: EOF received (header incomplete)")
            return None
        
        length = struct.unpack('>I', header)[0]
        logger.debug(f"read_message: Received header, message length = {length} bytes")
        logger.debug(f"read_message: Header bytes: {header.hex()}")
        
        if length > 10 * 1024 * 1024:
            logger.error(f"Message too large: {length} bytes")
            return None
        
        data = b''
        remaining = length
        chunk_count = 0
        while remaining > 0:
            chunk = sys.stdin.buffer.read(min(remaining, 8192))
            if not chunk:
                break
            data += chunk
            remaining -= len(chunk)
            chunk_count += 1
            if chunk_count % 10 == 0:
                logger.debug(f"read_message: Reading progress: {len(data)}/{length} bytes ({chunk_count} chunks)")
        
        if len(data) != length:
            logger.error(f"Incomplete message: expected {length}, got {len(data)}")
            return None
        
        logger.debug(f"read_message: Successfully read {len(data)} bytes in {chunk_count} chunks")
        
        parsed = json.loads(data.decode('utf-8'))
        logger.info(f"read_message: Request ID = {parsed.get('id', 'unknown')}, Method = {parsed.get('method', 'unknown')}")
        
        json_str = json.dumps(parsed, ensure_ascii=False, indent=2)
        if len(json_str) > 5000:
            logger.debug(f"read_message: Full JSON ({len(json_str)} chars):\n{json_str}")
        else:
            logger.debug(f"read_message: Full JSON:\n{json_str}")
        
        return parsed
    
    except Exception as e:
        logger.error(f"Error reading message: {e}", exc_info=True)
        return None


def write_message(data: Dict) -> bool:
    """向stdout写入帧格式消息（4字节大端序长度头）
    
    实现IPC协议的消息写入，包含详细的调试日志。
    
    Args:
        data: 要发送的数据字典
    
    Returns:
        bool: 发送成功返回True
    """
    try:
        logger.debug("=" * 80)
        logger.debug("STAGE 3: Python sending response to C++")
        logger.debug("=" * 80)
        
        json_str = json.dumps(data, ensure_ascii=False)
        json_bytes = json_str.encode('utf-8')
        
        logger.info(f"write_message: Response ID = {data.get('id', 'unknown')}, success = {data.get('success', False)}, count = {data.get('count', 0)}")
        logger.debug(f"write_message: Writing {len(json_bytes)} bytes to stdout")
        logger.debug(f"write_message: Response JSON preview: {json_str[:1000]}...")
        
        header = struct.pack('>I', len(json_bytes))
        logger.debug(f"write_message: Header bytes: {header.hex()}")

        with _stdout_write_lock:
            sys.stdout.buffer.write(header)
            sys.stdout.buffer.write(json_bytes)
            sys.stdout.buffer.flush()

        logger.debug("write_message: Successfully wrote message")
        logger.debug("=" * 80)
        return True
    
    except Exception as e:
        logger.error(f"Error writing message: {e}", exc_info=True)
        return False


def create_response(request_id: str, success: bool, results: List[Dict] = None,
                   error: Dict = None, task_id: str = "") -> Dict:
    """创建响应消息 - 使用 Pydantic 验证
    
    Args:
        request_id: 请求ID
        success: 是否成功
        results: 结果列表（可选）
        error: 错误信息（可选）
        task_id: 任务ID（V8.1新增）
    
    Returns:
        Dict: 响应消息字典
    """
    try:
        pydantic_response = IPCResponse(
            id=request_id,
            success=success,
            results=results or [],
            error=error,
            task_id=task_id if task_id else None
        )
        response = pydantic_response.model_dump(exclude_none=True)
        
        if results is not None:
            response["count"] = len(results)
        
        logger.debug(f"create_response: id={request_id}, success={success}, "
                    f"results_count={len(results) if results else 0}")
        
        return response
    except Exception as e:
        logger.error(f"Pydantic validation failed in create_response: {e}")
        response = {
            "version": 1,
            "id": request_id,
            "success": success,
            "timestamp": datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")
        }
        if task_id:
            response["task_id"] = task_id
        if results is not None:
            response["results"] = results
            response["count"] = len(results)
        if error is not None:
            response["error"] = error
        return response


def create_error_response(request_id: str, code: str, message: str, 
                         retryable: bool = False, task_id: str = "") -> Dict:
    """创建错误响应
    
    Args:
        request_id: 请求ID
        code: 错误代码
        message: 错误消息
        retryable: 是否可重试
        task_id: 任务ID（V8.1新增）
    
    Returns:
        Dict: 错误响应字典
    """
    return create_response(
        request_id,
        success=False,
        error={
            "code": code,
            "message": message,
            "retryable": retryable
        },
        task_id=task_id
    )


def process_ping(request: Dict) -> Dict:
    """处理ping请求
    
    用于健康检查。
    
    Args:
        request: 请求字典
    
    Returns:
        Dict: pong响应
    """
    return create_response(request.get("id", ""), success=True, results=[{"pong": True}])


def process_shutdown(request: Dict) -> Dict:
    """处理shutdown请求
    
    用于关闭Worker进程。
    
    Args:
        request: 请求字典
    
    Returns:
        Dict: shutdown响应
    """
    return create_response(request.get("id", ""), success=True, results=[{"shutdown": True}])


def process_set_log_level(request: Dict) -> Dict:
    """动态更新日志级别
    
    Args:
        request: 请求字典，params.level 为日志级别字符串 (DEBUG/INFO/WARNING/ERROR)
    
    Returns:
        Dict: 响应字典
    """
    import logging
    
    request_id = request.get("id", str(uuid.uuid4()))
    params = request.get("params", {})
    level_name = params.get("level", "INFO").upper()
    level = getattr(logging, level_name, logging.INFO)
    
    root_logger = logging.getLogger()
    root_logger.setLevel(level)
    for handler in root_logger.handlers:
        handler.setLevel(level)
    
    logger.info(f"Log level updated to {level_name}")
    
    return create_response(request_id, success=True, results=[{"log_level": level_name}])


def process_test_api(request: Dict) -> Dict:
    """处理API测试请求

    发送简单测试消息验证API连接。

    Args:
        request: 请求字典，包含provider和model参数

    Returns:
        Dict: 测试结果响应
    """
    from ai.providers import AIProviderFactory

    request_id = request.get("id", str(uuid.uuid4()))
    params = request.get("params", {})
    provider = params.get("provider", "zhipu")
    model = params.get("model", "")
    request_provider_cfg = params.get("provider_cfg") or {}

    logger.info(f"process_test_api: Testing API for provider={provider}, model={model}")

    try:
        # 必须用 UI 传入的 provider/model 创建实例，而不是 config 里的 default。
        # 否则用户在 UI 切换 provider 后 test_api 仍然测的是旧 provider。
        providers_cfg = config.config.get("providers", {})
        provider_cfg = providers_cfg.get(provider, {})
        if not provider_cfg and not request_provider_cfg:
            return create_response(
                request_id,
                success=False,
                results=[{
                    "provider": provider,
                    "model": model,
                    "status": "failed",
                    "message": f"Provider '{provider}' not found in config."
                }]
            )

        # 复制一份避免污染原配置；请求中的 provider_cfg 覆盖本地配置（支持 custom 热测试）
        provider_cfg = dict(provider_cfg or {})
        if isinstance(request_provider_cfg, dict) and request_provider_cfg:
            for key, value in request_provider_cfg.items():
                if key == "extra_params" and isinstance(value, dict):
                    merged_extra = dict(provider_cfg.get("extra_params") or {})
                    merged_extra.update(value)
                    provider_cfg["extra_params"] = merged_extra
                elif value is not None and value != "":
                    provider_cfg[key] = value

        # 兼容：api_format 既可在顶层，也可在 extra_params 中
        api_format = provider_cfg.get("api_format") or (provider_cfg.get("extra_params") or {}).get("api_format")
        if api_format:
            provider_cfg["api_format"] = api_format
            extra_params = dict(provider_cfg.get("extra_params") or {})
            extra_params["api_format"] = api_format
            provider_cfg["extra_params"] = extra_params

        provider_cfg["timeout_ms"] = config.config.get("worker", {}).get("api_timeout_ms", 60000)
        provider_cfg["max_retries"] = 1  # 测试只跑一次
        if model:
            provider_cfg["selected_model"] = model

        logger.info(
            f"process_test_api: provider_cfg keys={list(provider_cfg.keys())}, "
            f"base_url={provider_cfg.get('base_url', '')!r}, "
            f"api_format={provider_cfg.get('api_format', '')!r}, "
            f"selected_model={provider_cfg.get('selected_model', '')!r}"
        )

        provider_instance = AIProviderFactory.create_from_config(provider_cfg, provider)
        logger.info(f"process_test_api: Created provider instance: {provider_instance}")

        if not provider_instance:
            return create_response(
                request_id,
                success=False,
                results=[{
                    "provider": provider,
                    "model": model,
                    "status": "failed",
                    "message": "Failed to create provider instance."
                }]
            )

        test_messages = [
            {"role": "system", "content": "You are a music metadata expert. Respond with a JSON object containing a 'genre' field."},
            {"role": "user", "content": "What is the genre of 'Test Song' by 'Test Artist'?"}
        ]

        response = provider_instance.chat_completion_json(test_messages, temperature=0.3)
        
        if response.success:
            try:
                result = json.loads(response.content) if response.content else {}
                genre = result.get("genre", "unknown")
            except:
                genre = "unknown"
            
            return create_response(
                request_id, 
                success=True, 
                results=[{
                    "provider": provider,
                    "model": response.model,
                    "status": "connected",
                    "test_genre": genre,
                    "tokens_used": response.tokens_used,
                    "message": f"API connection successful. Test genre: {genre}"
                }]
            )
        else:
            error_msg = response.error or "Unknown error"
            return create_response(
                request_id,
                success=False,
                results=[{
                    "provider": provider,
                    "model": response.model or model,
                    "status": "failed",
                    "message": error_msg
                }]
            )
    
    except Exception as e:
        logger.error(f"Error testing API: {e}", exc_info=True)
        return create_response(
            request_id,
            success=False,
            results=[{
                "provider": provider,
                "model": model,
                "status": "error",
                "message": str(e)
            }]
        )


def process_scrape(request: Dict) -> Dict:
    """处理scrape请求 - 使用 Pydantic 验证
    
    阶段一：基础元数据刮削和纠正
    
    Args:
        request: 请求字典，包含音轨列表和刮削选项
    
    Returns:
        Dict: 响应字典，包含刮削结果
    """
    from core import ScrapeProcessor
    from abort_checker import set_abort_task, clear_abort_task
    
    request_id = request.get("id", str(uuid.uuid4()))
    task_id = request.get("task_id", request_id)
    params = request.get("params", {})
    tracks = params.get("tracks", [])
    options = params.get("options", {})
    abort_dir = params.get("abort_dir", "")
    
    logger.info(f"process_scrape: Request ID = {request_id}, Task ID = {task_id}, tracks = {len(tracks)}")
    
    if abort_dir:
        set_abort_task(task_id, abort_dir)
        logger.debug(f"process_scrape: Abort checker enabled, task_id={task_id}")
    
    if not tracks:
        clear_abort_task()
        return create_error_response(request_id, "INVALID_JSON", "No tracks provided")
    
    worker_config = config.config.get("worker", {})
    base_timeout_ms = worker_config.get("base_timeout_ms", 120000)
    per_track_timeout_ms = worker_config.get("per_track_timeout_ms", 60000)
    dynamic_timeout_ms = base_timeout_ms + len(tracks) * per_track_timeout_ms
    logger.debug(f"process_scrape: Dynamic timeout = {dynamic_timeout_ms}ms "
                f"(base={base_timeout_ms} + {len(tracks)} * {per_track_timeout_ms})")
    
    options["_timeout_ms"] = dynamic_timeout_ms
    
    try:
        processor = ScrapeProcessor(config.config)
        results = processor.process_batch(tracks, options)
        
        validated_results = []
        for i, result in enumerate(results):
            if isinstance(result, dict):
                if "scraped_fields" in result and result["scraped_fields"]:
                    logger.debug(f"process_scrape: Track {i} has scraped_fields: "
                               f"{json.dumps(result['scraped_fields'], ensure_ascii=False)[:200]}")
                validated_results.append(result)
            else:
                logger.warning(f"process_scrape: Result {i} is not a dict, converting")
                validated_results.append(result if isinstance(result, dict) else {})
        
        logger.info(f"process_scrape: Returning {len(validated_results)} validated results")
        
        try:
            pydantic_response = ScrapeResponseModel(
                id=request_id,
                success=True,
                results=[
                    ScrapeResultModel(
                        track_id=r.get("track_id", ""),
                        success=r.get("success", False),
                        scraped_fields={
                            k: ScrapeFieldModel(**v) 
                            for k, v in r.get("scraped_fields", {}).items()
                        },
                        release_source=r.get("release_source", "ai"),
                        error=r.get("error")
                    )
                    for r in validated_results
                ]
            )
            response_dict = pydantic_response.to_ipc_dict()
            logger.debug(f"process_scrape: Pydantic validation passed, response keys: {list(response_dict.keys())}")
        except Exception as pydantic_error:
            logger.error(f"process_scrape: Pydantic validation failed: {pydantic_error}", exc_info=True)
            response_dict = create_response(request_id, success=True, results=validated_results)
        
        clear_abort_task()
        return response_dict
    
    except Exception as e:
        logger.error(f"Error in scrape: {e}", exc_info=True)
        clear_abort_task()
        return create_error_response(request_id, "SCRAPE_ERROR", str(e))


def process_enhance(request: Dict) -> Dict:
    """处理enhance请求 - 使用 Pydantic 验证
    
    阶段二：元数据增强（翻译、流派分类、版本识别）
    
    Args:
        request: 请求字典，包含音轨列表和增强选项
    
    Returns:
        Dict: 响应字典，包含增强结果
    """
    from core import EnhanceProcessor
    from abort_checker import set_abort_task, clear_abort_task
    
    request_id = request.get("id", str(uuid.uuid4()))
    task_id = request.get("task_id", request_id)
    params = request.get("params", {})
    tracks = params.get("tracks", [])
    options = params.get("options", {})
    abort_dir = params.get("abort_dir", "")
    
    logger.info(f"process_enhance: Request ID = {request_id}, Task ID = {task_id}, tracks = {len(tracks)}")
    
    if abort_dir:
        set_abort_task(task_id, abort_dir)
        logger.debug(f"process_enhance: Abort checker enabled, task_id={task_id}")
    
    if not tracks:
        clear_abort_task()
        return create_error_response(request_id, "INVALID_JSON", "No tracks provided")
    
    worker_config = config.config.get("worker", {})
    base_timeout_ms = worker_config.get("base_timeout_ms", 120000)
    per_track_timeout_ms = worker_config.get("per_track_timeout_ms", 60000)
    dynamic_timeout_ms = base_timeout_ms + len(tracks) * per_track_timeout_ms
    logger.debug(f"process_enhance: Dynamic timeout = {dynamic_timeout_ms}ms "
                f"(base={base_timeout_ms} + {len(tracks)} * {per_track_timeout_ms})")
    
    options["_timeout_ms"] = dynamic_timeout_ms
    
    try:
        processor = EnhanceProcessor(config.config)
        results = processor.process_batch(tracks, options)
        
        validated_results = []
        for i, result in enumerate(results):
            if isinstance(result, dict):
                validated_results.append(result)
            else:
                logger.warning(f"process_enhance: Result {i} is not a dict, converting")
                validated_results.append(result if isinstance(result, dict) else {})
        
        logger.info(f"process_enhance: Returning {len(validated_results)} validated results")
        
        try:
            pydantic_response = EnhanceResponseModel(
                id=request_id,
                success=True,
                results=[
                    EnhanceResultModel(
                        track_id=r.get("track_id", ""),
                        success=r.get("success", False),
                        title_zh=r.get("title_zh", ""),
                        album_zh=r.get("album_zh", ""),
                        artist_zh=r.get("artist_zh", ""),
                        translation_confidence=r.get("translation_confidence", 0.0),
                        model=r.get("model", ""),
                        model_type=r.get("model_type", ""),
                        tokens_used=r.get("tokens_used", 0),
                        error=r.get("error")
                    )
                    for r in validated_results
                ]
            )
            response_dict = pydantic_response.to_ipc_dict()
            logger.debug(f"process_enhance: Pydantic validation passed, response keys: {list(response_dict.keys())}")
        except Exception as pydantic_error:
            logger.error(f"process_enhance: Pydantic validation failed: {pydantic_error}", exc_info=True)
            response_dict = create_response(request_id, success=True, results=validated_results)
        
        clear_abort_task()
        return response_dict
    
    except Exception as e:
        logger.error(f"Error in enhance: {e}", exc_info=True)
        clear_abort_task()
        return create_error_response(request_id, "ENHANCE_ERROR", str(e))


def process_normalize(request: Dict) -> Dict:
    """处理 normalize 请求 - 元数据实体归一化

    接收一批未知 alias 及其上下文 examples，调用 AI 推断哪些 alias 属于同一实体，
    返回 groups（已分组）+ uncertain（无法判定）。

    请求结构：
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

    响应结构：
        {
            "success": True,
            "result": {
                "groups": [{"canonical_name": "...", "confidence": 0.99, "aliases": [...], "reason": "..."}],
                "uncertain": [{"alias": "...", "reason": "..."}]
            }
        }
    """
    from core import NormalizeProcessor

    request_id = request.get("id", str(uuid.uuid4()))
    task_id = request.get("task_id", request_id)
    params = request.get("params", {})
    field = params.get("field", "artist")
    candidates = params.get("candidates", [])
    known_groups = params.get("known_groups", [])
    track_values = params.get("track_values", [])

    logger.info(
        f"process_normalize: Request ID = {request_id}, Task ID = {task_id}, "
        f"field = {field}, candidates = {len(candidates)}, "
        f"known_groups = {len(known_groups)}, track_values = {len(track_values)}"
    )

    # candidates 和 known_groups 都为空时才算无效请求。
    # candidates 为空但 known_groups 非空是合法场景：所有 alias 都被 SQLite 命中，
    # 此时 Python 仍需根据 known_groups + track_values 构造 track_updates 返回给 C++。
    if not candidates and not known_groups:
        return create_error_response(request_id, "INVALID_JSON", "No candidates and known_groups provided", task_id=task_id)

    try:
        processor = NormalizeProcessor(
            config.config,
            normalize_store=get_normalize_store()
        )
        result = processor.process(request)

        # NormalizeProcessor 返回的 dict 已包含 id/task_id/success/result
        if not result.get("success", False):
            err = result.get("error", {})
            return create_error_response(
                request_id,
                err.get("code", "NORMALIZE_ERROR"),
                err.get("message", "Unknown normalize error"),
                task_id=task_id,
            )

        # 包装为 IPC 响应格式
        return create_response(
            request_id,
            success=True,
            results=[result["result"]],
            task_id=task_id,
        )

    except Exception as e:
        logger.error(f"Error in normalize: {e}", exc_info=True)
        return create_error_response(request_id, "NORMALIZE_ERROR", str(e), task_id=task_id)


def process_save_normalize_aliases(request: Dict) -> Dict:
    """处理 save_normalize_aliases 请求 - 用户确认后持久化 alias 映射

    由 C++ 端在用户于 Normalize UI 确认后调用，把选中的 groups 持久化到
    normalize_alias 表（Python 端管理）。后续 normalize 调用会通过
    NormalizeStore.get_aliases 命中这些条目，跳过 AI 调用。

    请求结构：
        {
            "method": "save_normalize_aliases",
            "params": {
                "field": "artist",
                "aliases": [
                    {
                        "alias_name": "华仔",           # 原始写法
                        "canonical_name": "刘德华",
                        "source": "ai",                 # 可选，默认 "ai"
                        "confidence": 0.95,             # 可选，默认 1.0
                        "confirmed": true,              # 可选，默认 true
                        "reason": "..."                 # 可选，默认 ""
                    },
                    ...
                ]
            }
        }

    响应结构：
        {
            "success": True,
            "results": [{"saved": N}]     # N = 写入条目数
        }
    """
    request_id = request.get("id", str(uuid.uuid4()))
    task_id = request.get("task_id", request_id)
    params = request.get("params", {})
    field = params.get("field", "artist")
    aliases = params.get("aliases", [])

    logger.info(
        f"process_save_normalize_aliases: Request ID = {request_id}, "
        f"field = {field}, aliases = {len(aliases)}"
    )

    if not aliases:
        return create_response(
            request_id,
            success=True,
            results=[{"saved": 0}],
            task_id=task_id,
        )

    try:
        store = get_normalize_store()
        if store is None:
            return create_error_response(
                request_id,
                "STORE_UNAVAILABLE",
                "NormalizeStore not initialized",
                task_id=task_id,
            )

        # 给每条加上 field（C++ 端可能省略）
        entries = []
        for a in aliases:
            entries.append({
                "field": field,
                "alias_name": a.get("alias_name", ""),
                "canonical_name": a.get("canonical_name", ""),
                "source": a.get("source", "ai"),
                "confidence": a.get("confidence", 1.0),
                "confirmed": a.get("confirmed", True),
                "reason": a.get("reason", ""),
            })

        ok = store.upsert_aliases(entries)
        if not ok:
            return create_error_response(
                request_id,
                "STORE_WRITE_ERROR",
                "Failed to upsert aliases",
                task_id=task_id,
            )

        return create_response(
            request_id,
            success=True,
            results=[{"saved": len(entries)}],
            task_id=task_id,
        )
    except Exception as e:
        logger.error(f"Error in save_normalize_aliases: {e}", exc_info=True)
        return create_error_response(
            request_id,
            "SAVE_ALIASES_ERROR",
            str(e),
            task_id=task_id,
        )


def handle_request(request: Dict) -> Optional[Dict]:
    """处理单个请求
    
    根据method字段路由到对应的处理函数。
    
    Args:
        request: 请求字典
    
    Returns:
        Optional[Dict]: 响应字典，shutdown请求返回None
    """
    method = request.get("method", "")
    request_id = request.get("id", str(uuid.uuid4()))
    task_id = request.get("task_id", "")

    set_current_request_id(request_id)
    logger.info(f"Handling request: {method} (id: {request_id}, task_id: {task_id})")
    
    if method == "ping":
        return process_ping(request)
    elif method == "shutdown":
        response = process_shutdown(request)
        write_message(response)
        return None
    elif method == "test_api":
        return process_test_api(request)
    elif method == "scrape":
        return process_scrape(request)
    elif method == "enhance":
        return process_enhance(request)
    elif method == "normalize":
        return process_normalize(request)
    elif method == "save_normalize_aliases":
        return process_save_normalize_aliases(request)
    elif method == "set_log_level":
        return process_set_log_level(request)
    else:
        return create_error_response(request_id, "INVALID_JSON", f"Unknown method: {method}", task_id=task_id)


def main():
    """主Worker循环
    
    持续读取请求并处理，直到收到EOF或shutdown请求。
    """
    print("AI Worker: Starting main function", file=sys.stderr)
    print(f"AI Worker: Script directory = {SCRIPT_DIR}", file=sys.stderr)
    
    logger.info("AI Worker started")
    logger.info(f"Config loaded from: {SCRIPT_DIR / 'config.yaml'}")
    logger.info(f"Log level: {config.get('logging.level', 'INFO')}")
    
    os.chdir(SCRIPT_DIR)
    
    print("AI Worker: Entering main loop", file=sys.stderr)
    
    while True:
        try:
            print("AI Worker: Waiting for message...", file=sys.stderr)
            request = read_message()
            
            if request is None:
                logger.info("Received EOF, exiting")
                break
            
            response = handle_request(request)
            
            if response is None:
                break
            
            write_message(response)
        
        except KeyboardInterrupt:
            logger.info("Received interrupt, exiting")
            break
        
        except Exception as e:
            logger.error(f"Unexpected error: {e}", exc_info=True)
            break
    
    logger.info("AI Worker stopped")


if __name__ == "__main__":
    main()
