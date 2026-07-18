#pragma once

#include <foobar2000/SDK/foobar2000.h>
#include "../include/types.h"
#include "../include/backup_manager.h"
#include <string>
#include <functional>

namespace ai_metadata {

struct BatchSettings {
    int batch_size;
    int concurrency;
    bool auto_cleanup;
    bool cache_enabled;
    bool show_progress;

    BatchSettings()
        : batch_size(50)
        , concurrency(3)
        , auto_cleanup(true)
        , cache_enabled(true)
        , show_progress(true)
    {}
};

class DialogManager {
public:
    static bool ShowBatchSettingsDialog(HWND parent, BatchSettings& settings);
    
    static bool ShowErrorDialog(HWND parent, const ErrorInfo& error);
    
    static bool ShowConfirmDialog(HWND parent, const char* title, const char* message);
    
    static void ShowInfoDialog(HWND parent, const char* title, const char* message);
    
    static bool ShowScrapingOptionsDialog(HWND parent, ScrapingOptions& options);
    
    static bool ShowEnhancementOptionsDialog(HWND parent, EnhancementOptions& options);
    
    static bool ShowConfirmResultDialog(HWND parent, 
                                         std::vector<TrackScrapingResult>& results,
                                         std::vector<bool>& selected,
                                         const std::vector<TrackInput>& original_inputs);
    
    static bool ShowEditFieldDialog(HWND parent, 
                                     TrackScrapingResult& result,
                                     const std::string& field_name,
                                     const std::string& original_value,
                                     int item_index);
    
    static bool ShowEnhanceConfirmDialog(HWND parent,
                                          std::vector<EnhancementResult>& results,
                                          std::vector<bool>& selected,
                                          const EnhancementOptions& options,
                                          const std::vector<TrackInput>& original_inputs);

    // Normalize: 字段选择对话框（多选，弹窗点选）
    static bool ShowNormalizeFieldDialog(HWND parent, std::vector<std::string>& selected_fields);

    // Normalize: 确认对话框（显示 groups + uncertain，用户勾选要应用的 groups）
    // result 为非 const：支持双击编辑 group（修改 canonical_name / aliases）
    static bool ShowNormalizeConfirmDialog(HWND parent,
                                            const std::string& field,
                                            NormalizeResult& result,
                                            std::vector<bool>& selected_groups);
};

class BatchSettingsDialog {
public:
    static INT_PTR CALLBACK DlgProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp);

    static BatchSettings* s_settings;

    static void DoInitDialog(HWND wnd);
    static void OnOK(HWND wnd);
    static void OnCancel(HWND wnd);
    static void UpdateControls(HWND wnd);
    static void SaveSettings(HWND wnd);
};

class ErrorDialog {
public:
    static INT_PTR CALLBACK DlgProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp);

    static ErrorInfo* s_error;
    static bool s_result;

    static void DoInitDialog(HWND wnd);
    static void OnRetry(HWND wnd);
    static void OnCancel(HWND wnd);
};

class ScrapingOptionsDialog {
public:
    static INT_PTR CALLBACK DlgProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp);
    
    static ScrapingOptions* s_options;
    
    static void DoInitDialog(HWND wnd);
    static void OnOK(HWND wnd);
    static void OnCancel(HWND wnd);
    static void SaveOptions(HWND wnd);
    static void UpdateControls(HWND wnd);
};

class EnhancementOptionsDialog {
public:
    static INT_PTR CALLBACK DlgProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp);
    
    static EnhancementOptions* s_options;
    
    static void DoInitDialog(HWND wnd);
    static void OnOK(HWND wnd);
    static void OnCancel(HWND wnd);
    static void SaveOptions(HWND wnd);
    static void UpdateControls(HWND wnd);
};

class ConfirmResultDialog {
public:
    static INT_PTR CALLBACK DlgProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp);
    
    static std::vector<TrackScrapingResult>* s_results;
    static std::vector<bool>* s_selected;
    static const std::vector<TrackInput>* s_original_inputs;
    static bool s_confirmed;
    
    static std::map<std::string, bool> s_field_selection;
    
    static void DoInitDialog(HWND wnd);
    static void InitFieldCheckboxes(HWND wnd);
    static void SaveFieldSelection(HWND wnd);
    static bool IsFieldSelected(const std::string& field);
    static void PopulateListView(HWND wnd);
    static void OnSelectAll(HWND wnd);
    static void OnSelectNone(HWND wnd);
    static void OnSelectSuccess(HWND wnd);
    static void OnEditItem(HWND wnd);
    static void OnEditItemAt(HWND wnd, int item_index, int sub_item);
    static void OnOK(HWND wnd);
    static void OnCancel(HWND wnd);
};

class EditFieldDialog {
public:
    static INT_PTR CALLBACK DlgProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp);
    
    static TrackScrapingResult* s_result;
    static std::string s_field_name;
    static std::string s_original_value;
    static int s_item_index;
    
    static void DoInitDialog(HWND wnd);
    static void OnOK(HWND wnd);
    static void OnCancel(HWND wnd);
};

class CommonEditFieldDialog {
public:
    static INT_PTR CALLBACK DlgProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp);
    
    static std::string s_field_name;
    static std::string s_original_value;
    static std::string s_scraped_value;
    static std::string s_new_value;
    static float s_confidence;
    static std::string s_source;
    static bool s_confirmed;
    
    static void DoInitDialog(HWND wnd);
    static void OnOK(HWND wnd);
    static void OnCancel(HWND wnd);
    
    static bool Show(HWND parent, 
                     const std::string& field_name,
                     const std::string& original_value,
                     const std::string& scraped_value,
                     float confidence,
                     const std::string& source,
                     std::string& out_new_value);
};

class EnhanceConfirmDialog {
public:
    static INT_PTR CALLBACK DlgProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp);
    
    static std::vector<EnhancementResult>* s_results;
    static std::vector<bool>* s_selected;
    static const EnhancementOptions* s_options;
    static const std::vector<TrackInput>* s_original_inputs;
    static bool s_confirmed;
    
    static std::map<std::string, bool> s_field_selection;
    
    static void DoInitDialog(HWND wnd);
    static void InitFieldCheckboxes(HWND wnd);
    static void SaveFieldSelection(HWND wnd);
    static bool IsFieldSelected(const std::string& field);
    static void PopulateListView(HWND wnd);
    static void OnSelectAll(HWND wnd);
    static void OnSelectNone(HWND wnd);
    static void OnSelectSuccess(HWND wnd);
    static void OnEditItemAt(HWND wnd, int item_index, int sub_item);
    static void OnOK(HWND wnd);
    static void OnCancel(HWND wnd);
};

// Normalize 字段选择对话框
class NormalizeFieldDialog {
public:
    static INT_PTR CALLBACK DlgProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp);

    static std::vector<std::string>* s_selected_fields;
    static bool s_confirmed;

    static void DoInitDialog(HWND wnd);
    static void OnOK(HWND wnd);
    static void OnCancel(HWND wnd);
};

// Normalize 确认对话框
class NormalizeConfirmDialog {
public:
    static INT_PTR CALLBACK DlgProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp);

    static std::string s_field;
    static NormalizeResult* s_result;
    static std::vector<bool>* s_selected_groups;
    static bool s_confirmed;

    static void DoInitDialog(HWND wnd);
    static void PopulateListView(HWND wnd);
    static void OnSelectAll(HWND wnd);
    static void OnSelectNone(HWND wnd);
    static void OnEditGroupAt(HWND wnd, int item_index);
    static void OnOK(HWND wnd);
    static void OnCancel(HWND wnd);
};

// Normalize 单个 group 编辑对话框（修改 canonical_name + aliases，查看 reason）
class NormalizeEditDialog {
public:
    static INT_PTR CALLBACK DlgProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp);

    static std::string s_canonical_name;
    static std::vector<std::string> s_aliases;
    static std::string s_reason;        // 只读显示，让用户能看完整 reason
    static bool s_confirmed;

    static void DoInitDialog(HWND wnd);
    static void OnOK(HWND wnd);
    static void OnCancel(HWND wnd);

    // 弹出编辑对话框，修改 canonical 和 aliases（reason 只读显示）
    static bool Show(HWND parent,
                     const std::string& canonical_name,
                     const std::vector<std::string>& aliases,
                     const std::string& reason,
                     std::string& out_canonical,
                     std::vector<std::string>& out_aliases);
};

// 回滚类型选择对话框
// 用户勾选要回滚的操作类型（刮削 / 翻译 / 歌手规范化），
// 只有"该类型在所选 tracks 中有快照"的项才显示并允许勾选
class RollbackTypeDialog {
public:
    static INT_PTR CALLBACK DlgProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp);

    // 输入：available_types - 所选 tracks 中存在的操作类型集合（已去重）
    //       type_counts - 每个类型对应的 track 数量（供 UI 显示）
    // 输出：selected_types - 用户勾选的类型
    // 返回：true=用户确认；false=用户取消或对话框创建失败
    //       若返回 false，可检查 s_last_error_code 判断是取消还是失败
    static bool Show(HWND parent,
                     const std::vector<ai_metadata::OperationType>& available_types,
                     const std::map<ai_metadata::OperationType, int>& type_counts,
                     std::vector<ai_metadata::OperationType>& selected_types);

    static std::vector<ai_metadata::OperationType> s_available_types;
    static std::map<ai_metadata::OperationType, int> s_type_counts;
    static std::vector<bool> s_check_states;
    static bool s_confirmed;
    // 记录 DialogBoxParam 的返回值：-1 表示创建失败，IDOK/IDCANCEL 表示正常关闭
    static INT_PTR s_last_dialog_result;
    // 记录创建失败时的 GetLastError（仅 s_last_dialog_result == -1 时有效）
    static DWORD s_last_error_code;

    static void DoInitDialog(HWND wnd);
    static void OnOK(HWND wnd);
    static void OnCancel(HWND wnd);
};

}
