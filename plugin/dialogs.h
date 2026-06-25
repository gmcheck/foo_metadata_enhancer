#pragma once

#include <foobar2000/SDK/foobar2000.h>
#include "../include/types.h"
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

}
