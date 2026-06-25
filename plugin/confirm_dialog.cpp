#include "dialogs.h"
#include "../include/types.h"
#include "resource.h"
#include "../core/logger.h"
#include <foobar2000/SDK/foobar2000.h>
#include <windowsx.h>
#include <commctrl.h>
#include <sstream>
#include <iomanip>

namespace ai_metadata {

ScrapingOptions* ScrapingOptionsDialog::s_options = nullptr;
EnhancementOptions* EnhancementOptionsDialog::s_options = nullptr;
std::vector<TrackScrapingResult>* ConfirmResultDialog::s_results = nullptr;
std::vector<bool>* ConfirmResultDialog::s_selected = nullptr;
const std::vector<TrackInput>* ConfirmResultDialog::s_original_inputs = nullptr;
bool ConfirmResultDialog::s_confirmed = false;
std::map<std::string, bool> ConfirmResultDialog::s_field_selection;
TrackScrapingResult* EditFieldDialog::s_result = nullptr;
std::string EditFieldDialog::s_field_name;
std::string EditFieldDialog::s_original_value;
int EditFieldDialog::s_item_index = -1;

static std::wstring to_wstring(const std::string& str) {
    if (str.empty()) return std::wstring();
    int size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
    std::wstring result(size - 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &result[0], size);
    return result;
}

static std::string to_string(const std::wstring& wstr) {
    if (wstr.empty()) return std::string();
    int size = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string result(size - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &result[0], size, nullptr, nullptr);
    return result;
}

bool DialogManager::ShowScrapingOptionsDialog(HWND parent, ScrapingOptions& options) {
    ScrapingOptionsDialog::s_options = &options;
    
    INT_PTR result = DialogBox(
        core_api::get_my_instance(),
        MAKEINTRESOURCE(IDD_SCRAPING_OPTIONS),
        parent,
        ScrapingOptionsDialog::DlgProc
    );
    
    ScrapingOptionsDialog::s_options = nullptr;
    return result == IDOK;
}

bool DialogManager::ShowEnhancementOptionsDialog(HWND parent, EnhancementOptions& options) {
    EnhancementOptionsDialog::s_options = &options;
    
    INT_PTR result = DialogBox(
        core_api::get_my_instance(),
        MAKEINTRESOURCE(IDD_ENHANCEMENT_OPTIONS),
        parent,
        EnhancementOptionsDialog::DlgProc
    );
    
    EnhancementOptionsDialog::s_options = nullptr;
    return result == IDOK;
}

bool DialogManager::ShowConfirmResultDialog(HWND parent, 
                                             std::vector<TrackScrapingResult>& results,
                                             std::vector<bool>& selected,
                                             const std::vector<TrackInput>& original_inputs) {
    ConfirmResultDialog::s_results = &results;
    ConfirmResultDialog::s_selected = &selected;
    ConfirmResultDialog::s_original_inputs = &original_inputs;
    ConfirmResultDialog::s_confirmed = false;
    
    ConfirmResultDialog::s_selected->resize(results.size(), true);
    
    INT_PTR result = DialogBox(
        core_api::get_my_instance(),
        MAKEINTRESOURCE(IDD_CONFIRM_RESULT),
        parent,
        ConfirmResultDialog::DlgProc
    );
    
    ConfirmResultDialog::s_results = nullptr;
    ConfirmResultDialog::s_selected = nullptr;
    ConfirmResultDialog::s_original_inputs = nullptr;
    return ConfirmResultDialog::s_confirmed;
}

INT_PTR CALLBACK ScrapingOptionsDialog::DlgProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_INITDIALOG:
            DoInitDialog(wnd);
            return TRUE;
            
        case WM_COMMAND:
            switch (LOWORD(wp)) {
                case IDOK:
                    OnOK(wnd);
                    return TRUE;
                case IDCANCEL:
                    OnCancel(wnd);
                    return TRUE;
            }
            break;
    }
    return FALSE;
}

void ScrapingOptionsDialog::DoInitDialog(HWND wnd) {
    if (!s_options) return;
    
    CheckDlgButton(wnd, IDC_ENABLE_MUSICBRAINZ, s_options->enable_musicbrainz ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(wnd, IDC_ENABLE_DISCOGS, s_options->enable_discogs ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(wnd, IDC_ENABLE_AI, s_options->enable_ai ? BST_CHECKED : BST_UNCHECKED);
    
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << s_options->auto_accept_threshold;
    SetDlgItemTextW(wnd, IDC_AUTO_ACCEPT_THRESHOLD, to_wstring(oss.str()).c_str());
    
    oss.str("");
    oss << s_options->confirm_threshold;
    SetDlgItemTextW(wnd, IDC_CONFIRM_THRESHOLD, to_wstring(oss.str()).c_str());
}

void ScrapingOptionsDialog::OnOK(HWND wnd) {
    SaveOptions(wnd);
    EndDialog(wnd, IDOK);
}

void ScrapingOptionsDialog::OnCancel(HWND wnd) {
    EndDialog(wnd, IDCANCEL);
}

void ScrapingOptionsDialog::SaveOptions(HWND wnd) {
    if (!s_options) return;
    
    s_options->enable_musicbrainz = IsDlgButtonChecked(wnd, IDC_ENABLE_MUSICBRAINZ) == BST_CHECKED;
    s_options->enable_discogs = IsDlgButtonChecked(wnd, IDC_ENABLE_DISCOGS) == BST_CHECKED;
    s_options->enable_ai = IsDlgButtonChecked(wnd, IDC_ENABLE_AI) == BST_CHECKED;
    
    wchar_t buffer[32];
    GetDlgItemTextW(wnd, IDC_AUTO_ACCEPT_THRESHOLD, buffer, sizeof(buffer)/sizeof(wchar_t));
    s_options->auto_accept_threshold = static_cast<float>(_wtof(buffer));
    
    GetDlgItemTextW(wnd, IDC_CONFIRM_THRESHOLD, buffer, sizeof(buffer)/sizeof(wchar_t));
    s_options->confirm_threshold = static_cast<float>(_wtof(buffer));
}

INT_PTR CALLBACK EnhancementOptionsDialog::DlgProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_INITDIALOG:
            DoInitDialog(wnd);
            return TRUE;
            
        case WM_COMMAND:
            switch (LOWORD(wp)) {
                case IDOK:
                    OnOK(wnd);
                    return TRUE;
                case IDCANCEL:
                    OnCancel(wnd);
                    return TRUE;
            }
            break;
    }
    return FALSE;
}

void EnhancementOptionsDialog::DoInitDialog(HWND wnd) {
}

void EnhancementOptionsDialog::OnOK(HWND wnd) {
    SaveOptions(wnd);
    EndDialog(wnd, IDOK);
}

void EnhancementOptionsDialog::OnCancel(HWND wnd) {
    EndDialog(wnd, IDCANCEL);
}

void EnhancementOptionsDialog::SaveOptions(HWND wnd) {
    if (!s_options) return;
    
    s_options->translate_title = true;
    s_options->translate_album = true;
    s_options->translate_artist = true;
    s_options->classify_genre = true;
    s_options->identify_edition = true;
}

INT_PTR CALLBACK ConfirmResultDialog::DlgProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_INITDIALOG:
            DoInitDialog(wnd);
            return TRUE;
            
        case WM_COMMAND:
            switch (LOWORD(wp)) {
                case IDOK:
                    OnOK(wnd);
                    return TRUE;
                case IDCANCEL:
                    OnCancel(wnd);
                    return TRUE;
                case IDC_SELECT_ALL:
                    OnSelectAll(wnd);
                    return TRUE;
                case IDC_SELECT_NONE:
                    OnSelectNone(wnd);
                    return TRUE;
                case IDC_SELECT_SUCCESS:
                    OnSelectSuccess(wnd);
                    return TRUE;
                case IDC_EDIT_ITEM:
                    OnEditItem(wnd);
                    return TRUE;
            }
            break;
            
        case WM_NOTIFY:
            {
                LPNMHDR nmhdr = reinterpret_cast<LPNMHDR>(lp);
                if (nmhdr->idFrom == IDC_RESULT_LISTVIEW) {
                    if (nmhdr->code == NM_DBLCLK) {
                        LPNMITEMACTIVATE lpnmitem = reinterpret_cast<LPNMITEMACTIVATE>(lp);
                        OnEditItemAt(wnd, lpnmitem->iItem, lpnmitem->iSubItem);
                        return TRUE;
                    }
                }
            }
            break;
    }
    return FALSE;
}

void ConfirmResultDialog::DoInitDialog(HWND wnd) {
    InitFieldCheckboxes(wnd);
    
    HWND hList = GetDlgItem(wnd, IDC_RESULT_LISTVIEW);
    if (!hList) return;
    
    DWORD exStyle = ListView_GetExtendedListViewStyle(hList);
    exStyle |= LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_HEADERDRAGDROP;
    ListView_SetExtendedListViewStyle(hList, exStyle);
    
    LVCOLUMN lvc = {0};
    lvc.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
    lvc.fmt = LVCFMT_LEFT;
    
    const wchar_t* col_names[] = {L"Track ID", L"Title", L"Artist", L"Album", L"Year", L"Track#", L"Disc#", L"Composer", L"Lyricist", L"Conductor", L"Performer", L"Label", L"Confidence", L"Source"};
    int col_widths[] = {150, 200, 150, 150, 60, 50, 50, 100, 100, 100, 100, 100, 80, 80};
    
    for (int i = 0; i < 14; ++i) {
        lvc.pszText = const_cast<wchar_t*>(col_names[i]);
        lvc.cx = col_widths[i];
        ListView_InsertColumn(hList, i, &lvc);
    }
    
    PopulateListView(wnd);
}

void ConfirmResultDialog::InitFieldCheckboxes(HWND wnd) {
    if (s_field_selection.empty()) {
        s_field_selection["title"] = true;
        s_field_selection["artist"] = true;
        s_field_selection["album"] = true;
        s_field_selection["year"] = true;
        s_field_selection["track_number"] = true;
        s_field_selection["disc_number"] = true;
        s_field_selection["composer"] = true;
        s_field_selection["lyricist"] = true;
        s_field_selection["conductor"] = false;
        s_field_selection["performer"] = false;
        s_field_selection["label"] = true;
    }
    
    CheckDlgButton(wnd, IDC_FIELD_TITLE, s_field_selection["title"] ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(wnd, IDC_FIELD_ARTIST, s_field_selection["artist"] ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(wnd, IDC_FIELD_ALBUM, s_field_selection["album"] ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(wnd, IDC_FIELD_YEAR, s_field_selection["year"] ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(wnd, IDC_FIELD_TRACK_NUMBER, s_field_selection["track_number"] ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(wnd, IDC_FIELD_DISC_NUMBER, s_field_selection["disc_number"] ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(wnd, IDC_FIELD_COMPOSER, s_field_selection["composer"] ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(wnd, IDC_FIELD_LYRICIST, s_field_selection["lyricist"] ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(wnd, IDC_FIELD_CONDUCTOR, s_field_selection["conductor"] ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(wnd, IDC_FIELD_PERFORMER, s_field_selection["performer"] ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(wnd, IDC_FIELD_LABEL, s_field_selection["label"] ? BST_CHECKED : BST_UNCHECKED);
}

void ConfirmResultDialog::SaveFieldSelection(HWND wnd) {
    s_field_selection["title"] = IsDlgButtonChecked(wnd, IDC_FIELD_TITLE) == BST_CHECKED;
    s_field_selection["artist"] = IsDlgButtonChecked(wnd, IDC_FIELD_ARTIST) == BST_CHECKED;
    s_field_selection["album"] = IsDlgButtonChecked(wnd, IDC_FIELD_ALBUM) == BST_CHECKED;
    s_field_selection["year"] = IsDlgButtonChecked(wnd, IDC_FIELD_YEAR) == BST_CHECKED;
    s_field_selection["track_number"] = IsDlgButtonChecked(wnd, IDC_FIELD_TRACK_NUMBER) == BST_CHECKED;
    s_field_selection["disc_number"] = IsDlgButtonChecked(wnd, IDC_FIELD_DISC_NUMBER) == BST_CHECKED;
    s_field_selection["composer"] = IsDlgButtonChecked(wnd, IDC_FIELD_COMPOSER) == BST_CHECKED;
    s_field_selection["lyricist"] = IsDlgButtonChecked(wnd, IDC_FIELD_LYRICIST) == BST_CHECKED;
    s_field_selection["conductor"] = IsDlgButtonChecked(wnd, IDC_FIELD_CONDUCTOR) == BST_CHECKED;
    s_field_selection["performer"] = IsDlgButtonChecked(wnd, IDC_FIELD_PERFORMER) == BST_CHECKED;
    s_field_selection["label"] = IsDlgButtonChecked(wnd, IDC_FIELD_LABEL) == BST_CHECKED;
}

bool ConfirmResultDialog::IsFieldSelected(const std::string& field) {
    auto it = s_field_selection.find(field);
    if (it != s_field_selection.end()) {
        return it->second;
    }
    return false;
}

void ConfirmResultDialog::PopulateListView(HWND wnd) {
    HWND hList = GetDlgItem(wnd, IDC_RESULT_LISTVIEW);
    if (!hList || !s_results) return;
    
    ListView_DeleteAllItems(hList);
    
    auto get_field_value = [](const TrackScrapingResult& result, const std::string& field_name) -> std::string {
        auto it = result.scraped_fields.find(field_name);
        return (it != result.scraped_fields.end()) ? it->second.value : "";
    };
    
    for (size_t i = 0; i < s_results->size(); ++i) {
        const auto& result = (*s_results)[i];
        bool is_empty = !result.success || result.scraped_fields.empty();
        
        LVITEM lvi = {0};
        lvi.mask = LVIF_TEXT | LVIF_PARAM;
        lvi.iItem = static_cast<int>(i);
        lvi.lParam = static_cast<LPARAM>(i);
        
        std::wstring track_id_w;
        if (is_empty) {
            std::string prefix = result.track_id.empty() ? "[FAILED] " : "[FAILED] ";
            track_id_w = to_wstring(prefix + result.track_id.substr(0, 16) + (result.track_id.length() > 16 ? "..." : ""));
        } else {
            track_id_w = to_wstring(result.track_id.substr(0, 24) + (result.track_id.length() > 24 ? "..." : ""));
        }
        lvi.pszText = const_cast<wchar_t*>(track_id_w.c_str());
        ListView_InsertItem(hList, &lvi);
        
        std::wstring title = is_empty ? L"(no data)" : to_wstring(get_field_value(result, "title"));
        ListView_SetItemText(hList, static_cast<int>(i), 1, const_cast<wchar_t*>(title.c_str()));
        
        std::wstring artist = is_empty ? L"" : to_wstring(get_field_value(result, "artist"));
        ListView_SetItemText(hList, static_cast<int>(i), 2, const_cast<wchar_t*>(artist.c_str()));
        
        std::wstring album = is_empty ? L"" : to_wstring(get_field_value(result, "album"));
        ListView_SetItemText(hList, static_cast<int>(i), 3, const_cast<wchar_t*>(album.c_str()));
        
        std::wstring year = is_empty ? L"" : to_wstring(get_field_value(result, "year"));
        ListView_SetItemText(hList, static_cast<int>(i), 4, const_cast<wchar_t*>(year.c_str()));
        
        std::wstring track_num = is_empty ? L"" : to_wstring(get_field_value(result, "track_number"));
        ListView_SetItemText(hList, static_cast<int>(i), 5, const_cast<wchar_t*>(track_num.c_str()));
        
        std::wstring disc_num = is_empty ? L"" : to_wstring(get_field_value(result, "disc_number"));
        ListView_SetItemText(hList, static_cast<int>(i), 6, const_cast<wchar_t*>(disc_num.c_str()));
        
        std::wstring composer = is_empty ? L"" : to_wstring(get_field_value(result, "composer"));
        ListView_SetItemText(hList, static_cast<int>(i), 7, const_cast<wchar_t*>(composer.c_str()));
        
        std::wstring lyricist = is_empty ? L"" : to_wstring(get_field_value(result, "lyricist"));
        ListView_SetItemText(hList, static_cast<int>(i), 8, const_cast<wchar_t*>(lyricist.c_str()));
        
        std::wstring conductor = is_empty ? L"" : to_wstring(get_field_value(result, "conductor"));
        ListView_SetItemText(hList, static_cast<int>(i), 9, const_cast<wchar_t*>(conductor.c_str()));
        
        std::wstring performer = is_empty ? L"" : to_wstring(get_field_value(result, "performer"));
        ListView_SetItemText(hList, static_cast<int>(i), 10, const_cast<wchar_t*>(performer.c_str()));
        
        std::wstring label = is_empty ? L"" : to_wstring(get_field_value(result, "label"));
        ListView_SetItemText(hList, static_cast<int>(i), 11, const_cast<wchar_t*>(label.c_str()));
        
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2);
        float total_conf = 0.0f;
        int count = 0;
        for (const auto& field : result.scraped_fields) {
            total_conf += field.second.confidence;
            count++;
        }
        if (count > 0) {
            oss << (total_conf / count);
        } else {
            oss << "0.00";
        }
        std::wstring conf_str = to_wstring(oss.str());
        ListView_SetItemText(hList, static_cast<int>(i), 12, const_cast<wchar_t*>(conf_str.c_str()));
        
        std::string source;
        if (is_empty) {
            source = result.error.empty() ? "Failed" : "Error";
        } else {
            switch (result.release_source) {
                case DataSourceType::MUSICBRAINZ: source = "MusicBrainz"; break;
                case DataSourceType::DISCOGS: source = "Discogs"; break;
                case DataSourceType::AI: source = "AI"; break;
            }
        }
        std::wstring source_w = to_wstring(source);
        ListView_SetItemText(hList, static_cast<int>(i), 13, const_cast<wchar_t*>(source_w.c_str()));
        
        if (s_selected && i < s_selected->size() && (*s_selected)[i]) {
            ListView_SetCheckState(hList, static_cast<int>(i), TRUE);
        }
    }
}

void ConfirmResultDialog::OnSelectAll(HWND wnd) {
    if (!s_selected) return;
    
    for (size_t i = 0; i < s_selected->size(); ++i) {
        (*s_selected)[i] = true;
    }
    
    HWND hList = GetDlgItem(wnd, IDC_RESULT_LISTVIEW);
    if (hList) {
        for (size_t i = 0; i < s_selected->size(); ++i) {
            ListView_SetCheckState(hList, static_cast<int>(i), TRUE);
        }
    }
}

void ConfirmResultDialog::OnSelectNone(HWND wnd) {
    if (!s_selected) return;
    
    for (size_t i = 0; i < s_selected->size(); ++i) {
        (*s_selected)[i] = false;
    }
    
    HWND hList = GetDlgItem(wnd, IDC_RESULT_LISTVIEW);
    if (hList) {
        for (size_t i = 0; i < s_selected->size(); ++i) {
            ListView_SetCheckState(hList, static_cast<int>(i), FALSE);
        }
    }
}

void ConfirmResultDialog::OnSelectSuccess(HWND wnd) {
    if (!s_selected || !s_results) return;
    
    HWND hList = GetDlgItem(wnd, IDC_RESULT_LISTVIEW);
    
    for (size_t i = 0; i < s_selected->size(); ++i) {
        const auto& result = (*s_results)[i];
        bool is_success = result.success && !result.scraped_fields.empty();
        (*s_selected)[i] = is_success;
        
        if (hList) {
            ListView_SetCheckState(hList, static_cast<int>(i), is_success ? TRUE : FALSE);
        }
    }
}

void ConfirmResultDialog::OnEditItem(HWND wnd) {
    HWND hList = GetDlgItem(wnd, IDC_RESULT_LISTVIEW);
    if (!hList || !s_results) return;
    
    int selected = ListView_GetNextItem(hList, -1, LVNI_SELECTED);
    if (selected < 0 || selected >= static_cast<int>(s_results->size())) {
        return;
    }
    
    OnEditItemAt(wnd, selected, 1);
}

void ConfirmResultDialog::OnEditItemAt(HWND wnd, int item_index, int sub_item) {
    if (!s_results || item_index < 0 || item_index >= static_cast<int>(s_results->size())) {
        return;
    }
    
    static const char* column_fields[] = {
        "track_id", "title", "artist", "album", "year", 
        "track_number", "disc_number", "composer", "lyricist", 
        "conductor", "performer", "label", "confidence", "source"
    };
    
    if (sub_item < 0 || sub_item >= 14) {
        sub_item = 1;
    }
    
    if (sub_item == 0 || sub_item == 12 || sub_item == 13) {
        return;
    }
    
    std::string field_name = column_fields[sub_item];
    
    TrackScrapingResult& result = (*s_results)[item_index];
    
    std::string original_value;
    if (s_original_inputs && item_index < static_cast<int>(s_original_inputs->size())) {
        const auto& input = (*s_original_inputs)[item_index];
        if (field_name == "title") original_value = input.title;
        else if (field_name == "artist") original_value = input.artist;
        else if (field_name == "album") original_value = input.album;
        else if (field_name == "year") original_value = input.year;
        else if (field_name == "track_number") original_value = input.track_number;
        else if (field_name == "disc_number") original_value = input.disc_number;
        else if (field_name == "composer") original_value = input.composer;
        else if (field_name == "lyricist") original_value = input.lyricist;
        else if (field_name == "conductor") original_value = input.conductor;
        else if (field_name == "performer") original_value = input.performer;
        else if (field_name == "label") original_value = input.label;
    }
    
    if (DialogManager::ShowEditFieldDialog(wnd, result, field_name, original_value, item_index)) {
        PopulateListView(wnd);
    }
}

void ConfirmResultDialog::OnOK(HWND wnd) {
    SaveFieldSelection(wnd);
    
    HWND hList = GetDlgItem(wnd, IDC_RESULT_LISTVIEW);
    if (hList && s_selected) {
        for (size_t i = 0; i < s_selected->size(); ++i) {
            (*s_selected)[i] = ListView_GetCheckState(hList, static_cast<int>(i)) != FALSE;
        }
    }
    s_confirmed = true;
    EndDialog(wnd, IDOK);
}

void ConfirmResultDialog::OnCancel(HWND wnd) {
    s_confirmed = false;
    EndDialog(wnd, IDCANCEL);
}

bool DialogManager::ShowEditFieldDialog(HWND parent, 
                                          TrackScrapingResult& result,
                                          const std::string& field_name,
                                          const std::string& original_value,
                                          int item_index) {
    EditFieldDialog::s_result = &result;
    EditFieldDialog::s_field_name = field_name;
    EditFieldDialog::s_original_value = original_value;
    EditFieldDialog::s_item_index = item_index;
    
    INT_PTR ret = DialogBox(
        core_api::get_my_instance(),
        MAKEINTRESOURCE(IDD_EDIT_FIELD),
        parent,
        EditFieldDialog::DlgProc
    );
    
    EditFieldDialog::s_result = nullptr;
    return ret == IDOK;
}

INT_PTR CALLBACK EditFieldDialog::DlgProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_INITDIALOG:
            DoInitDialog(wnd);
            return TRUE;
            
        case WM_COMMAND:
            switch (LOWORD(wp)) {
                case IDOK:
                    OnOK(wnd);
                    return TRUE;
                case IDCANCEL:
                    OnCancel(wnd);
                    return TRUE;
            }
            break;
    }
    return FALSE;
}

void EditFieldDialog::DoInitDialog(HWND wnd) {
    std::wstring title = L"Edit Value: " + to_wstring(s_field_name);
    SetWindowTextW(wnd, title.c_str());
    
    SetDlgItemTextW(wnd, IDC_EDIT_EXISTING, to_wstring(s_original_value).c_str());
    
    auto it = s_result->scraped_fields.find(s_field_name);
    std::string scraped_value;
    float confidence = 0.0f;
    std::string source;
    
    if (it != s_result->scraped_fields.end()) {
        scraped_value = it->second.value;
        confidence = it->second.confidence;
        switch (it->second.source) {
            case DataSourceType::MUSICBRAINZ: source = "MusicBrainz"; break;
            case DataSourceType::DISCOGS: source = "Discogs"; break;
            case DataSourceType::AI: source = "AI"; break;
        }
    }
    
    SetDlgItemTextW(wnd, IDC_EDIT_ORIGINAL, to_wstring(scraped_value).c_str());
    
    std::string new_value = scraped_value.empty() ? s_original_value : scraped_value;
    SetDlgItemTextW(wnd, IDC_EDIT_VALUE, to_wstring(new_value).c_str());
    
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << confidence;
    SetDlgItemTextW(wnd, IDC_EDIT_CONFIDENCE, to_wstring(oss.str()).c_str());
    SetDlgItemTextW(wnd, IDC_EDIT_SOURCE, to_wstring(source).c_str());
}

void EditFieldDialog::OnOK(HWND wnd) {
    if (!s_result || s_field_name.empty()) {
        EndDialog(wnd, IDCANCEL);
        return;
    }
    
    wchar_t value_w[1024];
    GetDlgItemTextW(wnd, IDC_EDIT_VALUE, value_w, sizeof(value_w)/sizeof(wchar_t));
    std::string new_value = to_string(value_w);
    
    auto it = s_result->scraped_fields.find(s_field_name);
    if (it != s_result->scraped_fields.end()) {
        it->second.value = new_value;
    } else {
        ScrapedField field;
        field.value = new_value;
        field.confidence = 1.0f;
        field.source = DataSourceType::AI;
        s_result->scraped_fields[s_field_name] = field;
    }
    
    EndDialog(wnd, IDOK);
}

void EditFieldDialog::OnCancel(HWND wnd) {
    EndDialog(wnd, IDCANCEL);
}

std::string CommonEditFieldDialog::s_field_name;
std::string CommonEditFieldDialog::s_original_value;
std::string CommonEditFieldDialog::s_scraped_value;
std::string CommonEditFieldDialog::s_new_value;
float CommonEditFieldDialog::s_confidence = 0.0f;
std::string CommonEditFieldDialog::s_source;
bool CommonEditFieldDialog::s_confirmed = false;

bool CommonEditFieldDialog::Show(HWND parent, 
                                  const std::string& field_name,
                                  const std::string& original_value,
                                  const std::string& scraped_value,
                                  float confidence,
                                  const std::string& source,
                                  std::string& out_new_value) {
    s_field_name = field_name;
    s_original_value = original_value;
    s_scraped_value = scraped_value;
    s_confidence = confidence;
    s_source = source;
    s_confirmed = false;
    s_new_value.clear();
    
    INT_PTR ret = DialogBox(
        core_api::get_my_instance(),
        MAKEINTRESOURCE(IDD_EDIT_FIELD),
        parent,
        CommonEditFieldDialog::DlgProc
    );
    
    if (s_confirmed) {
        out_new_value = s_new_value;
    }
    return s_confirmed;
}

INT_PTR CALLBACK CommonEditFieldDialog::DlgProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_INITDIALOG:
            DoInitDialog(wnd);
            return TRUE;
            
        case WM_COMMAND:
            switch (LOWORD(wp)) {
                case IDOK:
                    OnOK(wnd);
                    return TRUE;
                case IDCANCEL:
                    OnCancel(wnd);
                    return TRUE;
            }
            break;
    }
    return FALSE;
}

void CommonEditFieldDialog::DoInitDialog(HWND wnd) {
    std::wstring title = L"Edit Value: " + to_wstring(s_field_name);
    SetWindowTextW(wnd, title.c_str());
    
    SetDlgItemTextW(wnd, IDC_EDIT_EXISTING, to_wstring(s_original_value).c_str());
    SetDlgItemTextW(wnd, IDC_EDIT_ORIGINAL, to_wstring(s_scraped_value).c_str());
    
    std::string new_value = s_scraped_value.empty() ? s_original_value : s_scraped_value;
    SetDlgItemTextW(wnd, IDC_EDIT_VALUE, to_wstring(new_value).c_str());
    
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << s_confidence;
    SetDlgItemTextW(wnd, IDC_EDIT_CONFIDENCE, to_wstring(oss.str()).c_str());
    SetDlgItemTextW(wnd, IDC_EDIT_SOURCE, to_wstring(s_source).c_str());
}

void CommonEditFieldDialog::OnOK(HWND wnd) {
    wchar_t value_w[1024];
    GetDlgItemTextW(wnd, IDC_EDIT_VALUE, value_w, sizeof(value_w)/sizeof(wchar_t));
    s_new_value = to_string(value_w);
    s_confirmed = true;
    EndDialog(wnd, IDOK);
}

void CommonEditFieldDialog::OnCancel(HWND wnd) {
    s_confirmed = false;
    EndDialog(wnd, IDCANCEL);
}

std::vector<EnhancementResult>* EnhanceConfirmDialog::s_results = nullptr;
std::vector<bool>* EnhanceConfirmDialog::s_selected = nullptr;
const EnhancementOptions* EnhanceConfirmDialog::s_options = nullptr;
const std::vector<TrackInput>* EnhanceConfirmDialog::s_original_inputs = nullptr;
bool EnhanceConfirmDialog::s_confirmed = false;
std::map<std::string, bool> EnhanceConfirmDialog::s_field_selection;

bool DialogManager::ShowEnhanceConfirmDialog(HWND parent,
                                              std::vector<EnhancementResult>& results,
                                              std::vector<bool>& selected,
                                              const EnhancementOptions& options,
                                              const std::vector<TrackInput>& original_inputs) {
    EnhanceConfirmDialog::s_results = &results;
    EnhanceConfirmDialog::s_selected = &selected;
    EnhanceConfirmDialog::s_options = &options;
    EnhanceConfirmDialog::s_original_inputs = &original_inputs;
    EnhanceConfirmDialog::s_confirmed = false;
    
    INT_PTR ret = DialogBox(
        core_api::get_my_instance(),
        MAKEINTRESOURCE(IDD_CONFIRM_ENHANCEMENT),
        parent,
        EnhanceConfirmDialog::DlgProc
    );
    
    EnhanceConfirmDialog::s_results = nullptr;
    EnhanceConfirmDialog::s_selected = nullptr;
    EnhanceConfirmDialog::s_options = nullptr;
    EnhanceConfirmDialog::s_original_inputs = nullptr;
    
    return EnhanceConfirmDialog::s_confirmed;
}

INT_PTR CALLBACK EnhanceConfirmDialog::DlgProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_INITDIALOG:
            DoInitDialog(wnd);
            return TRUE;
            
        case WM_COMMAND:
            switch (LOWORD(wp)) {
                case IDOK:
                    OnOK(wnd);
                    return TRUE;
                case IDCANCEL:
                    OnCancel(wnd);
                    return TRUE;
                case IDC_SELECT_ALL:
                    OnSelectAll(wnd);
                    return TRUE;
                case IDC_SELECT_NONE:
                    OnSelectNone(wnd);
                    return TRUE;
                case IDC_SELECT_SUCCESS:
                    OnSelectSuccess(wnd);
                    return TRUE;
            }
            break;
            
        case WM_NOTIFY: {
            LPNMHDR pnmh = reinterpret_cast<LPNMHDR>(lp);
            if (pnmh->idFrom == IDC_ENHANCE_LISTVIEW) {
                if (pnmh->code == NM_DBLCLK) {
                    LPNMITEMACTIVATE pnmitem = reinterpret_cast<LPNMITEMACTIVATE>(lp);
                    if (pnmitem->iItem >= 0) {
                        OnEditItemAt(wnd, pnmitem->iItem, pnmitem->iSubItem);
                    }
                    return TRUE;
                }
            }
            break;
        }
    }
    return FALSE;
}

void EnhanceConfirmDialog::DoInitDialog(HWND wnd) {
    InitFieldCheckboxes(wnd);
    
    HWND hList = GetDlgItem(wnd, IDC_ENHANCE_LISTVIEW);
    if (!hList) return;
    
    ListView_SetExtendedListViewStyle(hList, LVS_EX_FULLROWSELECT | LVS_EX_CHECKBOXES | LVS_EX_HEADERDRAGDROP);
    
    LVCOLUMNW lvc = {0};
    lvc.mask = LVCF_TEXT | LVCF_WIDTH;
    
    struct ColumnInfo { const wchar_t* name; int width; };
    ColumnInfo columns[] = {
        {L"Track ID", 100},
        {L"Title ZH", 150},
        {L"Album ZH", 150},
        {L"Artist ZH", 150},
        {L"Genre", 100},
        {L"Edition", 100},
        {L"Confidence", 80},
        {L"Success", 60}
    };
    
    for (int i = 0; i < _countof(columns); ++i) {
        lvc.pszText = const_cast<wchar_t*>(columns[i].name);
        lvc.cx = columns[i].width;
        ListView_InsertColumn(hList, i, &lvc);
    }
    
    PopulateListView(wnd);
}

void EnhanceConfirmDialog::InitFieldCheckboxes(HWND wnd) {
    if (s_field_selection.empty()) {
        s_field_selection["title_zh"] = true;
        s_field_selection["album_zh"] = true;
        s_field_selection["artist_zh"] = true;
        s_field_selection["genre"] = true;
        s_field_selection["edition"] = true;
    }
    
    CheckDlgButton(wnd, IDC_ENHANCE_FIELD_TITLE_ZH, s_field_selection["title_zh"] ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(wnd, IDC_ENHANCE_FIELD_ALBUM_ZH, s_field_selection["album_zh"] ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(wnd, IDC_ENHANCE_FIELD_ARTIST_ZH, s_field_selection["artist_zh"] ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(wnd, IDC_ENHANCE_FIELD_GENRE, s_field_selection["genre"] ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(wnd, IDC_ENHANCE_FIELD_EDITION, s_field_selection["edition"] ? BST_CHECKED : BST_UNCHECKED);
}

void EnhanceConfirmDialog::SaveFieldSelection(HWND wnd) {
    s_field_selection["title_zh"] = IsDlgButtonChecked(wnd, IDC_ENHANCE_FIELD_TITLE_ZH) == BST_CHECKED;
    s_field_selection["album_zh"] = IsDlgButtonChecked(wnd, IDC_ENHANCE_FIELD_ALBUM_ZH) == BST_CHECKED;
    s_field_selection["artist_zh"] = IsDlgButtonChecked(wnd, IDC_ENHANCE_FIELD_ARTIST_ZH) == BST_CHECKED;
    s_field_selection["genre"] = IsDlgButtonChecked(wnd, IDC_ENHANCE_FIELD_GENRE) == BST_CHECKED;
    s_field_selection["edition"] = IsDlgButtonChecked(wnd, IDC_ENHANCE_FIELD_EDITION) == BST_CHECKED;
}

bool EnhanceConfirmDialog::IsFieldSelected(const std::string& field) {
    auto it = s_field_selection.find(field);
    if (it != s_field_selection.end()) {
        return it->second;
    }
    return false;
}

void EnhanceConfirmDialog::PopulateListView(HWND wnd) {
    HWND hList = GetDlgItem(wnd, IDC_ENHANCE_LISTVIEW);
    if (!hList || !s_results) return;
    
    ListView_DeleteAllItems(hList);
    
    for (size_t i = 0; i < s_results->size(); ++i) {
        const auto& result = (*s_results)[i];
        bool is_failed = !result.success;
        
        LVITEMW lvi = {0};
        lvi.mask = LVIF_TEXT;
        lvi.iItem = static_cast<int>(i);
        
        lvi.iSubItem = 0;
        std::wstring track_id_w;
        if (is_failed) {
            track_id_w = to_wstring("[FAILED] " + result.track_id.substr(0, 20) + (result.track_id.length() > 20 ? "..." : ""));
        } else {
            track_id_w = to_wstring(result.track_id.substr(0, 24) + (result.track_id.length() > 24 ? "..." : ""));
        }
        lvi.pszText = const_cast<wchar_t*>(track_id_w.c_str());
        ListView_InsertItem(hList, &lvi);
        
        std::wstring title_zh_w = is_failed ? L"(no data)" : to_wstring(result.title_zh);
        std::wstring album_zh_w = is_failed ? L"" : to_wstring(result.album_zh);
        std::wstring artist_zh_w = is_failed ? L"" : to_wstring(result.artist_zh);
        std::wstring genre_w = is_failed ? L"" : to_wstring(result.genre_value);
        std::wstring edition_w = is_failed ? L"" : to_wstring(result.edition_value);
        
        ListView_SetItemText(hList, static_cast<int>(i), 1, const_cast<wchar_t*>(title_zh_w.c_str()));
        ListView_SetItemText(hList, static_cast<int>(i), 2, const_cast<wchar_t*>(album_zh_w.c_str()));
        ListView_SetItemText(hList, static_cast<int>(i), 3, const_cast<wchar_t*>(artist_zh_w.c_str()));
        ListView_SetItemText(hList, static_cast<int>(i), 4, const_cast<wchar_t*>(genre_w.c_str()));
        ListView_SetItemText(hList, static_cast<int>(i), 5, const_cast<wchar_t*>(edition_w.c_str()));
        
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2) << (is_failed ? 0.0f : result.translation_confidence);
        std::wstring conf_w = to_wstring(oss.str());
        ListView_SetItemText(hList, static_cast<int>(i), 6, const_cast<wchar_t*>(conf_w.c_str()));
        
        std::wstring success_w = is_failed ? L"Failed" : L"Yes";
        ListView_SetItemText(hList, static_cast<int>(i), 7, const_cast<wchar_t*>(success_w.c_str()));
        
        bool checked = !is_failed;
        if (s_selected && i < s_selected->size()) {
            checked = (*s_selected)[i];
        }
        ListView_SetCheckState(hList, static_cast<int>(i), checked ? TRUE : FALSE);
    }
}

void EnhanceConfirmDialog::OnSelectAll(HWND wnd) {
    HWND hList = GetDlgItem(wnd, IDC_ENHANCE_LISTVIEW);
    if (!hList) return;
    
    int count = ListView_GetItemCount(hList);
    for (int i = 0; i < count; ++i) {
        ListView_SetCheckState(hList, i, TRUE);
    }
}

void EnhanceConfirmDialog::OnSelectNone(HWND wnd) {
    HWND hList = GetDlgItem(wnd, IDC_ENHANCE_LISTVIEW);
    if (!hList) return;
    
    int count = ListView_GetItemCount(hList);
    for (int i = 0; i < count; ++i) {
        ListView_SetCheckState(hList, i, FALSE);
    }
}

void EnhanceConfirmDialog::OnSelectSuccess(HWND wnd) {
    if (!s_selected || !s_results) return;
    
    HWND hList = GetDlgItem(wnd, IDC_ENHANCE_LISTVIEW);
    
    for (size_t i = 0; i < s_selected->size(); ++i) {
        const auto& result = (*s_results)[i];
        bool is_success = result.success;
        (*s_selected)[i] = is_success;
        
        if (hList) {
            ListView_SetCheckState(hList, static_cast<int>(i), is_success ? TRUE : FALSE);
        }
    }
}

void EnhanceConfirmDialog::OnEditItemAt(HWND wnd, int item_index, int sub_item) {
    if (!s_results || item_index < 0 || item_index >= static_cast<int>(s_results->size())) {
        return;
    }
    
    static const char* column_fields[] = {
        "track_id", "title_zh", "album_zh", "artist_zh", "genre_value", "edition_value", "confidence", "success"
    };
    
    if (sub_item < 0 || sub_item >= 8) {
        sub_item = 1;
    }
    
    if (sub_item == 0 || sub_item == 6 || sub_item == 7) {
        return;
    }
    
    EnhancementResult& result = (*s_results)[item_index];
    std::string field_name = column_fields[sub_item];
    std::string original_value;
    std::string scraped_value;
    float confidence = 0.0f;
    std::string source = "AI";
    
    if (s_original_inputs && item_index < static_cast<int>(s_original_inputs->size())) {
        const auto& input = (*s_original_inputs)[item_index];
        if (field_name == "title_zh") original_value = input.title;
        else if (field_name == "album_zh") original_value = input.album;
        else if (field_name == "artist_zh") original_value = input.artist;
    }
    
    if (field_name == "title_zh") {
        scraped_value = result.title_zh;
        confidence = result.translation_confidence;
    }
    else if (field_name == "album_zh") {
        scraped_value = result.album_zh;
        confidence = result.translation_confidence;
    }
    else if (field_name == "artist_zh") {
        scraped_value = result.artist_zh;
        confidence = result.translation_confidence;
    }
    else if (field_name == "genre_value") {
        scraped_value = result.genre_value;
        confidence = result.genre_confidence;
        source = "AI Classification";
    }
    else if (field_name == "edition_value") {
        scraped_value = result.edition_value;
        confidence = result.edition_confidence;
        source = "AI Identification";
    }
    
    std::string new_value;
    if (CommonEditFieldDialog::Show(wnd, field_name, original_value, scraped_value, confidence, source, new_value)) {
        if (field_name == "title_zh") result.title_zh = new_value;
        else if (field_name == "album_zh") result.album_zh = new_value;
        else if (field_name == "artist_zh") result.artist_zh = new_value;
        else if (field_name == "genre_value") result.genre_value = new_value;
        else if (field_name == "edition_value") result.edition_value = new_value;
        
        PopulateListView(wnd);
    }
}

void EnhanceConfirmDialog::OnOK(HWND wnd) {
    SaveFieldSelection(wnd);
    
    HWND hList = GetDlgItem(wnd, IDC_ENHANCE_LISTVIEW);
    if (hList && s_selected) {
        for (size_t i = 0; i < s_selected->size(); ++i) {
            (*s_selected)[i] = ListView_GetCheckState(hList, static_cast<int>(i)) != FALSE;
        }
    }
    s_confirmed = true;
    EndDialog(wnd, IDOK);
}

void EnhanceConfirmDialog::OnCancel(HWND wnd) {
    s_confirmed = false;
    EndDialog(wnd, IDCANCEL);
}

}
