#pragma once

#include "../include/types.h"
#include <string>
#include <optional>

namespace ai_metadata {

/**
 * @brief 统一用户反馈接口
 *
 * 替代散落各处的 popup_message::g_show / console::print / Logger 调用组合。
 * 设计原则：
 *   - 正常流程结束 → console（不打扰用户）
 *   - 警告 / 可恢复错误 → popup（信息图标）
 *   - 错误 → 弹 IDD_ERROR 对话框（带错误分类、修复建议、View Log 按钮）
 *   - 危险操作前确认 → Yes/No popup（警告图标）
 *
 * 所有 error/warn 都会同时写入 Logger，保证日志可追溯。
 */
class Feedback {
public:
    // ==================== 简单通知（console only，不打扰） ====================

    /** 普通信息：仅输出到 fb2k console */
    static void info(const std::string& msg);

    /** 成功提示：仅输出到 fb2k console（带 [OK] 前缀） */
    static void success(const std::string& msg);

    // ==================== 弹窗通知（轻度打扰） ====================

    /** 警告弹窗：信息图标，单按钮 OK */
    static void warn(const std::string& msg, const std::string& title = "AI Metadata");

    // ==================== 错误对话框（IDD_ERROR） ====================

    /**
     * @brief 错误对话框（使用 IDD_ERROR 模板）
     *
     * 根据 category 自动填充：
     *   - 标题图标（错误/警告/信息）
     *   - 分类标签（如 "配置错误"、"网络错误"）
     *   - 修复建议（具体到操作步骤）
     *   - 推荐按钮（如 "Open Settings"、"Open Provider Dashboard"）
     *   - View Log 按钮（始终显示）
     *
     * @param message 一行错误摘要（用户最先看到）
     * @param detail  技术详情（多行，可选）
     * @param category 错误来源分类（决定图标、建议、按钮）
     * @param level    错误级别（默认 Error）
     * @return 用户点击的按钮：retry / view_log / cancel
     */
    enum class ErrorAction {
        Retry,      ///< 用户点击 Retry
        ViewLog,    ///< 用户点击 View Log（关闭后返回，调用方可继续处理）
        Cancel      ///< 用户点击 Cancel 或关闭对话框
    };
    static ErrorAction error(const std::string& message,
                             const std::string& detail,
                             ErrorCategory category = ErrorCategory::Unknown,
                             ErrorLevel level = ErrorLevel::Error);

    /**
     * @brief 从 ErrorInfo 触发错误对话框（便捷重载）
     */
    static ErrorAction error(const ErrorInfo& err);

    /**
     * @brief 简单错误弹窗（无分类，仅一行消息 + OK）
     *
     * 用于错误信息过于简单、不需要分类引导的场景。
     * 不弹 IDD_ERROR，直接 popup_message。
     */
    static void error_simple(const std::string& msg,
                             const std::string& title = "AI Metadata");

    // ==================== 确认对话框 ====================

    /** Yes/No 确认（问题图标） */
    static bool confirm(const std::string& msg,
                        const std::string& title = "AI Metadata");

    /** 危险操作确认（警告图标，默认聚焦 No） */
    static bool confirm_dangerous(const std::string& msg,
                                  const std::string& title = "AI Metadata");

    // ==================== 分类元信息（供 UI 调用） ====================

    /** 获取错误分类的中文显示名（如 "配置错误"） */
    static const char* category_display_name(ErrorCategory cat);

    /** 获取错误分类的修复建议（多行，含具体操作步骤） */
    static std::string category_fix_suggestion(ErrorCategory cat);

    /** 获取错误分类对应的日志路径提示（部分类别需要引导查看特定日志） */
    static std::string category_log_hint(ErrorCategory cat);
};

}  // namespace ai_metadata
