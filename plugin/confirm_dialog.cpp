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
#include <algorithm>

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
std::vector<int> ConfirmResultDialog::s_view_indices;
bool ConfirmResultDialog::s_sort_descending = false;
int ConfirmResultDialog::s_sort_column = -1;
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
bool NormalizeConfirmDialog::s_populating = false;

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
    // Data Sources / Confidence Thresholds 已迁移至 Preferences 页面，弹窗无需初始化控件
    (void)wnd;
    (void)s_options;
}

void ScrapingOptionsDialog::OnOK(HWND wnd) {
    SaveOptions(wnd);
    EndDialog(wnd, IDOK);
}

void ScrapingOptionsDialog::OnCancel(HWND wnd) {
    EndDialog(wnd, IDCANCEL);
}

void ScrapingOptionsDialog::SaveOptions(HWND wnd) {
    // 弹窗不再承载可编辑选项；options 由调用方从 SettingsManager 预填，保持原样返回
    (void)wnd;
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
                case IDC_FILTER_LOW_CONF:
                    OnFilterLowConf(wnd);
                    return TRUE;
                case IDC_SORT_CONF:
                    OnSortByConfidence(wnd);
                    return TRUE;
            }
            break;

        case WM_NOTIFY:
            {
                LPNMHDR nmhdr = reinterpret_cast<LPNMHDR>(lp);
                if (nmhdr->idFrom == IDC_RESULT_LISTVIEW) {
                    switch (nmhdr->code) {
                        case LVN_GETDISPINFO:
                            OnGetDispInfo(lp);
                            return TRUE;
                        case LVN_ITEMCHANGED:
                            OnItemChanged(wnd, lp);
                            return TRUE;
                        case NM_CLICK: {
                            // LVS_OWNERDATA + LVS_EX_CHECKBOXES：listview 不存储 checkbox 状态，
                            // 用户点击后不会自动切换，手动检测并切换
                            OnCheckboxClick(lp);
                            return TRUE;
                        }
                        case NM_DBLCLK: {
                            LPNMITEMACTIVATE lpnmitem = reinterpret_cast<LPNMITEMACTIVATE>(lp);
                            int orig_idx = ViewIndexToOriginal(lpnmitem->iItem);
                            if (orig_idx >= 0) {
                                OnEditItemAt(wnd, orig_idx, lpnmitem->iSubItem);
                            }
                            return TRUE;
                        }
                    }
                }
            }
            break;
    }
    return FALSE;
}

void ConfirmResultDialog::DoInitDialog(HWND wnd) {
    InitFieldCheckboxes(wnd);

    // 默认阈值 50（百分比）
    SetDlgItemTextW(wnd, IDC_CONF_THRESHOLD, L"50");
    // 默认不启用过滤
    CheckDlgButton(wnd, IDC_FILTER_LOW_CONF, BST_UNCHECKED);

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
        {IDC_FILTER_LOW_CONF,     AF_LEFT | AF_BOTTOM},
        {IDC_CONF_THRESHOLD,      AF_LEFT | AF_BOTTOM},
        {IDC_SORT_CONF,           AF_LEFT | AF_BOTTOM},
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
        s_field_selection["conductor"] = true;
        s_field_selection["performer"] = true;
        s_field_selection["label"] = true;
        s_field_selection["country"] = true;
        s_field_selection["catalog_number"] = true;
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

    // 初始化 s_selected（仅第一次进入时全为 true）
    if (s_selected) s_selected->resize(s_results->size(), true);

    // 初始化为默认视图（无过滤、无排序）
    s_view_indices.clear();
    s_sort_column = -1;
    s_sort_descending = false;

    RebuildViewIndices(wnd);
}

// 重建显示视图：根据 filter checkbox 和 sort 状态填充 s_view_indices，
// 然后更新 listview 的 item count 触发重绘。
void ConfirmResultDialog::RebuildViewIndices(HWND wnd) {
    if (!s_results) { s_view_indices.clear(); return; }

    HWND hList = GetDlgItem(wnd, IDC_RESULT_LISTVIEW);
    HWND hFilter = GetDlgItem(wnd, IDC_FILTER_LOW_CONF);
    HWND hThreshold = GetDlgItem(wnd, IDC_CONF_THRESHOLD);

    // 读取过滤阈值（0-100，因为 ES_NUMBER 限定数字）
    float threshold = 0.0f;
    bool filter_enabled = false;
    if (hFilter && hThreshold) {
        filter_enabled = (IsDlgButtonChecked(wnd, IDC_FILTER_LOW_CONF) == BST_CHECKED);
        wchar_t buf[16] = {0};
        GetDlgItemTextW(wnd, IDC_CONF_THRESHOLD, buf, 16);
        int v = _wtoi(buf);
        if (v < 0) v = 0;
        if (v > 100) v = 100;
        threshold = static_cast<float>(v) / 100.0f;
    }

    s_view_indices.clear();
    s_view_indices.reserve(s_results->size());
    for (int i = 0; i < static_cast<int>(s_results->size()); ++i) {
        if (filter_enabled) {
            float conf = GetItemConfidence(i);
            // UI 提示 "Hide confidence <"：隐藏低于阈值的项，只显示 >= threshold 的
            if (conf < threshold) continue;
        }
        s_view_indices.push_back(i);
    }

    // 排序：默认按 confidence 升序（低→高），便于先看低置信问题项
    if (s_sort_column == 15) {  // column 15 = Confidence
        std::sort(s_view_indices.begin(), s_view_indices.end(),
            [](int a, int b) {
                float ca = GetItemConfidence(a);
                float cb = GetItemConfidence(b);
                return s_sort_descending ? (ca > cb) : (ca < cb);
            });
    }

    if (hList) {
        int n = static_cast<int>(s_view_indices.size());
        ListView_SetItemCountEx(hList, n, LVSICF_NOINVALIDATEALL);
        if (n > 0) {
            // OWNERDATA + LVS_EX_CHECKBOXES 模式：listview 不存 state，只需触发重绘，
            // LVN_GETDISPINFO 回调会返回最新 state
            ListView_RedrawItems(hList, 0, n - 1);
            UpdateWindow(hList);
        }
    }
}

// listview 显示行号 → s_results 原始索引
int ConfirmResultDialog::ViewIndexToOriginal(int view_idx) {
    if (s_view_indices.empty()) return view_idx;  // 无过滤/排序时直接映射
    if (view_idx < 0 || view_idx >= static_cast<int>(s_view_indices.size())) return -1;
    return s_view_indices[view_idx];
}

// 计算指定 s_results 索引项的平均 confidence
float ConfirmResultDialog::GetItemConfidence(int original_idx) {
    if (!s_results || original_idx < 0 || original_idx >= static_cast<int>(s_results->size())) return 0.0f;
    const auto& result = (*s_results)[original_idx];
    if (result.scraped_fields.empty()) return 0.0f;
    float total = 0.0f;
    int count = 0;
    for (const auto& f : result.scraped_fields) {
        total += f.second.confidence;
        ++count;
    }
    return count > 0 ? (total / count) : 0.0f;
}

// LVN_GETDISPINFO 回调：按 iItem/iSubItem 提供单元格文本与复选框状态
void ConfirmResultDialog::OnGetDispInfo(LPARAM lp) {
    NMLVDISPINFO* di = reinterpret_cast<NMLVDISPINFO*>(lp);
    if (!di || !s_results) return;
    int view_idx = di->item.iItem;
    int idx = ViewIndexToOriginal(view_idx);
    if (idx < 0) return;

    // 强制返回 state，确保 listview 每次 redraw 都拿到最新 checkbox 状态
    // （OWNERDATA 模式下 listview 不存 state，必须主动提供）
    // 关键：必须同时设置 stateMask，listview 才知道 state 改了什么
    bool checked = s_selected && idx < static_cast<int>(s_selected->size()) && (*s_selected)[idx];
    di->item.state = INDEXTOSTATEIMAGEMASK(checked ? 2 : 1);
    di->item.stateMask = LVIS_STATEIMAGEMASK;
    di->item.mask |= LVIF_STATE;

    const auto& result = (*s_results)[idx];
    bool is_empty = !result.success || result.scraped_fields.empty();

    const TrackInput* orig = nullptr;
    if (s_original_inputs && idx < static_cast<int>(s_original_inputs->size())) {
        orig = &(*s_original_inputs)[idx];
    }

    if (!(di->item.mask & LVIF_TEXT)) return;

    // 复用单元格字符串缓冲区：使用 thread-local 静态缓冲区避免悬空
    static thread_local std::wstring cell_buf;
    auto set_text = [&](const std::string& s) {
        cell_buf = to_wstring(s);
        di->item.pszText = const_cast<wchar_t*>(cell_buf.c_str());
    };

    auto get_field_value = [&](const std::string& field_name) -> std::string {
        auto it = result.scraped_fields.find(field_name);
        if (it != result.scraped_fields.end() && !it->second.value.empty()) {
            return it->second.value;
        }
        if (orig) {
            if (field_name == "title")          return orig->title;
            if (field_name == "artist")         return orig->artist;
            if (field_name == "album")          return orig->album;
            if (field_name == "year")           return orig->year;
            if (field_name == "genre")          return orig->genre;
            if (field_name == "track_number")   return std::to_string(orig->track_number);
            if (field_name == "disc_number")    return std::to_string(orig->disc_number);
            if (field_name == "composer")       return orig->composer;
            if (field_name == "lyricist")       return orig->lyricist;
            if (field_name == "conductor")      return orig->conductor;
            if (field_name == "performer")      return orig->performer;
            if (field_name == "label")          return orig->label;
        }
        return "";
    };

    switch (di->item.iSubItem) {
        case 0: {  // Track ID
            if (is_empty) {
                set_text("[FAILED] " + result.track_id.substr(0, 16) + (result.track_id.length() > 16 ? "..." : ""));
            } else {
                set_text(result.track_id.substr(0, 24) + (result.track_id.length() > 24 ? "..." : ""));
            }
            break;
        }
        case 1: set_text(is_empty ? "(no data)" : get_field_value("title")); break;
        case 2: set_text(is_empty ? "" : get_field_value("artist")); break;
        case 3: set_text(is_empty ? "" : get_field_value("album")); break;
        case 4: set_text(is_empty ? "" : get_field_value("year")); break;
        case 5: set_text(is_empty ? "" : get_field_value("genre")); break;
        case 6: set_text(is_empty ? "" : get_field_value("track_number")); break;
        case 7: set_text(is_empty ? "" : get_field_value("disc_number")); break;
        case 8: set_text(is_empty ? "" : get_field_value("composer")); break;
        case 9: set_text(is_empty ? "" : get_field_value("lyricist")); break;
        case 10: set_text(is_empty ? "" : get_field_value("conductor")); break;
        case 11: set_text(is_empty ? "" : get_field_value("performer")); break;
        case 12: set_text(is_empty ? "" : get_field_value("label")); break;
        case 13: set_text(is_empty ? "" : get_field_value("country")); break;
        case 14: set_text(is_empty ? "" : get_field_value("catalog_number")); break;
        case 15: {  // Confidence
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(2);
            float total_conf = 0.0f;
            int count = 0;
            for (const auto& f : result.scraped_fields) {
                total_conf += f.second.confidence;
                count++;
            }
            oss << (count > 0 ? (total_conf / count) : 0.0f);
            set_text(oss.str());
            break;
        }
        case 16: {  // Source
            std::string source;
            if (is_empty) source = result.error.empty() ? "Failed" : "Error";
            else {
                switch (result.release_source) {
                    case DataSourceType::MUSICBRAINZ: source = "MusicBrainz"; break;
                    case DataSourceType::DISCOGS: source = "Discogs"; break;
                    case DataSourceType::AI: source = "AI"; break;
                }
            }
            set_text(source);
            break;
        }
    }
}

// LVN_ITEMCHANGED 同步复选框状态到 s_selected
void ConfirmResultDialog::OnItemChanged(HWND wnd, LPARAM lp) {
    NMLISTVIEW* pnm = reinterpret_cast<NMLISTVIEW*>(lp);
    if (!pnm || !s_selected) return;
    if (pnm->iItem < 0) return;
    int idx = ViewIndexToOriginal(pnm->iItem);
    if (idx < 0 || idx >= static_cast<int>(s_selected->size())) return;
    if (!(pnm->uChanged & LVIF_STATE)) return;
    int img = (pnm->uNewState >> 12) & 0xF;
    if (img == 0) return;  // 0 = 无 state image（初始化或无 checkbox 行）
    bool new_checked = (img == 2);
    // 仅状态真正变化时才更新，避免消息循环
    if (new_checked == (*s_selected)[idx]) return;
    (*s_selected)[idx] = new_checked;
    // 强制重绘该行触发 LVN_GETDISPINFO，使 checkbox 显示立即更新
    HWND hList = GetDlgItem(wnd, IDC_RESULT_LISTVIEW);
    if (hList) {
        ListView_RedrawItems(hList, pnm->iItem, pnm->iItem);
        UpdateWindow(hList);
    }
}

// LVS_OWNERDATA + LVS_EX_CHECKBOXES：listview 不存储 checkbox 状态，用户点击无效。
// NM_CLICK 中手动检测点击是否落在 checkbox 区域，切换 s_selected。
void ConfirmResultDialog::OnCheckboxClick(LPARAM lp) {
    if (!s_selected || !s_results) return;
    LPNMITEMACTIVATE pnm = reinterpret_cast<LPNMITEMACTIVATE>(lp);
    if (!pnm) return;
    HWND hList = pnm->hdr.hwndFrom;
    if (!hList) return;
    int view_idx = pnm->iItem;
    if (view_idx < 0) return;
    int orig_idx = ViewIndexToOriginal(view_idx);
    if (orig_idx < 0 || orig_idx >= static_cast<int>(s_selected->size())) return;

    LVHITTESTINFO hit = {};
    hit.pt = pnm->ptAction;
    if (ListView_SubItemHitTest(hList, &hit) == -1) return;
    if (!(hit.flags & LVHT_ONITEMSTATEICON)) return;

    (*s_selected)[orig_idx] = !(*s_selected)[orig_idx];
    ListView_RedrawItems(hList, view_idx, view_idx);
    UpdateWindow(hList);
}

// "Hide confidence <" checkbox 状态变化时触发过滤
// 处理在 DlgProc 的 IDC_FILTER_LOW_CONF case 中直接调用 RebuildViewIndices
void ConfirmResultDialog::OnFilterLowConf(HWND wnd) {
    RebuildViewIndices(wnd);
}

// "Sort by Confidence" 按钮：切换升降序
void ConfirmResultDialog::OnSortByConfidence(HWND wnd) {
    if (s_sort_column != 15) {
        s_sort_column = 15;
        s_sort_descending = false;  // 首次：升序（低→高，便于先看低置信）
    } else {
        s_sort_descending = !s_sort_descending;
    }
    RebuildViewIndices(wnd);
}

void ConfirmResultDialog::OnSelectAll(HWND wnd) {
    if (!s_selected) return;
    // 只勾选当前可见行（过滤隐藏的不操作）
    for (int vi : s_view_indices) {
        (*s_selected)[vi] = true;
    }
    HWND hList = GetDlgItem(wnd, IDC_RESULT_LISTVIEW);
    if (hList && !s_view_indices.empty()) {
        int n = static_cast<int>(s_view_indices.size());
        ListView_RedrawItems(hList, 0, n - 1);
        UpdateWindow(hList);
    }
}

void ConfirmResultDialog::OnSelectNone(HWND wnd) {
    if (!s_selected) return;
    // 只取消当前可见行
    for (int vi : s_view_indices) {
        (*s_selected)[vi] = false;
    }
    HWND hList = GetDlgItem(wnd, IDC_RESULT_LISTVIEW);
    if (hList && !s_view_indices.empty()) {
        int n = static_cast<int>(s_view_indices.size());
        ListView_RedrawItems(hList, 0, n - 1);
        UpdateWindow(hList);
    }
}

void ConfirmResultDialog::OnSelectSuccess(HWND wnd) {
    if (!s_selected || !s_results) return;
    HWND hList = GetDlgItem(wnd, IDC_RESULT_LISTVIEW);
    // 只操作当前可见行
    for (int vi : s_view_indices) {
        const auto& result = (*s_results)[vi];
        (*s_selected)[vi] = result.success && !result.scraped_fields.empty();
    }
    if (hList && !s_view_indices.empty()) {
        int n = static_cast<int>(s_view_indices.size());
        ListView_RedrawItems(hList, 0, n - 1);
        UpdateWindow(hList);
    }
}

void ConfirmResultDialog::OnEditItem(HWND wnd) {
    HWND hList = GetDlgItem(wnd, IDC_RESULT_LISTVIEW);
    if (!hList || !s_results) return;

    int selected = ListView_GetNextItem(hList, -1, LVNI_SELECTED);
    if (selected < 0) return;
    int orig_idx = ViewIndexToOriginal(selected);
    if (orig_idx < 0 || orig_idx >= static_cast<int>(s_results->size())) return;

    OnEditItemAt(wnd, orig_idx, 1);
}

void ConfirmResultDialog::OnEditItemAt(HWND wnd, int item_index, int sub_item) {
    // item_index 这里是原始 s_results 索引（由 OnEditItem 转换后传入）
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
    // LVS_OWNERDATA 模式：s_selected 已通过 LVN_ITEMCHANGED 实时同步，无需重新读取
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
                switch (pnmh->code) {
                    case LVN_GETDISPINFO:
                        OnGetDispInfo(lp);
                        return TRUE;
                    case LVN_ITEMCHANGED:
                        OnItemChanged(lp);
                        return TRUE;
                    case NM_CLICK: {
                        OnCheckboxClick(lp);
                        return TRUE;
                    }
                    case NM_DBLCLK: {
                        LPNMITEMACTIVATE pnmitem = reinterpret_cast<LPNMITEMACTIVATE>(lp);
                        if (pnmitem->iItem >= 0) {
                            OnEditItemAt(wnd, pnmitem->iItem, pnmitem->iSubItem);
                        }
                        return TRUE;
                    }
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

    // LVS_OWNERDATA：仅设置 item count
    int n = static_cast<int>(s_results->size());
    if (s_selected) s_selected->resize(n, false);
    // 默认勾选：成功且非中文跳过项
    for (int i = 0; i < n; ++i) {
        const auto& r = (*s_results)[i];
        bool is_failed = !r.success;
        bool is_skipped = !is_failed && r.title_zh.empty() && r.album_zh.empty() && r.artist_zh.empty();
        (*s_selected)[i] = !is_failed && !is_skipped;
    }
    ListView_SetItemCountEx(hList, n, LVSICF_NOINVALIDATEALL);
    if (n > 0) ListView_RedrawItems(hList, 0, n - 1);
}

void EnhanceConfirmDialog::OnGetDispInfo(LPARAM lp) {
    NMLVDISPINFO* di = reinterpret_cast<NMLVDISPINFO*>(lp);
    if (!di || !s_results) return;
    int idx = di->item.iItem;
    if (idx < 0 || idx >= static_cast<int>(s_results->size())) return;

    const auto& result = (*s_results)[idx];
    bool is_failed = !result.success;

    const TrackInput* orig = nullptr;
    if (s_original_inputs && idx < static_cast<int>(s_original_inputs->size())) {
        orig = &(*s_original_inputs)[idx];
    }

    if (di->item.mask & LVIF_STATE) {
        if (di->item.stateMask & LVIS_STATEIMAGEMASK) {
            bool checked = s_selected && idx < static_cast<int>(s_selected->size()) && (*s_selected)[idx];
            di->item.state = INDEXTOSTATEIMAGEMASK(checked ? 2 : 1);
        }
    }

    if (!(di->item.mask & LVIF_TEXT)) return;

    static thread_local std::wstring cell_buf;
    auto set_text = [&](const std::string& s) {
        cell_buf = to_wstring(s);
        di->item.pszText = const_cast<wchar_t*>(cell_buf.c_str());
    };
    auto set_wtext = [&](const std::wstring& s) {
        cell_buf = s;
        di->item.pszText = const_cast<wchar_t*>(cell_buf.c_str());
    };

    switch (di->item.iSubItem) {
        case 0: {  // Track ID
            if (is_failed) {
                set_text("[FAILED] " + result.track_id.substr(0, 20) + (result.track_id.length() > 20 ? "..." : ""));
            } else {
                set_text(result.track_id.substr(0, 24) + (result.track_id.length() > 24 ? "..." : ""));
            }
            break;
        }
        case 1: {  // Title ZH
            if (is_failed) { set_wtext(L"(no data)"); break; }
            std::string disp = result.title_zh;
            if (disp.empty() && orig) disp = orig->title;
            set_text(disp);
            break;
        }
        case 2: {  // Album ZH
            if (is_failed) { set_wtext(L""); break; }
            std::string disp = result.album_zh;
            if (disp.empty() && orig) disp = orig->album;
            set_text(disp);
            break;
        }
        case 3: {  // Artist ZH
            if (is_failed) { set_wtext(L""); break; }
            std::string disp = result.artist_zh;
            if (disp.empty() && orig) disp = orig->artist;
            set_text(disp);
            break;
        }
        case 4: {  // Confidence
            if (is_failed) { set_wtext(L"N/A"); break; }
            if (result.title_zh.empty() && result.album_zh.empty() && result.artist_zh.empty()) {
                set_wtext(L"N/A (Chinese)");
            } else {
                std::ostringstream oss;
                oss << std::fixed << std::setprecision(2) << result.translation_confidence;
                set_text(oss.str());
            }
            break;
        }
        case 5: {  // Success
            if (is_failed) { set_wtext(L"Failed"); break; }
            if (result.title_zh.empty() && result.album_zh.empty() && result.artist_zh.empty()) {
                set_wtext(L"Skipped");
            } else {
                set_wtext(L"Yes");
            }
            break;
        }
    }
}

void EnhanceConfirmDialog::OnItemChanged(LPARAM lp) {
    NMLISTVIEW* pnm = reinterpret_cast<NMLISTVIEW*>(lp);
    if (!pnm || !s_selected) return;
    if (pnm->iItem < 0 || pnm->iItem >= static_cast<int>(s_selected->size())) return;
    if (pnm->uChanged & LVIF_STATE) {
        if (pnm->uNewState & LVIS_STATEIMAGEMASK) {
            int img = (pnm->uNewState >> 12) & 0xF;
            (*s_selected)[pnm->iItem] = (img == 2);
        }
    }
}

// LVS_OWNERDATA + LVS_EX_CHECKBOXES：手动处理 checkbox 点击
void EnhanceConfirmDialog::OnCheckboxClick(LPARAM lp) {
    if (!s_selected || !s_results) return;
    LPNMITEMACTIVATE pnm = reinterpret_cast<LPNMITEMACTIVATE>(lp);
    if (!pnm) return;
    HWND hList = pnm->hdr.hwndFrom;
    if (!hList) return;
    int idx = pnm->iItem;
    if (idx < 0 || idx >= static_cast<int>(s_selected->size())) return;

    LVHITTESTINFO hit = {};
    hit.pt = pnm->ptAction;
    if (ListView_SubItemHitTest(hList, &hit) == -1) return;
    if (!(hit.flags & LVHT_ONITEMSTATEICON)) return;

    (*s_selected)[idx] = !(*s_selected)[idx];
    ListView_RedrawItems(hList, idx, idx);
    UpdateWindow(hList);
}

void EnhanceConfirmDialog::OnSelectAll(HWND wnd) {
    if (!s_selected) return;
    std::fill(s_selected->begin(), s_selected->end(), true);
    HWND hList = GetDlgItem(wnd, IDC_ENHANCE_LISTVIEW);
    if (hList && !s_selected->empty()) {
        int n = static_cast<int>(s_selected->size());
        for (int i = 0; i < n; ++i) {
            ListView_SetItemState(hList, i, INDEXTOSTATEIMAGEMASK(2), LVIS_STATEIMAGEMASK);
        }
        ListView_RedrawItems(hList, 0, n - 1);
        UpdateWindow(hList);
    }
}

void EnhanceConfirmDialog::OnSelectNone(HWND wnd) {
    if (!s_selected) return;
    std::fill(s_selected->begin(), s_selected->end(), false);
    HWND hList = GetDlgItem(wnd, IDC_ENHANCE_LISTVIEW);
    if (hList && !s_selected->empty()) {
        int n = static_cast<int>(s_selected->size());
        for (int i = 0; i < n; ++i) {
            ListView_SetItemState(hList, i, INDEXTOSTATEIMAGEMASK(1), LVIS_STATEIMAGEMASK);
        }
        ListView_RedrawItems(hList, 0, n - 1);
        UpdateWindow(hList);
    }
}

void EnhanceConfirmDialog::OnSelectSuccess(HWND wnd) {
    if (!s_selected || !s_results) return;
    HWND hList = GetDlgItem(wnd, IDC_ENHANCE_LISTVIEW);
    int n = static_cast<int>(s_selected->size());
    for (int i = 0; i < n; ++i) {
        const auto& result = (*s_results)[i];
        bool has_translation = result.success &&
            !(result.title_zh.empty() && result.album_zh.empty() && result.artist_zh.empty());
        (*s_selected)[i] = has_translation;
        if (hList) {
            ListView_SetItemState(hList, i, INDEXTOSTATEIMAGEMASK(has_translation ? 2 : 1), LVIS_STATEIMAGEMASK);
        }
    }
    if (hList && n > 0) {
        ListView_RedrawItems(hList, 0, n - 1);
        UpdateWindow(hList);
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
    // LVS_OWNERDATA：s_selected 已实时同步
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
                switch (pnmh->code) {
                    case LVN_GETDISPINFO:
                        OnGetDispInfo(lp);
                        return TRUE;
                    case LVN_ITEMCHANGED:
                        OnItemChanged(lp);
                        return TRUE;
                    case NM_CLICK: {
                        // LVS_OWNERDATA + LVS_EX_CHECKBOXES 模式下，点击 checkbox 时
                        // listview 不存储状态，会立即用 LVN_GETDISPINFO 还原。所以必须在
                        // NM_CLICK 中手动检测点击位置并切换 s_selected_groups
                        OnClick(wnd, lp);
                        return TRUE;
                    }
                    case NM_DBLCLK: {
                        LPNMITEMACTIVATE pnmitem = reinterpret_cast<LPNMITEMACTIVATE>(lp);
                        if (pnmitem->iItem >= 0) {
                            OnEditGroupAt(wnd, pnmitem->iItem);
                        }
                        return TRUE;
                    }
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

    // LVS_OWNERDATA：item 总数 = groups + uncertain
    int total = static_cast<int>(s_result->groups.size() + s_result->uncertain.size());
    // 确保 s_selected_groups 至少覆盖 groups（uncertain 不可选），新元素默认 true
    if (s_selected_groups && s_selected_groups->size() < s_result->groups.size()) {
        s_selected_groups->resize(s_result->groups.size(), true);
    }

    // 设置初始化标志，防止 ListView_SetItemCountEx 触发的 LVN_ITEMCHANGED
    // 把 s_selected_groups 覆盖成 false（listview 默认 state image = 1 = unchecked）
    s_populating = true;
    ListView_SetItemCountEx(hList, total, LVSICF_NOINVALIDATEALL);
    s_populating = false;

    // 触发重绘，让 LVN_GETDISPINFO 返回正确的 state（包括 checkbox 状态）
    if (total > 0) {
        ListView_RedrawItems(hList, 0, total - 1);
        UpdateWindow(hList);
    }
}

// 判断行是否为 uncertain（按索引计算，无需 lParam）
inline bool NormalizeConfirmDialog::IsUncertainRow(int idx) {
    if (!s_result) return false;
    return idx >= static_cast<int>(s_result->groups.size());
}

void NormalizeConfirmDialog::OnGetDispInfo(LPARAM lp) {
    NMLVDISPINFO* di = reinterpret_cast<NMLVDISPINFO*>(lp);
    if (!di || !s_result) return;
    int idx = di->item.iItem;
    int group_count = static_cast<int>(s_result->groups.size());
    int total = group_count + static_cast<int>(s_result->uncertain.size());
    if (idx < 0 || idx >= total) return;

    bool is_uncertain = (idx >= group_count);

    // 复选框状态：LVS_EX_CHECKBOXES 需要返回 LVIF_STATE。
    // 关键修复：即使 mask 不包含 LVIF_STATE，也要强制设置 state，
    // 因为 LVS_OWNERDATA 下 listview 不存储状态，每次绘制 checkbox 都会请求。
    // 如果只在 mask & LVIF_STATE 时设置，首次绘制可能漏掉，导致 checkbox 显示为未勾选。
    di->item.mask |= LVIF_STATE;
    if (is_uncertain) {
        // uncertain 行：无复选框（state image 0）
        di->item.state = INDEXTOSTATEIMAGEMASK(0);
        di->item.stateMask = LVIS_STATEIMAGEMASK;
    } else {
        bool checked = s_selected_groups && idx < static_cast<int>(s_selected_groups->size()) && (*s_selected_groups)[idx];
        di->item.state = INDEXTOSTATEIMAGEMASK(checked ? 2 : 1);
        di->item.stateMask = LVIS_STATEIMAGEMASK;
    }

    if (!(di->item.mask & LVIF_TEXT)) return;

    // 注意：每个 subitem 的文本缓冲必须独立，避免被后续覆盖
    static thread_local std::wstring cell_buf;
    auto set_text = [&](const std::string& s) {
        cell_buf = to_wstring(s);
        di->item.pszText = const_cast<wchar_t*>(cell_buf.c_str());
    };
    auto set_wtext = [&](const std::wstring& s) {
        cell_buf = s;
        di->item.pszText = const_cast<wchar_t*>(cell_buf.c_str());
    };

    if (!is_uncertain) {
        const auto& g = s_result->groups[idx];
        switch (di->item.iSubItem) {
            case 0: set_wtext(L"Group"); break;
            case 1: set_text(g.canonical_name); break;
            case 2: {
                std::string aliases_str;
                for (size_t j = 0; j < g.aliases.size(); ++j) {
                    if (g.aliases[j].empty()) continue;
                    if (!aliases_str.empty()) aliases_str += ", ";
                    aliases_str += g.aliases[j];
                }
                set_text(aliases_str);
                break;
            }
            case 3: {
                std::wostringstream conf_ss;
                conf_ss << std::fixed << std::setprecision(2) << g.confidence;
                set_wtext(conf_ss.str());
                break;
            }
            case 4: set_text(g.reason); break;
        }
    } else {
        int u_idx = idx - group_count;
        const auto& u = s_result->uncertain[u_idx];
        switch (di->item.iSubItem) {
            case 0: set_wtext(L"? Uncertain"); break;
            case 1: set_wtext(L"(uncertain)"); break;
            case 2: set_text(u.alias); break;
            case 3: set_wtext(L"-"); break;
            case 4: set_text(u.reason); break;
        }
    }
}

void NormalizeConfirmDialog::OnItemChanged(LPARAM lp) {
    NMLISTVIEW* pnm = reinterpret_cast<NMLISTVIEW*>(lp);
    if (!pnm || !s_selected_groups || !s_result) return;
    // 初始化期间（PopulateListView 调用 ListView_SetItemCountEx/RedrawItems）
    // 会触发 LVN_ITEMCHANGED，此时不应同步 s_selected_groups（会用 listview
    // 默认 unchecked 覆盖我们设置的 true）
    if (s_populating) return;
    int idx = pnm->iItem;
    if (idx < 0 || idx >= static_cast<int>(s_result->groups.size())) return;  // 仅 groups 可选
    // 只有当 state image 真正变化时才同步（uChanged & LVIF_STATE）
    if (!(pnm->uChanged & LVIF_STATE)) return;
    // 提取 state image index（bits 12-15）
    int img = (pnm->uNewState >> 12) & 0xF;
    if (img == 0) return;  // 0 表示无 state image（uncertain 行或初始化）
    bool new_checked = (img == 2);  // 2 = checked, 1 = unchecked
    // 仅当状态真正变化时才更新和重绘（避免消息循环）
    if (idx < static_cast<int>(s_selected_groups->size()) &&
        (*s_selected_groups)[idx] != new_checked) {
        (*s_selected_groups)[idx] = new_checked;
        // LVS_OWNERDATA 下 listview 不存储状态，需手动触发重绘
        // pnm->hdr.hwndFrom 就是 listview 控件句柄
        ListView_RedrawItems(pnm->hdr.hwndFrom, idx, idx);
        UpdateWindow(pnm->hdr.hwndFrom);
    }
}

void NormalizeConfirmDialog::OnClick(HWND wnd, LPARAM lp) {
    // LVS_OWNERDATA + LVS_EX_CHECKBOXES 模式下，点击 checkbox 时 listview 不存储状态，
    // 会立即用 LVN_GETDISPINFO 还原为 s_selected_groups 中的值，导致用户点击"无效"。
    // 这里手动检测点击是否落在 checkbox 区域，并切换 s_selected_groups[idx]。
    if (!s_selected_groups || !s_result) return;

    LPNMITEMACTIVATE pnm = reinterpret_cast<LPNMITEMACTIVATE>(lp);
    if (!pnm) return;

    HWND hList = pnm->hdr.hwndFrom;
    if (!hList) return;

    int idx = pnm->iItem;
    if (idx < 0 || idx >= static_cast<int>(s_result->groups.size())) return;  // 仅 groups 可选

    // 检测点击位置是否在 checkbox 区域（state image 区域）
    // 方法：获取该 item 的 rect，判断点击 X 坐标是否落在 checkbox 宽度内
    LVHITTESTINFO hit = {};
    hit.pt = pnm->ptAction;
    // ptAction 是 client 坐标，直接用 hit test
    if (ListView_SubItemHitTest(hList, &hit) == -1) return;

    // 检查 hit 标志：LVHT_ONITEMSTATEICON 表示点击在 checkbox 上
    if (!(hit.flags & LVHT_ONITEMSTATEICON)) return;

    // 切换状态
    if (idx >= static_cast<int>(s_selected_groups->size())) return;
    (*s_selected_groups)[idx] = !(*s_selected_groups)[idx];

    // 强制重绘该行（触发 LVN_GETDISPINFO 返回新状态）
    ListView_RedrawItems(hList, idx, idx);
    UpdateWindow(hList);
}

void NormalizeConfirmDialog::OnSelectAll(HWND wnd) {
    if (!s_selected_groups || !s_result) return;
    std::fill(s_selected_groups->begin(), s_selected_groups->end(), true);
    HWND hList = GetDlgItem(wnd, IDC_NORMALIZE_CONFIRM_LIST);
    if (hList) {
        int total = static_cast<int>(s_result->groups.size() + s_result->uncertain.size());
        if (total > 0) {
            ListView_RedrawItems(hList, 0, total - 1);
            UpdateWindow(hList);
        }
    }
}

void NormalizeConfirmDialog::OnSelectNone(HWND wnd) {
    if (!s_selected_groups || !s_result) return;
    std::fill(s_selected_groups->begin(), s_selected_groups->end(), false);
    HWND hList = GetDlgItem(wnd, IDC_NORMALIZE_CONFIRM_LIST);
    if (hList) {
        int total = static_cast<int>(s_result->groups.size() + s_result->uncertain.size());
        if (total > 0) {
            ListView_RedrawItems(hList, 0, total - 1);
            UpdateWindow(hList);
        }
    }
}

void NormalizeConfirmDialog::OnEditGroupAt(HWND wnd, int item_index) {
    if (!s_result || item_index < 0) return;

    HWND hList = GetDlgItem(wnd, IDC_NORMALIZE_CONFIRM_LIST);
    if (!hList) return;

    int group_count = (int)s_result->groups.size();
    bool is_uncertain = (item_index >= group_count);

    std::string init_canonical;
    std::vector<std::string> init_aliases;
    std::string init_reason;

    if (is_uncertain) {
        int uncertain_idx = item_index - group_count;
        if (uncertain_idx < 0 || uncertain_idx >= (int)s_result->uncertain.size()) return;
        const auto& u = s_result->uncertain[uncertain_idx];
        init_canonical = u.alias;
        init_aliases = {u.alias};
        init_reason = u.reason;
    } else {
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

    // 去重 alias
    {
        std::set<std::string> seen;
        std::vector<std::string> deduped;
        deduped.reserve(out_aliases.size());
        for (const auto& a : out_aliases) {
            if (a.empty()) continue;
            if (seen.insert(a).second) deduped.push_back(a);
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

    if (is_uncertain) {
        int uncertain_idx = item_index - group_count;
        s_result->uncertain.erase(s_result->uncertain.begin() + uncertain_idx);

        NormalizeGroup new_g;
        new_g.canonical_name = out_canonical;
        new_g.aliases = out_aliases;
        new_g.confidence = 1.0f;
        new_g.reason = "User confirmed (from uncertain)";
        s_result->groups.push_back(std::move(new_g));

        if (s_selected_groups) s_selected_groups->push_back(true);
    } else {
        NormalizeGroup& g = s_result->groups[item_index];
        g.canonical_name = out_canonical;
        g.aliases = out_aliases;
    }

    // 刷新列表（LVS_OWNERDATA：重新设置 item count 触发 LVN_GETDISPINFO）
    PopulateListView(wnd);
}

void NormalizeConfirmDialog::OnOK(HWND wnd) {
    Logger::instance().info("[NormalizeConfirm] OnOK: ENTER");

    // LVS_OWNERDATA：s_selected_groups 已在 LVN_ITEMCHANGED 中实时同步，无需读取 ListView
    if (!s_selected_groups || !s_result) {
        Logger::instance().info("[NormalizeConfirm] OnOK: null s_selected_groups or s_result, cancelling");
        s_confirmed = false;
        EndDialog(wnd, IDCANCEL);
        return;
    }

    Logger::instance().info("[NormalizeConfirm] OnOK: groups=" + std::to_string(s_result->groups.size()) +
                            ", uncertain=" + std::to_string(s_result->uncertain.size()) +
                            ", selected_groups_size=" + std::to_string(s_selected_groups->size()));

    // 仅保留 groups 部分的勾选状态（resize 时保留已有值，新元素默认 true）
    if (s_selected_groups->size() < s_result->groups.size()) {
        s_selected_groups->resize(s_result->groups.size(), true);
    } else if (s_selected_groups->size() > s_result->groups.size()) {
        s_selected_groups->resize(s_result->groups.size());
    }

    // 统计选中数量（诊断用）
    int selected_count = 0;
    for (size_t i = 0; i < s_selected_groups->size(); ++i) {
        if ((*s_selected_groups)[i]) ++selected_count;
    }
    Logger::instance().info("[NormalizeConfirm] OnOK: selected_count=" + std::to_string(selected_count));

    s_confirmed = true;
    Logger::instance().info("[NormalizeConfirm] OnOK: calling EndDialog");
    EndDialog(wnd, IDOK);
    Logger::instance().info("[NormalizeConfirm] OnOK: EXIT");
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

// ==================== Completion Dialog ====================

CompletionStats CompletionDialog::s_stats;

static std::wstring format_elapsed(int64_t ms) {
    if (ms < 1000) {
        return std::to_wstring(ms) + L" ms";
    }
    double sec = ms / 1000.0;
    std::wostringstream woss;
    if (sec < 60) {
        woss << std::fixed << std::setprecision(1) << sec << L" s";
    } else {
        int m = static_cast<int>(sec) / 60;
        int s = static_cast<int>(sec) % 60;
        woss << m << L"m " << s << L"s";
    }
    return woss.str();
}

bool CompletionDialog::Show(HWND parent, const CompletionStats& stats) {
    s_stats = stats;
    INT_PTR ret = DialogBoxParam(
        core_api::get_my_instance(),
        MAKEINTRESOURCE(IDD_COMPLETION),
        parent,
        CompletionDialog::DlgProc,
        0
    );
    return ret == IDOK;
}

INT_PTR CALLBACK CompletionDialog::DlgProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) {
    static bool s_details_expanded = false;
    switch (msg) {
        case WM_INITDIALOG: {
            s_details_expanded = false;
            // 标题
            if (!s_stats.caption.empty()) {
                SetWindowTextW(wnd, to_wstring(s_stats.caption).c_str());
            }
            // 各字段
            SetDlgItemInt(wnd, IDC_TOTAL_TRACKS, s_stats.total_tracks, FALSE);
            SetDlgItemInt(wnd, IDC_SUCCESS_COUNT, s_stats.success_count, FALSE);
            SetDlgItemInt(wnd, IDC_FAILED_COUNT, s_stats.failed_count, FALSE);
            SetDlgItemInt(wnd, IDC_CACHE_HITS, s_stats.cache_hits, FALSE);
            SetDlgItemInt(wnd, IDC_API_CALLS, s_stats.api_calls, FALSE);
            SetDlgItemInt(wnd, IDC_TOKENS_USED, s_stats.tokens_used, FALSE);
            SetDlgItemTextW(wnd, IDC_ELAPSED_TIME, format_elapsed(s_stats.elapsed_ms).c_str());
            if (!s_stats.details_label.empty()) {
                SetDlgItemTextW(wnd, IDC_DETAILS_LABEL, to_wstring(s_stats.details_label).c_str());
            } else {
                SetDlgItemTextW(wnd, IDC_DETAILS_LABEL, L"");
            }
            // 填充失败详情到隐藏的 EDITTEXT；若无失败项则禁用 View Details 按钮
            if (!s_stats.failed_details.empty()) {
                std::ostringstream oss;
                for (const auto& line : s_stats.failed_details) {
                    oss << line << "\r\n";
                }
                SetDlgItemTextW(wnd, IDC_DETAILS_EDIT, to_wstring(oss.str()).c_str());
            } else {
                EnableWindow(GetDlgItem(wnd, IDVIEWDETAILS), FALSE);
            }
            return TRUE;
        }
        case WM_COMMAND:
            switch (LOWORD(wp)) {
                case IDOK:
                    EndDialog(wnd, IDOK);
                    return TRUE;
                case IDVIEWDETAILS: {
                    // 切换详情编辑框的显示状态
                    HWND hEdit = GetDlgItem(wnd, IDC_DETAILS_EDIT);
                    if (hEdit) {
                        s_details_expanded = !s_details_expanded;
                        ShowWindow(hEdit, s_details_expanded ? SW_SHOW : SW_HIDE);
                        SetDlgItemTextW(wnd, IDVIEWDETAILS,
                                        s_details_expanded ? L"Hide Details" : L"View Details");
                    }
                    return TRUE;
                }
                case IDCANCEL:
                    EndDialog(wnd, IDCANCEL);
                    return TRUE;
            }
            break;
    }
    return FALSE;
}

}
