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
import uuid
from datetime import datetime, timezone
from pathlib import Path
from typing import Dict, List, Optional, Any

SCRIPT_DIR = Path(__file__).parent.resolve()

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

try:
    from common.models import (
        IPCResponse,
        Stage1ScrapingResponseModel,
        Stage1ScrapingResultModel,
        Stage1ScrapedFieldModel,
        create_stage1_scraping_result,
        create_stage1_error_result,
        Stage2EnhancementResponseModel,
        Stage2EnhancementResultModel,
        create_stage2_enhancement_result,
        create_stage2_error_result
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


def process_test_api(request: Dict) -> Dict:
    """处理API测试请求
    
    发送简单测试消息验证API连接。
    
    Args:
        request: 请求字典，包含provider和model参数
    
    Returns:
        Dict: 测试结果响应
    """
    from ai.adapter import ModelAdapter
    
    request_id = request.get("id", str(uuid.uuid4()))
    params = request.get("params", {})
    provider = params.get("provider", "zhipu")
    model = params.get("model", "")
    
    logger.info(f"process_test_api: Testing API for provider={provider}, model={model}")
    
    try:
        adapter = ModelAdapter(config.config)
        
        if not adapter.provider:
            return create_response(
                request_id,
                success=False,
                results=[{
                    "provider": provider,
                    "model": model,
                    "status": "failed",
                    "message": "No AI provider configured. Check config.yaml."
                }]
            )
        
        test_messages = [
            {"role": "system", "content": "You are a music metadata expert. Respond with a JSON object containing a 'genre' field."},
            {"role": "user", "content": "What is the genre of 'Test Song' by 'Test Artist'?"}
        ]
        
        response = adapter.provider.chat_completion_json(test_messages, temperature=0.3)
        
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


def process_stage1_scrape(request: Dict) -> Dict:
    """处理stage1_scrape请求 - 使用 Pydantic 验证
    
    阶段一：基础元数据刮削和纠正
    
    Args:
        request: 请求字典，包含音轨列表和刮削选项
    
    Returns:
        Dict: 响应字典，包含刮削结果
    """
    from core import Stage1Processor
    from abort_checker import set_abort_task, clear_abort_task
    
    request_id = request.get("id", str(uuid.uuid4()))
    task_id = request.get("task_id", request_id)
    params = request.get("params", {})
    tracks = params.get("tracks", [])
    options = params.get("options", {})
    abort_dir = params.get("abort_dir", "")
    
    logger.info(f"process_stage1_scrape: Request ID = {request_id}, Task ID = {task_id}, tracks = {len(tracks)}")
    
    if abort_dir:
        set_abort_task(task_id, abort_dir)
        logger.debug(f"process_stage1_scrape: Abort checker enabled, task_id={task_id}")
    
    if not tracks:
        clear_abort_task()
        return create_error_response(request_id, "INVALID_JSON", "No tracks provided")
    
    worker_config = config.config.get("worker", {})
    base_timeout_ms = worker_config.get("base_timeout_ms", 120000)
    per_track_timeout_ms = worker_config.get("per_track_timeout_ms", 60000)
    dynamic_timeout_ms = base_timeout_ms + len(tracks) * per_track_timeout_ms
    logger.debug(f"process_stage1_scrape: Dynamic timeout = {dynamic_timeout_ms}ms "
                f"(base={base_timeout_ms} + {len(tracks)} * {per_track_timeout_ms})")
    
    options["_timeout_ms"] = dynamic_timeout_ms
    
    try:
        processor = Stage1Processor(config.config)
        results = processor.process_batch(tracks, options)
        
        validated_results = []
        for i, result in enumerate(results):
            if isinstance(result, dict):
                if "scraped_fields" in result and result["scraped_fields"]:
                    logger.debug(f"process_stage1_scrape: Track {i} has scraped_fields: "
                               f"{json.dumps(result['scraped_fields'], ensure_ascii=False)[:200]}")
                validated_results.append(result)
            else:
                logger.warning(f"process_stage1_scrape: Result {i} is not a dict, converting")
                validated_results.append(result if isinstance(result, dict) else {})
        
        logger.info(f"process_stage1_scrape: Returning {len(validated_results)} validated results")
        
        try:
            pydantic_response = Stage1ScrapingResponseModel(
                id=request_id,
                success=True,
                results=[
                    Stage1ScrapingResultModel(
                        track_id=r.get("track_id", ""),
                        success=r.get("success", False),
                        scraped_fields={
                            k: Stage1ScrapedFieldModel(**v) 
                            for k, v in r.get("scraped_fields", {}).items()
                        },
                        release_source=r.get("release_source", "ai"),
                        error=r.get("error")
                    )
                    for r in validated_results
                ]
            )
            response_dict = pydantic_response.to_ipc_dict()
            logger.debug(f"process_stage1_scrape: Pydantic validation passed, response keys: {list(response_dict.keys())}")
        except Exception as pydantic_error:
            logger.error(f"process_stage1_scrape: Pydantic validation failed: {pydantic_error}", exc_info=True)
            response_dict = create_response(request_id, success=True, results=validated_results)
        
        clear_abort_task()
        return response_dict
    
    except Exception as e:
        logger.error(f"Error in stage1_scrape: {e}", exc_info=True)
        clear_abort_task()
        return create_error_response(request_id, "SCRAPE_ERROR", str(e))


def process_stage2_enhance(request: Dict) -> Dict:
    """处理stage2_enhance请求 - 使用 Pydantic 验证
    
    阶段二：元数据增强（翻译、流派分类、版本识别）
    
    Args:
        request: 请求字典，包含音轨列表和增强选项
    
    Returns:
        Dict: 响应字典，包含增强结果
    """
    from core import Stage2Processor
    from abort_checker import set_abort_task, clear_abort_task
    
    request_id = request.get("id", str(uuid.uuid4()))
    task_id = request.get("task_id", request_id)
    params = request.get("params", {})
    tracks = params.get("tracks", [])
    options = params.get("options", {})
    abort_dir = params.get("abort_dir", "")
    
    logger.info(f"process_stage2_enhance: Request ID = {request_id}, Task ID = {task_id}, tracks = {len(tracks)}")
    
    if abort_dir:
        set_abort_task(task_id, abort_dir)
        logger.debug(f"process_stage2_enhance: Abort checker enabled, task_id={task_id}")
    
    if not tracks:
        clear_abort_task()
        return create_error_response(request_id, "INVALID_JSON", "No tracks provided")
    
    worker_config = config.config.get("worker", {})
    base_timeout_ms = worker_config.get("base_timeout_ms", 120000)
    per_track_timeout_ms = worker_config.get("per_track_timeout_ms", 60000)
    dynamic_timeout_ms = base_timeout_ms + len(tracks) * per_track_timeout_ms
    logger.debug(f"process_stage2_enhance: Dynamic timeout = {dynamic_timeout_ms}ms "
                f"(base={base_timeout_ms} + {len(tracks)} * {per_track_timeout_ms})")
    
    options["_timeout_ms"] = dynamic_timeout_ms
    
    try:
        processor = Stage2Processor(config.config)
        results = processor.process_batch(tracks, options)
        
        validated_results = []
        for i, result in enumerate(results):
            if isinstance(result, dict):
                validated_results.append(result)
            else:
                logger.warning(f"process_stage2_enhance: Result {i} is not a dict, converting")
                validated_results.append(result if isinstance(result, dict) else {})
        
        logger.info(f"process_stage2_enhance: Returning {len(validated_results)} validated results")
        
        try:
            pydantic_response = Stage2EnhancementResponseModel(
                id=request_id,
                success=True,
                results=[
                    Stage2EnhancementResultModel(
                        track_id=r.get("track_id", ""),
                        success=r.get("success", False),
                        title_zh=r.get("title_zh", ""),
                        album_zh=r.get("album_zh", ""),
                        artist_zh=r.get("artist_zh", ""),
                        translation_confidence=r.get("translation_confidence", 0.0),
                        genre_value=r.get("genre_value", ""),
                        genre_confidence=r.get("genre_confidence", 0.0),
                        edition_value=r.get("edition_value", ""),
                        edition_confidence=r.get("edition_confidence", 0.0),
                        model=r.get("model", ""),
                        model_type=r.get("model_type", ""),
                        tokens_used=r.get("tokens_used", 0),
                        error=r.get("error")
                    )
                    for r in validated_results
                ]
            )
            response_dict = pydantic_response.to_ipc_dict()
            logger.debug(f"process_stage2_enhance: Pydantic validation passed, response keys: {list(response_dict.keys())}")
        except Exception as pydantic_error:
            logger.error(f"process_stage2_enhance: Pydantic validation failed: {pydantic_error}", exc_info=True)
            response_dict = create_response(request_id, success=True, results=validated_results)
        
        clear_abort_task()
        return response_dict
    
    except Exception as e:
        logger.error(f"Error in stage2_enhance: {e}", exc_info=True)
        clear_abort_task()
        return create_error_response(request_id, "ENHANCE_ERROR", str(e))


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
    
    logger.info(f"Handling request: {method} (id: {request_id}, task_id: {task_id})")
    
    if method == "ping":
        return process_ping(request)
    elif method == "shutdown":
        response = process_shutdown(request)
        write_message(response)
        return None
    elif method == "test_api":
        return process_test_api(request)
    elif method == "stage1_scrape":
        return process_stage1_scrape(request)
    elif method == "stage2_enhance":
        return process_stage2_enhance(request)
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
