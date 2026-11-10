// RoutePerApp - Hauptfenster.
// Die Entscheidungen (Spalten, Zonen, Zelltexte, Karten, Freigaben, Undo-Texte) stecken
// in gui_logic.cpp und core.cpp und sind dort unit-getestet; hier steckt nur Darstellung.
#include "gui_share.hpp"
#include "resource.hpp"

#include <uxtheme.h>
#include <objidl.h>

#include <algorithm>

// Aussehen, Sicherheitsstufe und Bildschirmdichte stehen in app.manifest (wird beim
// Durchbau in die Datei eingebaut): Administratorrechte werden dort verlangt.

using namespace sr;
using namespace srgui;

// ---------------------------------------------------------------- ids und Maße

namespace ids {
enum {
    IDC_LIST = 1001, IDC_SEARCH = 1002, IDC_CHK_NET = 1003,
    IDC_BTN_RELOAD = 1004, IDC_BTN_ADMIN = 1005
};
enum {
    IDM_ZONE_WLAN = 2001, IDM_ZONE_LAN = 2002, IDM_ZONE_STANDARD = 2003,
    IDM_CLEAR = 2004, IDM_DETAILS = 2005, IDM_COPY_PATH = 2006,
    IDM_UNDO = 2007, IDM_RELOAD = 2008, IDM_RESTART_ADMIN = 2009,
    IDM_FOCUS_SEARCH = 2010, IDM_EXIT = 2011
};
enum {
    IDM_HIDE = 2012, IDM_ABOUT = 2013, IDM_KEYS = 2014,
    IDM_TRAY_OPEN = 2015, IDM_REINJECT = 2016,
    IDM_AUTOSTART = 2017, IDM_DEF_STANDARD = 2018, IDM_DEF_WLAN = 2019,
    IDM_DEF_LAN = 2020, IDM_HOTKEYS = 2021,
    IDM_LANG_DE = 2022, IDM_LANG_EN = 2023
};
// Nummern der globalen Tastenkürzel (Strg+Alt+...)
enum { HK_WLAN = 1, HK_LAN = 2, HK_STANDARD = 3, HK_RAISE = 4 };
enum { TIMER_REFRESH = 1, TIMER_SELFTEST = 2 };
constexpr UINT WM_APP_TRAY = WM_APP + 1;      // Meldung vom Symbol in der Taskleiste
constexpr UINT TRAY_UID = 1;
} // namespace ids

// Fassung an einer Stelle; steht ausserdem in app.rc (Dateieigenschaften) und im Selbsttest-Log.
static const wchar_t* kAppVersion = RPA_VERSION_TEXT_W;

// Wird gemeldet, wenn die Taskleiste neu gestartet wurde (Explorer abgestürzt o. ä.).
static UINT g_taskbarCreated = 0;

namespace theme {
const COLORREF bg = RGB(246, 247, 249);
const COLORREF card = RGB(255, 255, 255);
const COLORREF cardHot = RGB(232, 241, 255);
const COLORREF text = RGB(23, 27, 34);
const COLORREF muted = RGB(110, 118, 129);
const COLORREF border = RGB(214, 219, 227);
const COLORREF borderHot = RGB(31, 96, 214);
const COLORREF rowAlt = RGB(249, 250, 252);
const COLORREF rowHot = RGB(223, 235, 254);
const COLORREF statusBg = RGB(236, 238, 242);
const COLORREF wlan = RGB(10, 107, 66);
const COLORREF lan = RGB(24, 84, 196);
const COLORREF standard = RGB(104, 112, 124);

inline COLORREF Accent(Zone z) {
    switch (z) {
    case Zone::Wlan: return wlan;
    case Zone::Lan:  return lan;
    default:         return standard;
    }
}
} // namespace theme

static App* g_app = nullptr;
static int g_dpi = 96;

App* TheApp() { return g_app; }

static int S(int v) { return MulDiv(v, g_dpi, 96); }

static HFONT MakeFont(int points, int weight) {
    return CreateFontW(-MulDiv(points, g_dpi, 72), 0, 0, 0, weight,
                       FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_TT_PRECIS,
                       CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FF_DONTCARE, L"Segoe UI");
}

static const wchar_t* kCardClass = L"RoutePerApp.CardZone";

static void CopyToClipboard(HWND owner, const std::wstring& text);

// Layout-Maße in 96-dpi-Einheiten (skaliert mit S())
static int BandTop();
static int CardTop();
static int CardHeight();
static int ListTop();
static int StatusHeight();

// ---------------------------------------------------------------- Kartenfenster

static void PaintCard(HWND hwnd, HDC target) {
    App* app = TheApp();
    CardState* st = reinterpret_cast<CardState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!app || !st) return;

    RECT rc;
    GetClientRect(hwnd, &rc);
    const int w = rc.right - rc.left;
    const int h = rc.bottom - rc.top;
    if (w <= 2 || h <= 2) return;

    HDC mem = CreateCompatibleDC(target);
    if (!mem) return;
    HBITMAP bmp = CreateCompatibleBitmap(target, w, h);
    if (!bmp) { DeleteDC(mem); return; }
    HBITMAP oldBmp = static_cast<HBITMAP>(SelectObject(mem, bmp));

    SetBkMode(mem, TRANSPARENT);
    HBRUSH fill = CreateSolidBrush(st->hot ? theme::cardHot : theme::card);
    HPEN pen = CreatePen(PS_SOLID, 1, st->hot ? theme::borderHot : theme::border);
    HPEN oldPen = static_cast<HPEN>(SelectObject(mem, pen));
    HBRUSH oldBrush = static_cast<HBRUSH>(SelectObject(mem, fill));
    RoundRect(mem, 0, 0, w - 1, h - 1, S(8), S(8));
    SelectObject(mem, oldPen);
    SelectObject(mem, oldBrush);
    DeleteObject(pen);
    DeleteObject(fill);

    // Farbiger Akzentbalken: WLAN grün, LAN blau, Standard grau
    HBRUSH acc = CreateSolidBrush(theme::Accent(st->zone));
    RECT bar = { S(2), S(10), S(6), h - S(10) };
    FillRect(mem, &bar, acc);
    DeleteObject(acc);

    const int x = S(18);
    int y = S(10);
    RECT line = { x, y, w - S(12), y + S(22) };
    HFONT old = static_cast<HFONT>(SelectObject(mem, app->fontUiBold));
    SetTextColor(mem, st->text.offline ? theme::muted : theme::text);
    DrawTextW(mem, st->text.title.c_str(), -1, &line, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
    SelectObject(mem, old);

    y += S(23);
    line = { x, y, w - S(12), y + S(17) };
    old = static_cast<HFONT>(SelectObject(mem, app->fontSmall));
    SetTextColor(mem, theme::muted);
    DrawTextW(mem, st->text.kind.c_str(), -1, &line, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
    SelectObject(mem, old);

    y += S(21);
    for (size_t i = 0; i < st->text.lines.size() && i < 4; ++i) {
        line = { x, y, w - S(12), y + S(18) };
        SetTextColor(mem, theme::text);
        DrawTextW(mem, st->text.lines[i].c_str(), -1, &line, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
        y += S(19);
    }

    if (st->hot) {
        HPEN hot = CreatePen(PS_SOLID, 2, theme::borderHot);
        HPEN p2 = static_cast<HPEN>(SelectObject(mem, hot));
        HBRUSH none = static_cast<HBRUSH>(GetStockObject(NULL_BRUSH));
        HBRUSH ob = static_cast<HBRUSH>(SelectObject(mem, none));
        RoundRect(mem, 1, 1, w - 2, h - 2, S(8), S(8));
        SelectObject(mem, p2);
        SelectObject(mem, ob);
        DeleteObject(hot);

        RECT hint = { x, h - S(22), w - S(12), h - S(6) };
        old = static_cast<HFONT>(SelectObject(mem, app->fontSmall));
        SetTextColor(mem, theme::borderHot);
        std::wstring txt = Tr(L"hier ablegen \x2013 ") + std::wstring(ZoneName(st->zone));
        DrawTextW(mem, txt.c_str(), -1, &hint, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
        SelectObject(mem, old);
    }

    BitBlt(target, 0, 0, w, h, mem, 0, 0, SRCCOPY);
    SelectObject(mem, oldBmp);
    DeleteObject(bmp);
    DeleteDC(mem);
}

static LRESULT CALLBACK CardProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_NCCREATE: {
        CREATESTRUCTW* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        PaintCard(hwnd, dc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_SIZE:
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_LBUTTONUP: {
        App* app = TheApp();
        CardState* st = reinterpret_cast<CardState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (app && st) {
            std::vector<unsigned long> sel = app->SelectedPids();
            if (sel.empty()) {
                app->SetNote(Tr(L"Zuerst ein Programm in der Liste markieren - oder direkt auf diese Karte ziehen."), 8);
            } else {
                app->ApplyToRows(sel, st->zone);
            }
        }
        return 0;
    }
    case WM_DESTROY:
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ---------------------------------------------------------------- Tabellenfenster (Subclass)

static LRESULT CALLBACK ListProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR ref) {
    App* app = reinterpret_cast<App*>(ref);
    if (msg == WM_NOTIFY && app) {
        LPNMHDR nmh = reinterpret_cast<LPNMHDR>(lp);
        HWND hdr = ListView_GetHeader(hwnd);
        if (hdr && nmh->hwndFrom == hdr && (nmh->code == HDN_ITEMCLICKW || nmh->code == HDN_ITEMCLICK)) {
            app->OnHeaderClick(static_cast<int>(nmh->idFrom));
        }
    } else if (msg == WM_LBUTTONDOWN && app) {
        app->dragPossible = true;
        app->dragStartPt.x = GET_X_LPARAM(lp);
        app->dragStartPt.y = GET_Y_LPARAM(lp);
    } else if (msg == WM_LBUTTONUP && app) {
        app->dragPossible = false;
    } else if (msg == WM_MOUSEMOVE && app && app->dragPossible && (wp & MK_LBUTTON)) {
        int dx = GET_X_LPARAM(lp) - app->dragStartPt.x;
        int dy = GET_Y_LPARAM(lp) - app->dragStartPt.y;
        if (dx < 0) dx = -dx;
        if (dy < 0) dy = -dy;
        if (dx > GetSystemMetrics(SM_CXDRAG) || dy > GetSystemMetrics(SM_CYDRAG)) {
            app->dragPossible = false;
            app->BeginDragFromList();
        }
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

// Suchfeld: Esc leert, Enter lädt neu
static LRESULT CALLBACK SearchProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR ref) {
    App* app = reinterpret_cast<App*>(ref);
    if (msg == WM_CHAR && app) {
        if (wp == VK_ESCAPE) {
            SetWindowTextW(hwnd, L"");
            app->ChangeQuery(L"");
            return 0;
        }
        if (wp == VK_RETURN) {
            app->RefreshAll(true);
            app->SetNote(Tr(L"Neu geladen."), 4);
            return 0;
        }
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

// ---------------------------------------------------------------- Hauptfenster

static LRESULT CALLBACK MainProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    App* app = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        CREATESTRUCTW* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        // DefWindowProc muss den Nichtclientbereich aufsetzen - sonst bleibt u. a. der Fenstertitel leer.
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
    if (!app) return DefWindowProcW(hwnd, msg, wp, lp);
    if (g_taskbarCreated && msg == g_taskbarCreated) {    // Taskleiste neu gestartet: Symbol neu anlegen
        app->HideTray();
        app->ShowTray();
        return 0;
    }

    switch (msg) {
    case WM_CREATE:
        app->hwnd = hwnd;
        // Das Symbol in der Taskleiste meldet Klicks mit einer eigenen WM_APP-Meldung, und
        // die kommt vom Explorer. Lauf RoutePerApp als Administrator (hohe Sicherheitsstufe),
        // der Explorer läuft in niedrigerer Stufe: Windows stellt solche Meldungen sonst
        // zurück, und das Symbol reagiert auf keinen Klick. Deshalb muss genau diese eine
        // Meldung ausdrücklich durchgelassen werden.
        ChangeWindowMessageFilterEx(hwnd, ids::WM_APP_TRAY, MSGFLT_ALLOW, nullptr);
        if (g_taskbarCreated) ChangeWindowMessageFilterEx(hwnd, g_taskbarCreated, MSGFLT_ALLOW, nullptr);
        if (!app->Create(reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd, GWLP_HINSTANCE)))) return -1;
        return 0;

    case WM_GETMINMAXINFO: {
        MINMAXINFO* mmi = reinterpret_cast<MINMAXINFO*>(lp);
        mmi->ptMinTrackSize.x = S(900);
        mmi->ptMinTrackSize.y = S(560);
        return 0;
    }

    case WM_SIZE:
        app->LayoutChildren(LOWORD(lp), HIWORD(lp));
        return 0;

    case WM_DPICHANGED: {
        g_dpi = HIWORD(wp);
        app->dpi = g_dpi;
        RECT* sug = reinterpret_cast<RECT*>(lp);
        SetWindowPos(hwnd, nullptr, sug->left, sug->top,
                     sug->right - sug->left, sug->bottom - sug->top, SWP_NOZORDER | SWP_NOACTIVATE);
        app->RebuildFontsAndImages();
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        const int w = rc.right, h = rc.bottom;
        if (w > 2 && h > 2) {
            HDC mem = CreateCompatibleDC(dc);
            HBITMAP bmp = CreateCompatibleBitmap(dc, w, h);
            if (mem && bmp) {
                HBITMAP oldBmp = static_cast<HBITMAP>(SelectObject(mem, bmp));
                app->PaintBackground(mem, rc);
                app->PaintStatus(mem, rc);
                BitBlt(dc, 0, 0, w, h, mem, 0, 0, SRCCOPY);
                SelectObject(mem, oldBmp);
            }
            if (bmp) DeleteObject(bmp);
            if (mem) DeleteDC(mem);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_TIMER:
        if (wp == ids::TIMER_SELFTEST) {
            KillTimer(hwnd, ids::TIMER_SELFTEST);
            const int bad = app->SelfTest();
            PostQuitMessage(bad);
            return 0;
        }
        if (wp == ids::TIMER_REFRESH) {
            if (!app->dragging) app->RefreshAll(true);
            app->ExpireNote();
        }
        return 0;

    case WM_NOTIFY: {
        LPNMHDR nmh = reinterpret_cast<LPNMHDR>(lp);
        LRESULT res = 0;
        app->OnListNotify(nmh, res);
        return res;
    }

    case WM_HOTKEY:
        app->OnHotkey(static_cast<int>(wp));
        return 0;

    case WM_COMMAND:
        app->OnCommand(LOWORD(wp), HIWORD(wp));
        return 0;

    case WM_CONTEXTMENU: {
        POINT pt;
        pt.x = GET_X_LPARAM(lp);
        pt.y = GET_Y_LPARAM(lp);
        app->ShowContextMenu(pt);
        return 0;
    }

    case ids::WM_APP_TRAY: {
        // Windows schickt die Kennung des Symbols im niedrigen Wort und das auslösende
        // Ereignis im hohen Wort. Sicherheitshalber wird auch die umgekehrte Lesart
        // akzeptiert, falls eine Windows-Version die Worte vertauscht.
        UINT ev = 0;
        if (LOWORD(lp) == ids::TRAY_UID) ev = static_cast<UINT>(HIWORD(lp));
        else if (HIWORD(lp) == ids::TRAY_UID) ev = static_cast<UINT>(LOWORD(lp));
        else {
            const WORD lo = LOWORD(lp), hi = HIWORD(lp);
            if (lo == WM_LBUTTONUP || lo == WM_LBUTTONDBLCLK || lo == NIN_SELECT ||
                lo == WM_RBUTTONUP || lo == WM_CONTEXTMENU || lo == WM_MBUTTONUP) ev = lo;
            else if (hi == NIN_SELECT || hi == NIN_KEYSELECT) ev = hi;
        }
        switch (ev) {
        case WM_LBUTTONUP:                              // einfacher Klick: Fenster vor
        case WM_LBUTTONDBLCLK:
        case NIN_SELECT:
        case NIN_KEYSELECT:
            app->RestoreFromTray();
            break;
        case WM_CONTEXTMENU:                            // Rechtsklick: eigenes Menü
        case WM_RBUTTONUP:
        case WM_MBUTTONUP: {
            POINT pt = {};
            GetCursorPos(&pt);
            app->ShowTrayMenu(pt);
            break;
        }
        default:
            break;
        }
        return 0;
    }

    case WM_CLOSE:
        if (app->quitting) break;             // jetzt wirklich schließen
        app->HideToTray();                    // Fenster zu - die Zuordnungen bleiben in Kraft
        return 0;

    case WM_INITMENU:
        app->RefreshMenu(reinterpret_cast<HMENU>(wp));
        return 0;

    case WM_QUERYENDSESSION:
        return TRUE;

    case WM_ENDSESSION:
        if (wp) app->Quit();
        return 0;

    case WM_DESTROY:
        app->Destroy();
        PostQuitMessage(0);
        return 0;

    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ---------------------------------------------------------------- App: Aufbau

std::vector<HWND> App::CardWindows() const {
    std::vector<HWND> v;
    for (int i = 1; i <= 3; ++i) if (cards[i]) v.push_back(cards[i]);
    return v;
}

bool App::Create(HINSTANCE hinst) {
    if (!hwnd) return false;
    dpi = g_dpi;
    fontTitle = MakeFont(15, FW_SEMIBOLD);
    fontUi = MakeFont(9, FW_REGULAR);
    fontUiBold = MakeFont(9, FW_SEMIBOLD);
    fontSmall = MakeFont(8, FW_REGULAR);

    std::wstring err;
    if (!engine.Init(err)) startupError = err;
    elevated = engine.elevated;
    EnsureHookCopies();

    search = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                             WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                             0, 0, S(240), S(26), hwnd,
                             reinterpret_cast<HMENU>(ids::IDC_SEARCH), hinst, nullptr);
    SendMessageW(search, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(Tr(L"Programm oder Benutzer suchen")));
    SetWindowSubclass(search, SearchProc, 2, reinterpret_cast<DWORD_PTR>(this));

    chkNet = CreateWindowExW(0, L"BUTTON", Tr(L"nur mit Verbindung"),
                             WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                             0, 0, S(170), S(24), hwnd,
                             reinterpret_cast<HMENU>(ids::IDC_CHK_NET), hinst, nullptr);

    btnReload = CreateWindowExW(0, L"BUTTON", Tr(L"Neu laden (F5)"),
                                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                0, 0, S(150), S(28), hwnd,
                                reinterpret_cast<HMENU>(ids::IDC_BTN_RELOAD), hinst, nullptr);

    btnAdmin = CreateWindowExW(0, L"BUTTON", Tr(L"Als Administrator neu starten"),
                               WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                               0, 0, S(240), S(28), hwnd,
                               reinterpret_cast<HMENU>(ids::IDC_BTN_ADMIN), hinst, nullptr);
    ShowWindow(btnAdmin, elevated ? SW_HIDE : SW_SHOW);

    for (HWND h : { search, chkNet, btnReload, btnAdmin }) {
        SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(fontUi), TRUE);
    }

    for (int i = 1; i <= 3; ++i) {
        cardState[i].zone = static_cast<Zone>(i);
        cards[i] = CreateWindowExW(0, kCardClass, L"",
                                   WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
                                   0, 0, 10, 10, hwnd, nullptr, hinst, &cardState[i]);
        cardState[i].hwnd = cards[i];
    }

    list = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                           WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SHOWSELALWAYS,
                           0, 0, 10, 10, hwnd,
                           reinterpret_cast<HMENU>(ids::IDC_LIST), hinst, nullptr);
    SendMessageW(list, WM_SETFONT, reinterpret_cast<WPARAM>(fontUi), TRUE);
    ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_INFOTIP);
    SetWindowSubclass(list, ListProc, 1, reinterpret_cast<DWORD_PTR>(this));

    BuildImageList();

    LVCOLUMNW col = {};
    col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
    col.fmt = LVCFMT_LEFT;
    const wchar_t* names[COL_COUNT] = { Tr(L"Programm"), L"PID", Tr(L"Adapter heute"),
                                        Tr(L"WLAN"), Tr(L"LAN"), Tr(L"Standard"), Tr(L"Benutzer") };
    const int widths[COL_COUNT] = { S(300), S(70), S(160), S(120), S(120), S(120), S(150) };
    for (int c = 0; c < COL_COUNT; ++c) {
        col.iSubItem = c;
        col.cx = widths[c];
        col.pszText = const_cast<wchar_t*>(names[c]);
        SendMessageW(list, LVM_INSERTCOLUMNW, static_cast<WPARAM>(c), reinterpret_cast<LPARAM>(&col));
    }

    RegisterDropTargets(this);
    SetTimer(hwnd, ids::TIMER_REFRESH, 1000, nullptr);

    engine.RefreshAdapters();
    engine.RefreshProcesses();
    engine.RebuildRows();
    RebuildView();
    SyncTable(true);
    UpdateHeaderLabels();
    UpdateCards();
    InvalidateRect(hwnd, nullptr, TRUE);

    LoadSettings();
    if (lang::Language() != language) SwitchLanguage(language);
    RefreshAutostartState();
    RegisterHotkeys();
    ShowTray();

    // "routeperapp.exe --selbsttest" prueft den Ablauf gleich selbst (siehe SelfTest).
    if (selfTest) SetTimer(hwnd, ids::TIMER_SELFTEST, 800, nullptr);

    if (!startupError.empty()) SetNote(startupError, 20);
    else if (!engine.problems.empty()) SetNote(engine.problems[0], 15);
    else SetNote(Tr(L"Programm in der Liste greifen und auf WLAN, LAN oder Standard ziehen."), 12);
    return true;
}

void App::BuildImageList() {
    if (icons) {
        ImageList_Destroy(icons);
        icons = nullptr;
    }
    iconIndex.clear();
    int px = GetSystemMetrics(SM_CXSMICON);
    if (px <= 0) px = 16;
    iconPx = px;
    icons = ImageList_Create(px, px, ILC_COLOR32 | ILC_MASK, 16, 256);
    if (!icons) return;
    HICON generic = static_cast<HICON>(LoadImageW(nullptr, IDI_APPLICATION, IMAGE_ICON, px, px, LR_SHARED));
    if (generic) ImageList_AddIcon(icons, generic);   // geteilter Icon: nicht zerstören
    if (list) ListView_SetImageList(list, icons, LVSIL_SMALL);
}

void App::RebuildFontsAndImages() {
    if (fontTitle) DeleteObject(fontTitle);
    if (fontUi) DeleteObject(fontUi);
    if (fontUiBold) DeleteObject(fontUiBold);
    if (fontSmall) DeleteObject(fontSmall);
    fontTitle = MakeFont(15, FW_SEMIBOLD);
    fontUi = MakeFont(9, FW_REGULAR);
    fontUiBold = MakeFont(9, FW_SEMIBOLD);
    fontSmall = MakeFont(8, FW_REGULAR);

    for (HWND h : { search, chkNet, btnReload, btnAdmin, list }) {
        if (h) SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(fontUi), TRUE);
    }
    for (HWND h : CardWindows()) SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(fontUi), TRUE);

    BuildImageList();
    RECT rc;
    GetClientRect(hwnd, &rc);
    LayoutChildren(rc.right, rc.bottom);
    SyncTable(true);
    InvalidateRect(hwnd, nullptr, TRUE);
}

// Die Hook-Bibliotheken liegen in einem eigenen Unterordner des Datenordners. Vor jeder
// Anbindung vergleicht der Kern sie mit den Exemplaren neben der Programmdatei. Hier läuft
// das schon beim Start, damit eine neue Fassung auch in der Laufzeitumgebung ankommt.
void App::EnsureHookCopies() {
    std::wstring err;
    if (!engine.VerifyHooks(CurrentExeDir(), err) && !err.empty())
        engine.problems.push_back(err);
    engine.Publish();
}

void App::Destroy() {
    if (destroyed) return;
    destroyed = true;
    UnregisterHotkeys();
    HideTray();
    if (hwnd) {
        KillTimer(hwnd, ids::TIMER_REFRESH);
        RemoveWindowSubclass(list, ListProc, 1);
        RemoveWindowSubclass(search, SearchProc, 2);
    }
    RevokeDropTargets(this);
    if (icons) { ImageList_Destroy(icons); icons = nullptr; }
    iconIndex.clear();
    if (fontTitle) DeleteObject(fontTitle);
    if (fontUi) DeleteObject(fontUi);
    if (fontUiBold) DeleteObject(fontUiBold);
    if (fontSmall) DeleteObject(fontSmall);
    fontTitle = fontUi = fontUiBold = fontSmall = nullptr;
    if (appIcon) { DestroyIcon(appIcon); appIcon = nullptr; }
    // Handles ungültig machen: das Fenster wird beim Beenden genau einmal zerstört.
    list = search = chkNet = btnReload = btnAdmin = nullptr;
    for (int i = 0; i < 4; ++i) cards[i] = nullptr;
    hwnd = nullptr;
}

void App::LayoutChildren(int cx, int cy) {
    if (!hwnd) return;
    const int right = cx - S(20);
    const int top = S(14);

    int x = right;
    if (!elevated) {
        x -= S(240);
        SetWindowPos(btnAdmin, nullptr, x, top, S(240), S(28), SWP_NOZORDER);
        x -= S(10);
    }
    x -= S(150);
    SetWindowPos(btnReload, nullptr, x, top, S(150), S(28), SWP_NOZORDER);
    x -= S(170) + S(8);
    SetWindowPos(chkNet, nullptr, x, top + S(2), S(170), S(24), SWP_NOZORDER);
    x -= S(240) + S(10);
    SetWindowPos(search, nullptr, x, top, S(240), S(26), SWP_NOZORDER);

    const int gap = S(12);
    const int bandW = cx - S(40);
    const int standardW = S(250);
    int twoW = (bandW - standardW - gap * 2) / 2;
    if (twoW < S(190)) twoW = S(190);
    const int cyy = CardTop();
    const int ch = CardHeight();
    int cxx = S(20);
    SetWindowPos(cards[1], nullptr, cxx, cyy, twoW, ch, SWP_NOZORDER);
    cxx += twoW + gap;
    SetWindowPos(cards[2], nullptr, cxx, cyy, twoW, ch, SWP_NOZORDER);
    cxx += twoW + gap;
    int lastW = bandW - twoW * 2 - gap * 2;
    if (lastW < S(150)) lastW = S(150);
    SetWindowPos(cards[3], nullptr, cxx, cyy, lastW, ch, SWP_NOZORDER);

    const int ly = ListTop();
    int lh = cy - ly - StatusHeight() - S(6);
    if (lh < S(120)) lh = S(120);
    SetWindowPos(list, nullptr, S(20), ly, bandW, lh, SWP_NOZORDER);
}

// ---------------------------------------------------------------- Hintergrund und Status

static int BandTop() { return S(64); }
static int CardTop() { return BandTop() + S(8); }
static int CardHeight() { return S(118); }
static int ListTop() { return CardTop() + CardHeight() + S(14); }
static int StatusHeight() { return S(34); }

void App::PaintBackground(HDC dc, const RECT& client) const {
    HBRUSH bg = CreateSolidBrush(theme::bg);
    FillRect(dc, &client, bg);
    DeleteObject(bg);

    SetBkMode(dc, TRANSPARENT);

    RECT title = { S(22), S(10), client.right - S(20), S(38) };
    HFONT old = static_cast<HFONT>(SelectObject(dc, fontTitle));
    SetTextColor(dc, theme::text);
    DrawTextW(dc, L"RoutePerApp", -1, &title, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    SelectObject(dc, old);

    RECT sub = { S(23), S(38), client.right - S(20), BandTop() };
    old = static_cast<HFONT>(SelectObject(dc, fontSmall));
    SetTextColor(dc, theme::muted);
    std::wstring hint = Tr(L"Programm aus der Liste auf WLAN, LAN oder Standard ziehen - neue Verbindungen laufen ab sofort dar\u00FCber");
    DrawTextW(dc, hint.c_str(), -1, &sub, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
    SelectObject(dc, old);
}

static RECT StatusRect(const RECT& client) {
    return RECT{ client.left, client.bottom - StatusHeight(), client.right, client.bottom };
}

void App::PaintStatus(HDC dc, const RECT& client) const {
    const RECT band = StatusRect(client);
    HBRUSH bg = CreateSolidBrush(theme::statusBg);
    FillRect(dc, &band, bg);
    DeleteObject(bg);

    HPEN pen = CreatePen(PS_SOLID, 1, theme::border);
    HPEN oldPen = static_cast<HPEN>(SelectObject(dc, pen));
    MoveToEx(dc, band.left, band.top, nullptr);
    LineTo(dc, band.right, band.top);
    SelectObject(dc, oldPen);
    DeleteObject(pen);

    SetBkMode(dc, TRANSPARENT);
    const std::wstring text = StatusText();

    // Rechts: Tastenkürzel, damit die Liste nicht verrutscht
    HFONT old = static_cast<HFONT>(SelectObject(dc, fontSmall));
    SetTextColor(dc, theme::muted);
    const std::wstring keys = Tr(L"Strg+Z  r\u00FCckg\u00E4ngig   \u2022   F5  neu laden   \u2022   Doppelklick  Details");
    SIZE ks = {};
    GetTextExtentPoint32W(dc, keys.c_str(), static_cast<int>(keys.size()), &ks);
    RECT right = { band.right - ks.cx - S(18), band.top + S(4), band.right - S(14), band.bottom - S(4) };
    DrawTextW(dc, keys.c_str(), -1, &right, DT_RIGHT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);

    RECT left = { band.left + S(18), band.top + S(4), right.left - S(16), band.bottom - S(4) };
    SelectObject(dc, old);
    old = static_cast<HFONT>(SelectObject(dc, fontUi));
    SetTextColor(dc, theme::text);
    DrawTextW(dc, text.c_str(), -1, &left, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
    SelectObject(dc, old);
}

std::wstring App::StatusText() const {
    std::wstring noteNow = note;
    if (!noteNow.empty() && noteUntil != 0 && GetTickCount64() > noteUntil) noteNow.clear();
    if (dragging) {
        noteNow = DragHint(dragPids.size(), hoverZone);
    } else if (noteNow.empty() && !engine.problems.empty()) {
        noteNow = engine.problems[0];
    }
    size_t net = 0;
    for (const ProcRow& r : view) if (r.hasNetwork) ++net;
    return StatusLine(noteNow, view.size(), net, elevated, engine.lastError);
}

void App::SetNote(const std::wstring& text, int seconds) {
    note = text;
    noteUntil = (seconds > 0) ? (GetTickCount64() + static_cast<unsigned long long>(seconds) * 1000ull) : 0;
    if (hwnd) {
        RECT rc;
        GetClientRect(hwnd, &rc);
        const RECT band = StatusRect(rc);
        InvalidateRect(hwnd, &band, FALSE);
    }
}

void App::ExpireNote() {
    if (note.empty() || noteUntil == 0) return;
    if (GetTickCount64() <= noteUntil) return;
    note.clear();
    noteUntil = 0;
    if (hwnd) {
        RECT rc;
        GetClientRect(hwnd, &rc);
        const RECT band = StatusRect(rc);
        InvalidateRect(hwnd, &band, FALSE);
    }
}

// ---------------------------------------------------------------- Inhalt aktualisieren

void App::RefreshAll(bool withAdapters) {
    if (!hwnd) return;
    std::wstring err;
    engine.ReloadIfChanged(err);
    if (!err.empty()) engine.lastError = err;
    if (withAdapters) engine.RefreshAdapters();
    engine.RefreshProcesses();
    engine.RebuildRows();
    engine.elevated = elevated;
    RebuildView();
    SyncTable(false);
    UpdateHeaderLabels();
    UpdateCards();
    RECT rc;
    GetClientRect(hwnd, &rc);
    const RECT band = StatusRect(rc);
    InvalidateRect(hwnd, &band, FALSE);
}

void App::RebuildView() {
    std::vector<ProcRow> sorted = engine.rows;
    SortRows(sorted, sortField, sortAsc);
    std::vector<unsigned long> pids;
    FilterRows(sorted, query, onlyNetwork, pids);
    view.clear();
    view.reserve(pids.size());
    for (unsigned long pid : pids) {
        for (const ProcRow& r : sorted) {
            if (r.pid == pid) { view.push_back(r); break; }
        }
    }
    if (view.empty() && !query.empty()) {
        SetNote(Tr(L"Kein Programm passt zu \xBB") + query + Tr(L"\u00BB  -  Esc im Suchfeld l\u00F6scht die Suche."), 6);
    }
}

const ProcRow* App::RowForPid(unsigned long pid) const {
    for (const ProcRow& r : view) if (r.pid == pid) return &r;
    return nullptr;
}

int App::IconForPath(const std::wstring& path) {
    if (!icons || path.empty()) return 0;
    const std::wstring key = ToLowerW(path);
    auto it = iconIndex.find(key);
    if (it != iconIndex.end()) return it->second;
    HICON big = nullptr, smallIcon = nullptr;
    int idx = 0;
    const HRESULT hr = SHDefExtractIconW(path.c_str(), 0, 0, &big, &smallIcon, static_cast<UINT>(iconPx));
    HICON use = smallIcon ? smallIcon : big;
    if (SUCCEEDED(hr) && use) {
        const int added = ImageList_AddIcon(icons, use);
        if (added >= 0) idx = added;
    }
    if (big) DestroyIcon(big);
    if (smallIcon) DestroyIcon(smallIcon);
    iconIndex[key] = idx;
    return idx;
}

static std::wstring CellText(const ProcRow& r, int col) {
    switch (col) {
    case COL_NAME:    return NameCell(r);
    case COL_PID:     return std::to_wstring(r.pid);
    case COL_VIA:     return ViaCell(r);
    case COL_WLAN:    return ZoneCell(r, Zone::Wlan);
    case COL_LAN:     return ZoneCell(r, Zone::Lan);
    case COL_DEFAULT: return ZoneCell(r, Zone::Standard);
    case COL_USER:    return r.user;
    default:          return std::wstring();
    }
}

void App::SyncTable(bool full) {
    if (!list) return;
    std::vector<unsigned long> pids;
    pids.reserve(view.size());
    for (const ProcRow& r : view) pids.push_back(r.pid);

    bool sameOrder = !full && (pids == shownPids) && (cellCache.size() == view.size());

    if (!sameOrder) {
        const std::vector<unsigned long> keep = SelectedPids();
        SendMessageW(list, WM_SETREDRAW, FALSE, 0);
        ListView_DeleteAllItems(list);
        for (size_t i = 0; i < view.size(); ++i) {
            const ProcRow& r = view[i];
            std::wstring name = NameCell(r);
            LVITEMW it = {};
            it.mask = LVIF_TEXT | LVIF_IMAGE | LVIF_PARAM;
            it.iItem = static_cast<int>(i);
            it.iSubItem = 0;
            it.iImage = IconForPath(r.path);
            it.lParam = static_cast<LPARAM>(r.pid);
            it.pszText = reinterpret_cast<LPWSTR>(name.data());
            SendMessageW(list, LVM_INSERTITEMW, 0, reinterpret_cast<LPARAM>(&it));
            for (int c = 1; c < COL_COUNT; ++c) {
                std::wstring t = CellText(r, c);
                LVITEMW sub = {};
                sub.iSubItem = c;
                sub.pszText = reinterpret_cast<LPWSTR>(t.data());
                SendMessageW(list, LVM_SETITEMTEXTW, static_cast<WPARAM>(i), reinterpret_cast<LPARAM>(&sub));
            }
        }
        shownPids = pids;
        cellCache.assign(view.size(), std::vector<std::wstring>());
        for (size_t i = 0; i < view.size(); ++i) {
            cellCache[i].resize(COL_COUNT);
            for (int c = 0; c < COL_COUNT; ++c) cellCache[i][c] = CellText(view[i], c);
        }
        SendMessageW(list, WM_SETREDRAW, TRUE, 0);
        RedrawWindow(list, nullptr, nullptr, RDW_ERASE | RDW_FRAME | RDW_INVALIDATE | RDW_ALLCHILDREN);
        for (size_t i = 0; i < view.size(); ++i) {
            const bool sel = std::find(keep.begin(), keep.end(), view[i].pid) != keep.end();
            ListView_SetItemState(list, static_cast<int>(i), sel ? LVIS_SELECTED : 0, LVIS_SELECTED);
        }
        return;
    }

    for (size_t i = 0; i < view.size(); ++i) {
        for (int c = 0; c < COL_COUNT; ++c) {
            std::wstring t = CellText(view[i], c);
            if (t == cellCache[i][c]) continue;
            LVITEMW sub = {};
            sub.iSubItem = c;
            sub.pszText = reinterpret_cast<LPWSTR>(t.data());
            SendMessageW(list, LVM_SETITEMTEXTW, static_cast<WPARAM>(i), reinterpret_cast<LPARAM>(&sub));
            cellCache[i][c] = t;
        }
    }
}

void App::UpdateHeaderLabels() {
    if (!list) return;
    const Zone zones[3] = { Zone::Wlan, Zone::Lan, Zone::Standard };
    const AdapterKind kinds[3] = { AdapterKind::Wlan, AdapterKind::Lan, AdapterKind::Other };
    for (int i = 0; i < 3; ++i) {
        const int col = ColumnForZone(zones[i]);
        if (col < 0) continue;
        const AdapterInfo* ad = (zones[i] == Zone::Standard) ? nullptr : engine.RoleAdapter(kinds[i]);
        const std::wstring text = ColumnHeader(zones[i], ad);
        if (headerCache[i] == text) continue;
        headerCache[i] = text;
        std::wstring mutableText = text;
        LVCOLUMNW lc = {};
        lc.mask = LVCF_TEXT;
        lc.pszText = reinterpret_cast<LPWSTR>(mutableText.data());
        SendMessageW(list, LVM_SETCOLUMNW, static_cast<WPARAM>(col), reinterpret_cast<LPARAM>(&lc));
    }
}

void App::UpdateCards() {
    const Zone zones[3] = { Zone::Wlan, Zone::Lan, Zone::Standard };
    const AdapterKind kinds[3] = { AdapterKind::Wlan, AdapterKind::Lan, AdapterKind::Other };
    size_t explicitDefault = 0;
    for (const ProcRow& r : engine.rows) {
        if (r.ruleId != 0 && r.mode == RouteMode::None) ++explicitDefault;
    }
    for (int i = 1; i <= 3; ++i) {
        const AdapterInfo* ad = (zones[i - 1] == Zone::Standard) ? nullptr : engine.RoleAdapter(kinds[i - 1]);
        CardText t = (zones[i - 1] == Zone::Standard)
            ? BuildStandardCard(explicitDefault, engine.rows.size() - explicitDefault)
            : BuildCard(ad, zones[i - 1]);
        if (dragging && hoverZone == zones[i - 1]) {
            t.lines.push_back(Tr(L"Ziel f\u00FCr den aktuellen Zug"));
        }
        cardState[i].text = t;
        if (cardState[i].hwnd) InvalidateRect(cardState[i].hwnd, nullptr, FALSE);
    }
}

// ---------------------------------------------------------------- Benutzereingaben

void App::OnListNotify(LPNMHDR nmh, LRESULT& res) {
    res = 0;
    if (!list || nmh->hwndFrom != list) return;

    switch (nmh->code) {
    case NM_CUSTOMDRAW: {
        LPNMLVCUSTOMDRAW cd = reinterpret_cast<LPNMLVCUSTOMDRAW>(nmh);
        switch (cd->nmcd.dwDrawStage) {
        case CDDS_PREPAINT:
            res = CDRF_NOTIFYITEMDRAW;
            break;
        case CDDS_ITEMPREPAINT:
            res = CDRF_NOTIFYSUBITEMDRAW;
            break;
        case CDDS_ITEMPREPAINT | CDDS_SUBITEM: {
            const size_t idx = static_cast<size_t>(cd->nmcd.dwItemSpec);
            if (idx >= view.size()) { res = CDRF_DODEFAULT; break; }
            const ProcRow& r = view[idx];
            const int col = cd->iSubItem;
            const UINT state = static_cast<UINT>(SendMessageW(list, LVM_GETITEMSTATE, cd->nmcd.dwItemSpec, LVIS_SELECTED));
            const bool selected = (state & LVIS_SELECTED) != 0;
            const bool dropHere = (col == hoverColumn && dragging);

            if (selected) cd->clrTextBk = GetSysColor(COLOR_HIGHLIGHT);
            else if (dropHere) cd->clrTextBk = theme::rowHot;
            else cd->clrTextBk = (idx & 1) ? theme::rowAlt : theme::card;

            COLORREF fg = theme::text;
            const Zone zc = ZoneForColumn(col);
            if (zc != Zone::None) {
                if (!ZoneCell(r, zc).empty()) fg = theme::Accent(zc);
                else fg = theme::muted;
            } else if (col == COL_VIA) {
                fg = r.viaAdapter.empty() ? theme::muted : theme::text;
            }
            if (StateOfRow(r) == RowState::Blocked && col == COL_NAME) fg = theme::muted;
            if (selected) fg = GetSysColor(COLOR_HIGHLIGHTTEXT);
            cd->clrText = fg;
            res = CDRF_NEWFONT | CDRF_DODEFAULT;
            break;
        }
        default:
            res = CDRF_DODEFAULT;
            break;
        }
        break;
    }

    // Doppelklick: LVN_ITEMACTIVATE und NM_DBLCLK kommen beide - deshalb nur eines auswerten,
    // sonst öffnet sich der Detaildialog zweifach. (Enter läuft über die Accelerator-Tabelle.)
    case NM_DBLCLK: {
        LPNMLISTVIEW lv = reinterpret_cast<LPNMLISTVIEW>(nmh);
        if (lv->iItem >= 0 && static_cast<size_t>(lv->iItem) < view.size()) {
            ShowDetailsFor(view[static_cast<size_t>(lv->iItem)].pid);
        }
        break;
    }

    case LVN_GETINFOTIP: {
        NMLVGETINFOTIPW* tip = reinterpret_cast<NMLVGETINFOTIPW*>(nmh);
        const size_t idx = static_cast<size_t>(tip->iItem);
        if (idx >= view.size()) break;
        const ProcRow& r = view[idx];
        std::wstring text = RowStateText(r);
        if (!r.path.empty()) text += L"\n" + r.path;
        else if (!r.pathAccess) text += Tr(L"\nPfad nicht lesbar - Zuordnung nur \u00FCber den Bildnamen m\u00F6glich");
        if (r.is32Bit) text += Tr(L"\n32-Bit-Programm");
        if (r.mode != RouteMode::None) text += Tr(L"\nZiel: ") + ModeLabel(r.mode);
        lstrcpynW(tip->pszText, text.c_str(), tip->cchTextMax);
        break;
    }

    case NM_RCLICK: {
        POINT pt;
        GetCursorPos(&pt);
        ShowContextMenu(pt);
        break;
    }

    default:
        break;
    }
}

void App::OnCommand(int id, int code) {
    switch (id) {
    case ids::IDC_SEARCH:
        if (code == 1) { SetFocus(search); break; }      // Strg+F
        if (code == EN_CHANGE) {
            const int len = GetWindowTextLengthW(search);
            std::vector<wchar_t> buf(static_cast<size_t>(len) + 1, 0);
            GetWindowTextW(search, buf.data(), static_cast<int>(buf.size()));
            ChangeQuery(buf.data());
        }
        break;
    case ids::IDC_CHK_NET:
        onlyNetwork = (Button_GetCheck(chkNet) == BST_CHECKED);
        RebuildView();
        SyncTable(true);
        break;
    case ids::IDC_BTN_RELOAD:
    case ids::IDM_RELOAD:
        RefreshAll(true);
        SetNote(Tr(L"Neu geladen."), 4);
        break;
    case ids::IDC_BTN_ADMIN:
    case ids::IDM_RESTART_ADMIN:
        RestartElevated();
        break;
    case ids::IDM_LANG_DE:
        SwitchLanguage(L"de");
        break;
    case ids::IDM_LANG_EN:
        SwitchLanguage(L"en");
        break;
    case ids::IDM_UNDO:
        UndoLast();
        break;
    case ids::IDM_ZONE_WLAN:
        ApplyToRows(SelectedPids(), Zone::Wlan);
        break;
    case ids::IDM_ZONE_LAN:
        ApplyToRows(SelectedPids(), Zone::Lan);
        break;
    case ids::IDM_ZONE_STANDARD:
        ApplyToRows(SelectedPids(), Zone::Standard);
        break;
    case ids::IDM_CLEAR:
        ClearAssignments(SelectedPids());
        break;
    case ids::IDM_DETAILS: {
        const std::vector<unsigned long> sel = SelectedPids();
        if (!sel.empty()) ShowDetailsFor(sel[0]);
        break;
    }
    case ids::IDM_COPY_PATH: {
        const std::vector<unsigned long> sel = SelectedPids();
        const ProcRow* r = sel.empty() ? nullptr : RowForPid(sel[0]);
        if (r && !r->path.empty()) {
            CopyToClipboard(hwnd, r->path);
            SetNote(Tr(L"Pfad kopiert: ") + r->path, 5);
        } else {
            SetNote(Tr(L"Kein Programmpfad bekannt - Pfad kopieren geht nur bei lesbaren Programmen."), 6);
        }
        break;
    }
    case ids::IDM_FOCUS_SEARCH:
        SetFocus(search);
        break;
    case ids::IDM_AUTOSTART:
        ToggleAutostart();
        break;
    case ids::IDM_DEF_STANDARD:
        SetDefaultRoute(Zone::Standard);
        break;
    case ids::IDM_DEF_WLAN:
        SetDefaultRoute(Zone::Wlan);
        break;
    case ids::IDM_DEF_LAN:
        SetDefaultRoute(Zone::Lan);
        break;
    case ids::IDM_HOTKEYS:
        ToggleHotkeys();
        break;
    case ids::IDM_HIDE:
        HideToTray();
        break;
    case ids::IDM_TRAY_OPEN:
        RestoreFromTray();
        break;
    case ids::IDM_REINJECT: {
        int already = 0, failed = 0;
        std::wstring ierr;
        const int applied = engine.ReinjectMatching(ierr, already, failed);
        std::wstring msg = Tr(L"Neu gebunden: ") + std::to_wstring(applied) + Tr(L", bereits gebunden: ") + std::to_wstring(already);
        if (failed) msg += Tr(L", fehlgeschlagen: ") + std::to_wstring(failed);
        if (!ierr.empty()) msg += Tr(L"  \x2013  ") + ierr;
        SetNote(msg, 10);
        RefreshAll(true);
        break;
    }
    case ids::IDM_KEYS:
        ShowShortcutHelp();
        break;
    case ids::IDM_ABOUT:
        ShowAbout();
        break;
    case ids::IDM_EXIT:
        Quit();
        break;
    default:
        break;
    }
}

void App::OnHeaderClick(int column) {
    SortField field = sortField;
    switch (column) {
    case COL_NAME:    field = SortField::Name; break;
    case COL_PID:     field = SortField::Pid; break;
    case COL_VIA:     field = SortField::Via; break;
    case COL_USER:    field = SortField::User; break;
    case COL_WLAN:
    case COL_LAN:
    case COL_DEFAULT: field = SortField::Mode; break;
    default: return;
    }
    if (field == sortField) sortAsc = !sortAsc;
    else { sortField = field; sortAsc = true; }
    RebuildView();
    SyncTable(true);
}

void App::ChangeQuery(const std::wstring& q) {
    query = q;
    RebuildView();
    SyncTable(true);
}

// ---------------------------------------------------------------- Auswahl und Spalten

std::vector<unsigned long> App::SelectedPids() const {
    std::vector<unsigned long> out;
    if (!list) return out;
    int idx = -1;
    for (;;) {
        idx = static_cast<int>(SendMessageW(list, LVM_GETNEXTITEM,
                                           static_cast<WPARAM>(static_cast<int>(idx)),
                                           static_cast<LPARAM>(LVNI_SELECTED)));
        if (idx < 0) break;
        if (static_cast<size_t>(idx) < view.size()) out.push_back(view[static_cast<size_t>(idx)].pid);
    }
    return out;
}

int App::ColumnAtPoint(POINT clientPt) const {
    if (!list) return -1;
    LVHITTESTINFO hi = {};
    hi.pt = clientPt;
    const int item = static_cast<int>(SendMessageW(list, LVM_SUBITEMHITTEST, 0, reinterpret_cast<LPARAM>(&hi)));
    if (item >= 0 && hi.iSubItem >= 0) return hi.iSubItem;

    HWND hdr = ListView_GetHeader(list);
    if (!hdr) return -1;
    const int count = Header_GetItemCount(hdr);
    int acc = 0;
    for (int c = 0; c < count && c < COL_COUNT; ++c) {
        HDITEMW hc = {};
        hc.mask = HDI_WIDTH;
        if (!Header_GetItem(hdr, c, &hc)) continue;
        acc += hc.cxy;
        if (clientPt.x < acc) return c;
    }
    return -1;
}

// ---------------------------------------------------------------- Drag & Drop-Zustand

void App::BeginDragFromList() {
    if (!list) return;
    const std::vector<unsigned long> pids = SelectedPids();
    if (pids.empty()) return;
    dragPids = pids;
    dragging = true;
    UpdateCards();

    SrDataObject* data = new SrDataObject(pids);
    SrDropSource* src = new SrDropSource();
    DWORD effect = DROPEFFECT_NONE;
    const HRESULT hr = DoDragDrop(data, src, DROPEFFECT_MOVE | DROPEFFECT_COPY, &effect);
    src->Release();
    data->Release();

    FinishDragFeedback();
    if (FAILED(hr) || effect == DROPEFFECT_NONE) {
        SetNote(Tr(L"Zug abgebrochen - nichts ge\u00E4ndert."), 5);
    }
}

void App::SetHover(Zone zone, int column) {
    if (zone == hoverZone && column == hoverColumn) return;
    hoverZone = zone;
    hoverColumn = column;
    for (int i = 1; i <= 3; ++i) {
        const bool hot = (cardState[i].zone == zone && zone != Zone::None);
        if (hot != cardState[i].hot) {
            cardState[i].hot = hot;
            if (cardState[i].hwnd) InvalidateRect(cardState[i].hwnd, nullptr, TRUE);
        }
    }
    if (list) InvalidateRect(list, nullptr, FALSE);
    UpdateCards();
    if (hwnd) {
        RECT rc;
        GetClientRect(hwnd, &rc);
        const RECT band = StatusRect(rc);
        InvalidateRect(hwnd, &band, FALSE);
    }
}

void App::FinishDragFeedback() {
    dragging = false;
    dragPids.clear();
    for (int i = 1; i <= 3; ++i) {
        cardState[i].hot = false;
        if (cardState[i].hwnd) InvalidateRect(cardState[i].hwnd, nullptr, TRUE);
    }
    hoverZone = Zone::None;
    hoverColumn = -1;
    if (list) InvalidateRect(list, nullptr, FALSE);
}

// ---------------------------------------------------------------- Zuordnungen anwenden

static std::wstring ZoneVerb(Zone zone) {
    switch (zone) {
    case Zone::Wlan:     return Tr(L"\u00FCber WLAN");
    case Zone::Lan:      return Tr(L"\u00FCber LAN");
    case Zone::Standard: return Tr(L"Windows-Standard");
    default:             return L"";
    }
}

void App::ApplyToRows(const std::vector<unsigned long>& pids, Zone zone) {
    if (zone == Zone::None) return;
    const RouteMode mode = ZoneMode(zone);
    std::wstring err, firstError;
    std::vector<UndoStep> steps;
    int changed = 0;

    for (unsigned long pid : pids) {
        const ProcRow* r = RowForPid(pid);
        if (!r) continue;
        UndoStep step;
        step.label = r->image + Tr(L" \x2192 ") + ZoneVerb(zone);
        if (r->ruleId != 0) {
            const Rule* cur = engine.rules.Find(r->ruleId);
            if (cur) { step.previous = *cur; step.ruleId = cur->id; step.wasNew = false; }
        }
        if (!engine.Assign(r->image, r->path, mode, L"", err)) {
            if (firstError.empty()) firstError = err;
            continue;
        }
        if (step.ruleId == 0) {
            int idx = FindEquivalentRule(engine.rules, r->image, mode, L"");
            if (idx < 0) idx = FindEquivalentRule(engine.rules, PatternFromPath(r->path), mode, L"");
            if (idx >= 0) {
                step.previous = engine.rules.rules[idx];
                step.ruleId = engine.rules.rules[idx].id;
                step.wasNew = true;
            }
        }
        steps.push_back(step);
        ++changed;
    }

    for (const UndoStep& s : steps) undoStack.Push(s);

    int already = 0, failed = 0;
    std::wstring ierr;
    const int applied = engine.ReinjectMatching(ierr, already, failed);

    std::wstring msg;
    if (changed == 0) {
        msg = firstError.empty() ? Tr(L"Nichts ge\u00E4ndert.") : firstError;
    } else {
        msg = std::to_wstring(changed);
        msg += (changed == 1) ? Tr(L" Programm ") : Tr(L" Programme ");
        msg += ZoneVerb(zone) + Tr(L"  \x2013  ") + InjectionSummary(applied, already, failed, ierr);
        msg += Tr(L"  \u2013  bestehende Verbindungen bleiben bestehen, neue laufen ab sofort dar\u00FCber");
    }
    SetNote(msg, 14);
    RefreshAll(false);
}

void App::ApplyToFileDrop(const std::vector<std::wstring>& files, Zone zone) {
    if (zone == Zone::None) return;
    const RouteMode mode = ZoneMode(zone);
    std::wstring err, why, firstError, firstWhy;
    int ok = 0, rejected = 0;

    for (const std::wstring& f : files) {
        if (!DroppedFileAllowed(f, why)) {
            ++rejected;
            if (firstWhy.empty()) firstWhy = why;
            continue;
        }
        const std::wstring pat = PatternFromPath(f);
        UndoStep step;
        step.label = pat + Tr(L" \x2192 ") + ZoneVerb(zone);
        if (!engine.Assign(pat, f, mode, L"", err)) {
            if (firstError.empty()) firstError = err;
            continue;
        }
        const int idx = FindEquivalentRule(engine.rules, pat, mode, L"");
        if (idx >= 0) {
            step.previous = engine.rules.rules[idx];
            step.ruleId = engine.rules.rules[idx].id;
            step.wasNew = true;
        }
        undoStack.Push(step);
        ++ok;
    }

    int already = 0, failed = 0;
    std::wstring ierr;
    const int applied = engine.ReinjectMatching(ierr, already, failed);

    std::wstring msg;
    if (ok == 0) {
        msg = !firstWhy.empty() ? firstWhy : (firstError.empty() ? Tr(L"Keine Zuordnung m\u00F6glich.") : firstError);
    } else {
        msg = std::to_wstring(ok);
        msg += (ok == 1) ? Tr(L" Programm geregelt ") : Tr(L" Programme geregelt ");
        msg += ZoneVerb(zone) + Tr(L"  \x2013  ") + InjectionSummary(applied, already, failed, ierr);
        if (rejected) msg += Tr(L"  \x2013  ") + std::to_wstring(rejected) + Tr(L" abgelehnt: ") + firstWhy;
        msg += Tr(L"  \u2013  gilt ab dem n\u00E4chsten Start des Programms");
    }
    SetNote(msg, 16);
    RefreshAll(false);
}

void App::ClearAssignments(const std::vector<unsigned long>& pids) {
    std::wstring err, firstError;
    int removed = 0;
    for (unsigned long pid : pids) {
        const ProcRow* r = RowForPid(pid);
        if (!r || r->ruleId == 0) continue;
        const Rule* cur = engine.rules.Find(r->ruleId);
        if (!cur) continue;
        UndoStep step;
        step.ruleId = cur->id;
        step.previous = *cur;
        step.wasNew = false;
        step.label = Tr(L"Zuordnung von ") + r->image + Tr(L" entfernt");
        if (!engine.ClearAssignment(cur->id, err)) {
            if (firstError.empty()) firstError = err;
            continue;
        }
        undoStack.Push(step);
        ++removed;
    }
    int already = 0, failed = 0;
    std::wstring ierr;
    engine.ReinjectMatching(ierr, already, failed);
    if (removed == 0) SetNote(firstError.empty() ? Tr(L"Keine Zuordnung vorhanden.") : firstError, 6);
    else SetNote(std::to_wstring(removed) + Tr(L" Zuordnung(en) entfernt  \x2013  Windows-Standard wieder aktiv"), 10);
    RefreshAll(false);
}

void App::UndoLast() {
    UndoStep st;
    if (!undoStack.Pop(st)) {
        SetNote(Tr(L"Nichts mehr r\u00FCckg\u00E4ngig."), 5);
        return;
    }
    std::wstring err;
    bool ok = false;
    if (st.wasNew) ok = engine.rules.Remove(st.ruleId);
    else if (engine.rules.Find(st.ruleId)) ok = engine.rules.Update(st.ruleId, st.previous);
    else ok = engine.rules.InsertTop(st.previous);

    if (!ok) {
        SetNote(Tr(L"R\u00FCckg\u00E4ngig fehlgeschlagen: Regel nicht mehr gefunden."), 8);
        RefreshAll(false);
        return;
    }
    if (!engine.SaveAndPublish(err)) engine.lastError = err;

    int already = 0, failed = 0;
    std::wstring ierr;
    const int applied = engine.ReinjectMatching(ierr, already, failed);
    SetNote(Tr(L"R\u00FCckg\u00E4ngig: ") + st.label + Tr(L"  \x2013  ") + InjectionSummary(applied, already, failed, ierr), 12);
    RefreshAll(false);
}

void App::RestartElevated() {
    std::wstring err;
    if (!RelaunchElevated(err)) {
        SetNote(Tr(L"Neustart als Administrator fehlgeschlagen: ") + err, 12);
        return;
    }
    Destroy();
    PostQuitMessage(0);
}

// ---------------------------------------------------------------- Taskleiste, Menü, Beenden

bool App::HasSelection() const {
    return list != nullptr && ListView_GetSelectedCount(list) > 0;
}

bool App::ShowTray() {
    if (trayReady) return true;
    if (!hwnd || !appIcon) return false;
    tray.cbSize = sizeof(tray);
    tray.hWnd = hwnd;
    tray.uID = ids::TRAY_UID;
    tray.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    tray.uCallbackMessage = ids::WM_APP_TRAY;
    tray.uVersion = NOTIFYICON_VERSION_4;      // Klick wird korrekt gemeldet, Menü erst beim Loslassen
    tray.hIcon = appIcon;
    lstrcpynW(tray.szTip, Tr(L"RoutePerApp  \x2013  Internetweg pro Programm"), static_cast<int>(sizeof(tray.szTip) / sizeof(wchar_t)));
    trayReady = Shell_NotifyIconW(NIM_ADD, &tray) != FALSE;
    if (trayReady) Shell_NotifyIconW(NIM_SETVERSION, &tray);
    return trayReady;
}

void App::HideTray() {
    if (!trayReady) return;
    tray.uFlags = 0;
    Shell_NotifyIconW(NIM_DELETE, &tray);
    trayReady = false;
}

void App::ShowTrayMenu(POINT ptScreen) {
    HMENU menu = MakeTrayMenu();
    if (!menu) return;
    // Ohne diesen Griff bleibt das Popup stehen, wenn man daneben klickt.
    SetForegroundWindow(hwnd);
    const int cmd = static_cast<int>(TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_TOPALIGN | TPM_LEFTALIGN,
                                                    ptScreen.x, ptScreen.y, 0, hwnd, nullptr));
    PostMessageW(hwnd, WM_NULL, 0, 0);
    DestroyMenu(menu);
    if (cmd > 0) OnCommand(cmd, 0);
}

// Das Taskleisten-Menue wird hier zusammengesetzt, das Aufklappen steht in ShowTrayMenu.
// Getrennt, damit der eingebaute Selbsttest (--selbsttest) die Eintraege nachlesen kann.
HMENU App::MakeTrayMenu() {
    HMENU menu = CreatePopupMenu();
    if (!menu) return nullptr;
    AppendMenuW(menu, MF_STRING, ids::IDM_TRAY_OPEN, Tr(L"RoutePerApp öffnen"));
    AppendMenuW(menu, MF_STRING, ids::IDM_RELOAD, Tr(L"Neu laden"));
    AppendMenuW(menu, MF_STRING, ids::IDM_REINJECT, Tr(L"Laufende Programme neu binden"));
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    HMENU def = CreatePopupMenu();
    AppendMenuW(def, MF_STRING, ids::IDM_DEF_WLAN, L"WLAN");
    AppendMenuW(def, MF_STRING, ids::IDM_DEF_LAN, L"LAN");
    AppendMenuW(def, MF_STRING, ids::IDM_DEF_STANDARD, Tr(L"Windows-Standard"));
    CheckMenuItem(def, ids::IDM_DEF_WLAN, MF_BYCOMMAND |
                (engine.rules.defaultMode == RouteMode::Wlan ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(def, ids::IDM_DEF_LAN, MF_BYCOMMAND |
                (engine.rules.defaultMode == RouteMode::Lan ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(def, ids::IDM_DEF_STANDARD, MF_BYCOMMAND |
                (engine.rules.defaultMode == RouteMode::None ? MF_CHECKED : MF_UNCHECKED));
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(def), Tr(L"Standardweg (ohne eigene Zuordnung)"));
    AppendMenuW(menu, MF_STRING, ids::IDM_AUTOSTART, Tr(L"Bei Windows-Anmeldung starten"));
    CheckMenuItem(menu, ids::IDM_AUTOSTART, MF_BYCOMMAND | (autostartOn ? MF_CHECKED : MF_UNCHECKED));
    const bool de = (lang::Language() == L"de");
    CheckMenuItem(menu, ids::IDM_LANG_DE, MF_BYCOMMAND | (de ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(menu, ids::IDM_LANG_EN, MF_BYCOMMAND | (de ? MF_UNCHECKED : MF_CHECKED));
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ids::IDM_EXIT, Tr(L"Beenden  (Routing abschalten)"));
    return menu;
}

void App::HideToTray() {
    if (!hwnd) return;
    ShowWindow(hwnd, SW_HIDE);
    inTray = true;
    if (!ShowTray()) {
        SetNote(Tr(L"Das Symbol in der Taskleiste konnte nicht angelegt werden - Beenden geht über Datei \x2192 Beenden."), 20);
        return;
    }
    SetNote(Tr(L"Fenster ist geschlossen, aber RoutePerApp läuft weiter und hält die Zuordnungen. Beenden geht über das Symbol in der Taskleiste oder über Datei \u2192 Beenden."), 45);
}

void App::RestoreFromTray() {
    if (!hwnd) return;
    ShowWindow(hwnd, IsIconic(hwnd) ? SW_RESTORE : SW_SHOW);
    inTray = false;
    SetForegroundWindow(hwnd);
}

void App::Quit() {
    if (quitting) return;
    quitting = true;
    HideTray();
    if (hwnd) DestroyWindow(hwnd);
    else PostQuitMessage(0);
}

// Prueft den Ablauf, den man sonst nur von aussen nachspielen koennte: Fenster auf,
// verstecken, Taskleisten-Meldung, Fenster wieder da, Menues und Tastenkuerzel in Ordnung.
// Aufgerufen mit "routeperapp.exe --selbsttest". Der Rueckgabewert ist die Zahl der Punkte,
// die nicht stimmen; die Einzelpunkte stehen in selbsttest.log im Datenordner.
// Absichtlich im Programm und nicht in einem Automaten-Skript: ein Fenster, das einem
// anderen Programm Meldungen schickt, halten Virenwächter für Schadsoftware.
int App::SelfTest() {
    int bad = 0;
    std::wstring out = Tr(L"RoutePerApp ") ;
    out += kAppVersion;
    out += Tr(L" Selbsttest\r\n\r\n");

    auto check = [&](const wchar_t* name, bool ok) {
        if (!ok) ++bad;
        out += ok ? Tr(L"ok      ") : Tr(L"FEHLER  ");
        out += name;
        out += L"\r\n";
    };
    auto menuText = [](HMENU menu, int pos, UINT id) {
        HMENU sub = GetSubMenu(menu, pos);
        if (!sub) return std::wstring();
        wchar_t buf[256] = { 0 };
        GetMenuStringW(sub, id, buf, 255, MF_BYCOMMAND);
        return std::wstring(buf);
    };
    auto has = [](const std::wstring& text, const wchar_t* needle) {
        return text.find(needle) != std::wstring::npos;
    };

    check(Tr(L"Fenster steht und ist sichtbar"), hwnd != nullptr && IsWindow(hwnd) && IsWindowVisible(hwnd));
    HMENU bar = GetMenu(hwnd);
    check(Tr(L"Menueleiste vorhanden"), bar != nullptr);
    if (bar) {
        check(Tr(L"Datei: Autostart-Punkt vorhanden"),
              menuText(bar, 0, ids::IDM_AUTOSTART) == Tr(L"Bei Windows-Anmeldung starten"));
        check(Tr(L"Weg: Standardweg-Punkte vorhanden"),
              !menuText(bar, 2, ids::IDM_DEF_STANDARD).empty() && !menuText(bar, 2, ids::IDM_DEF_WLAN).empty());
        check(Tr(L"Programm: Tastenkuerzel-Punkt vorhanden"),
              menuText(bar, 3, ids::IDM_HOTKEYS) == Tr(L"Globale Tastenkürzel benutzen"));
    }
    check(Tr(L"Systemmenue vorhanden"), GetSystemMenu(hwnd, FALSE) != nullptr);

    if (hotkeysOn) {
        // Noch einmal dasselbe Kuerzel anmelden: nur wenn das scheitert, gehoert es uns.
        const bool second = RegisterHotKey(hwnd, 0xF101, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'W') != FALSE;
        if (second) UnregisterHotKey(hwnd, 0xF101);
        check(Tr(L"Strg+Alt+W ist angemeldet"), !second);
    } else {
        out += Tr(L"ueberspr. globale Tastenkuerzel sind abgeschaltet\r\n");
    }

    HideToTray();
    check(Tr(L"Fenster ist nach dem Schliessen versteckt"), !IsWindowVisible(hwnd));
    check(Tr(L"Symbol in der Taskleiste angelegt"), trayReady);

    HMENU trayMenu = MakeTrayMenu();
    const int items = trayMenu ? GetMenuItemCount(trayMenu) : 0;
    wchar_t last[256] = { 0 };
    if (trayMenu && items > 0) GetMenuStringW(trayMenu, static_cast<UINT>(items - 1), last, 255, MF_BYPOSITION);
    check(Tr(L"Taskleisten-Menue mit Beenden-Punkt"), trayMenu != nullptr && items >= 6 && has(last, Tr(L"Beenden")));
    if (trayMenu) DestroyMenu(trayMenu);

    PostMessageW(hwnd, ids::WM_APP_TRAY, static_cast<WPARAM>(ids::TRAY_UID),
                 static_cast<LPARAM>(MAKELONG(ids::TRAY_UID, WM_LBUTTONUP)));
    for (int i = 0; i < 40 && !IsWindowVisible(hwnd); ++i) {
        MSG m = {};
        if (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        } else Sleep(25);
    }
    check(Tr(L"Klick aufs Symbol holt das Fenster zurueck"), IsWindowVisible(hwnd));

    // Nach einem Neustart der Taskleiste ist das Symbol weg und muss neu eingetragen werden.
    HideTray();
    check(Tr(L"Symbol wird nach Taskleisten-Neustart neu angelegt"), ShowTray());
    HideTray();

    // Sicherheit: angebunden wird nur, wenn die Hook-Bibliotheken unveraendert
    // neben dem Programm liegen. Ohne Vorlage darf nichts in ein Programm gehen.
    {
        std::wstring grund;
        const bool falsch = engine.VerifyHooks(L"C:\\RoutePerApp-gibt-es-nicht", grund);
        check(Tr(L"Hooks: ohne Vorlage wird nichts angebunden"), !falsch && !grund.empty());
        check(Tr(L"Hooks: Vorlagen neben dem Programm sind unveraendert"),
              engine.VerifyHooks(CurrentExeDir(), grund));
        check(Tr(L"Hooks: Ordner ist gegen normale Benutzer geschlossen"),
              !elevated || FolderIsHardened(engine.paths.hookDir));
    }

    // Sprache: das Umschalten muss bis in die Menues wirken, und keine sichtbare
    // Beschriftung darf ohne Uebersetzung zurueckbleiben. Fehlende Texte werden
    // aufgeschrieben, damit die Sprachdatei sie ergaenzen kann.
    // Die beiden Dialoge werden hier nur zusammengesetzt, nicht gezeigt: ihre Texte
    // gehoeren ebenso zur Oberflaeche und tauchen dann in der Vorlage auf.
    ShowShortcutHelp(false);
    ShowAbout(false);

    lang::ForgetMissing();
    SwitchLanguage(L"en");
    HMENU barEn = GetMenu(hwnd);
    check(Tr(L"Sprache Englisch: Menues neu gebaut"), barEn != nullptr);
    if (barEn)
        check(Tr(L"Sprache Englisch: Datei-Menue uebersetzt"),
              menuText(barEn, 0, ids::IDM_RELOAD) == Tr(L"Neu laden\tF5"));
    wchar_t titel[512] = { 0 };
    GetWindowTextW(hwnd, titel, ARRAYSIZE(titel));
    check(Tr(L"Sprache Englisch: Fenstertitel uebersetzt"),
          std::wstring(titel) == Tr(L"RoutePerApp  \x2013  Internetweg pro Programm"));
    wchar_t knopf[256] = { 0 };
    if (btnReload) GetWindowTextW(btnReload, knopf, ARRAYSIZE(knopf));
    check(Tr(L"Sprache Englisch: Knopfueberschrift uebersetzt"),
          std::wstring(knopf) == Tr(L"Neu laden (F5)"));
    wchar_t spalte[256] = { 0 };
    LVCOLUMNW lc = {};
    lc.mask = LVCF_TEXT;
    lc.iSubItem = 2;
    lc.pszText = spalte;
    lc.cchTextMax = ARRAYSIZE(spalte);
    if (list) SendMessageW(list, LVM_GETCOLUMNW, 2, reinterpret_cast<LPARAM>(&lc));
    check(Tr(L"Sprache Englisch: Spaltenkopf uebersetzt"),
          std::wstring(spalte) == Tr(L"Adapter heute"));
    // Die Meldung des Hook-Schutzes erscheint sonst nur im Fehlerfall. Sie wird
    // hier absichtlich erzeugt, damit die Sprachdatei sie ebenfalls abdecken muss.
    {
        std::wstring grund;
        engine.VerifyHooks(L"C:\\RoutePerApp-gibt-es-nicht", grund);
        engine.VerifyHooks(CurrentExeDir(), grund);   // danach wieder der echte Zustand
    }

    const std::vector<std::wstring> offen = lang::Untranslated();
    if (!offen.empty()) {
        std::wstring perr;
        bool geschrieben = lang::WriteTemplate(engine.paths.dir + L"\\language.vorlage.json", &perr);
        if (!geschrieben) {
            wchar_t mod[MAX_PATH * 4] = { 0 };
            if (GetModuleFileNameW(nullptr, mod, ARRAYSIZE(mod))) {
                std::wstring p = mod;
                const size_t s = p.find_last_of(L"\\/");
                if (s != std::wstring::npos)
                    geschrieben = lang::WriteTemplate(p.substr(0, s + 1) + L"language.vorlage.json", &perr);
            }
        }
        if (geschrieben) out += Tr(L"Vorlage fuer fehlende Uebersetzungen geschrieben\r\n");
        for (const std::wstring& key : offen)
            out += Tr(L"fehlt: ") + key + L"\r\n";
    }
    check(Tr(L"Sprache Englisch: keine Beschriftung ohne Uebersetzung"), offen.empty());
    SwitchLanguage(L"de");
    HMENU barDe = GetMenu(hwnd);
    check(Tr(L"Sprache Deutsch: wiederhergestellt"),
          barDe != nullptr && menuText(barDe, 0, ids::IDM_RELOAD) == Tr(L"Neu laden\tF5"));

    out += Tr(L"\r\nPunkte nicht in Ordnung: ") + std::to_wstring(bad) + L"\r\n";
    std::wstring err;
    if (!WriteTextFileAtomic(engine.paths.dir + L"\\selbsttest.log", out, err)) {
        // Ohne Administratorrechte ist der Datenordner manchmal gesperrt - dann neben der Datei.
        wchar_t mod[MAX_PATH * 4] = { 0 };
        if (GetModuleFileNameW(nullptr, mod, ARRAYSIZE(mod))) {
            std::wstring p = mod;
            const size_t s = p.find_last_of(L"\\/");
            if (s != std::wstring::npos) p = p.substr(0, s + 1) + L"selbsttest.log";
            WriteTextFileAtomic(p, out, err);
        }
    }
    OutputDebugStringW(out.c_str());
    return bad;
}

// ---------------------------------------------------------------- Autostart, Standardweg, Tastenkürzel

static const wchar_t* kSettingsName = L"RoutePerApp";      // Name von Aufgabe und Registry-Eintrag
static const wchar_t* kRunKeyPath = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

// Ein Hilfsprogramm verborgen laufen lassen und seinen Rückgabewert holen.
static bool RunHiddenAndWait(const std::wstring& exe, const std::wstring& args,
                             unsigned long& exitCode, std::wstring& err) {
    std::wstring cmd = L"\"" + exe + L"\" " + args;
    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi = {};
    if (!CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                        nullptr, nullptr, &si, &pi)) {
        err = LastErrText(GetLastError());
        return false;
    }
    WaitForSingleObject(pi.hProcess, 30000);
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

static std::wstring SchtasksPath() {
    wchar_t sys[MAX_PATH] = {};
    if (GetSystemDirectoryW(sys, ARRAYSIZE(sys)) == 0) return L"schtasks.exe";
    return std::wstring(sys) + L"\\schtasks.exe";
}

static std::wstring ThisExePath() {
    wchar_t buf[MAX_PATH * 4] = {};
    const DWORD n = GetModuleFileNameW(nullptr, buf, ARRAYSIZE(buf));
    return (n == 0 || n >= ARRAYSIZE(buf)) ? std::wstring() : std::wstring(buf, n);
}

static bool LogonTaskExists(bool& exists) {
    unsigned long code = 1;
    std::wstring err;
    if (!RunHiddenAndWait(SchtasksPath(), std::wstring(L"/Query /TN ") + kSettingsName, code, err)) return false;
    exists = (code == 0);
    return true;
}

// Eine geplante Aufgabe ist der bessere Weg: sie startet das Programm bei der Anmeldung
// ohne das sonst übliche Nachfragen nach Administratorrechten.
static bool CreateLogonTask(std::wstring& err) {
    const std::wstring exe = ThisExePath();
    if (exe.empty()) { err = Tr(L"Pfad zum Programm nicht bekannt"); return false; }
    // Der Programmpfad bekommt in der Aufgabe zusätzliche Anführungszeichen,
    // weil er Leerzeichen enthalten kann.
    const std::wstring args = std::wstring(L"/Create /F /TN ") + kSettingsName +
        L" /SC ONLOGON /RL HIGHEST /TR \"\\\"" + exe + L"\\\"\"";
    unsigned long code = 1;
    if (!RunHiddenAndWait(SchtasksPath(), args, code, err)) return false;
    if (code != 0) { err = Tr(L"Aufgabe wurde nicht angelegt (Rückgabe ") + std::to_wstring(code) + L")"; return false; }
    return true;
}

static void DeleteLogonTask() {
    unsigned long code = 1;
    std::wstring err;
    RunHiddenAndWait(SchtasksPath(), std::wstring(L"/Delete /F /TN ") + kSettingsName, code, err);
}

static bool RunKeyOn(bool& on) {
    on = false;
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKeyPath, 0, KEY_READ, &key) != ERROR_SUCCESS) return true;
    DWORD type = 0, cb = 0;
    const LSTATUS r = RegQueryValueExW(key, kSettingsName, nullptr, &type, nullptr, &cb);
    RegCloseKey(key);
    if (r == ERROR_FILE_NOT_FOUND) return true;
    if (r != ERROR_SUCCESS) { SetLastError(static_cast<DWORD>(r)); return false; }
    on = true;
    return true;
}

static bool SetRunKey(bool on, std::wstring& err) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKeyPath, 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr) != ERROR_SUCCESS) {
        err = Tr(L"Registry-Eintrag nicht erreichbar: ") + LastErrText(GetLastError());
        return false;
    }
    bool ok = true;
    if (on) {
        const std::wstring exe = ThisExePath();
        if (exe.empty()) { err = Tr(L"Pfad zum Programm nicht bekannt"); ok = false; }
        else if (RegSetValueExW(key, kSettingsName, 0, REG_SZ, reinterpret_cast<const BYTE*>(exe.c_str()),
                                static_cast<DWORD>((exe.size() + 1) * sizeof(wchar_t))) != ERROR_SUCCESS) {
            err = Tr(L"Eintrag nicht geschrieben: ") + LastErrText(GetLastError());
            ok = false;
        }
    } else if (RegDeleteValueW(key, kSettingsName) != ERROR_SUCCESS && GetLastError() != ERROR_FILE_NOT_FOUND) {
        err = Tr(L"Eintrag nicht entfernt: ") + LastErrText(GetLastError());
        ok = false;
    }
    RegCloseKey(key);
    return ok;
}

void App::RefreshAutostartState() {
    bool task = false, run = false;
    LogonTaskExists(task);
    RunKeyOn(run);
    autostartOn = task || run;
}

void App::ToggleAutostart() {
    std::wstring err;
    if (autostartOn) {
        DeleteLogonTask();
        SetRunKey(false, err);
        RefreshAutostartState();
        SetNote(Tr(L"Autostart abgeschaltet  \x2013  RoutePerApp startet nicht mehr von selbst."), 10);
        return;
    }
    if (CreateLogonTask(err)) {
        RefreshAutostartState();
        SetNote(Tr(L"Autostart eingeschaltet: RoutePerApp startet bei jeder Anmeldung ohne Nachfrage (geplante Aufgabe RoutePerApp in der Aufgabenplanung)."), 16);
        return;
    }
    std::wstring err2;
    if (SetRunKey(true, err2)) {
        RefreshAutostartState();
        SetNote(Tr(L"Autostart über den Anmeldeschlüssel eingeschaltet (Aufgabe ging nicht: ") + err +
                Tr(L"). Windows fragt bei der Anmeldung nach Administratorrechten."), 20);
        return;
    }
    SetNote(Tr(L"Autostart nicht möglich: ") + err, 14);
}

// Der Weg für alle Programme, die keine eigene Zuordnung haben.
void App::SetDefaultRoute(Zone zone) {
    std::wstring err;
    engine.rules.defaultMode = ZoneMode(zone);
    engine.rules.defaultAdapterGuid.clear();
    if (!engine.SaveAndPublish(err)) {
        SetNote(Tr(L"Standardweg nicht gespeichert: ") + err, 12);
        return;
    }
    int already = 0, failed = 0;
    std::wstring ierr;
    const int applied = engine.ReinjectMatching(ierr, already, failed);
    std::wstring msg = Tr(L"Standardweg für alle Programme ohne eigene Zuordnung: ");
    msg += ZoneName(zone);
    msg += Tr(L"  \x2013  ") + InjectionSummary(applied, already, failed, ierr);
    SetNote(msg, 14);
    RefreshAll(false);
}

void App::LoadSettings() {
    const std::wstring file = engine.paths.dir + L"\\settings.json";
    std::wstring text, err;
    if (!ReadTextFile(file, text, err)) return;
    JsonPtr root;
    if (!JsonParse(text, root).ok) return;
    hotkeysOn = root->boolOr(L"hotkeys", true);
    const std::wstring saved = root->strOr(L"language", L"de");
    if (!saved.empty()) language = saved;
}

void App::SaveSettings() {
    JsonPtr root = Json::MakeObject();
    root->set(L"hotkeys", Json::MakeBool(hotkeysOn));
    root->set(L"language", Json::MakeString(language));
    std::wstring err;
    if (!WriteTextFileAtomic(engine.paths.dir + L"\\settings.json", JsonSerialize(root, true), err))
        SetNote(Tr(L"Einstellungen nicht gespeichert: ") + err, 10);
}

void App::RegisterHotkeys() {
    hotkeysRegistered = false;
    if (!hwnd) return;
    if (!hotkeysOn) return;
    struct Entry { int id; WORD vk; const wchar_t* what; };
    const Entry table[] = {
        { ids::HK_WLAN, 'W', L"WLAN" }, { ids::HK_LAN, 'L', L"LAN" },
        { ids::HK_STANDARD, 'S', L"Standard" }, { ids::HK_RAISE, 'H', Tr(L"Fenster holen") }
    };
    std::wstring failed;
    for (const Entry& e : table) {
        if (RegisterHotKey(hwnd, e.id, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, e.vk)) continue;
        if (!failed.empty()) failed += L", ";
        failed += e.what;
    }
    hotkeysRegistered = failed.empty();
    if (!hotkeysRegistered)
        SetNote(Tr(L"Globale Tastenkürzel teilweise nicht möglich (anderes Programm belegt sie): ") + failed, 16);
}

void App::UnregisterHotkeys() {
    if (!hwnd) return;
    const int table[] = { ids::HK_WLAN, ids::HK_LAN, ids::HK_STANDARD, ids::HK_RAISE };
    for (int id : table) UnregisterHotKey(hwnd, id);
    hotkeysRegistered = false;
}

void App::ToggleHotkeys() {
    hotkeysOn = !hotkeysOn;
    if (hotkeysOn) RegisterHotkeys(); else UnregisterHotkeys();
    SaveSettings();
    SetNote(hotkeysOn ? Tr(L"Globale Tastenkürzel an: Strg+Alt+W / L / S für das Vordergrundprogramm, Strg+Alt+H holt das Fenster.")
                      : Tr(L"Globale Tastenkürzel abgeschaltet."), 12);
}

// Tastenkürzel wirken auf das Programm, dessen Fenster gerade Vordergrund ist.
void App::OnHotkey(int which) {
    if (which == ids::HK_RAISE) { RestoreFromTray(); return; }

    HWND fg = GetForegroundWindow();
    if (!fg) { SetNote(Tr(L"Kein Vordergrundfenster gefunden."), 6); return; }
    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);
    if (pid == GetCurrentProcessId()) { SetNote(Tr(L"RoutePerApp selbst wird nicht umgebogen."), 6); return; }

    HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    wchar_t buf[MAX_PATH * 4] = {};
    DWORD n = ARRAYSIZE(buf);
    if (!proc || !QueryFullProcessImageNameW(proc, 0, buf, &n)) {
        if (proc) CloseHandle(proc);
        SetNote(Tr(L"Das Vordergrundprogramm ist nicht feststellbar  \x2013  erst sein Fenster anklicken."), 8);
        return;
    }
    CloseHandle(proc);
    const std::wstring full(buf, n);

    const Zone zone = (which == ids::HK_WLAN) ? Zone::Wlan
                    : (which == ids::HK_LAN) ? Zone::Lan : Zone::Standard;
    if (RowForPid(pid)) {
        std::vector<unsigned long> pids;
        pids.push_back(pid);
        ApplyToRows(pids, zone);
        return;
    }
    std::vector<std::wstring> files;
    files.push_back(full);
    ApplyToFileDrop(files, zone);
}

static HMENU MakeMenuBar() {
    HMENU bar = CreateMenu();
    if (!bar) return nullptr;

    HMENU file = CreatePopupMenu();
    AppendMenuW(file, MF_STRING, ids::IDM_RELOAD, Tr(L"Neu laden\tF5"));
    AppendMenuW(file, MF_STRING, ids::IDM_REINJECT, Tr(L"Laufende Programme neu binden"));
    AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(file, MF_STRING, ids::IDM_HIDE, Tr(L"In die Taskleiste legen"));
    AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(file, MF_STRING, ids::IDM_AUTOSTART, Tr(L"Bei Windows-Anmeldung starten"));
    AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(file, MF_STRING, ids::IDM_EXIT, Tr(L"Beenden\tStrg+Q"));
    AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(file), Tr(L"Datei"));

    HMENU edit = CreatePopupMenu();
    AppendMenuW(edit, MF_STRING, ids::IDM_UNDO, Tr(L"Rückgängig\tStrg+Z"));
    AppendMenuW(edit, MF_STRING, ids::IDM_CLEAR, Tr(L"Zuordnung aufheben\tEntf"));
    AppendMenuW(edit, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(edit, MF_STRING, ids::IDM_DETAILS, Tr(L"Details\tEnter"));
    AppendMenuW(edit, MF_STRING, ids::IDM_COPY_PATH, Tr(L"Programmpfad kopieren"));
    AppendMenuW(edit, MF_STRING, ids::IDM_FOCUS_SEARCH, Tr(L"Suchfeld\tStrg+F"));
    AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(edit), Tr(L"Bearbeiten"));

    HMENU way = CreatePopupMenu();
    AppendMenuW(way, MF_STRING, ids::IDM_ZONE_WLAN, Tr(L"markierte Programme  \x2192  WLAN"));
    AppendMenuW(way, MF_STRING, ids::IDM_ZONE_LAN, Tr(L"markierte Programme  \x2192  LAN"));
    AppendMenuW(way, MF_STRING, ids::IDM_ZONE_STANDARD, Tr(L"markierte Programme  \x2192  Standard"));
    AppendMenuW(way, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(way, MF_STRING, ids::IDM_DEF_WLAN, Tr(L"ohne eigene Zuordnung  \x2192  WLAN"));
    AppendMenuW(way, MF_STRING, ids::IDM_DEF_LAN, Tr(L"ohne eigene Zuordnung  \x2192  LAN"));
    AppendMenuW(way, MF_STRING, ids::IDM_DEF_STANDARD, Tr(L"ohne eigene Zuordnung  \x2192  Windows-Standard"));
    AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(way), Tr(L"Weg"));

    HMENU win = CreatePopupMenu();
    AppendMenuW(win, MF_STRING, ids::IDM_RESTART_ADMIN, Tr(L"Als Administrator neu starten"));
    AppendMenuW(win, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(win, MF_STRING, ids::IDM_HOTKEYS, Tr(L"Globale Tastenkürzel benutzen"));
    AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(win), Tr(L"Programm"));

    HMENU help = CreatePopupMenu();
    AppendMenuW(help, MF_STRING, ids::IDM_KEYS, Tr(L"Tastenkürzel und Ablauf"));
    AppendMenuW(help, MF_STRING, ids::IDM_ABOUT, Tr(L"Über RoutePerApp"));
    AppendMenuW(help, MF_SEPARATOR, 0, nullptr);
    HMENU langMenu = CreatePopupMenu();
    AppendMenuW(langMenu, MF_STRING, ids::IDM_LANG_DE, L"Deutsch");
    AppendMenuW(langMenu, MF_STRING, ids::IDM_LANG_EN, L"English");
    AppendMenuW(help, MF_POPUP, reinterpret_cast<UINT_PTR>(langMenu), Tr(L"Sprache"));
    AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(help), Tr(L"?"));
    return bar;
}

void App::ShowMenu() {
    if (hwnd) SetMenu(hwnd, MakeMenuBar());
}

// Verzeichnis der eigenen Datei: Sprachdateien koennen neben dem Programm liegen.
static std::wstring ThisExeDir() {
    wchar_t buf[MAX_PATH] = {};
    const DWORD n = GetModuleFileNameW(nullptr, buf, ARRAYSIZE(buf));
    if (!n) return L".";
    std::wstring path(buf, n);
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring(L".") : path.substr(0, slash);
}

// Sprache umschalten, ohne das Programm neu zu starten: jeder Text wird neu geholt und
// jede Stelle, die einen Text zeigt, bekommt ihn erneut. Die Menues entstehen ganz neu,
// Spaltenkoepfe werden ersetzt, Zellen und Karten neu gebildet, das Fachtext neu gesetzt.
void App::SwitchLanguage(const std::wstring& code) {
    const std::wstring want = code.empty() ? std::wstring(L"de") : code;
    language = want;
    std::wstring problem;
    lang::Init(want, ThisExeDir(), engine.paths.dir, &problem);
    if (!problem.empty()) SetNote(problem, 12);

    if (hwnd) {
        HMENU bar = MakeMenuBar();
        if (bar) { SetMenu(hwnd, bar); DrawMenuBar(hwnd); }
        SetWindowTextW(hwnd, Tr(L"RoutePerApp  \x2013  Internetweg pro Programm"));
    }
    if (list) {
        const wchar_t* names[COL_COUNT] = {
            Tr(L"Programm"), L"PID", Tr(L"Adapter heute"),
            Tr(L"WLAN"), Tr(L"LAN"), Tr(L"Standard"), Tr(L"Benutzer")
        };
        LVCOLUMNW lc = {};
        lc.mask = LVCF_TEXT;
        for (int c = 0; c < COL_COUNT; ++c) {
            lc.iSubItem = c;
            lc.pszText = const_cast<wchar_t*>(names[c]);
            SendMessageW(list, LVM_SETCOLUMNW, static_cast<WPARAM>(c), reinterpret_cast<LPARAM>(&lc));
        }
        for (int i = 0; i < 3; ++i) headerCache[i].clear();
    }
    if (search)
        SendMessageW(search, EM_SETCUEBANNER, TRUE,
                     reinterpret_cast<LPARAM>(Tr(L"Programm oder Benutzer suchen")));
    if (chkNet) SetWindowTextW(chkNet, Tr(L"nur mit Verbindung"));
    if (btnReload) SetWindowTextW(btnReload, Tr(L"Neu laden (F5)"));
    if (btnAdmin) SetWindowTextW(btnAdmin, Tr(L"Als Administrator neu starten"));
    for (int i = 1; i <= 3; ++i) if (cards[i]) InvalidateRect(cards[i], nullptr, TRUE);

    cellCache.clear();
    SyncTable(true);
    UpdateHeaderLabels();
    UpdateCards();
    note.clear();
    noteUntil = 0;
    if (hwnd) InvalidateRect(hwnd, nullptr, TRUE);

    if (trayReady) {
        lstrcpynW(tray.szTip, Tr(L"RoutePerApp  \x2013  Internetweg pro Programm"),
                  static_cast<int>(sizeof(tray.szTip) / sizeof(wchar_t)));
        Shell_NotifyIconW(NIM_MODIFY, &tray);
    }
    SaveSettings();
}

void App::RefreshMenu(HMENU bar) {
    if (!bar) return;
    const bool sel = HasSelection();
    const bool canUndo = !undoStack.Empty();
    const int count = GetMenuItemCount(bar);
    for (int i = 0; i < count; ++i) {
        HMENU sub = GetSubMenu(bar, i);
        if (!sub) continue;
        EnableMenuItem(sub, ids::IDM_UNDO, MF_BYCOMMAND | (canUndo ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(sub, ids::IDM_CLEAR, MF_BYCOMMAND | (sel ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(sub, ids::IDM_DETAILS, MF_BYCOMMAND | (sel ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(sub, ids::IDM_COPY_PATH, MF_BYCOMMAND | (sel ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(sub, ids::IDM_ZONE_WLAN, MF_BYCOMMAND | (sel ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(sub, ids::IDM_ZONE_LAN, MF_BYCOMMAND | (sel ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(sub, ids::IDM_ZONE_STANDARD, MF_BYCOMMAND | (sel ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(sub, ids::IDM_RESTART_ADMIN, MF_BYCOMMAND | (elevated ? MF_GRAYED : MF_ENABLED));
        CheckMenuItem(sub, ids::IDM_AUTOSTART, MF_BYCOMMAND | (autostartOn ? MF_CHECKED : MF_UNCHECKED));
        CheckMenuItem(sub, ids::IDM_HOTKEYS, MF_BYCOMMAND | (hotkeysOn ? MF_CHECKED : MF_UNCHECKED));
        const bool de = (lang::Language() == L"de");
        CheckMenuItem(sub, ids::IDM_LANG_DE, MF_BYCOMMAND | (de ? MF_CHECKED : MF_UNCHECKED));
        CheckMenuItem(sub, ids::IDM_LANG_EN, MF_BYCOMMAND | (de ? MF_UNCHECKED : MF_CHECKED));
        CheckMenuItem(sub, ids::IDM_DEF_WLAN, MF_BYCOMMAND |
                    (engine.rules.defaultMode == RouteMode::Wlan ? MF_CHECKED : MF_UNCHECKED));
        CheckMenuItem(sub, ids::IDM_DEF_LAN, MF_BYCOMMAND |
                    (engine.rules.defaultMode == RouteMode::Lan ? MF_CHECKED : MF_UNCHECKED));
        CheckMenuItem(sub, ids::IDM_DEF_STANDARD, MF_BYCOMMAND |
                    (engine.rules.defaultMode == RouteMode::None ? MF_CHECKED : MF_UNCHECKED));
    }
}

void App::ShowAbout(bool show) {
    std::wstring t;
    t += Tr(L"RoutePerApp  \x2013  Internetweg pro Programm  (Fassung ") + std::wstring(kAppVersion) + Tr(L")\r\n\r\n");
    t += Tr(L"Jedes Programm läuft über die Netzwerkkarte, die du ihm gibst: WLAN, LAN oder die\r\n");
    t += Tr(L"ganz normale Windows-Routenwahl. Zugewiesen wird per Drag & Drop auf eine Karte.\r\n\r\n");
    t += Tr(L"Das Programm bleibt nach dem Schließen des Fensters im Hintergrund laufen, sonst\r\n");
    t += Tr(L"greifen die Zuordnungen nicht für Programme, die später gestartet werden.\r\n");
    t += Tr(L"Beenden: Symbol in der Taskleiste  \x2192  Beenden, oder Datei  \x2192  Beenden.\r\n\r\n");
    t += elevated ? Tr(L"läuft mit Administratorrechten")
                  : Tr(L"läuft ohne Administratorrechte  \u2013  Programme anderer Benutzer und Dienste\r\nkönnen nicht umgebogen werden (Programm  \u2192  Als Administrator neu starten)");
    if (!show) return;
    MessageBoxW(hwnd, t.c_str(), Tr(L"Über RoutePerApp"), MB_OK | MB_ICONINFORMATION);
}

void App::ShowShortcutHelp(bool show) {
    std::wstring t;
    t += Tr(L"In der Liste: ein Programm markieren, dann auf eine Karte oder in eine Spalte ziehen.\r\n\r\n");
    t += Tr(L"F5            neu laden\r\n");
    t += Tr(L"Strg+F        Suchfeld\r\n");
    t += Tr(L"Enter         Details des markierten Programms\r\n");
    t += Tr(L"Entf          Zuordnung aufheben\r\n");
    t += Tr(L"Strg+Z        letzte Zuordnung rückgängig\r\n");
    t += Tr(L"Strg+Q        RoutePerApp beenden\r\n");
    t += Tr(L"Fenster zu    RoutePerApp läuft im Hintergrund weiter\r\n\r\n");
    t += Tr(L"Globale Kürzel (gelten auch bei verstecktem Fenster):\r\n");
    t += Tr(L"Strg+Alt+W    Vordergrundprogramm  ->  WLAN\r\n");
    t += Tr(L"Strg+Alt+L    Vordergrundprogramm  ->  LAN\r\n");
    t += Tr(L"Strg+Alt+S    Vordergrundprogramm  ->  Windows-Standard\r\n");
    t += Tr(L"Strg+Alt+H    RoutePerApp-Fenster holen\r\n");
    t += Tr(L"Abzuschalten über Programm  ->  Globale Tastenkürzel benutzen.\r\n");
    if (!show) return;
    MessageBoxW(hwnd, t.c_str(), Tr(L"Tastenkürzel"), MB_OK | MB_ICONINFORMATION);
}

// ---------------------------------------------------------------- Details und Menü

void App::ShowDetailsFor(unsigned long pid) {
    const ProcRow* r = RowForPid(pid);
    if (!r) return;
    std::wstring t;
    t += Tr(L"Programm:  ") + r->image + L"\r\n";
    t += Tr(L"PID:  ") + std::to_wstring(r->pid) + (r->is32Bit ? Tr(L"   (32 Bit)") : Tr(L"   (64 Bit)")) + L"\r\n";
    t += Tr(L"Benutzer:  ") + (r->user.empty() ? std::wstring(Tr(L"unbekannt")) : r->user) + L"\r\n";
    t += Tr(L"Pfad:  ") + (r->path.empty() ? std::wstring(Tr(L"nicht lesbar")) : r->path) + L"\r\n";
    t += Tr(L"Verbindungen gerade:  ") + std::to_wstring(r->conns) + L"\r\n";
    t += Tr(L"Aktueller Adapter:  ") + ViaCell(*r) + L"\r\n";
    t += Tr(L"Ziel:  ") + ModeLabel(r->mode) + Tr(L"   \x2013   ") + RowStateText(*r) + L"\r\n";
    if (r->ruleId != 0) t += Tr(L"Regel:  #") + std::to_wstring(r->ruleId) + L"\r\n";
    if (r->mode == RouteMode::Adapter && !r->modeGuid.empty()) t += Tr(L"fester Adapter:  ") + r->modeGuid + L"\r\n";

    const std::wstring dll = engine.HookDllFor(r->is32Bit);
    ProcInfo pi;
    pi.pid = r->pid;
    pi.ppid = r->ppid;
    pi.image = r->image;
    pi.path = r->path;
    pi.user = r->user;
    pi.pathAccess = r->pathAccess;
    pi.is32Bit = r->is32Bit;
    std::wstring why;
    const bool can = CanInjectProcess(pi, elevated, FileExists(dll), why);
    t += Tr(L"sofortige Bindung:  ") + (can ? std::wstring(Tr(L"möglich")) : why) + L"\r\n";
    if (r->injected) {
        t += Tr(L"Hook-Bibliothek ist geladen\r\n");
        unsigned long mask = 0;
        std::wstring e2;
        if (QueryHookPatchMask(r->pid, dll, mask, e2)) {
            t += Tr(L"aktive Abfangstellen:  ") + std::to_wstring(mask) + Tr(L" (Bitmaske)\r\n");
        }
        unsigned long idx = 0;
        if (QueryHookIfIndex(r->pid, dll, idx, e2) && idx != 0) {
            t += Tr(L"gebundener Interface-Index:  ") + std::to_wstring(idx) + L"\r\n";
        }
    }
    MessageBoxW(hwnd, t.c_str(), Tr(L"Details"), MB_OK | MB_ICONINFORMATION);
}

static void CopyToClipboard(HWND owner, const std::wstring& text) {
    if (!OpenClipboard(owner)) return;
    EmptyClipboard();
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, (text.size() + 1) * sizeof(wchar_t));
    if (mem) {
        wchar_t* dst = static_cast<wchar_t*>(GlobalLock(mem));
        if (dst) {
            for (size_t i = 0; i < text.size(); ++i) dst[i] = text[i];
            dst[text.size()] = 0;
            GlobalUnlock(mem);
            SetClipboardData(CF_UNICODETEXT, mem);
        }
    }
    CloseClipboard();
}

void App::ShowContextMenu(POINT ptScreen) {
    if (!list) return;
    std::vector<unsigned long> sel = SelectedPids();

    if (ptScreen.x == -1 && ptScreen.y == -1) {
        // Tastatur: Menü am markierten Eintrag öffnen
        const int idx = static_cast<int>(SendMessageW(list, LVM_GETNEXTITEM, static_cast<WPARAM>(-1),
                                                      static_cast<LPARAM>(LVNI_FOCUSED)));
        if (idx >= 0) {
            LVITEMW it = {};
            it.mask = LVIF_STATE;
            it.stateMask = LVIS_FOCUSED;
            RECT rc = {};
            ListView_GetItemRect(list, idx, &rc, LVIR_BOUNDS);
            MapWindowPoints(list, nullptr, reinterpret_cast<POINT*>(&rc), 2);
            ptScreen.x = rc.left + S(12);
            ptScreen.y = rc.bottom;
        } else {
            GetCursorPos(&ptScreen);
        }
    } else {
        // Rechtsklick: falls direkt auf eine Zeile geklickt wurde, nur diese Zeile meinen
        POINT client = ptScreen;
        ScreenToClient(list, &client);
        LVHITTESTINFO hi = {};
        hi.pt = client;
        const int hit = static_cast<int>(SendMessageW(list, LVM_SUBITEMHITTEST, 0, reinterpret_cast<LPARAM>(&hi)));
        if (hit >= 0 && static_cast<size_t>(hit) < view.size()) {
            const unsigned long pid = view[static_cast<size_t>(hit)].pid;
            if (std::find(sel.begin(), sel.end(), pid) == sel.end()) sel.assign(1, pid);
        }
    }

    HMENU menu = CreatePopupMenu();
    const bool haveSel = !sel.empty();
    const std::wstring undoLabel = Tr(L"Rückgängig") +
        (undoStack.Empty() ? std::wstring() : (L":  " + undoStack.TopLabel()));
    AppendMenuW(menu, MF_STRING, ids::IDM_ZONE_WLAN, Tr(L"\u00FCber WLAN (5G-Router)"));
    AppendMenuW(menu, MF_STRING, ids::IDM_ZONE_LAN, Tr(L"\u00FCber LAN (Fritzbox)"));
    AppendMenuW(menu, MF_STRING, ids::IDM_ZONE_STANDARD, Tr(L"Windows-Standard (Bindung l\u00F6schen)"));
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ids::IDM_CLEAR, Tr(L"Zuordnung entfernen"));
    AppendMenuW(menu, MF_STRING, ids::IDM_UNDO, undoLabel.c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ids::IDM_DETAILS, L"Details");
    AppendMenuW(menu, MF_STRING, ids::IDM_COPY_PATH, Tr(L"Programmpfad kopieren"));
    AppendMenuW(menu, MF_STRING, ids::IDM_RELOAD, Tr(L"Neu laden (F5)"));
    if (!elevated) AppendMenuW(menu, MF_STRING, ids::IDM_RESTART_ADMIN, Tr(L"Als Administrator neu starten"));

    EnableMenuItem(menu, ids::IDM_ZONE_WLAN, haveSel ? MF_ENABLED : MF_GRAYED);
    EnableMenuItem(menu, ids::IDM_ZONE_LAN, haveSel ? MF_ENABLED : MF_GRAYED);
    EnableMenuItem(menu, ids::IDM_ZONE_STANDARD, haveSel ? MF_ENABLED : MF_GRAYED);
    EnableMenuItem(menu, ids::IDM_CLEAR, haveSel ? MF_ENABLED : MF_GRAYED);
    EnableMenuItem(menu, ids::IDM_UNDO, undoStack.Empty() ? MF_GRAYED : MF_ENABLED);
    EnableMenuItem(menu, ids::IDM_DETAILS, haveSel ? MF_ENABLED : MF_GRAYED);
    EnableMenuItem(menu, ids::IDM_COPY_PATH, haveSel ? MF_ENABLED : MF_GRAYED);

    SetForegroundWindow(hwnd);
    const int chosen = TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_TOPALIGN,
                                        ptScreen.x, ptScreen.y, hwnd, nullptr);
    DestroyMenu(menu);
    if (chosen != 0) {
        PostMessageW(hwnd, WM_COMMAND, MAKEWPARAM(chosen, 0), 0);
    }
}

// ---------------------------------------------------------------- Einstieg

static bool RegisterClasses(HINSTANCE hinst, HICON icon) {
    WNDCLASSEXW card = {};
    card.cbSize = sizeof(card);
    card.style = CS_HREDRAW | CS_VREDRAW;
    card.lpfnWndProc = CardProc;
    card.hInstance = hinst;
    card.hCursor = LoadCursorW(nullptr, IDC_HAND);
    card.lpszClassName = kCardClass;
    if (!RegisterClassExW(&card)) return false;

    WNDCLASSEXW main = {};
    main.cbSize = sizeof(main);
    main.style = CS_HREDRAW | CS_VREDRAW;
    main.lpfnWndProc = MainProc;
    main.hInstance = hinst;
    main.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    main.hbrBackground = nullptr;
    main.lpszClassName = L"RoutePerApp.MainWindow";
    main.hIcon = icon ? icon : LoadIconW(nullptr, IDI_APPLICATION);
    main.hIconSm = icon ? icon : LoadIconW(nullptr, IDI_APPLICATION);
    if (!RegisterClassExW(&main)) return false;
    return true;
}

int APIENTRY wWinMain(HINSTANCE hinst, HINSTANCE, LPWSTR cmdLine, int nCmdShow) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    // Es darf nur ein Exemplar laufen: Zuordnungen gelten für den ganzen Rechner,
    // zwei Exemplare würden sich gegenseitig die Programme umbiegen. Wer das Programm
    // noch einmal startet (z. B. weil das Fenster nur versteckt war), holt das
    // bestehende Fenster zurück.
    HANDLE single = CreateMutexW(nullptr, FALSE, L"Local\\RoutePerApp.EinzelnesExemplar");
    if (single && GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND other = FindWindowW(L"RoutePerApp.MainWindow", nullptr);
        if (other) {
            PostMessageW(other, ids::WM_APP_TRAY, static_cast<WPARAM>(ids::TRAY_UID),
                         static_cast<LPARAM>(MAKELONG(ids::TRAY_UID, WM_LBUTTONUP)));  // wie ein Klick aufs Symbol
            SetForegroundWindow(other);
        } else {
            MessageBoxW(nullptr, Tr(L"RoutePerApp läuft bereits, aber sein Fenster wurde nicht gefunden.\nIm Zweifelsfall über den Task-Manager beenden."),
                        L"RoutePerApp", MB_OK | MB_ICONINFORMATION);
        }
        if (single) CloseHandle(single);
        return 0;
    }

    INITCOMMONCONTROLSEX icc = {};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES | ICC_BAR_CLASSES;
    InitCommonControlsEx(&icc);

    if (FAILED(OleInitialize(nullptr))) return 1;

    g_dpi = GetDpiForSystem();

    static App app;
    g_app = &app;
    if (cmdLine && (wcsstr(cmdLine, L"--selbsttest") || wcsstr(cmdLine, L"/selbsttest")))
        app.selfTest = true;

    // Sprache beim Start: "routeperapp.exe --sprache en" (auch /sprache en). Ohne Angabe
    // wird Deutsch benutzt, die Sprache des Quelltextes. Gespeicherte Wahl kommt dazu.
    std::wstring wanted = L"de";
    if (cmdLine) {
        const wchar_t* at = wcsstr(cmdLine, L"--sprache");
        if (!at) at = wcsstr(cmdLine, L"/sprache");
        if (at) {
            at += 9;
            while (*at == L' ' || *at == L'=' || *at == L':') ++at;
            wchar_t code[8] = {};
            size_t take = 0;
            while (take < 6 && at[take] != 0 && at[take] != L' ' && at[take] != L'"') {
                wchar_t c = at[take];
                if (c >= L'A' && c <= L'Z') c = static_cast<wchar_t>(c - L'A' + L'a');
                code[take] = c;
                ++take;
            }
            if (take) wanted = std::wstring(code, take);
        }
    }
    app.language = wanted;
    std::wstring langProblem;
    lang::Init(wanted, ThisExeDir(), app.engine.paths.dir, &langProblem);
    g_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    // Das Symbol kommt aus der Resourcedatei (app.ico, aus assets/logo.png gebaut), damit
    // Datei, Fenster, Taskleiste und das verborgene Fach dasselbe Zeichen zeigen.
    app.appIcon = LoadIconW(hinst, MAKEINTRESOURCEW(IDI_APP));
    if (!app.appIcon) app.appIcon = LoadIconW(nullptr, IDI_APPLICATION);

    if (!RegisterClasses(hinst, app.appIcon)) {
        MessageBoxW(nullptr, (Tr(L"Fensterklassen konnten nicht angelegt werden: ") + LastErrText(GetLastError())).c_str(),
                    L"RoutePerApp", MB_OK | MB_ICONERROR);
        OleUninitialize();
        return 1;
    }

    const int w = S(1180);
    const int h = S(760);
    HWND hwnd = CreateWindowExW(WS_EX_APPWINDOW, L"RoutePerApp.MainWindow",
                                Tr(L"RoutePerApp  \x2013  Internetweg pro Programm"),
                                WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                                CW_USEDEFAULT, CW_USEDEFAULT, w, h,
                                nullptr, MakeMenuBar(), hinst, &app);
    if (!hwnd) {
        MessageBoxW(nullptr, (Tr(L"Fenster konnte nicht ge\u00F6ffnet werden: ") + LastErrText(GetLastError())).c_str(),
                    L"RoutePerApp", MB_OK | MB_ICONERROR);
        OleUninitialize();
        return 1;
    }
    app.hwnd = hwnd;

    ACCEL accels[] = {
        { FVIRTKEY | FCONTROL, static_cast<WORD>('Z'), ids::IDM_UNDO },
        { FVIRTKEY, VK_F5, ids::IDM_RELOAD },
        { FVIRTKEY | FCONTROL, static_cast<WORD>('F'), ids::IDC_SEARCH },
        { FVIRTKEY, VK_DELETE, ids::IDM_CLEAR },
        { FVIRTKEY, VK_RETURN, ids::IDM_DETAILS },
        { FVIRTKEY | FCONTROL, static_cast<WORD>('Q'), ids::IDM_EXIT },
    };

    HACCEL accel = CreateAcceleratorTableW(accels, static_cast<int>(ARRAYSIZE(accels)));

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg = {};
    for (;;) {
        const int got = GetMessageW(&msg, nullptr, 0, 0);
        if (got == 0) break;
        if (got == -1) break;
        if (accel && TranslateAcceleratorW(hwnd, accel, &msg)) continue;
        if (IsDialogMessageW(hwnd, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (accel) DestroyAcceleratorTable(accel);
    // Das Fenster ist im Normalfall schon durch WM_DESTROY zerstört (App::Destroy
    // setzt dann hwnd auf null); sonst wird es hier geschlossen.
    if (app.hwnd) DestroyWindow(app.hwnd);
    OleUninitialize();
    return static_cast<int>(msg.wParam);
}
