#include "dialogs.h"
#include "../include/types.h"
#include "resource.h"
#include "../core/logger.h"
#include <foobar2000/SDK/foobar2000.h>
#include <windowsx.h>
#include <commctrl.h>
#include <sstream>
#include <iomanip>
#include <vector>

namespace ai_metadata {

// ==================== Anchoring System ====================
// 在 WM_INITDIALOG 时记录每个控件初始位置（一次性），在 WM_SIZE 时根据锚定规则重新布局
// 避免多次 WM_SIZE 后坐标漂移；比 GetWindowRect 方式更稳定
enum : uint32_t {
    AF_LEFT   = 0x01,  // x 固定距左边（默认）
    AF_RIGHT  = 0x02,  // x 固定距右边
    AF_TOP    = 0x04,  // y 固定距顶部（默认）
    AF_BOTTOM = 0x08,  // y 固定距底部
    // 组合：AF_LEFT|AF_RIGHT = 横向填充；AF_TOP|AF_BOTTOM = 纵向填充
};

struct AnchorEntry {
    int id;
    uint32_t flags;
};

struct AnchorState {
    int id;
    uint32_t flags;
    int init_left;
    int init_top;
    int init_width;
    int init_height;
    int init_dlg_cx;
    int init_dlg_cy;
};

// 对话框是模态的，不会同时打开，所以用一份全局状态即可
static std::vector<AnchorState> g_anchor_states;

static void InitAnchors(HWND wnd, const AnchorEntry* entries, int count) {
    RECT dlg_rc;
    GetClientRect(wnd, &dlg_rc);
    int dlg_cx = dlg_rc.right;
    int dlg_cy = dlg_rc.bottom;

    g_anchor_states.clear();
    g_anchor_states.reserve(count);
    for (int i = 0; i < count; ++i) {
        HWND hCtrl = GetDlgItem(wnd, entries[i].id);
        if (!hCtrl) continue;
        RECT rc;
        GetWindowRect(hCtrl, &rc);
        MapWindowPoints(nullptr, wnd, reinterpret_cast<LPPOINT>(&rc), 2);
        g_anchor_states.push_back({
            entries[i].id,
            entries[i].flags,
            rc.left, rc.top,
            rc.right - rc.left, rc.bottom - rc.top,
            dlg_cx, dlg_cy
        });
    }
}

static void ApplyAnchors(HWND wnd, int cx, int cy) {
    for (const auto& s : g_anchor_states) {
        HWND hCtrl = GetDlgItem(wnd, s.id);
        if (!hCtrl) continue;

        int x = s.init_left;
        int y = s.init_top;
        int w = s.init_width;
        int h = s.init_height;

        // 横向
        uint32_t hf = s.flags & (AF_LEFT | AF_RIGHT);
        if (hf == (AF_LEFT | AF_RIGHT)) {
            // 横向填充：左边距不变，右边距不变，宽度随 cx 变化
            int right_margin = s.init_dlg_cx - (s.init_left + s.init_width);
            x = s.init_left;
            w = cx - s.init_left - right_margin;
            if (w < 10) w = 10;
        } else if (hf == AF_RIGHT) {
            // 右锚定：保持距右边的距离
            int right_margin = s.init_dlg_cx - (s.init_left + s.init_width);
            x = cx - right_margin - s.init_width;
        }
        // 否则左锚定：x = init_left

        // 纵向
        uint32_t vf = s.flags & (AF_TOP | AF_BOTTOM);
        if (vf == (AF_TOP | AF_BOTTOM)) {
            // 纵向填充
            int bottom_margin = s.init_dlg_cy - (s.init_top + s.init_height);
            y = s.init_top;
            h = cy - s.init_top - bottom_margin;
            if (h < 10) h = 10;
        } else if (vf == AF_BOTTOM) {
            // 底锚定
            int bottom_margin = s.init_dlg_cy - (s.init_top + s.init_height);
            y = cy - bottom_margin - s.init_height;
        }
        // 否则顶锚定：y = init_top

        MoveWindow(hCtrl, x, y, w, h, TRUE);
    }
}

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

// Normalize 对话框静态成员
std::vector<std::string>* NormalizeFieldDialog::s_selected_fields = nullptr;
bool NormalizeFieldDialog::s_confirmed = false;

std::string NormalizeConfirmDialog::s_field;
NormalizeResult* NormalizeConfirmDialog::s_result = nullptr;
std::vector<bool>* NormalizeConfirmDialog::s_selected_groups = nullptr;
bool NormalizeConfirmDialog::s_confirmed = false;

std::string NormalizeEditDialog::s_canonical_name;
std::vector<std::string> NormalizeEditDialog::s_aliases;
std::string NormalizeEditDialog::s_reason;
bool NormalizeEditDialog::s_confirmed = false;

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
}

INT_PTR CALLBACK ConfirmResultDialog::DlgProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_INITDIALOG:
            DoInitDialog(wnd);
            return TRUE;

        case WM_SIZE:
            ApplyAnchors(wnd, LOWORD(lp), HIWORD(lp));
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
    
    const wchar_t* col_names[] = {L"Track ID", L"Title", L"Artist", L"Album", L"Year", L"Genre", L"Track#", L"Disc#", L"Composer", L"Lyricist", L"Conductor", L"Performer", L"Label", L"Country", L"Catalog#", L"Confidence", L"Source"};
    int col_widths[] = {150, 180, 140, 140, 55, 90, 45, 45, 90, 90, 90, 90, 90, 70, 90, 70, 75};

    for (int i = 0; i < 17; ++i) {
        lvc.pszText = const_cast<wchar_t*>(col_names[i]);
        lvc.cx = col_widths[i];
        ListView_InsertColumn(hList, i, &lvc);
    }
    
    PopulateListView(wnd);

    // 记录所有需要随窗口缩放的控件初始位置
    // 锚定规则：顶部 GroupBox 仅横向拉伸；ListView 纵横向填充；底部按钮/文字左下或右下锚定
    static const AnchorEntry entries[] = {
        {IDC_FIELD_GROUP,         AF_LEFT | AF_RIGHT | AF_TOP},
        {IDC_RESULT_LISTVIEW,     AF_LEFT | AF_RIGHT | AF_TOP | AF_BOTTOM},
        {IDC_SELECT_ALL,          AF_LEFT | AF_BOTTOM},
        {IDC_SELECT_NONE,         AF_LEFT | AF_BOTTOM},
        {IDC_SELECT_SUCCESS,      AF_LEFT | AF_BOTTOM},
        {IDC_EDIT_ITEM,           AF_LEFT | AF_BOTTOM},
        {IDC_HINT_DOUBLE_CLICK,   AF_LEFT | AF_BOTTOM},
        {IDOK,                    AF_RIGHT | AF_BOTTOM},
        {IDCANCEL,                AF_RIGHT | AF_BOTTOM},
        {IDC_HINT_CHECKED,        AF_LEFT | AF_BOTTOM},
    };
    InitAnchors(wnd, entries, _countof(entries));
}

void ConfirmResultDialog::InitFieldCheckboxes(HWND wnd) {
    if (s_field_selection.empty()) {
        s_field_selection["title"] = true;
        s_field_selection["artist"] = true;
        s_field_selection["album"] = true;
        s_field_selection["year"] = true;
        s_field_selection["genre"] = true;
        s_field_selection["track_number"] = true;
        s_field_selection["disc_number"] = true;
        s_field_selection["composer"] = true;
        s_field_selection["lyricist"] = true;
        s_field_selection["conductor"] = false;
        s_field_selection["performer"] = false;
        s_field_selection["label"] = true;
        s_field_selection["country"] = false;
        s_field_selection["catalog_number"] = false;
    }

    CheckDlgButton(wnd, IDC_FIELD_TITLE, s_field_selection["title"] ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(wnd, IDC_FIELD_ARTIST, s_field_selection["artist"] ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(wnd, IDC_FIELD_ALBUM, s_field_selection["album"] ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(wnd, IDC_FIELD_YEAR, s_field_selection["year"] ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(wnd, IDC_FIELD_GENRE, s_field_selection["genre"] ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(wnd, IDC_FIELD_TRACK_NUMBER, s_field_selection["track_number"] ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(wnd, IDC_FIELD_DISC_NUMBER, s_field_selection["disc_number"] ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(wnd, IDC_FIELD_COMPOSER, s_field_selection["composer"] ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(wnd, IDC_FIELD_LYRICIST, s_field_selection["lyricist"] ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(wnd, IDC_FIELD_CONDUCTOR, s_field_selection["conductor"] ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(wnd, IDC_FIELD_PERFORMER, s_field_selection["performer"] ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(wnd, IDC_FIELD_LABEL, s_field_selection["label"] ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(wnd, IDC_FIELD_COUNTRY, s_field_selection["country"] ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(wnd, IDC_FIELD_CATALOG_NUMBER, s_field_selection["catalog_number"] ? BST_CHECKED : BST_UNCHECKED);
}

void ConfirmResultDialog::SaveFieldSelection(HWND wnd) {
    s_field_selection["title"] = IsDlgButtonChecked(wnd, IDC_FIELD_TITLE) == BST_CHECKED;
    s_field_selection["artist"] = IsDlgButtonChecked(wnd, IDC_FIELD_ARTIST) == BST_CHECKED;
    s_field_selection["album"] = IsDlgButtonChecked(wnd, IDC_FIELD_ALBUM) == BST_CHECKED;
    s_field_selection["year"] = IsDlgButtonChecked(wnd, IDC_FIELD_YEAR) == BST_CHECKED;
    s_field_selection["genre"] = IsDlgButtonChecked(wnd, IDC_FIELD_GENRE) == BST_CHECKED;
    s_field_selection["track_number"] = IsDlgButtonChecked(wnd, IDC_FIELD_TRACK_NUMBER) == BST_CHECKED;
    s_field_selection["disc_number"] = IsDlgButtonChecked(wnd, IDC_FIELD_DISC_NUMBER) == BST_CHECKED;
    s_field_selection["composer"] = IsDlgButtonChecked(wnd, IDC_FIELD_COMPOSER) == BST_CHECKED;
    s_field_selection["lyricist"] = IsDlgButtonChecked(wnd, IDC_FIELD_LYRICIST) == BST_CHECKED;
    s_field_selection["conductor"] = IsDlgButtonChecked(wnd, IDC_FIELD_CONDUCTOR) == BST_CHECKED;
    s_field_selection["performer"] = IsDlgButtonChecked(wnd, IDC_FIELD_PERFORMER) == BST_CHECKED;
    s_field_selection["label"] = IsDlgButtonChecked(wnd, IDC_FIELD_LABEL) == BST_CHECKED;
    s_field_selection["country"] = IsDlgButtonChecked(wnd, IDC_FIELD_COUNTRY) == BST_CHECKED;
    s_field_selection["catalog_number"] = IsDlgButtonChecked(wnd, IDC_FIELD_CATALOG_NUMBER) == BST_CHECKED;
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

    // 优先返回 scraped_fields 中的值；若不存在则回退到原始输入值
    // 这样显示和保存内容保持一致（保存时未刮削字段会保留原值）
    auto get_field_value = [](const TrackScrapingResult& result,
                              const std::string& field_name,
                              const TrackInput* original_input) -> std::string {
        auto it = result.scraped_fields.find(field_name);
        if (it != result.scraped_fields.end() && !it->second.value.empty()) {
            return it->second.value;
        }
        // 回退到原始输入值（country/catalog_number 不在 TrackInput，无法回退）
        if (original_input) {
            if (field_name == "title")          return original_input->title;
            if (field_name == "artist")         return original_input->artist;
            if (field_name == "album")          return original_input->album;
            if (field_name == "year")           return original_input->year;
            if (field_name == "genre")          return original_input->genre;
            if (field_name == "track_number")   return std::to_string(original_input->track_number);
            if (field_name == "disc_number")    return std::to_string(original_input->disc_number);
            if (field_name == "composer")       return original_input->composer;
            if (field_name == "lyricist")       return original_input->lyricist;
            if (field_name == "conductor")      return original_input->conductor;
            if (field_name == "performer")      return original_input->performer;
            if (field_name == "label")          return original_input->label;
        }
        return "";
    };

    for (size_t i = 0; i < s_results->size(); ++i) {
        const auto& result = (*s_results)[i];
        bool is_empty = !result.success || result.scraped_fields.empty();

        // 取原始输入以便回退显示
        const TrackInput* original_input = nullptr;
        if (s_original_inputs && i < s_original_inputs->size()) {
            original_input = &(*s_original_inputs)[i];
        }

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

        std::wstring title = is_empty ? L"(no data)" : to_wstring(get_field_value(result, "title", original_input));
        ListView_SetItemText(hList, static_cast<int>(i), 1, const_cast<wchar_t*>(title.c_str()));

        std::wstring artist = is_empty ? L"" : to_wstring(get_field_value(result, "artist", original_input));
        ListView_SetItemText(hList, static_cast<int>(i), 2, const_cast<wchar_t*>(artist.c_str()));

        std::wstring album = is_empty ? L"" : to_wstring(get_field_value(result, "album", original_input));
        ListView_SetItemText(hList, static_cast<int>(i), 3, const_cast<wchar_t*>(album.c_str()));

        std::wstring year = is_empty ? L"" : to_wstring(get_field_value(result, "year", original_input));
        ListView_SetItemText(hList, static_cast<int>(i), 4, const_cast<wchar_t*>(year.c_str()));

        std::wstring genre = is_empty ? L"" : to_wstring(get_field_value(result, "genre", original_input));
        ListView_SetItemText(hList, static_cast<int>(i), 5, const_cast<wchar_t*>(genre.c_str()));

        std::wstring track_num = is_empty ? L"" : to_wstring(get_field_value(result, "track_number", original_input));
        ListView_SetItemText(hList, static_cast<int>(i), 6, const_cast<wchar_t*>(track_num.c_str()));

        std::wstring disc_num = is_empty ? L"" : to_wstring(get_field_value(result, "disc_number", original_input));
        ListView_SetItemText(hList, static_cast<int>(i), 7, const_cast<wchar_t*>(disc_num.c_str()));

        std::wstring composer = is_empty ? L"" : to_wstring(get_field_value(result, "composer", original_input));
        ListView_SetItemText(hList, static_cast<int>(i), 8, const_cast<wchar_t*>(composer.c_str()));

        std::wstring lyricist = is_empty ? L"" : to_wstring(get_field_value(result, "lyricist", original_input));
        ListView_SetItemText(hList, static_cast<int>(i), 9, const_cast<wchar_t*>(lyricist.c_str()));

        std::wstring conductor = is_empty ? L"" : to_wstring(get_field_value(result, "conductor", original_input));
        ListView_SetItemText(hList, static_cast<int>(i), 10, const_cast<wchar_t*>(conductor.c_str()));

        std::wstring performer = is_empty ? L"" : to_wstring(get_field_value(result, "performer", original_input));
        ListView_SetItemText(hList, static_cast<int>(i), 11, const_cast<wchar_t*>(performer.c_str()));

        std::wstring label = is_empty ? L"" : to_wstring(get_field_value(result, "label", original_input));
        ListView_SetItemText(hList, static_cast<int>(i), 12, const_cast<wchar_t*>(label.c_str()));

        std::wstring country = is_empty ? L"" : to_wstring(get_field_value(result, "country", original_input));
        ListView_SetItemText(hList, static_cast<int>(i), 13, const_cast<wchar_t*>(country.c_str()));

        std::wstring catalog = is_empty ? L"" : to_wstring(get_field_value(result, "catalog_number", original_input));
        ListView_SetItemText(hList, static_cast<int>(i), 14, const_cast<wchar_t*>(catalog.c_str()));

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
        ListView_SetItemText(hList, static_cast<int>(i), 15, const_cast<wchar_t*>(conf_str.c_str()));

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
        ListView_SetItemText(hList, static_cast<int>(i), 16, const_cast<wchar_t*>(source_w.c_str()));

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
        "track_id", "title", "artist", "album", "year", "genre",
        "track_number", "disc_number", "composer", "lyricist",
        "conductor", "performer", "label", "country", "catalog_number",
        "confidence", "source"
    };

    if (sub_item < 0 || sub_item >= 17) {
        sub_item = 1;
    }

    // Non-editable: track_id (0), confidence (15), source (16)
    if (sub_item == 0 || sub_item == 15 || sub_item == 16) {
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
        else if (field_name == "genre") original_value = input.genre;
        else if (field_name == "track_number") original_value = std::to_string(input.track_number);
        else if (field_name == "disc_number") original_value = std::to_string(input.disc_number);
        else if (field_name == "composer") original_value = input.composer;
        else if (field_name == "lyricist") original_value = input.lyricist;
        else if (field_name == "conductor") original_value = input.conductor;
        else if (field_name == "performer") original_value = input.performer;
        else if (field_name == "label") original_value = input.label;
        // country / catalog_number: not in TrackInput (only fetched from sources)
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

        case WM_SIZE:
            ApplyAnchors(wnd, LOWORD(lp), HIWORD(lp));
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
        {L"Confidence", 80},
        {L"Success", 60}
    };
    
    for (int i = 0; i < _countof(columns); ++i) {
        lvc.pszText = const_cast<wchar_t*>(columns[i].name);
        lvc.cx = columns[i].width;
        ListView_InsertColumn(hList, i, &lvc);
    }
    
    PopulateListView(wnd);

    // 锚定规则同 ConfirmResultDialog：顶部 GroupBox 横向拉伸；ListView 填充；底部按钮/文字左下/右下锚定
    static const AnchorEntry entries[] = {
        {IDC_FIELD_GROUP,         AF_LEFT | AF_RIGHT | AF_TOP},
        {IDC_ENHANCE_LISTVIEW,    AF_LEFT | AF_RIGHT | AF_TOP | AF_BOTTOM},
        {IDC_SELECT_ALL,          AF_LEFT | AF_BOTTOM},
        {IDC_SELECT_NONE,         AF_LEFT | AF_BOTTOM},
        {IDC_SELECT_SUCCESS,      AF_LEFT | AF_BOTTOM},
        {IDC_HINT_DOUBLE_CLICK,   AF_LEFT | AF_BOTTOM},
        {IDOK,                    AF_RIGHT | AF_BOTTOM},
        {IDCANCEL,                AF_RIGHT | AF_BOTTOM},
        {IDC_HINT_CHECKED,        AF_LEFT | AF_BOTTOM},
    };
    InitAnchors(wnd, entries, _countof(entries));
}

void EnhanceConfirmDialog::InitFieldCheckboxes(HWND wnd) {
    if (s_field_selection.empty()) {
        s_field_selection["title_zh"] = true;
        s_field_selection["album_zh"] = true;
        s_field_selection["artist_zh"] = true;
    }

    CheckDlgButton(wnd, IDC_ENHANCE_FIELD_TITLE_ZH, s_field_selection["title_zh"] ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(wnd, IDC_ENHANCE_FIELD_ALBUM_ZH, s_field_selection["album_zh"] ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(wnd, IDC_ENHANCE_FIELD_ARTIST_ZH, s_field_selection["artist_zh"] ? BST_CHECKED : BST_UNCHECKED);
}

void EnhanceConfirmDialog::SaveFieldSelection(HWND wnd) {
    s_field_selection["title_zh"] = IsDlgButtonChecked(wnd, IDC_ENHANCE_FIELD_TITLE_ZH) == BST_CHECKED;
    s_field_selection["album_zh"] = IsDlgButtonChecked(wnd, IDC_ENHANCE_FIELD_ALBUM_ZH) == BST_CHECKED;
    s_field_selection["artist_zh"] = IsDlgButtonChecked(wnd, IDC_ENHANCE_FIELD_ARTIST_ZH) == BST_CHECKED;
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
        
        // 取原始输入以便在 *_zh 为空时回退显示（与 scrape 行为一致）
        const TrackInput* original_input = nullptr;
        if (s_original_inputs && i < s_original_inputs->size()) {
            original_input = &(*s_original_inputs)[i];
        }
        
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
        
        // 显示回退：当 *_zh 为空（已是中文无需翻译）时，显示原始值，避免 UI 空白
        std::string title_disp = result.title_zh;
        std::string album_disp = result.album_zh;
        std::string artist_disp = result.artist_zh;
        if (!is_failed && original_input) {
            if (title_disp.empty())  title_disp  = original_input->title;
            if (album_disp.empty())  album_disp  = original_input->album;
            if (artist_disp.empty()) artist_disp = original_input->artist;
        }
        
        std::wstring title_zh_w = is_failed ? L"(no data)" : to_wstring(title_disp);
        std::wstring album_zh_w = is_failed ? L"" : to_wstring(album_disp);
        std::wstring artist_zh_w = is_failed ? L"" : to_wstring(artist_disp);

        ListView_SetItemText(hList, static_cast<int>(i), 1, const_cast<wchar_t*>(title_zh_w.c_str()));
        ListView_SetItemText(hList, static_cast<int>(i), 2, const_cast<wchar_t*>(album_zh_w.c_str()));
        ListView_SetItemText(hList, static_cast<int>(i), 3, const_cast<wchar_t*>(artist_zh_w.c_str()));

        // Confidence display: when success but all translation fields are empty,
        // the track was already Chinese - show "N/A" instead of misleading "0.00".
        std::wstring conf_w;
        if (is_failed) {
            conf_w = L"N/A";
        } else if (result.title_zh.empty() && result.album_zh.empty() && result.artist_zh.empty()) {
            conf_w = L"N/A (Chinese)";
        } else {
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(2) << result.translation_confidence;
            conf_w = to_wstring(oss.str());
        }
        ListView_SetItemText(hList, static_cast<int>(i), 4, const_cast<wchar_t*>(conf_w.c_str()));

        std::wstring success_w = is_failed ? L"Failed" : (conf_w == L"N/A (Chinese)" ? L"Skipped" : L"Yes");
        ListView_SetItemText(hList, static_cast<int>(i), 5, const_cast<wchar_t*>(success_w.c_str()));

        // Already-Chinese tracks have nothing to write; default to unchecked.
        bool is_skipped = !is_failed && result.title_zh.empty() && result.album_zh.empty() && result.artist_zh.empty();
        bool checked = !is_failed && !is_skipped;
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
        // "Success" = has actual translation to write. Skip already-Chinese tracks.
        bool has_translation = result.success &&
            !(result.title_zh.empty() && result.album_zh.empty() && result.artist_zh.empty());
        (*s_selected)[i] = has_translation;

        if (hList) {
            ListView_SetCheckState(hList, static_cast<int>(i), has_translation ? TRUE : FALSE);
        }
    }
}

void EnhanceConfirmDialog::OnEditItemAt(HWND wnd, int item_index, int sub_item) {
    if (!s_results || item_index < 0 || item_index >= static_cast<int>(s_results->size())) {
        return;
    }
    
    static const char* column_fields[] = {
        "track_id", "title_zh", "album_zh", "artist_zh", "confidence", "success"
    };

    if (sub_item < 0 || sub_item >= 6) {
        sub_item = 1;
    }

    if (sub_item == 0 || sub_item == 4 || sub_item == 5) {
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

    std::string new_value;
    if (CommonEditFieldDialog::Show(wnd, field_name, original_value, scraped_value, confidence, source, new_value)) {
        if (field_name == "title_zh") result.title_zh = new_value;
        else if (field_name == "album_zh") result.album_zh = new_value;
        else if (field_name == "artist_zh") result.artist_zh = new_value;
        
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

// ==================== Normalize Field Dialog ====================

bool DialogManager::ShowNormalizeFieldDialog(HWND parent, std::vector<std::string>& selected_fields) {
    NormalizeFieldDialog::s_selected_fields = &selected_fields;
    NormalizeFieldDialog::s_confirmed = false;

    INT_PTR ret = DialogBoxParam(
        core_api::get_my_instance(),
        MAKEINTRESOURCE(IDD_NORMALIZE_FIELD),
        parent,
        NormalizeFieldDialog::DlgProc,
        0
    );

    NormalizeFieldDialog::s_selected_fields = nullptr;
    return NormalizeFieldDialog::s_confirmed;
}

INT_PTR CALLBACK NormalizeFieldDialog::DlgProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_INITDIALOG:
            DoInitDialog(wnd);
            return TRUE;
        case WM_COMMAND:
            switch (LOWORD(wp)) {
                case IDOK: OnOK(wnd); return TRUE;
                case IDCANCEL: OnCancel(wnd); return TRUE;
                case IDC_NORMALIZE_SELECT_ALL: {
                    HWND hList = GetDlgItem(wnd, IDC_NORMALIZE_FIELD_LIST);
                    int count = ListView_GetItemCount(hList);
                    for (int i = 0; i < count; ++i) {
                        ListView_SetCheckState(hList, i, TRUE);
                    }
                    return TRUE;
                }
                case IDC_NORMALIZE_SELECT_NONE: {
                    HWND hList = GetDlgItem(wnd, IDC_NORMALIZE_FIELD_LIST);
                    int count = ListView_GetItemCount(hList);
                    for (int i = 0; i < count; ++i) {
                        ListView_SetCheckState(hList, i, FALSE);
                    }
                    return TRUE;
                }
            }
            break;
    }
    return FALSE;
}

void NormalizeFieldDialog::DoInitDialog(HWND wnd) {
    HWND hList = GetDlgItem(wnd, IDC_NORMALIZE_FIELD_LIST);
    if (!hList) return;

    ListView_SetExtendedListViewStyle(hList, LVS_EX_FULLROWSELECT | LVS_EX_CHECKBOXES);

    // 简单单列显示
    LVCOLUMNW lvc = {0};
    lvc.mask = LVCF_TEXT | LVCF_WIDTH;
    lvc.pszText = const_cast<wchar_t*>(L"Field");
    lvc.cx = 220;
    ListView_InsertColumn(hList, 0, &lvc);

    // 当前仅支持 artist；其他字段灰显/标记为未来支持
    struct FieldEntry { const wchar_t* name; bool enabled; };
    FieldEntry fields[] = {
        {L"artist", true},
        {L"album_artist", false},
        {L"album", false},
        {L"genre", false},
        {L"composer", false},
        {L"label", false}
    };

    ListView_DeleteAllItems(hList);
    for (int i = 0; i < _countof(fields); ++i) {
        LVITEMW lvi = {0};
        lvi.mask = LVIF_TEXT;
        lvi.iItem = i;
        lvi.iSubItem = 0;
        std::wstring label = fields[i].name;
        if (!fields[i].enabled) label += L" (coming soon)";
        lvi.pszText = const_cast<wchar_t*>(label.c_str());
        ListView_InsertItem(hList, &lvi);
        ListView_SetCheckState(hList, i, fields[i].enabled ? TRUE : FALSE);
        // 未来支持的字段禁用勾选（通过 LVIF_STATE 方式较复杂，这里通过 OnOK 时过滤）
    }
}

void NormalizeFieldDialog::OnOK(HWND wnd) {
    HWND hList = GetDlgItem(wnd, IDC_NORMALIZE_FIELD_LIST);
    if (!hList || !s_selected_fields) {
        s_confirmed = false;
        EndDialog(wnd, IDCANCEL);
        return;
    }

    s_selected_fields->clear();
    int count = ListView_GetItemCount(hList);
    for (int i = 0; i < count; ++i) {
        if (ListView_GetCheckState(hList, i)) {
            wchar_t buf[64] = {0};
            LVITEMW lvi = {0};
            lvi.iItem = i;
            lvi.iSubItem = 0;
            lvi.mask = LVIF_TEXT;
            lvi.pszText = buf;
            lvi.cchTextMax = _countof(buf);
            ListView_GetItem(hList, &lvi);
            std::wstring wstr(buf);
            // 去掉可能的 " (coming soon)" 后缀
            size_t pos = wstr.find(L" (coming soon)");
            if (pos != std::wstring::npos) wstr = wstr.substr(0, pos);
            // UTF-16 -> UTF-8
            std::string s;
            int sz = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
            if (sz > 0) {
                s.resize(sz - 1);
                WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &s[0], sz, nullptr, nullptr);
            }
            if (!s.empty()) s_selected_fields->push_back(s);
        }
    }
    s_confirmed = true;
    EndDialog(wnd, IDOK);
}

void NormalizeFieldDialog::OnCancel(HWND wnd) {
    s_confirmed = false;
    EndDialog(wnd, IDCANCEL);
}

// ==================== Normalize Confirm Dialog ====================

bool DialogManager::ShowNormalizeConfirmDialog(HWND parent,
                                                const std::string& field,
                                                NormalizeResult& result,
                                                std::vector<bool>& selected_groups) {
    NormalizeConfirmDialog::s_field = field;
    NormalizeConfirmDialog::s_result = &result;
    NormalizeConfirmDialog::s_selected_groups = &selected_groups;
    NormalizeConfirmDialog::s_confirmed = false;

    INT_PTR ret = DialogBoxParam(
        core_api::get_my_instance(),
        MAKEINTRESOURCE(IDD_NORMALIZE_CONFIRM),
        parent,
        NormalizeConfirmDialog::DlgProc,
        0
    );

    NormalizeConfirmDialog::s_result = nullptr;
    NormalizeConfirmDialog::s_selected_groups = nullptr;
    return NormalizeConfirmDialog::s_confirmed;
}

INT_PTR CALLBACK NormalizeConfirmDialog::DlgProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_INITDIALOG:
            DoInitDialog(wnd);
            return TRUE;
        case WM_SIZE:
            ApplyAnchors(wnd, LOWORD(lp), HIWORD(lp));
            return TRUE;
        case WM_COMMAND:
            switch (LOWORD(wp)) {
                case IDOK: OnOK(wnd); return TRUE;
                case IDCANCEL: OnCancel(wnd); return TRUE;
                case IDC_NORMALIZE_SELECT_ALL: OnSelectAll(wnd); return TRUE;
                case IDC_NORMALIZE_SELECT_NONE: OnSelectNone(wnd); return TRUE;
            }
            break;
        case WM_NOTIFY: {
            LPNMHDR pnmh = reinterpret_cast<LPNMHDR>(lp);
            if (pnmh->idFrom == IDC_NORMALIZE_CONFIRM_LIST) {
                if (pnmh->code == NM_DBLCLK) {
                    LPNMITEMACTIVATE pnmitem = reinterpret_cast<LPNMITEMACTIVATE>(lp);
                    if (pnmitem->iItem >= 0) {
                        OnEditGroupAt(wnd, pnmitem->iItem);
                    }
                    return TRUE;
                }
            }
            break;
        }
    }
    return FALSE;
}

void NormalizeConfirmDialog::DoInitDialog(HWND wnd) {
    // 设置标题显示字段名
    std::wstring title_w = to_wstring("Normalize " + s_field + " - Confirm Groups");
    SetWindowTextW(wnd, title_w.c_str());

    HWND hList = GetDlgItem(wnd, IDC_NORMALIZE_CONFIRM_LIST);
    if (!hList) return;

    ListView_SetExtendedListViewStyle(hList, LVS_EX_FULLROWSELECT | LVS_EX_CHECKBOXES);

    LVCOLUMNW lvc = {0};
    lvc.mask = LVCF_TEXT | LVCF_WIDTH;
    struct ColInfo { const wchar_t* name; int width; };
    ColInfo cols[] = {
        {L"Type", 80},
        {L"Canonical Name", 200},
        {L"Aliases (will be replaced)", 280},
        {L"Confidence", 80},
        {L"Reason", 200}
    };
    for (int i = 0; i < _countof(cols); ++i) {
        lvc.pszText = const_cast<wchar_t*>(cols[i].name);
        lvc.cx = cols[i].width;
        ListView_InsertColumn(hList, i, &lvc);
    }

    PopulateListView(wnd);

    // 锚定规则：顶部说明横向拉伸；ListView 填充；Select All/None 左下锚定；
    // Uncertain GroupBox 和说明左下锚定且横向拉伸；Apply/Cancel 右下锚定
    static const AnchorEntry entries[] = {
        {IDC_NORMALIZE_TOP_HINT,       AF_LEFT | AF_RIGHT | AF_TOP},
        {IDC_NORMALIZE_CONFIRM_LIST,   AF_LEFT | AF_RIGHT | AF_TOP | AF_BOTTOM},
        {IDC_NORMALIZE_SELECT_ALL,     AF_LEFT | AF_BOTTOM},
        {IDC_NORMALIZE_SELECT_NONE,    AF_LEFT | AF_BOTTOM},
        {IDC_NORMALIZE_UNCERTAIN_GROUP, AF_LEFT | AF_RIGHT | AF_BOTTOM},
        {IDC_NORMALIZE_UNCERTAIN_HINT, AF_LEFT | AF_RIGHT | AF_BOTTOM},
        {IDOK,                         AF_RIGHT | AF_BOTTOM},
        {IDCANCEL,                     AF_RIGHT | AF_BOTTOM},
    };
    InitAnchors(wnd, entries, _countof(entries));
}

void NormalizeConfirmDialog::PopulateListView(HWND wnd) {
    HWND hList = GetDlgItem(wnd, IDC_NORMALIZE_CONFIRM_LIST);
    if (!hList || !s_result) return;

    ListView_DeleteAllItems(hList);

    int row = 0;
    // Groups
    for (size_t i = 0; i < s_result->groups.size(); ++i) {
        const auto& g = s_result->groups[i];

        LVITEMW lvi = {0};
        lvi.mask = LVIF_TEXT;
        lvi.iItem = row;
        lvi.iSubItem = 0;
        std::wstring type_w = L"Group";
        lvi.pszText = const_cast<wchar_t*>(type_w.c_str());
        ListView_InsertItem(hList, &lvi);

        std::wstring canonical_w = to_wstring(g.canonical_name);
        ListView_SetItemText(hList, row, 1, const_cast<wchar_t*>(canonical_w.c_str()));

        // 拼接 aliases（过滤空字符串，避免出现连续分隔符如 ", ,"）
        std::string aliases_str;
        for (size_t j = 0; j < g.aliases.size(); ++j) {
            if (g.aliases[j].empty()) continue;
            if (!aliases_str.empty()) aliases_str += ", ";
            aliases_str += g.aliases[j];
        }
        std::wstring aliases_w = to_wstring(aliases_str);
        ListView_SetItemText(hList, row, 2, const_cast<wchar_t*>(aliases_w.c_str()));

        std::wostringstream conf_ss;
        conf_ss << std::fixed << std::setprecision(2) << g.confidence;
        std::wstring conf_w = conf_ss.str();
        ListView_SetItemText(hList, row, 3, const_cast<wchar_t*>(conf_w.c_str()));

        std::wstring reason_w = to_wstring(g.reason);
        ListView_SetItemText(hList, row, 4, const_cast<wchar_t*>(reason_w.c_str()));

        // 默认勾选
        bool checked = (s_selected_groups && row < (int)s_selected_groups->size()) ? (*s_selected_groups)[row] : true;
        ListView_SetCheckState(hList, row, checked ? TRUE : FALSE);
        row++;
    }

    // Uncertain
    for (size_t i = 0; i < s_result->uncertain.size(); ++i) {
        const auto& u = s_result->uncertain[i];

        LVITEMW lvi = {0};
        lvi.mask = LVIF_TEXT;
        lvi.iItem = row;
        lvi.iSubItem = 0;
        std::wstring type_w = L"? Uncertain";
        lvi.pszText = const_cast<wchar_t*>(type_w.c_str());
        ListView_InsertItem(hList, &lvi);

        // uncertain: Canonical Name 空（未找到规范名）
        ListView_SetItemText(hList, row, 1, const_cast<wchar_t*>(L"(uncertain)"));

        // Aliases 列显示 alias 本身（待规范的别名）
        std::wstring aliases_w = to_wstring(u.alias);
        ListView_SetItemText(hList, row, 2, const_cast<wchar_t*>(aliases_w.c_str()));

        ListView_SetItemText(hList, row, 3, const_cast<wchar_t*>(L"-"));

        // Reason 列显示原因（完整内容）
        std::wstring reason_w = to_wstring(u.reason);
        ListView_SetItemText(hList, row, 4, const_cast<wchar_t*>(reason_w.c_str()));

        // uncertain 不允许勾选（禁用 checkbox 不可行，直接置灰行）
        ListView_SetCheckState(hList, row, FALSE);
        // 标记为不可选：通过 item data
        LVITEMW lvi2 = {0};
        lvi2.iItem = row;
        lvi2.mask = LVIF_PARAM;
        lvi2.lParam = 1;  // 1 = uncertain，不可选
        ListView_SetItem(hList, &lvi2);
        row++;
    }
}

void NormalizeConfirmDialog::OnSelectAll(HWND wnd) {
    HWND hList = GetDlgItem(wnd, IDC_NORMALIZE_CONFIRM_LIST);
    if (!hList) return;
    int count = ListView_GetItemCount(hList);
    for (int i = 0; i < count; ++i) {
        LPARAM lp = 0;
        LVITEMW lvi = {0};
        lvi.iItem = i;
        lvi.mask = LVIF_PARAM;
        lvi.lParam = 0;
        ListView_GetItem(hList, &lvi);
        if (lvi.lParam == 0) {
            ListView_SetCheckState(hList, i, TRUE);
        }
    }
}

void NormalizeConfirmDialog::OnSelectNone(HWND wnd) {
    HWND hList = GetDlgItem(wnd, IDC_NORMALIZE_CONFIRM_LIST);
    if (!hList) return;
    int count = ListView_GetItemCount(hList);
    for (int i = 0; i < count; ++i) {
        ListView_SetCheckState(hList, i, FALSE);
    }
}

void NormalizeConfirmDialog::OnEditGroupAt(HWND wnd, int item_index) {
    if (!s_result || item_index < 0) return;

    HWND hList = GetDlgItem(wnd, IDC_NORMALIZE_CONFIRM_LIST);
    if (!hList) return;

    // 判断行类型：lParam==1 表示 uncertain
    LVITEMW lvi = {0};
    lvi.iItem = item_index;
    lvi.mask = LVIF_PARAM;
    lvi.lParam = 0;
    ListView_GetItem(hList, &lvi);
    bool is_uncertain = (lvi.lParam == 1);

    int group_count = (int)s_result->groups.size();

    // 准备编辑对话框的初始值
    std::string init_canonical;
    std::vector<std::string> init_aliases;
    std::string init_reason;

    if (is_uncertain) {
        // uncertain 行：用 alias 本身作为初始 canonical 和 aliases
        int uncertain_idx = item_index - group_count;
        if (uncertain_idx < 0 || uncertain_idx >= (int)s_result->uncertain.size()) return;
        const auto& u = s_result->uncertain[uncertain_idx];
        init_canonical = u.alias;
        init_aliases = {u.alias};
        init_reason = u.reason;
    } else {
        // group 行：编辑现有 group
        if (item_index >= group_count) return;
        const auto& g = s_result->groups[item_index];
        init_canonical = g.canonical_name;
        init_aliases = g.aliases;
        init_reason = g.reason;
    }

    std::string out_canonical;
    std::vector<std::string> out_aliases;
    if (!NormalizeEditDialog::Show(wnd, init_canonical, init_aliases, init_reason, out_canonical, out_aliases)) {
        return;
    }

    // 去重 alias：保留首次出现的顺序，避免用户编辑后产生重复（如 "尹美莱" 出现两次）
    // 去重同时去除空字符串
    {
        std::set<std::string> seen;
        std::vector<std::string> deduped;
        deduped.reserve(out_aliases.size());
        for (const auto& a : out_aliases) {
            if (a.empty()) continue;
            if (seen.insert(a).second) {
                deduped.push_back(a);
            }
        }
        out_aliases.swap(deduped);
    }

    // 校验：canonical 必须在 aliases 中
    bool canonical_in_aliases = false;
    for (const auto& a : out_aliases) {
        if (a == out_canonical) { canonical_in_aliases = true; break; }
    }
    if (!canonical_in_aliases && !out_aliases.empty()) {
        out_aliases.insert(out_aliases.begin(), out_canonical);
    }

    // 保存当前勾选状态（PopulateListView 会重建列表）
    std::vector<bool> saved_checks;
    int old_count = ListView_GetItemCount(hList);
    for (int i = 0; i < old_count; ++i) {
        saved_checks.push_back(ListView_GetCheckState(hList, i) != FALSE);
    }

    if (is_uncertain) {
        // uncertain → group：从 uncertain 删除，添加新 group
        int uncertain_idx = item_index - group_count;
        s_result->uncertain.erase(s_result->uncertain.begin() + uncertain_idx);

        NormalizeGroup new_g;
        new_g.canonical_name = out_canonical;
        new_g.aliases = out_aliases;
        new_g.confidence = 1.0f;  // 人工确认
        new_g.reason = "User confirmed (from uncertain)";
        s_result->groups.push_back(std::move(new_g));

        // 扩容 selected_groups，新 group 默认勾选
        if (s_selected_groups) {
            s_selected_groups->push_back(true);
        }
    } else {
        // 编辑现有 group
        NormalizeGroup& g = s_result->groups[item_index];
        g.canonical_name = out_canonical;
        g.aliases = out_aliases;
    }

    // 刷新 ListView 显示
    PopulateListView(wnd);

    // 恢复勾选状态（按行号对齐；groups 数量可能已变化）
    int new_count = ListView_GetItemCount(hList);
    for (int i = 0; i < new_count && i < (int)saved_checks.size(); ++i) {
        LVITEMW lvi2 = {0};
        lvi2.iItem = i;
        lvi2.mask = LVIF_PARAM;
        lvi2.lParam = 0;
        ListView_GetItem(hList, &lvi2);
        if (lvi2.lParam == 0) {
            ListView_SetCheckState(hList, i, saved_checks[i] ? TRUE : FALSE);
        }
    }
    // 新增的 group 行（如果是 uncertain→group 转换）默认勾选
    if (is_uncertain && s_selected_groups && !s_selected_groups->empty()) {
        int last_group_row = (int)s_result->groups.size() - 1;
        if (last_group_row < new_count) {
            ListView_SetCheckState(hList, last_group_row, TRUE);
        }
    }
}

void NormalizeConfirmDialog::OnOK(HWND wnd) {
    HWND hList = GetDlgItem(wnd, IDC_NORMALIZE_CONFIRM_LIST);
    if (!hList || !s_selected_groups || !s_result) {
        s_confirmed = false;
        EndDialog(wnd, IDCANCEL);
        return;
    }

    // 只收集 groups 部分（前 s_result->groups.size() 行）
    size_t group_count = s_result->groups.size();
    s_selected_groups->assign(group_count, false);
    for (size_t i = 0; i < group_count; ++i) {
        (*s_selected_groups)[i] = ListView_GetCheckState(hList, (int)i) != FALSE;
    }
    s_confirmed = true;
    EndDialog(wnd, IDOK);
}

void NormalizeConfirmDialog::OnCancel(HWND wnd) {
    s_confirmed = false;
    EndDialog(wnd, IDCANCEL);
}

// ==================== Normalize Edit Dialog ====================

bool NormalizeEditDialog::Show(HWND parent,
                                const std::string& canonical_name,
                                const std::vector<std::string>& aliases,
                                const std::string& reason,
                                std::string& out_canonical,
                                std::vector<std::string>& out_aliases) {
    s_canonical_name = canonical_name;
    s_aliases = aliases;
    s_reason = reason;
    s_confirmed = false;

    INT_PTR ret = DialogBoxParam(
        core_api::get_my_instance(),
        MAKEINTRESOURCE(IDD_NORMALIZE_EDIT),
        parent,
        NormalizeEditDialog::DlgProc,
        0
    );

    if (s_confirmed) {
        out_canonical = s_canonical_name;
        out_aliases = s_aliases;
    }

    s_canonical_name.clear();
    s_aliases.clear();
    s_reason.clear();
    return s_confirmed;
}

INT_PTR CALLBACK NormalizeEditDialog::DlgProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_INITDIALOG:
            DoInitDialog(wnd);
            return TRUE;
        case WM_COMMAND:
            switch (LOWORD(wp)) {
                case IDOK: OnOK(wnd); return TRUE;
                case IDCANCEL: OnCancel(wnd); return TRUE;
            }
            break;
    }
    return FALSE;
}

void NormalizeEditDialog::DoInitDialog(HWND wnd) {
    // Canonical name 编辑框（使用 Wide API 支持中文）
    HWND hCanonical = GetDlgItem(wnd, IDC_NORMALIZE_EDIT_CANONICAL);
    if (hCanonical) {
        std::wstring w = to_wstring(s_canonical_name);
        SetWindowTextW(hCanonical, w.c_str());
    }

    // Aliases 多行编辑框（每行一个 alias）
    HWND hAliases = GetDlgItem(wnd, IDC_NORMALIZE_EDIT_ALIASES);
    if (hAliases) {
        std::wstring text;
        for (size_t i = 0; i < s_aliases.size(); ++i) {
            if (i > 0) text += L"\r\n";
            text += to_wstring(s_aliases[i]);
        }
        SetWindowTextW(hAliases, text.c_str());
    }

    // Reason 只读多行编辑框（显示完整原因，便于查看与复制）
    HWND hReason = GetDlgItem(wnd, IDC_NORMALIZE_EDIT_REASON);
    if (hReason) {
        std::wstring reason_w = to_wstring(s_reason);
        SetWindowTextW(hReason, reason_w.c_str());
    }
}

void NormalizeEditDialog::OnOK(HWND wnd) {
    // 读取 canonical
    HWND hCanonical = GetDlgItem(wnd, IDC_NORMALIZE_EDIT_CANONICAL);
    if (hCanonical) {
        int len = GetWindowTextLengthW(hCanonical);
        std::wstring w;
        w.resize(len + 1);
        int actual = GetWindowTextW(hCanonical, &w[0], len + 1);
        w.resize(actual > 0 ? actual : 0);
        s_canonical_name = to_string(w);
    }

    // 读取 aliases（按行拆分）
    HWND hAliases = GetDlgItem(wnd, IDC_NORMALIZE_EDIT_ALIASES);
    s_aliases.clear();
    if (hAliases) {
        int len = GetWindowTextLengthW(hAliases);
        std::wstring w;
        w.resize(len + 1);
        int actual = GetWindowTextW(hAliases, &w[0], len + 1);
        w.resize(actual > 0 ? actual : 0);

        // 按 \r\n 或 \n 拆分
        std::wstring line;
        for (size_t i = 0; i < w.size(); ++i) {
            wchar_t c = w[i];
            if (c == L'\r') continue;
            if (c == L'\n') {
                // trim 两端空白
                // 简单 trim
                size_t s = line.find_first_not_of(L" \t");
                size_t e = line.find_last_not_of(L" \t");
                if (s != std::wstring::npos && e != std::wstring::npos && e >= s) {
                    s_aliases.push_back(to_string(line.substr(s, e - s + 1)));
                }
                line.clear();
            } else {
                line += c;
            }
        }
        // 最后一行
        if (!line.empty()) {
            size_t s = line.find_first_not_of(L" \t");
            size_t e = line.find_last_not_of(L" \t");
            if (s != std::wstring::npos && e != std::wstring::npos && e >= s) {
                s_aliases.push_back(to_string(line.substr(s, e - s + 1)));
            }
        }
    }

    // 校验
    if (s_canonical_name.empty()) {
        MessageBoxW(wnd, L"Canonical name cannot be empty.", L"Normalize Edit", MB_OK | MB_ICONWARNING);
        return;
    }
    if (s_aliases.empty()) {
        MessageBoxW(wnd, L"Aliases list cannot be empty.", L"Normalize Edit", MB_OK | MB_ICONWARNING);
        return;
    }

    s_confirmed = true;
    EndDialog(wnd, IDOK);
}

void NormalizeEditDialog::OnCancel(HWND wnd) {
    s_confirmed = false;
    EndDialog(wnd, IDCANCEL);
}

// ==================== Rollback Type Dialog ====================

// 静态成员定义
std::vector<ai_metadata::OperationType> RollbackTypeDialog::s_available_types;
std::map<ai_metadata::OperationType, int> RollbackTypeDialog::s_type_counts;
std::vector<bool> RollbackTypeDialog::s_check_states;
bool RollbackTypeDialog::s_confirmed = false;
INT_PTR RollbackTypeDialog::s_last_dialog_result = 0;
DWORD RollbackTypeDialog::s_last_error_code = 0;

// 操作类型显示名（中英文混合，便于用户理解）
static std::wstring operation_type_display_name(ai_metadata::OperationType t) {
    switch (t) {
        case ai_metadata::OperationType::Scrape:    return L"Scrape (metadata fetching)";
        case ai_metadata::OperationType::Translate: return L"Enhance (translate)";
        case ai_metadata::OperationType::Normalize: return L"Normalize (artist name)";
    }
    return L"Unknown";
}

bool RollbackTypeDialog::Show(HWND parent,
                              const std::vector<ai_metadata::OperationType>& available_types,
                              const std::map<ai_metadata::OperationType, int>& type_counts,
                              std::vector<ai_metadata::OperationType>& selected_types) {
    s_available_types = available_types;
    s_type_counts = type_counts;
    s_check_states.assign(available_types.size(), true);  // 默认全选
    s_confirmed = false;
    s_last_dialog_result = 0;
    s_last_error_code = 0;

    HINSTANCE hInst = core_api::get_my_instance();
    LOG_INFO("[RollbackTypeDialog] Opening dialog: parent=" + std::to_string((uintptr_t)parent) +
             ", hInst=" + std::to_string((uintptr_t)hInst) +
             ", available_types=" + std::to_string(available_types.size()) +
             ", resource_id=" + std::to_string(IDD_ROLLBACK_TYPE_SELECT));

    // 在调用 DialogBoxParam 前清除 GetLastError，便于准确捕获失败原因
    SetLastError(ERROR_SUCCESS);

    INT_PTR ret = DialogBoxParam(
        hInst,
        MAKEINTRESOURCE(IDD_ROLLBACK_TYPE_SELECT),
        parent,
        RollbackTypeDialog::DlgProc,
        0
    );

    DWORD err = GetLastError();
    s_last_dialog_result = ret;
    s_last_error_code = err;
    LOG_INFO("[RollbackTypeDialog] DialogBoxParam returned " + std::to_string((INT_PTR)ret) +
             ", GetLastError=" + std::to_string(err) +
             ", s_confirmed=" + (s_confirmed ? "true" : "false"));

    // 诊断：DialogBoxParam 返回 -1 表示对话框创建失败
    if (ret == -1) {
        std::string err_msg = "[RollbackTypeDialog] DialogBoxParam FAILED with GetLastError=" +
                              std::to_string(err);
        // 常见错误码翻译
        switch (err) {
            case ERROR_RESOURCE_NAME_NOT_FOUND: err_msg += " (ERROR_RESOURCE_NAME_NOT_FOUND: 资源未找到)"; break;
            case ERROR_INVALID_WINDOW_HANDLE: err_msg += " (ERROR_INVALID_WINDOW_HANDLE: 父窗口句柄无效)"; break;
            case ERROR_INVALID_PARAMETER: err_msg += " (ERROR_INVALID_PARAMETER: 参数无效)"; break;
            case 1812: err_msg += " (ERROR_RESOURCE_DATA_NOT_FOUND: 资源数据未找到)"; break;
            case 1813: err_msg += " (ERROR_RESOURCE_TYPE_NOT_FOUND: 资源类型未找到)"; break;
            default: break;
        }
        LOG_ERROR(err_msg);
        // 不在此处弹窗，由调用方根据返回值处理
    }

    selected_types.clear();
    if (s_confirmed) {
        for (size_t i = 0; i < s_available_types.size(); ++i) {
            if (i < s_check_states.size() && s_check_states[i]) {
                selected_types.push_back(s_available_types[i]);
            }
        }
    }

    s_available_types.clear();
    s_type_counts.clear();
    s_check_states.clear();
    return s_confirmed;
}

INT_PTR CALLBACK RollbackTypeDialog::DlgProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_INITDIALOG:
            DoInitDialog(wnd);
            return TRUE;
        case WM_COMMAND:
            switch (LOWORD(wp)) {
                case IDOK: OnOK(wnd); return TRUE;
                case IDCANCEL: OnCancel(wnd); return TRUE;
            }
            break;
        case WM_NOTIFY: {
            LPNMHDR pnmh = reinterpret_cast<LPNMHDR>(lp);
            if (pnmh && pnmh->idFrom == IDC_ROLLBACK_TYPE_LIST) {
                if (pnmh->code == LVN_ITEMCHANGED) {
                    LPNMLISTVIEW pnmlv = reinterpret_cast<LPNMLISTVIEW>(lp);
                    if (pnmlv && (pnmlv->uChanged & LVIF_STATE)) {
                        // 跟踪 checkbox 状态变化
                        int idx = pnmlv->iItem;
                        if (idx >= 0 && idx < (int)s_check_states.size()) {
                            BOOL checked = ListView_GetCheckState(pnmh->hwndFrom, idx);
                            s_check_states[idx] = (checked != FALSE);
                        }
                    }
                }
            }
            break;
        }
    }
    return FALSE;
}

void RollbackTypeDialog::DoInitDialog(HWND wnd) {
    LOG_INFO("[RollbackTypeDialog] DoInitDialog: wnd=" + std::to_string((uintptr_t)wnd) +
             ", available_types=" + std::to_string(s_available_types.size()));

    HWND hList = GetDlgItem(wnd, IDC_ROLLBACK_TYPE_LIST);
    if (!hList) {
        LOG_ERROR("[RollbackTypeDialog] GetDlgItem(IDC_ROLLBACK_TYPE_LIST) returned NULL, GetLastError=" +
                  std::to_string(GetLastError()));
        return;
    }
    LOG_INFO("[RollbackTypeDialog] ListView control created successfully");

    // 启用 checkbox 风格
    ListView_SetExtendedListViewStyle(hList, LVS_EX_FULLROWSELECT | LVS_EX_CHECKBOXES);

    // 单列显示（无表头）
    LVCOLUMNW col = {};
    col.mask = LVCF_FMT | LVCF_WIDTH | LVCF_TEXT;
    col.fmt = LVCFMT_LEFT;
    col.cx = 340;
    col.pszText = const_cast<LPWSTR>(L"Operation");
    ListView_InsertColumn(hList, 0, &col);

    // 插入每行
    for (size_t i = 0; i < s_available_types.size(); ++i) {
        ai_metadata::OperationType t = s_available_types[i];
        std::wstring label = operation_type_display_name(t);
        auto it = s_type_counts.find(t);
        if (it != s_type_counts.end() && it->second > 0) {
            label += L"  (" + std::to_wstring(it->second) + L" track(s) can rollback)";
        }

        LVITEMW item = {};
        item.mask = LVIF_TEXT;
        item.iItem = (int)i;
        item.iSubItem = 0;
        item.pszText = const_cast<LPWSTR>(label.c_str());
        ListView_InsertItem(hList, &item);

        // 默认勾选
        ListView_SetCheckState(hList, (int)i, s_check_states[i] ? TRUE : FALSE);
    }

    // 若没有可回滚类型，禁用 OK
    if (s_available_types.empty()) {
        EnableWindow(GetDlgItem(wnd, IDOK), FALSE);
    }
}

void RollbackTypeDialog::OnOK(HWND wnd) {
    HWND hList = GetDlgItem(wnd, IDC_ROLLBACK_TYPE_LIST);
    if (hList) {
        // 从 ListView 读取最终勾选状态（防止 WM_NOTIFY 漏掉的状态）
        for (size_t i = 0; i < s_check_states.size(); ++i) {
            BOOL checked = ListView_GetCheckState(hList, (int)i);
            s_check_states[i] = (checked != FALSE);
        }
    }

    // 至少选一个
    bool any_selected = false;
    for (bool b : s_check_states) {
        if (b) { any_selected = true; break; }
    }
    if (!any_selected) {
        MessageBoxW(wnd, L"Please select at least one operation type to rollback.",
                    L"Rollback", MB_OK | MB_ICONWARNING);
        return;
    }

    s_confirmed = true;
    EndDialog(wnd, IDOK);
}

void RollbackTypeDialog::OnCancel(HWND wnd) {
    s_confirmed = false;
    EndDialog(wnd, IDCANCEL);
}

}
