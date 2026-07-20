# TODO

> 本文件记录 foo_metadata_enhancer 插件尚未完成的改进项。
> 已完成 / 已取消的项不在此列。

---

## P2 - 多字段 Normalize 真正支持 / 或禁用多选

**现状**：`NormalizeFieldDialog::DoInitDialog` 中 UI 是多选 checkbox，但 artist 之外的字段（album_artist / album / genre / composer / label）都标 `(coming soon)` 并初始 unchecked。

**相关位置**：

- [confirm_dialog.cpp:1311](plugin/confirm_dialog.cpp#L1311) - `NormalizeFieldDialog::DoInitDialog`
- Python 侧 `worker/core/normalize_processor.py` 仅实现 artist 字段的 alias→canonical 映射

**未完成原因**：其他字段的归一化规则未设计，alias 表结构与 prompt 模板未扩展。

**处理方案（二选一）**：

1. 改成单选 radio（仅 artist 可选），UI 与后端能力对齐
2. 补齐其他字段的归一化规则（alias 表 schema、prompt 模板、Python 处理器扩展）

---

## P2 - 预热 AI Core（on_main_window_loaded）

**现状**：搜索 `on_main_window_loaded` / `init_session` 在项目中无任何匹配。AI Core 仍是用户首次点击菜单时懒加载（`ensure_ai_core_initialized`），首次操作会有几秒 Python worker 启动延迟。

**未完成原因**：未实现 fb2k 的 `initquit` / `playback_stream_callback` 钩子。

**处理方案**：实现 `service_impl_t<initquit>`，在 fb2k 启动后异步预热 AI Core（启动 Python worker 进程、加载配置、初始化 SQLite）。

---

## P2 - Rollback 菜单动态 disable

**现状**：[menu_handler.cpp:929](plugin/menu_handler.cpp#L929) `MenuNodeCommand` 的 `m_enabled` 是构造时传入的固定 bool，不查询快照状态。

**未完成原因**：`get_display_data` 在菜单弹出时调用，理论上可在此查询 `BackupManager::has_snapshot(track_ids)` 动态设置 `FLAG_DISABLED`，但当前实现未接入。

**处理方案**：

1. 在 `MenuNodeCommand` 增加 `std::function<bool(metadb_handle_list_cref)>` 动态判定器
2. Rollback 节点传入快照查询函数
3. `get_display_data` 调用判定器决定 `FLAG_DISABLED`

---

## P3 - Auto chain 阶段可配置

**现状**：[menu_handler.cpp:1301](plugin/menu_handler.cpp#L1301) `chain_to_enhance=true` 在 `scrape_and_enhance` 菜单中硬编码，配置页没有对应开关。

**未完成原因**：当前提供两个独立菜单（Scrape / Enhance）+ 一个组合菜单（Scrape & Enhance Auto），用户通过菜单选择已能实现等价效果；可配置化收益不明显。

**处理方案（如需实施）**：

1. `EnhancementOptions` 增加 `auto_chain_after_scrape` 字段
2. 配置页 Processing 加 checkbox
3. `scrape_and_enhance` 菜单根据配置决定是否自动触发 enhance

---

## 备注：已取消的项

- **P0 API Key DPAPI 加密** - 本地使用场景，无需加密
- **P3 配置导入/导出** - 不需要

## 备注：已完成的项

- P0 统一错误反馈（IDD_ERROR + View Log 按钮 + ErrorCategory 分类）
- P0 Confirm 对话框改 LVS_OWNERDATA 虚拟列表（三个对话框全部虚拟化）
- P1 菜单重组（Maintenance 子菜单 + Normalize 保留原名）
- P1 配置页重组为 5 页（General / AI Provider / Processing / Cache & Logs / Advanced）
- P1 启用 IDD_COMPLETION 完成统计对话框
- P1 feedback helper 替换残留 popup（preferences_page.cpp 中 7 处 popup_message::g_show 已全部切换为 Feedback::success/warn/error）
