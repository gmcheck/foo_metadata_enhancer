#include "dialogs.h"
#include "resource.h"
#include "../core/logger.h"
#include <sstream>
#include <iomanip>
#include <commctrl.h>
#include <windows.h>

namespace ai_metadata {

BatchSettings* BatchSettingsDialog::s_settings = nullptr;
ErrorInfo* ErrorDialog::s_error = nullptr;
bool ErrorDialog::s_result = false;

bool DialogManager::ShowBatchSettingsDialog(HWND parent, BatchSettings& settings) {
    BatchSettingsDialog::s_settings = &settings;
    
    INT_PTR result = DialogBoxParam(
        core_api::get_my_instance(),
        MAKEINTRESOURCE(IDD_BATCH_SETTINGS),
        parent,
        BatchSettingsDialog::DlgProc,
        0
    );
    
    return result == IDOK;
}

bool DialogManager::ShowErrorDialog(HWND parent, const ErrorInfo& error) {
    ErrorDialog::s_error = const_cast<ErrorInfo*>(&error);
    ErrorDialog::s_result = false;
    
    INT_PTR result = DialogBoxParam(
        core_api::get_my_instance(),
        MAKEINTRESOURCE(IDD_ERROR),
        parent,
        ErrorDialog::DlgProc,
        0
    );
    
    return ErrorDialog::s_result;
}

bool DialogManager::ShowConfirmDialog(HWND parent, const char* title, const char* message) {
    return MessageBoxA(parent, message, title, MB_YESNO | MB_ICONQUESTION) == IDYES;
}

void DialogManager::ShowInfoDialog(HWND parent, const char* title, const char* message) {
    MessageBoxA(parent, message, title, MB_OK | MB_ICONINFORMATION);
}

INT_PTR CALLBACK BatchSettingsDialog::DlgProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_INITDIALOG:
            BatchSettingsDialog::DoInitDialog(wnd);
            return TRUE;
            
        case WM_COMMAND:
            switch (LOWORD(wp)) {
                case IDOK:
                    BatchSettingsDialog::OnOK(wnd);
                    return TRUE;
                    
                case IDCANCEL:
                    BatchSettingsDialog::OnCancel(wnd);
                    return TRUE;
            }
            break;
            
        case WM_CLOSE:
            BatchSettingsDialog::OnCancel(wnd);
            return TRUE;
    }
    
    return FALSE;
}

void BatchSettingsDialog::DoInitDialog(HWND wnd) {
    UpdateControls(wnd);
}

void BatchSettingsDialog::UpdateControls(HWND wnd) {
    if (!s_settings) return;
    
    SetDlgItemInt(wnd, IDC_BATCH_SIZE, s_settings->batch_size, FALSE);
    SetDlgItemInt(wnd, IDC_CONCURRENCY, s_settings->concurrency, FALSE);
    CheckDlgButton(wnd, IDC_AUTO_CLEANUP, s_settings->auto_cleanup ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(wnd, IDC_ENABLE_CACHE, s_settings->cache_enabled ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(wnd, IDC_SHOW_PROGRESS, s_settings->show_progress ? BST_CHECKED : BST_UNCHECKED);
}

void BatchSettingsDialog::SaveSettings(HWND wnd) {
    if (!s_settings) return;
    
    s_settings->batch_size = GetDlgItemInt(wnd, IDC_BATCH_SIZE, NULL, FALSE);
    s_settings->concurrency = GetDlgItemInt(wnd, IDC_CONCURRENCY, NULL, FALSE);
    s_settings->auto_cleanup = IsDlgButtonChecked(wnd, IDC_AUTO_CLEANUP) == BST_CHECKED;
    s_settings->cache_enabled = IsDlgButtonChecked(wnd, IDC_ENABLE_CACHE) == BST_CHECKED;
    s_settings->show_progress = IsDlgButtonChecked(wnd, IDC_SHOW_PROGRESS) == BST_CHECKED;
}

void BatchSettingsDialog::OnOK(HWND wnd) {
    SaveSettings(wnd);
    EndDialog(wnd, IDOK);
}

void BatchSettingsDialog::OnCancel(HWND wnd) {
    EndDialog(wnd, IDCANCEL);
}

INT_PTR CALLBACK ErrorDialog::DlgProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_INITDIALOG:
            ErrorDialog::DoInitDialog(wnd);
            return TRUE;
            
        case WM_COMMAND:
            switch (LOWORD(wp)) {
                case IDC_RETRY_BTN:
                    ErrorDialog::OnRetry(wnd);
                    return TRUE;
                    
                case IDCANCEL:
                    ErrorDialog::OnCancel(wnd);
                    return TRUE;
                    
                case IDC_VIEW_LOG_BTN:
                    {
                        std::string profile_path = core_api::get_profile_path();
                        std::string log_path = profile_path + "\\foo_ai_metadata\\logs";
                        
                        CreateDirectoryA((profile_path + "\\foo_ai_metadata").c_str(), NULL);
                        CreateDirectoryA(log_path.c_str(), NULL);
                        
                        std::string log_file = log_path + "\\core.log";
                        
                        int len = MultiByteToWideChar(CP_UTF8, 0, log_file.c_str(), -1, NULL, 0);
                        std::wstring wlog_file(len, 0);
                        MultiByteToWideChar(CP_UTF8, 0, log_file.c_str(), -1, &wlog_file[0], len);
                        
                        std::wstring params = L"/select,\"" + wlog_file + L"\"";
                        ShellExecuteW(NULL, NULL, L"explorer.exe", params.c_str(), NULL, SW_SHOWNORMAL);
                    }
                    return TRUE;
            }
            break;
            
        case WM_CLOSE:
            ErrorDialog::OnCancel(wnd);
            return TRUE;
    }
    
    return FALSE;
}

void ErrorDialog::DoInitDialog(HWND wnd) {
    if (!s_error) return;
    
    const wchar_t* title = L"Error";
    
    if (s_error->level == ErrorLevel::Info) {
        title = L"Information";
    } else if (s_error->level == ErrorLevel::Warning) {
        title = L"Warning";
    } else if (s_error->level == ErrorLevel::Error) {
        title = L"Error";
    } else if (s_error->level == ErrorLevel::Critical) {
        title = L"Critical Error";
    }
    
    SetWindowTextW(wnd, title);
    
    SetDlgItemTextA(wnd, IDC_ERROR_TITLE, s_error->message.c_str());
    SetDlgItemTextA(wnd, IDC_ERROR_MESSAGE, s_error->detail.c_str());
    SetDlgItemTextA(wnd, IDC_ERROR_DETAILS, s_error->detail.c_str());
    
    if (!s_error->can_retry) {
        EnableWindow(GetDlgItem(wnd, IDC_RETRY_BTN), FALSE);
    }
    
    if (s_error->level == ErrorLevel::Critical) {
        EnableWindow(GetDlgItem(wnd, IDC_RETRY_BTN), FALSE);
    }
}

void ErrorDialog::OnRetry(HWND wnd) {
    s_result = true;
    EndDialog(wnd, IDOK);
}

void ErrorDialog::OnCancel(HWND wnd) {
    s_result = false;
    EndDialog(wnd, IDCANCEL);
}

}
