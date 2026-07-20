"""IPC 工具模块：Python Worker 与 C++ 之间的进度上报。

为什么需要独立模块：
    ai_worker.py 作为 __main__ 运行，模块名是 __main__ 而不是 ai_worker。
    其它模块（scrape_processor.py、resolver.py）若用 `from ai_worker import write_progress`
    会触发 ImportError，因为 ai_worker 模块未被 Python 加载（只有 __main__）。

    把 write_progress 放到本独立模块，所有模块都从 ipc_utils 导入即可正确共享状态。
"""
import json
import logging
import struct
import sys
import threading

logger = logging.getLogger(__name__)

# stdout 写入锁：write_message 和 write_progress 都写 sys.stdout.buffer，
# 心跳线程与主线程并发写会破坏 4 字节长度头帧格式，必须互斥。
_stdout_write_lock = threading.Lock()

# 当前活跃请求 ID（供 write_progress 使用）
_current_request_id: str = ""


def set_current_request_id(request_id: str) -> None:
    """记录当前正在处理的请求 ID，供进度上报使用"""
    global _current_request_id
    _current_request_id = request_id


def get_current_request_id() -> str:
    return _current_request_id


def write_progress(progress: float, message: str) -> None:
    """向 C++ 端发送进度消息（不删除 pending request，只刷新 send_time）。

    用于长耗时操作（如 AI 调用）期间，让 C++ 端知道 Python 仍在工作，
    避免触发 max_silence_time_ms 误判导致 worker 被强制重启。

    线程安全：通过 _stdout_write_lock 与 write_message 互斥。

    Args:
        progress: 0.0 ~ 1.0 进度比例
        message: 人类可读的进度描述
    """
    if not _current_request_id:
        return
    try:
        data = {
            "id": _current_request_id,
            "type": "progress",
            "progress": float(progress),
            "message": str(message),
        }
        json_bytes = json.dumps(data, ensure_ascii=False).encode('utf-8')
        header = struct.pack('>I', len(json_bytes))
        with _stdout_write_lock:
            sys.stdout.buffer.write(header)
            sys.stdout.buffer.write(json_bytes)
            sys.stdout.buffer.flush()
        logger.info(f"write_progress: sent, id={_current_request_id}, progress={progress}, msg={message}")
    except Exception as e:
        logger.error(f"write_progress failed: {e}", exc_info=True)


def acquire_stdout_lock():
    """获取 stdout 写入锁（供 write_message 函数使用）"""
    return _stdout_write_lock.acquire()


def release_stdout_lock():
    """释放 stdout 写入锁（供 write_message 函数使用）"""
    _stdout_write_lock.release()
