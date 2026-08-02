// SettingsDialog.cpp - 设置窗口实现
// 模式对话框 + Tab 控件，修改即时生效（实时预览任务栏）
#include "pch.h"
#include "SettingsDialog.h"
#include "AppConfig.h"
#include "OverlayWindow.h"

#include <commctrl.h>
#include <commdlg.h>
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")

// ===================== 常量 =====================

static const wchar_t* kClassName = L"TaskbarStudioSettings";
static const int kDialogW = 420;
static const int kDialogH = 340;
static const int kTabCtrlId = 100;

// Tab 页索引
enum TabPage { TAB_DISPLAY = 0, TAB_APPEARANCE, TAB_COLOR, TAB_TEMP, TAB_NET, TAB_COUNT };

// 控件 ID 范围
enum : int {
    IDC_BASE = 200,
    // 显示页 (200-219)
    IDC_SHOW_CPU_TEMP = 200, IDC_SHOW_CPU_USAGE, IDC_SHOW_GPU_TEMP,
    IDC_SHOW_GPU_USAGE, IDC_SHOW_MEM, IDC_SHOW_NET_UP, IDC_SHOW_NET_DOWN,
    // 外观页 (220-239)
    IDC_FONT_COMBO = 220, IDC_FONT_LABEL, IDC_SIZE_COMBO, IDC_SIZE_LABEL,
    IDC_SPACING_COMBO, IDC_SPACING_LABEL,
    IDC_DOTS_CHECK, IDC_SEPARATOR_CHECK,
    // 颜色页 (240-269)
    IDC_TEXT_COLOR_BASE = 240,  // 240-255: 文字色板 (16色)
    IDC_NETUP_COLOR_BASE = 260, // 260-265: 上行色板 (6色)
    IDC_NETDOWN_COLOR_BASE = 270, // 270-275: 下行色板 (6色)
    IDC_TEXT_COLOR_LABEL = 280, IDC_NETUP_LABEL, IDC_NETDOWN_LABEL,
    IDC_NET_SPLIT_CHECK,
    IDC_CUSTOM_COLOR,
    // 温度页 (290-299)
    IDC_TEMP_GRADIENT_CHECK = 290, IDC_TEMP_LOW_COMBO, IDC_TEMP_HIGH_COMBO,
    IDC_TEMP_LOW_LABEL, IDC_TEMP_HIGH_LABEL,
    // 网络页 (300-309)
    IDC_NET_SPLIT_CHECK2 = 300,
};

// ===================== 色板定义 =====================

struct ColorSwatch { uint32_t argb; const wchar_t* name; };

static const ColorSwatch kTextPalette[] = {
    {0xFFFFFFFF, L"纯白"}, {0xFFF1F5F9, L"霜白"}, {0xFFE2E8F0, L"银灰"},
    {0xFFFBBF24, L"琥珀"}, {0xFFFB923C, L"暖橙"}, {0xFFF87171, L"珊瑚"},
    {0xFFA78BFA, L"紫罗兰"}, {0xFF818CF8, L"靛蓝"}, {0xFF60A5FA, L"天青"},
    {0xFF34D399, L"翡翠"}, {0xFF4ADE80, L"草绿"}, {0xFF2DD4BF, L"碧青"},
};
static const int kTextPaletteCount = 12;

static const ColorSwatch kNetPalette[] = {
    {0xFFFF8C00, L"橙"}, {0xFF00CED1, L"青"}, {0xFF4A90E2, L"蓝"},
    {0xFF00FF66, L"绿"}, {0xFFFF69B4, L"粉"}, {0xFFFFFFFF, L"白"},
};
static const int kNetPaletteCount = 6;

// ===================== 状态 =====================

static HWND g_hDlg = nullptr;
static HWND g_hTab = nullptr;
static HWND g_hPage[TAB_COUNT] = {};   // 每个 tab 页的容器
static std::vector<std::wstring> g_fontList;
static HFONT g_hUiFont = nullptr;

// ===================== 前置声明 =====================

static void ApplyConfig();
static void CreateTabPage(HWND hDlg, int page);
static void ShowPage(int page);
static void PopulateFonts();
static LRESULT CALLBACK DlgProc(HWND, UINT, WPARAM, LPARAM);

// ===================== 字体枚举 =====================

struct FontEnumCtx {
    std::vector<std::wstring>* list;
};

static int CALLBACK FontEnumProc(const LOGFONTW* lf, const TEXTMETRICW* tm,
                                  DWORD fontType, LPARAM lParam) {
    auto* ctx = reinterpret_cast<FontEnumCtx*>(lParam);

    // 过滤：只要 TrueType/OpenType，排除符号/装饰字体
    if (!(fontType & TRUETYPE_FONTTYPE) && !(fontType & DEVICE_FONTTYPE)) return 1;
    if (lf->lfCharSet == SYMBOL_CHARSET) return 1;
    if (lf->lfFaceName[0] == L'@') return 1;  // 垂直排版变体

    // 过滤不适合小字号的字体（高度 < 10 时 tmInternalLeading 占比过大）
    if (tm->tmHeight > 0 && tm->tmInternalLeading > tm->tmHeight / 3) return 1;

    // 去重（同一家族可能有 Bold/Italic 变体）
    std::wstring name = lf->lfFaceName;
    auto& list = *ctx->list;
    if (std::find(list.begin(), list.end(), name) == list.end()) {
        list.push_back(name);
    }
    return 1;
}

static void PopulateFonts() {
    g_fontList.clear();
    // 推荐字体置顶（现代免费字体优先，未安装时 DWrite 自动 fallback）
    const wchar_t* recommended[] = {
        L"Inter",              // 现代 UI 无衬线，屏幕优化
        L"LXGW WenKai",       // 霞鹜文楷，中文楷体风格
        L"Maple Mono SC",     // 圆润等宽，中文友好
        L"MiSans",            // 小米出品，现代简洁
        L"Sarasa UI SC",      // 更纱黑体，中英混排
        L"Segoe UI",          // Windows 系统默认
        L"Microsoft YaHei",   // 微软雅黑，中文系统字体
    };
    for (auto* r : recommended) {
        g_fontList.push_back(r);
    }

    // 分隔后列出所有系统字体
    HDC hdc = GetDC(nullptr);
    LOGFONTW lf{};
    lf.lfCharSet = DEFAULT_CHARSET;
    FontEnumCtx ctx{&g_fontList};
    EnumFontFamiliesExW(hdc, &lf, FontEnumProc, reinterpret_cast<LPARAM>(&ctx), 0);
    ReleaseDC(nullptr, hdc);

    // 排序（推荐字体已在前面，其余按字母序）
    if (g_fontList.size() > 7) {
        std::sort(g_fontList.begin() + 7, g_fontList.end());
    }
}

// ===================== 公共接口 =====================

void SettingsDialog::Show(HWND owner) {
    if (g_hDlg && IsWindow(g_hDlg)) {
        SetForegroundWindow(g_hDlg);
        return;
    }

    // 注册窗口类
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = DlgProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kClassName;
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassExW(&wc);

    // 居中于主显示器
    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    int x = (sw - kDialogW) / 2;
    int y = (sh - kDialogH) / 2;

    g_hDlg = CreateWindowExW(WS_EX_TOOLWINDOW, kClassName, L"TaskbarStudio 设置",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        x, y, kDialogW, kDialogH, owner, nullptr, wc.hInstance, nullptr);

    if (g_hDlg) ShowWindow(g_hDlg, SW_SHOW);
}

void SettingsDialog::Close() {
    if (g_hDlg) { DestroyWindow(g_hDlg); g_hDlg = nullptr; }
}

bool SettingsDialog::IsOpen() {
    return g_hDlg && IsWindow(g_hDlg);
}

// ===================== 应用配置 =====================

static void ApplyConfig() {
    AppConfig::Instance().Save();

    // 同步到悬浮窗
    const DisplayConfig& dc = AppConfig::Instance().Get();
    OverlayConfig oc{};
    oc.showCpuTemp      = dc.showCpuTemp;
    oc.showCpuUsage     = dc.showCpuUsage;
    oc.showGpuTemp      = dc.showGpuTemp;
    oc.showGpuUsage     = dc.showGpuUsage;
    oc.showMemUsage     = dc.showMemUsage;
    oc.showNetUp        = dc.showNetUp;
    oc.showNetDown      = dc.showNetDown;
    oc.fontSize         = dc.fontSize;
    oc.textColor        = dc.textColor;
    oc.accentColor      = dc.accentColor;
    oc.showIndicatorDots= dc.showIndicatorDots;
    oc.tempColorGradient= dc.tempColorGradient;
    oc.netColorSplit    = dc.netColorSplit;
    oc.showSeparator    = dc.showSeparator;
    oc.netUpColor       = dc.netUpColor;
    oc.netDownColor     = dc.netDownColor;
    oc.fontFamily       = dc.fontFamily;
    oc.tempLowThreshold = dc.tempLowThreshold;
    oc.tempHighThreshold= dc.tempHighThreshold;
    oc.spacingScale     = dc.spacingScale;
    oc.alignRight       = false;
    OverlayWindow::Instance().SetConfig(oc);
}

// ===================== 创建 Tab 页内容 =====================

static HWND CreateCtrl(HWND parent, const wchar_t* cls, const wchar_t* text,
                        DWORD style, int x, int y, int w, int h, int id) {
    return CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style,
        x, y, w, h, parent, (HMENU)(INT_PTR)id, GetModuleHandleW(nullptr), nullptr);
}

static void CreateDisplayPage(HWND page) {
    const DisplayConfig& dc = AppConfig::Instance().Get();
    int y = 10;
    struct { const wchar_t* label; bool checked; int id; } items[] = {
        {L"CPU 温度", dc.showCpuTemp, IDC_SHOW_CPU_TEMP},
        {L"CPU 占用率", dc.showCpuUsage, IDC_SHOW_CPU_USAGE},
        {L"GPU 温度", dc.showGpuTemp, IDC_SHOW_GPU_TEMP},
        {L"GPU 占用率", dc.showGpuUsage, IDC_SHOW_GPU_USAGE},
        {L"内存占用", dc.showMemUsage, IDC_SHOW_MEM},
        {L"网络上行", dc.showNetUp, IDC_SHOW_NET_UP},
        {L"网络下行", dc.showNetDown, IDC_SHOW_NET_DOWN},
    };
    for (auto& item : items) {
        HWND h = CreateCtrl(page, L"BUTTON", item.label,
            BS_AUTOCHECKBOX | WS_TABSTOP, 15, y, 160, 22, item.id);
        SendMessageW(h, BM_SETCHECK, item.checked ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(h, WM_SETFONT, (WPARAM)g_hUiFont, TRUE);
        y += 28;
    }
}

static void CreateAppearancePage(HWND page) {
    const DisplayConfig& dc = AppConfig::Instance().Get();
    int y = 10;

    // 字体选择
    CreateCtrl(page, L"STATIC", L"显示字体:", 0, 15, y + 2, 60, 20, IDC_FONT_LABEL);
    HWND hFont = CreateCtrl(page, L"COMBOBOX", L"",
        CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 80, y, 200, 250, IDC_FONT_COMBO);
    SendMessageW(hFont, WM_SETFONT, (WPARAM)g_hUiFont, TRUE);
    // 填充字体列表
    int selIdx = 0;
    for (int i = 0; i < (int)g_fontList.size(); i++) {
        SendMessageW(hFont, CB_ADDSTRING, 0, (LPARAM)g_fontList[i].c_str());
        if (g_fontList[i] == dc.fontFamily) selIdx = i;
    }
    SendMessageW(hFont, CB_SETCURSEL, selIdx, 0);
    y += 32;

    // 字号
    CreateCtrl(page, L"STATIC", L"文字大小:", 0, 15, y + 2, 60, 20, IDC_SIZE_LABEL);
    HWND hSize = CreateCtrl(page, L"COMBOBOX", L"",
        CBS_DROPDOWNLIST | WS_TABSTOP, 80, y, 80, 120, IDC_SIZE_COMBO);
    SendMessageW(hSize, WM_SETFONT, (WPARAM)g_hUiFont, TRUE);
    SendMessageW(hSize, CB_ADDSTRING, 0, (LPARAM)L"小 (10)");
    SendMessageW(hSize, CB_ADDSTRING, 0, (LPARAM)L"中 (12)");
    SendMessageW(hSize, CB_ADDSTRING, 0, (LPARAM)L"大 (14)");
    int sizeIdx = (dc.fontSize <= 10.0f) ? 0 : (dc.fontSize <= 12.0f) ? 1 : 2;
    SendMessageW(hSize, CB_SETCURSEL, sizeIdx, 0);
    y += 32;

    // 间距
    CreateCtrl(page, L"STATIC", L"项目间距:", 0, 15, y + 2, 60, 20, IDC_SPACING_LABEL);
    HWND hSpace = CreateCtrl(page, L"COMBOBOX", L"",
        CBS_DROPDOWNLIST | WS_TABSTOP, 80, y, 80, 120, IDC_SPACING_COMBO);
    SendMessageW(hSpace, WM_SETFONT, (WPARAM)g_hUiFont, TRUE);
    SendMessageW(hSpace, CB_ADDSTRING, 0, (LPARAM)L"紧凑");
    SendMessageW(hSpace, CB_ADDSTRING, 0, (LPARAM)L"标准");
    SendMessageW(hSpace, CB_ADDSTRING, 0, (LPARAM)L"宽松");
    int spIdx = (dc.spacingScale <= 0.85f) ? 0 : (dc.spacingScale <= 1.15f) ? 1 : 2;
    SendMessageW(hSpace, CB_SETCURSEL, spIdx, 0);
    y += 36;

    // 复选框
    HWND hDots = CreateCtrl(page, L"BUTTON", L"指标前彩色圆点",
        BS_AUTOCHECKBOX | WS_TABSTOP, 15, y, 130, 22, IDC_DOTS_CHECK);
    SendMessageW(hDots, BM_SETCHECK, dc.showIndicatorDots ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(hDots, WM_SETFONT, (WPARAM)g_hUiFont, TRUE);

    HWND hSep = CreateCtrl(page, L"BUTTON", L"指标间竖线分隔",
        BS_AUTOCHECKBOX | WS_TABSTOP, 155, y, 130, 22, IDC_SEPARATOR_CHECK);
    SendMessageW(hSep, BM_SETCHECK, dc.showSeparator ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(hSep, WM_SETFONT, (WPARAM)g_hUiFont, TRUE);
}

static void CreateColorPage(HWND page) {
    const DisplayConfig& dc = AppConfig::Instance().Get();
    int y = 8;

    // 文字颜色色板
    HWND hLbl = CreateCtrl(page, L"STATIC", L"文字颜色:", 0, 15, y, 70, 18, IDC_TEXT_COLOR_LABEL);
    SendMessageW(hLbl, WM_SETFONT, (WPARAM)g_hUiFont, TRUE);
    y += 22;
    for (int i = 0; i < kTextPaletteCount; i++) {
        int col = i % 8;
        int row = i / 8;
        int bx = 15 + col * 36;
        int by = y + row * 28;
        HWND hBtn = CreateCtrl(page, L"BUTTON", L"",
            BS_OWNERDRAW | WS_TABSTOP, bx, by, 30, 22, IDC_TEXT_COLOR_BASE + i);
        // 用 Tag 存颜色值
        SetWindowLongPtrW(hBtn, GWLP_USERDATA, (LONG_PTR)kTextPalette[i].argb);
    }
    y += 62;

    // 网络上行色
    HWND hUp = CreateCtrl(page, L"STATIC", L"上行色:", 0, 15, y, 55, 18, IDC_NETUP_LABEL);
    SendMessageW(hUp, WM_SETFONT, (WPARAM)g_hUiFont, TRUE);
    for (int i = 0; i < kNetPaletteCount; i++) {
        HWND hBtn = CreateCtrl(page, L"BUTTON", L"",
            BS_OWNERDRAW | WS_TABSTOP, 75 + i * 36, y - 2, 30, 22, IDC_NETUP_COLOR_BASE + i);
        SetWindowLongPtrW(hBtn, GWLP_USERDATA, (LONG_PTR)kNetPalette[i].argb);
    }
    y += 28;

    // 网络下行色
    HWND hDn = CreateCtrl(page, L"STATIC", L"下行色:", 0, 15, y, 55, 18, IDC_NETDOWN_LABEL);
    SendMessageW(hDn, WM_SETFONT, (WPARAM)g_hUiFont, TRUE);
    for (int i = 0; i < kNetPaletteCount; i++) {
        HWND hBtn = CreateCtrl(page, L"BUTTON", L"",
            BS_OWNERDRAW | WS_TABSTOP, 75 + i * 36, y - 2, 30, 22, IDC_NETDOWN_COLOR_BASE + i);
        SetWindowLongPtrW(hBtn, GWLP_USERDATA, (LONG_PTR)kNetPalette[i].argb);
    }
    y += 30;

    // 网络异色开关
    HWND hSplit = CreateCtrl(page, L"BUTTON", L"上下行异色（关闭则统一用文字色）",
        BS_AUTOCHECKBOX | WS_TABSTOP, 15, y, 250, 22, IDC_NET_SPLIT_CHECK);
    SendMessageW(hSplit, BM_SETCHECK, dc.netColorSplit ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(hSplit, WM_SETFONT, (WPARAM)g_hUiFont, TRUE);
    y += 28;

    // 自定义颜色按钮（打开系统色盘）
    HWND hCustom = CreateCtrl(page, L"BUTTON", L"自定义文字色...",
        BS_PUSHBUTTON | WS_TABSTOP, 15, y, 110, 24, IDC_CUSTOM_COLOR);
    SendMessageW(hCustom, WM_SETFONT, (WPARAM)g_hUiFont, TRUE);
}

static void CreateTempPage(HWND page) {
    const DisplayConfig& dc = AppConfig::Instance().Get();
    int y = 10;

    HWND hGrad = CreateCtrl(page, L"BUTTON", L"温度色阶渐变（低温绿 → 高温红）",
        BS_AUTOCHECKBOX | WS_TABSTOP, 15, y, 280, 22, IDC_TEMP_GRADIENT_CHECK);
    SendMessageW(hGrad, BM_SETCHECK, dc.tempColorGradient ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(hGrad, WM_SETFONT, (WPARAM)g_hUiFont, TRUE);
    y += 34;

    // 低温阈值
    CreateCtrl(page, L"STATIC", L"开始变色:", 0, 15, y + 2, 70, 20, IDC_TEMP_LOW_LABEL);
    HWND hLow = CreateCtrl(page, L"COMBOBOX", L"",
        CBS_DROPDOWNLIST | WS_TABSTOP, 90, y, 70, 150, IDC_TEMP_LOW_COMBO);
    SendMessageW(hLow, WM_SETFONT, (WPARAM)g_hUiFont, TRUE);
    for (int t = 30; t <= 70; t += 5) {
        wchar_t buf[16]; swprintf_s(buf, L"%d°C", t);
        int idx = (int)SendMessageW(hLow, CB_ADDSTRING, 0, (LPARAM)buf);
        if (t == (int)dc.tempLowThreshold) SendMessageW(hLow, CB_SETCURSEL, idx, 0);
    }
    y += 32;

    // 高温阈值
    CreateCtrl(page, L"STATIC", L"全红温度:", 0, 15, y + 2, 70, 20, IDC_TEMP_HIGH_LABEL);
    HWND hHigh = CreateCtrl(page, L"COMBOBOX", L"",
        CBS_DROPDOWNLIST | WS_TABSTOP, 90, y, 70, 150, IDC_TEMP_HIGH_COMBO);
    SendMessageW(hHigh, WM_SETFONT, (WPARAM)g_hUiFont, TRUE);
    for (int t = 70; t <= 110; t += 5) {
        wchar_t buf[16]; swprintf_s(buf, L"%d°C", t);
        int idx = (int)SendMessageW(hHigh, CB_ADDSTRING, 0, (LPARAM)buf);
        if (t == (int)dc.tempHighThreshold) SendMessageW(hHigh, CB_SETCURSEL, idx, 0);
    }
}

static void CreateNetPage(HWND page) {
    const DisplayConfig& dc = AppConfig::Instance().Get();
    int y = 10;
    HWND hSplit = CreateCtrl(page, L"BUTTON", L"上下行异色",
        BS_AUTOCHECKBOX | WS_TABSTOP, 15, y, 120, 22, IDC_NET_SPLIT_CHECK2);
    SendMessageW(hSplit, BM_SETCHECK, dc.netColorSplit ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(hSplit, WM_SETFONT, (WPARAM)g_hUiFont, TRUE);
    y += 30;
    CreateCtrl(page, L"STATIC", L"（颜色在[颜色]页设置）", 0, 15, y, 200, 18, 0);
}

// 页容器窗口过程：转发 WM_COMMAND / WM_DRAWITEM 到父对话框
static LRESULT CALLBACK PageWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_COMMAND:
        case WM_DRAWITEM:
            // 转发给父对话框处理
            return SendMessageW(GetParent(hwnd), msg, wp, lp);
        default:
            return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

static bool g_pageClassRegistered = false;
static const wchar_t* kPageClassName = L"TSSettingsPage";

static void CreateTabPage(HWND hDlg, int page) {
    // 注册页容器类（仅一次）
    if (!g_pageClassRegistered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = PageWndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = kPageClassName;
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        RegisterClassExW(&wc);
        g_pageClassRegistered = true;
    }

    // 获取 Tab 控件的显示区域
    RECT rc;
    GetClientRect(g_hTab, &rc);
    TabCtrl_AdjustRect(g_hTab, FALSE, &rc);

    HWND hPage = CreateWindowExW(0, kPageClassName, L"", WS_CHILD | WS_CLIPCHILDREN,
        rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top,
        hDlg, nullptr, GetModuleHandleW(nullptr), nullptr);
    g_hPage[page] = hPage;

    switch (page) {
        case TAB_DISPLAY:    CreateDisplayPage(hPage); break;
        case TAB_APPEARANCE: CreateAppearancePage(hPage); break;
        case TAB_COLOR:      CreateColorPage(hPage); break;
        case TAB_TEMP:       CreateTempPage(hPage); break;
        case TAB_NET:        CreateNetPage(hPage); break;
    }
}

static void ShowPage(int page) {
    for (int i = 0; i < TAB_COUNT; i++) {
        if (g_hPage[i]) ShowWindow(g_hPage[i], (i == page) ? SW_SHOW : SW_HIDE);
    }
}

// ===================== 色板按钮绘制 =====================

static void DrawColorButton(LPDRAWITEMSTRUCT dis) {
    uint32_t argb = (uint32_t)GetWindowLongPtrW(dis->hwndItem, GWLP_USERDATA);
    HDC hdc = dis->hDC;
    RECT rc = dis->rcItem;

    // 填充颜色
    COLORREF cr = RGB((argb >> 16) & 0xFF, (argb >> 8) & 0xFF, argb & 0xFF);
    HBRUSH hBr = CreateSolidBrush(cr);
    FillRect(hdc, &rc, hBr);
    DeleteObject(hBr);

    // 边框（选中时高亮）
    HPEN hPen = CreatePen(PS_SOLID, (dis->itemState & ODS_SELECTED) ? 2 : 1,
        (dis->itemState & ODS_SELECTED) ? RGB(0, 120, 215) : RGB(128, 128, 128));
    HPEN hOld = (HPEN)SelectObject(hdc, hPen);
    HBRUSH hOldBr = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, rc.left, rc.top, rc.right, rc.bottom);
    SelectObject(hdc, hOld);
    SelectObject(hdc, hOldBr);
    DeleteObject(hPen);
}

// ===================== 命令处理 =====================

static void HandleCommand(HWND hDlg, int id, int code) {
    DisplayConfig dc = AppConfig::Instance().Get();
    bool changed = false;

    // 显示页复选框
    if (id >= IDC_SHOW_CPU_TEMP && id <= IDC_SHOW_NET_DOWN && code == BN_CLICKED) {
        bool checked = (IsDlgButtonChecked(g_hPage[TAB_DISPLAY], id) == BST_CHECKED);
        switch (id) {
            case IDC_SHOW_CPU_TEMP:  dc.showCpuTemp = checked; break;
            case IDC_SHOW_CPU_USAGE: dc.showCpuUsage = checked; break;
            case IDC_SHOW_GPU_TEMP:  dc.showGpuTemp = checked; break;
            case IDC_SHOW_GPU_USAGE: dc.showGpuUsage = checked; break;
            case IDC_SHOW_MEM:       dc.showMemUsage = checked; break;
            case IDC_SHOW_NET_UP:    dc.showNetUp = checked; break;
            case IDC_SHOW_NET_DOWN:  dc.showNetDown = checked; break;
        }
        changed = true;
    }

    // 外观页
    if (id == IDC_FONT_COMBO && code == CBN_SELCHANGE) {
        int sel = (int)SendMessageW(GetDlgItem(g_hPage[TAB_APPEARANCE], id), CB_GETCURSEL, 0, 0);
        if (sel >= 0 && sel < (int)g_fontList.size()) {
            dc.fontFamily = g_fontList[sel];
            changed = true;
        }
    }
    if (id == IDC_SIZE_COMBO && code == CBN_SELCHANGE) {
        int sel = (int)SendMessageW(GetDlgItem(g_hPage[TAB_APPEARANCE], id), CB_GETCURSEL, 0, 0);
        dc.fontSize = (sel == 0) ? 10.0f : (sel == 1) ? 12.0f : 14.0f;
        changed = true;
    }
    if (id == IDC_SPACING_COMBO && code == CBN_SELCHANGE) {
        int sel = (int)SendMessageW(GetDlgItem(g_hPage[TAB_APPEARANCE], id), CB_GETCURSEL, 0, 0);
        dc.spacingScale = (sel == 0) ? 0.8f : (sel == 1) ? 1.0f : 1.3f;
        changed = true;
    }
    if (id == IDC_DOTS_CHECK && code == BN_CLICKED) {
        dc.showIndicatorDots = (IsDlgButtonChecked(g_hPage[TAB_APPEARANCE], id) == BST_CHECKED);
        changed = true;
    }
    if (id == IDC_SEPARATOR_CHECK && code == BN_CLICKED) {
        dc.showSeparator = (IsDlgButtonChecked(g_hPage[TAB_APPEARANCE], id) == BST_CHECKED);
        changed = true;
    }

    // 颜色页 - 色板按钮
    if (id >= IDC_TEXT_COLOR_BASE && id < IDC_TEXT_COLOR_BASE + kTextPaletteCount && code == BN_CLICKED) {
        dc.textColor = kTextPalette[id - IDC_TEXT_COLOR_BASE].argb;
        changed = true;
    }
    if (id >= IDC_NETUP_COLOR_BASE && id < IDC_NETUP_COLOR_BASE + kNetPaletteCount && code == BN_CLICKED) {
        dc.netUpColor = kNetPalette[id - IDC_NETUP_COLOR_BASE].argb;
        changed = true;
    }
    if (id >= IDC_NETDOWN_COLOR_BASE && id < IDC_NETDOWN_COLOR_BASE + kNetPaletteCount && code == BN_CLICKED) {
        dc.netDownColor = kNetPalette[id - IDC_NETDOWN_COLOR_BASE].argb;
        changed = true;
    }
    if ((id == IDC_NET_SPLIT_CHECK || id == IDC_NET_SPLIT_CHECK2) && code == BN_CLICKED) {
        dc.netColorSplit = (IsDlgButtonChecked(g_hPage[TAB_COLOR], IDC_NET_SPLIT_CHECK) == BST_CHECKED);
        changed = true;
    }

    // 自定义颜色（系统色盘）
    if (id == IDC_CUSTOM_COLOR && code == BN_CLICKED) {
        static COLORREF customColors[16] = {};  // 用户自定义色缓存
        CHOOSECOLORW cc{};
        cc.lStructSize = sizeof(cc);
        cc.hwndOwner = g_hDlg;
        cc.rgbResult = RGB((dc.textColor >> 16) & 0xFF, (dc.textColor >> 8) & 0xFF, dc.textColor & 0xFF);
        cc.lpCustColors = customColors;
        cc.Flags = CC_FULLOPEN | CC_RGBINIT;
        if (ChooseColorW(&cc)) {
            dc.textColor = 0xFF000000u
                | ((uint32_t)GetRValue(cc.rgbResult) << 16)
                | ((uint32_t)GetGValue(cc.rgbResult) << 8)
                | (uint32_t)GetBValue(cc.rgbResult);
            changed = true;
        }
    }

    // 温度页
    if (id == IDC_TEMP_GRADIENT_CHECK && code == BN_CLICKED) {
        dc.tempColorGradient = (IsDlgButtonChecked(g_hPage[TAB_TEMP], id) == BST_CHECKED);
        changed = true;
    }
    if (id == IDC_TEMP_LOW_COMBO && code == CBN_SELCHANGE) {
        int sel = (int)SendMessageW(GetDlgItem(g_hPage[TAB_TEMP], id), CB_GETCURSEL, 0, 0);
        dc.tempLowThreshold = 30.0f + sel * 5.0f;
        changed = true;
    }
    if (id == IDC_TEMP_HIGH_COMBO && code == CBN_SELCHANGE) {
        int sel = (int)SendMessageW(GetDlgItem(g_hPage[TAB_TEMP], id), CB_GETCURSEL, 0, 0);
        dc.tempHighThreshold = 70.0f + sel * 5.0f;
        changed = true;
    }

    if (changed) {
        AppConfig::Instance().Set(dc);
        ApplyConfig();
    }
}

// ===================== 窗口过程 =====================

static LRESULT CALLBACK DlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: {
            // 创建 UI 字体
            g_hUiFont = CreateFontW(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Microsoft YaHei");

            // 枚举字体
            PopulateFonts();

            // 创建 Tab 控件
            g_hTab = CreateWindowExW(0, WC_TABCONTROLW, L"",
                WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | TCS_TABS,
                5, 5, kDialogW - 26, kDialogH - 50,
                hwnd, (HMENU)(INT_PTR)kTabCtrlId, GetModuleHandleW(nullptr), nullptr);
            SendMessageW(g_hTab, WM_SETFONT, (WPARAM)g_hUiFont, TRUE);

            // 添加 Tab 页
            TCITEMW ti{};
            ti.mask = TCIF_TEXT;
            const wchar_t* tabNames[] = {L"显示", L"外观", L"颜色", L"温度", L"网络"};
            for (int i = 0; i < TAB_COUNT; i++) {
                ti.pszText = (LPWSTR)tabNames[i];
                TabCtrl_InsertItem(g_hTab, i, &ti);
            }

            // 创建所有页面
            for (int i = 0; i < TAB_COUNT; i++) {
                CreateTabPage(hwnd, i);
            }
            ShowPage(0);
            return 0;
        }

        case WM_NOTIFY: {
            NMHDR* nmh = (NMHDR*)lp;
            if (nmh->idFrom == kTabCtrlId && nmh->code == TCN_SELCHANGE) {
                int sel = TabCtrl_GetCurSel(g_hTab);
                ShowPage(sel);
            }
            return 0;
        }

        case WM_COMMAND:
            HandleCommand(hwnd, LOWORD(wp), HIWORD(wp));
            return 0;

        case WM_DRAWITEM: {
            DRAWITEMSTRUCT* dis = (DRAWITEMSTRUCT*)lp;
            DrawColorButton(dis);
            return TRUE;
        }

        case WM_CLOSE:
            SettingsDialog::Close();
            return 0;

        case WM_DESTROY:
            if (g_hUiFont) { DeleteObject(g_hUiFont); g_hUiFont = nullptr; }
            g_hDlg = nullptr;
            g_hTab = nullptr;
            for (int i = 0; i < TAB_COUNT; i++) g_hPage[i] = nullptr;
            return 0;

        default:
            return DefWindowProcW(hwnd, msg, wp, lp);
    }
}
