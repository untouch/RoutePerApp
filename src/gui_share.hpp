// RoutePerApp - gemeinsame Erklärung für gui.cpp und gui_drag.cpp.
// Die eigentliche Fachlogik liegt in core.cpp und gui_logic.cpp (beides unit-getestet);
// hier werden nur Fenster, Farben und Drag-&-Drop zusammengesetzt.
#pragma once

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <shellapi.h>
#include <ole2.h>
#include <oleidl.h>

#include <string>
#include <vector>
#include <map>

#include "core.hpp"
#include "gui_logic.hpp"

// Eine Adapterkarte ist ein eigenes Kindfenster: eigene Zeichenfläche, eigene Ablagezone.
struct CardState {
    srgui::Zone zone = srgui::Zone::None;
    HWND hwnd = nullptr;
    bool hot = false;                 // gerade wird etwas darüber gezogen
    srgui::CardText text;
};

class App {
public:
    // Aufbau und Lebensdauer
    bool Create(HINSTANCE hinst);
    void Destroy();

    // Engine und Anzeige
    sr::Engine engine{ sr::DefaultPaths() };
    HWND hwnd = nullptr;
    HWND list = nullptr;
    HWND search = nullptr;
    HWND chkNet = nullptr;
    HWND btnReload = nullptr;
    HWND btnAdmin = nullptr;
    // Karten werden durchgehend mit den Indizes 1..3 (Zone Wlan/Lan/Standard) benutzt.
    HWND cards[4] = { nullptr, nullptr, nullptr, nullptr };
    CardState cardState[4];

    HFONT fontTitle = nullptr;
    HFONT fontUi = nullptr;
    HFONT fontUiBold = nullptr;
    HFONT fontSmall = nullptr;
    HICON appIcon = nullptr;
    NOTIFYICONDATAW tray = {};
    bool trayReady = false;
    bool inTray = false;
    bool quitting = false;
    bool destroyed = false;
    HIMAGELIST icons = nullptr;
    int iconPx = 16;
    std::vector<std::vector<std::wstring>> cellCache;   // zuletzt gezeigter Zelltext
    std::wstring headerCache[3];                        // Spaltenköpfe der Zonen

    std::vector<sr::ProcRow> view;            // genau die Zeilen, die man gerade sieht
    std::vector<unsigned long> shownPids;     // Reihenfolge der aktuell gezeigten Zeilen
    std::vector<unsigned long> dragPids;      // Prozesse des laufenden Zuges
    std::map<std::wstring, int> iconIndex;    // Programmpfad -> Bildindex

    srgui::UndoStack undoStack;
    sr::SortField sortField = sr::SortField::Name;
    bool sortAsc = true;
    bool onlyNetwork = false;
    bool elevated = false;
    bool dragging = false;
    srgui::Zone hoverZone = srgui::Zone::None;
    int hoverColumn = -1;
    POINT dragStartPt = { 0, 0 };
    bool dragPossible = false;
    bool suppressDrag = false;
    std::wstring query;
    std::wstring note;
    unsigned long long noteUntil = 0;         // Tick-Zeitpunkt, bis der Hinweis steht
    std::wstring startupError;
    int dpi = 96;

    // Anzeige aktualisieren
    void RebuildView();
    void SyncTable(bool full);
    void UpdateCards();
    void UpdateHeaderLabels();
    void PaintBackground(HDC hdc, const RECT& rc) const;
    void PaintStatus(HDC hdc, const RECT& rc) const;
    void ExpireNote();
    void BuildImageList();
    void RebuildFontsAndImages();
    void RefreshAll(bool withAdapters);
    void LayoutChildren(int cx, int cy);

    // Sprache: "de" ist die Sprache des Quelltextes, "en" kommt aus lang/en.json.
    void SwitchLanguage(const std::wstring& code);
    std::wstring language = L"de";

    // Eingaben
    void OnListNotify(LPNMHDR nmh, LRESULT& res);
    void OnCommand(int id, int code);
    void OnHeaderClick(int column);
    void ShowContextMenu(POINT ptScreen);
    void ShowDetailsFor(unsigned long pid);
    void ChangeQuery(const std::wstring& q);
    void UndoLast();
    void RestartElevated();

    // Zuordnungen anwenden (der Kern des Drag & Drop)
    void ApplyToRows(const std::vector<unsigned long>& pids, srgui::Zone zone);
    void ApplyToFileDrop(const std::vector<std::wstring>& files, srgui::Zone zone);
    void ClearAssignments(const std::vector<unsigned long>& pids);

    // Drag-&-Drop-Helfer (in gui_drag.cpp)
    void BeginDragFromList();
    void SetHover(srgui::Zone zone, int column);
    void FinishDragFeedback();
    std::vector<unsigned long> SelectedPids() const;
    int ColumnAtPoint(POINT clientPt) const;

    // Taskleisten-Symbol: Zuordnungen bleiben erhalten, wenn das Fenster zu ist.
    bool ShowTray();
    void HideTray();
    void ShowTrayMenu(POINT ptScreen);
    HMENU MakeTrayMenu();
    int SelfTest();
    void RestoreFromTray();
    void HideToTray();
    void Quit();
    void ShowMenu();
    void RefreshMenu(HMENU bar);
    void ShowAbout(bool show = true);
    void ShowShortcutHelp(bool show = true);
    bool HasSelection() const;

    // Autostart, Standardweg für Programme ohne eigene Zuordnung, globale Tastenkürzel.
    bool autostartOn = false;
    bool hotkeysOn = true;
    bool hotkeysRegistered = false;
    bool selfTest = false;
    void RefreshAutostartState();
    void ToggleAutostart();
    void SetDefaultRoute(srgui::Zone zone);
    void LoadSettings();
    void SaveSettings();
    void RegisterHotkeys();
    void UnregisterHotkeys();
    void ToggleHotkeys();
    void OnHotkey(int which);

    // Kleinkram
    std::wstring StatusText() const;
    void SetNote(const std::wstring& text, int seconds = 8);
    const sr::ProcRow* RowForPid(unsigned long pid) const;
    int IconForPath(const std::wstring& path);
    void EnsureHookCopies();
    std::vector<HWND> CardWindows() const;
};

// Die drei Ablage-Ziele als eigene Fensterklassen brauchen diesen Zugriff.
App* TheApp();

// ---------------------------------------------------------------- Drag & Drop (gui_drag.cpp)

struct SrDataObject : public IDataObject {
    ULONG refs = 1;
    FORMATETC fmt = {};
    STGMEDIUM med = {};
    bool owned = false;

    static unsigned short ClipFormat();
    explicit SrDataObject(const std::vector<unsigned long>& pids);

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;
    HRESULT STDMETHODCALLTYPE GetData(FORMATETC* pfe, STGMEDIUM* pmed) override;
    HRESULT STDMETHODCALLTYPE GetDataHere(FORMATETC*, STGMEDIUM*) override;
    HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC* pfe) override;
    HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(FORMATETC*, FORMATETC* pfeOut) override;
    HRESULT STDMETHODCALLTYPE SetData(FORMATETC*, STGMEDIUM*, BOOL) override { return DV_E_FORMATETC; }
    HRESULT STDMETHODCALLTYPE EnumFormatEtc(DWORD, IEnumFORMATETC** ppEnum) override;
    HRESULT STDMETHODCALLTYPE DAdvise(FORMATETC*, DWORD, IAdviseSink*, DWORD*) override { return DV_E_FORMATETC; }
    HRESULT STDMETHODCALLTYPE DUnadvise(DWORD) override { return DV_E_FORMATETC; }
    HRESULT STDMETHODCALLTYPE EnumDAdvise(IEnumSTATDATA**) override { return DV_E_FORMATETC; }
};

struct SrDropSource : public IDropSource {
    ULONG refs = 1;

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;
    HRESULT STDMETHODCALLTYPE QueryContinueDrag(BOOL esc, DWORD keyState) override;
    HRESULT STDMETHODCALLTYPE GiveFeedback(DWORD) override { return S_OK; }
};

// Rückgabewerte von QueryContinueDrag (stehen in diesem SDK-Header nicht drin).
#ifndef DRAGDROP_ACTION_NONE
#define DRAGDROP_ACTION_NONE   0   // Zug fortsetzen
#define DRAGDROP_ACTION_CANCEL 1   // Zug abbrechen (Esc)
#define DRAGDROP_ACTION_DROP   2   // Zug abschließen
#endif

// Ablageziel: entweder eine Tabelle (Spalte entscheidet) oder eine Karte (feste Zone).
struct SrDropTarget : public IDropTarget {
    ULONG refs = 1;
    App* app = nullptr;
    HWND hwnd = nullptr;
    bool columnMode = false;
    srgui::Zone zone = srgui::Zone::None;

    SrDropTarget(App* a, HWND h, bool asColumn, srgui::Zone z);

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;
    HRESULT STDMETHODCALLTYPE DragEnter(IDataObject*, DWORD keyState, POINTL pt, DWORD* pdwEffect) override;
    HRESULT STDMETHODCALLTYPE DragOver(DWORD keyState, POINTL pt, DWORD* pdwEffect) override;
    HRESULT STDMETHODCALLTYPE DragLeave() override;
    HRESULT STDMETHODCALLTYPE Drop(IDataObject* data, DWORD keyState, POINTL pt, DWORD* pdwEffect) override;
};

void RegisterDropTargets(App* app);
void RevokeDropTargets(App* app);
