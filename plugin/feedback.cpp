// fb2k SDK 必须第一个 include（它定义了 cpp_quote 等宏，影响后续 windows.h 解析）
#include <foobar2000/SDK/foobar2000.h>

#include "../core/feedback.h"
#include "../core/logger.h"
#include "resource.h"
#include "preferences_page.h"

#include <windowsx.h>
#include <shellapi.h>
#include <sstream>
#include <chrono>
#include <mutex>

namespace ai_metadata {

// ============================================================================
// 内部错误对话框（IDD_ERROR）实现
// ============================================================================

namespace {

struct ErrorDialogContext {
    std::string message;
    std::string detail;
    std::string suggestion;
    std::string category_name;
    std::string log_hint;
    ErrorCategory category = ErrorCategory::Unknown;
    ErrorLevel level = ErrorLevel::Error;
    bool can_retry = false;
    bool can_open_settings = false;
    Feedback::ErrorAction action = Feedback::ErrorAction::Cancel;
};

// 单实例上下文（对话框模态，不会并发）
ErrorDialogContext g_error_ctx;

// 加载系统标准图标
HICON load_system_icon(ErrorLevel level) {
    LPCTSTR icon_id = IDI_INFORMATION;
    switch (level) {
        case ErrorLevel::Critical: icon_id = IDI_ERROR; break;
        case ErrorLevel::Error:    icon_id = IDI_ERROR; break;
        case ErrorLevel::Warning:  icon_id = IDI_WARNING; break;
        case ErrorLevel::Info:     icon_id = IDI_INFORMATION; break;
    }
    HICON icon = LoadIcon(NULL, icon_id);
    return icon ? icon : LoadIcon(NULL, IDI_INFORMATION);
}

// 复制文本到剪贴板（UNICODE 版）
void copy_to_clipboard(HWND wnd, const std::string& utf8_text) {
    if (!OpenClipboard(wnd)) return;
    EmptyClipboard();
    pfc::stringcvt::string_wide_from_utf8 wide(utf8_text.c_str());
    size_t bytes = (wcslen(wide.get_ptr()) + 1) * sizeof(wchar_t);
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (hMem) {
        wchar_t* p = static_cast<wchar_t*>(GlobalLock(hMem));
        if (p) {
            wcscpy_s(p, wcslen(wide.get_ptr()) + 1, wide.get_ptr());
            GlobalUnlock(hMem);
            SetClipboardData(CF_UNICODETEXT, hMem);
        }
    }
    CloseClipboard();
}

// 便捷：把 UTF-8 字符串设置到对话框控件
inline void set_dlg_text_utf8(HWND wnd, int id, const std::string& utf8) {
    pfc::stringcvt::string_wide_from_utf8 wide(utf8.c_str());
    SetDlgItemTextW(wnd, id, wide.get_ptr());
}

std::string build_clipboard_text(const ErrorDialogContext& ctx) {
    std::ostringstream oss;
    oss << "AI Metadata Error Report\n";
    oss << "========================\n\n";
    oss << "Category: " << ctx.category_name << "\n";
    oss << "Level: ";
    switch (ctx.level) {
        case ErrorLevel::Critical: oss << "Critical"; break;
        case ErrorLevel::Error:    oss << "Error"; break;
        case ErrorLevel::Warning:  oss << "Warning"; break;
        case ErrorLevel::Info:     oss << "Info"; break;
    }
    oss << "\n\n";
    oss << "Message:\n" << ctx.message << "\n\n";
    if (!ctx.suggestion.empty()) {
        oss << "Suggested fix:\n" << ctx.suggestion << "\n\n";
    }
    if (!ctx.detail.empty()) {
        oss << "Technical details:\n" << ctx.detail << "\n\n";
    }
    if (!ctx.log_hint.empty()) {
        oss << "Log: " << ctx.log_hint << "\n";
    }
    return oss.str();
}

INT_PTR CALLBACK error_dlg_proc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_INITDIALOG: {
            // 居中到父窗口
            HWND parent = GetParent(wnd);
            if (parent) {
                RECT rc_parent, rc_self;
                GetWindowRect(parent, &rc_parent);
                GetWindowRect(wnd, &rc_self);
                int x = rc_parent.left + ((rc_parent.right - rc_parent.left) - (rc_self.right - rc_self.left)) / 2;
                int y = rc_parent.top + ((rc_parent.bottom - rc_parent.top) - (rc_self.bottom - rc_self.top)) / 2;
                SetWindowPos(wnd, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
            }

            // 设置图标
            HICON icon = load_system_icon(g_error_ctx.level);
            if (icon) {
                SendDlgItemMessage(wnd, IDC_ERROR_ICON, STM_SETICON, reinterpret_cast<WPARAM>(icon), 0);
            }

            // 分类标签
            std::string cat_label = "[Category] " + g_error_ctx.category_name;
            set_dlg_text_utf8(wnd, IDC_ERROR_CATEGORY, cat_label);

            // 标题（按级别前缀）
            std::string title_prefix;
            switch (g_error_ctx.level) {
                case ErrorLevel::Critical: title_prefix = "[CRITICAL] "; break;
                case ErrorLevel::Error:    title_prefix = "[ERROR] "; break;
                case ErrorLevel::Warning:  title_prefix = "[WARNING] "; break;
                case ErrorLevel::Info:     title_prefix = "[INFO] "; break;
            }
            std::string title = title_prefix + g_error_ctx.message;
            set_dlg_text_utf8(wnd, IDC_ERROR_TITLE, title);
            set_dlg_text_utf8(wnd, IDC_ERROR_MESSAGE, g_error_ctx.message);

            // 修复建议
            set_dlg_text_utf8(wnd, IDC_ERROR_SUGGESTION, g_error_ctx.suggestion);

            // 技术详情
            set_dlg_text_utf8(wnd, IDC_ERROR_DETAILS, g_error_ctx.detail);

            // 日志路径提示
            if (!g_error_ctx.log_hint.empty()) {
                set_dlg_text_utf8(wnd, IDC_DETAILS_TEXT, g_error_ctx.log_hint);
            }

            // 根据可重试性启用/禁用 Retry
            EnableWindow(GetDlgItem(wnd, IDC_RETRY_BTN), g_error_ctx.can_retry);
            // 根据分类决定是否显示 Open Settings
            EnableWindow(GetDlgItem(wnd, IDC_OPEN_SETTINGS_BTN), g_error_ctx.can_open_settings);

            // 默认聚焦 Cancel（避免误点 Retry）
            SetFocus(GetDlgItem(wnd, IDCANCEL));
            return FALSE;  // 我们手动设置了焦点
        }

        case WM_COMMAND:
            switch (LOWORD(wp)) {
                case IDC_RETRY_BTN:
                    g_error_ctx.action = Feedback::ErrorAction::Retry;
                    EndDialog(wnd, IDRETRY);
                    return TRUE;

                case IDC_VIEW_LOG_BTN: {
                    // 打开日志文件夹（不关闭对话框，允许用户继续操作）
                    std::string log_path = Logger::instance().get_log_file_path();
                    if (!log_path.empty()) {
                        // 用 explorer 打开日志所在文件夹并选中文件（UNICODE 版）
                        std::string cmd_utf8 = "/select,\"" + log_path + "\"";
                        pfc::stringcvt::string_wide_from_utf8 w_cmd(cmd_utf8.c_str());
                        ShellExecuteW(NULL, L"open", L"explorer.exe", w_cmd.get_ptr(), NULL, SW_SHOWNORMAL);
                    }
                    g_error_ctx.action = Feedback::ErrorAction::ViewLog;
                    return TRUE;
                }

                case IDC_OPEN_SETTINGS_BTN: {
                    // 打开 fb2k 偏好设置到 AI Metadata General 页
                    static_api_ptr_t<ui_control> ui;
                    ui->show_preferences(AIPreferencePageGeneral::g_guid);
                    g_error_ctx.action = Feedback::ErrorAction::Cancel;
                    EndDialog(wnd, 0);
                    return TRUE;
                }

                case IDC_COPY_ERROR_BTN: {
                    std::string text = build_clipboard_text(g_error_ctx);
                    copy_to_clipboard(wnd, text);
                    // 短暂提示
                    set_dlg_text_utf8(wnd, IDC_COPY_ERROR_BTN, "Copied!");
                    SetTimer(wnd, 1, 1500, NULL);  // 1.5s 后恢复
                    return TRUE;
                }

                case IDCANCEL:
                    g_error_ctx.action = Feedback::ErrorAction::Cancel;
                    EndDialog(wnd, IDCANCEL);
                    return TRUE;
            }
            break;

        case WM_TIMER:
            if (wp == 1) {
                KillTimer(wnd, 1);
                set_dlg_text_utf8(wnd, IDC_COPY_ERROR_BTN, "Copy");
            }
            return TRUE;

        case WM_CLOSE:
            g_error_ctx.action = Feedback::ErrorAction::Cancel;
            EndDialog(wnd, IDCANCEL);
            return TRUE;
    }
    return FALSE;
}

}  // anonymous namespace

// ============================================================================
// Feedback 公共接口实现
// ============================================================================

void Feedback::info(const std::string& msg) {
    console::printf("[AI Metadata] %s", msg.c_str());
    LOG_INFO("feedback.info: " + msg);
}

void Feedback::success(const std::string& msg) {
    console::printf("[AI Metadata] [OK] %s", msg.c_str());
    LOG_INFO("feedback.success: " + msg);
}

void Feedback::warn(const std::string& msg, const std::string& title) {
    std::string full = "[" + title + "] " + msg;
    popup_message::g_show(msg.c_str(), title.c_str());
    LOG_WARN("feedback.warn: " + msg);
}

Feedback::ErrorAction Feedback::error(const std::string& message,
                                       const std::string& detail,
                                       ErrorCategory category,
                                       ErrorLevel level) {
    g_error_ctx = ErrorDialogContext{};
    g_error_ctx.message = message;
    g_error_ctx.detail = detail;
    g_error_ctx.category = category;
    g_error_ctx.level = level;
    g_error_ctx.category_name = category_display_name(category);
    g_error_ctx.suggestion = category_fix_suggestion(category);
    g_error_ctx.log_hint = category_log_hint(category);

    // 根据分类决定可用的按钮
    switch (category) {
        case ErrorCategory::Config:
        case ErrorCategory::Auth:
            g_error_ctx.can_open_settings = true;
            g_error_ctx.can_retry = false;
            break;
        case ErrorCategory::Network:
        case ErrorCategory::RateLimit:
        case ErrorCategory::ApiError:
        case ErrorCategory::DataSource:
        case ErrorCategory::PythonWorker:
            g_error_ctx.can_retry = true;
            g_error_ctx.can_open_settings = false;
            break;
        case ErrorCategory::FileSystem:
            g_error_ctx.can_retry = true;
            g_error_ctx.can_open_settings = false;
            break;
        case ErrorCategory::AiInference:
            g_error_ctx.can_retry = true;
            g_error_ctx.can_open_settings = true;  // 可能需要调整 prompt
            break;
        default:
            g_error_ctx.can_retry = false;
            g_error_ctx.can_open_settings = false;
            break;
    }

    // 同时记录到日志
    std::string log_msg = "feedback.error [" + g_error_ctx.category_name + "] " + message;
    if (!detail.empty()) log_msg += " | detail: " + detail;
    LOG_ERROR(log_msg);

    // 弹模态对话框
    INT_PTR ret = DialogBox(
        core_api::get_my_instance(),
        MAKEINTRESOURCE(IDD_ERROR),
        core_api::get_main_window(),
        error_dlg_proc
    );
    (void)ret;
    return g_error_ctx.action;
}

Feedback::ErrorAction Feedback::error(const ErrorInfo& err) {
    return error(err.message, err.detail, err.category, err.level);
}

void Feedback::error_simple(const std::string& msg, const std::string& title) {
    popup_message::g_show(msg.c_str(), title.c_str());
    LOG_ERROR("feedback.error_simple: " + msg);
}

bool Feedback::confirm(const std::string& msg, const std::string& title) {
    // fb2k 是 UNICODE 构建，MessageBox 解析为 MessageBoxW；用 pfc::stringcvt 转 wide
    pfc::stringcvt::string_wide_from_utf8 w_msg(msg.c_str());
    pfc::stringcvt::string_wide_from_utf8 w_title(title.c_str());
    int ret = MessageBoxW(
        core_api::get_main_window(),
        w_msg.get_ptr(),
        w_title.get_ptr(),
        MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2
    );
    LOG_INFO(std::string("feedback.confirm: ") + (ret == IDYES ? "YES" : "NO") + " | " + msg);
    return ret == IDYES;
}

bool Feedback::confirm_dangerous(const std::string& msg, const std::string& title) {
    pfc::stringcvt::string_wide_from_utf8 w_msg(msg.c_str());
    pfc::stringcvt::string_wide_from_utf8 w_title(title.c_str());
    int ret = MessageBoxW(
        core_api::get_main_window(),
        w_msg.get_ptr(),
        w_title.get_ptr(),
        MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2
    );
    LOG_INFO(std::string("feedback.confirm_dangerous: ") + (ret == IDYES ? "YES" : "NO") + " | " + msg);
    return ret == IDYES;
}

// ============================================================================
// 错误分类元信息
// ============================================================================

const char* Feedback::category_display_name(ErrorCategory cat) {
    switch (cat) {
        case ErrorCategory::Config:        return "Configuration Error";
        case ErrorCategory::Network:       return "Network Error";
        case ErrorCategory::Auth:          return "Authentication Failed";
        case ErrorCategory::RateLimit:     return "Rate Limit Exceeded";
        case ErrorCategory::ApiError:      return "API Error";
        case ErrorCategory::PythonWorker:  return "Python Worker Error";
        case ErrorCategory::AiInference:   return "AI Inference Error";
        case ErrorCategory::DataSource:    return "Data Source Error";
        case ErrorCategory::FileSystem:    return "File System Error";
        case ErrorCategory::UserCancelled: return "Cancelled";
        case ErrorCategory::NoData:        return "No Data";
        case ErrorCategory::Unknown:
        default:                           return "Unknown Error";
    }
}

std::string Feedback::category_fix_suggestion(ErrorCategory cat) {
    switch (cat) {
        case ErrorCategory::Config:
            return "1. Open Settings to verify:\n"
                   "   - AI Provider is selected\n"
                   "   - API Key is filled (or use OPENROUTER_API_KEY env var)\n"
                   "   - Model name is valid for the chosen provider\n"
                   "   - Python path points to a working Python 3.10+ install\n"
                   "2. Use 'Test API' button on General tab to verify connection.\n"
                   "3. Restart foobar2000 after changing Python path.";

        case ErrorCategory::Network:
            return "1. Check internet connection (try opening a website).\n"
                   "2. If behind proxy, configure HTTPS_PROXY env var.\n"
                   "3. Verify firewall / antivirus is not blocking foobar2000.\n"
                   "4. Retry - transient network issues are common.";

        case ErrorCategory::Auth:
            return "1. Open Settings and re-enter API key.\n"
                   "2. Verify the key is valid at the provider's dashboard:\n"
                   "   - OpenRouter: https://openrouter.ai/keys\n"
                   "   - DeepSeek:   https://platform.deepseek.com/api_keys\n"
                   "   - Zhipu:      https://open.bigmodel.cn/usercenter/apikeys\n"
                   "3. Check if the key has enough quota / credits.";

        case ErrorCategory::RateLimit:
            return "1. You have hit the provider's rate limit.\n"
                   "2. Wait a few minutes before retrying.\n"
                   "3. Consider reducing batch size in Settings > Processing.\n"
                   "4. Upgrade your API plan if this happens frequently.";

        case ErrorCategory::ApiError:
            return "1. The AI provider returned a business error.\n"
                   "2. Common causes:\n"
                   "   - Model name not found (check Settings > General > Model)\n"
                   "   - Request payload too large (reduce batch size)\n"
                   "   - Provider internal error (5xx, retry later)\n"
                   "3. Check technical details below for HTTP status and response.";

        case ErrorCategory::PythonWorker:
            return "1. Python worker failed to start or crashed.\n"
                   "2. Verify Python path in Settings > General.\n"
                   "3. Check 'Auto-install packages' option to install missing deps.\n"
                   "4. Manually test: run `python -c \"import openai\"` in terminal.\n"
                   "5. View log for Python traceback / stderr output.";

        case ErrorCategory::AiInference:
            return "1. AI returned a response that could not be parsed.\n"
                   "2. Common causes:\n"
                   "   - JSON format error (model may be too small, try a larger one)\n"
                   "   - Required fields missing in response\n"
                   "   - Response truncated (increase max_tokens)\n"
                   "3. Try switching to a different model in Settings > General.\n"
                   "4. Retry - some models occasionally produce malformed output.";

        case ErrorCategory::DataSource:
            return "1. MusicBrainz or Discogs query failed.\n"
                   "2. MusicBrainz is free but rate-limited (1 req/sec).\n"
                   "3. Discogs requires personal token (Settings > Data Sources).\n"
                   "4. Network issues can cause timeouts - retry.";

        case ErrorCategory::FileSystem:
            return "1. Could not read/write file metadata.\n"
                   "2. Common causes:\n"
                   "   - File is read-only or in use by another program\n"
                   "   - Database is locked (close other foobar2000 instances)\n"
                   "   - Insufficient permissions on file/folder\n"
                   "3. Verify file is not in a network drive with limited write access.";

        case ErrorCategory::UserCancelled:
            return "Operation cancelled by user. No action needed.";

        case ErrorCategory::NoData:
            return "Nothing to process.\n"
                   "1. Select at least one track in playlist.\n"
                   "2. For Normalize: selected field must have non-empty values.\n"
                   "3. For Rollback: a snapshot must exist (run Scrape/Enhance first).";

        case ErrorCategory::Unknown:
        default:
            return "An unexpected error occurred.\n"
                   "1. View log for full details.\n"
                   "2. If reproducible, report with log file and steps to reproduce.";
    }
}

std::string Feedback::category_log_hint(ErrorCategory cat) {
    std::string base = "Log file: " + Logger::instance().get_log_file_path();
    switch (cat) {
        case ErrorCategory::PythonWorker:
            return base + "\nPython stderr is captured in the same log file (search for '[Python]').";
        case ErrorCategory::AiInference:
            return base + "\nAI raw responses are logged at DEBUG level (Settings > Cache & Logs > Log Level).";
        case ErrorCategory::Network:
        case ErrorCategory::ApiError:
        case ErrorCategory::RateLimit:
            return base + "\nHTTP requests/responses are logged at DEBUG level.";
        default:
            return base;
    }
}

}  // namespace ai_metadata
