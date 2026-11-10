// RoutePerApp - Drag & Drop: Datenobjekt, Zugquelle, Ablageziele.
// Entscheidungen (welche Zone, welche Datei erlaubt, Hinweiszeichenketten) stecken in
// gui_logic.cpp und sind dort getestet; hier steckt nur die OLE-Mechanik.
#include "gui_share.hpp"

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "uuid.lib")

using namespace sr;
using namespace srgui;

static const wchar_t* kDragFormatName = L"RoutePerApp.ProcessList";

// ---------------------------------------------------------------- Datenobjekt für den eigenen Zug

unsigned short SrDataObject::ClipFormat() {
    static unsigned short cf = static_cast<unsigned short>(RegisterClipboardFormatW(kDragFormatName));
    return cf;
}

static std::wstring PackPids(const std::vector<unsigned long>& pids) {
    std::wstring s;
    for (unsigned long p : pids) {
        if (!s.empty()) s += L'\n';
        s += std::to_wstring(p);
    }
    return s;
}

static std::vector<unsigned long> UnpackPids(const wchar_t* text) {
    std::vector<unsigned long> out;
    if (!text) return out;
    const wchar_t* p = text;
    while (*p) {
        wchar_t* end = nullptr;
        unsigned long long v = wcstoull(p, &end, 10);
        if (end == p) { ++p; continue; }
        if (v && v <= 0xFFFFFFFFull) out.push_back(static_cast<unsigned long>(v));
        p = (*end == L'\n') ? end + 1 : end;
    }
    return out;
}

SrDataObject::SrDataObject(const std::vector<unsigned long>& pids) {
    fmt.cfFormat = ClipFormat();
    fmt.ptd = nullptr;
    fmt.dwAspect = DVASPECT_CONTENT;
    fmt.lindex = -1;
    fmt.tymed = TYMED_HGLOBAL;
    std::wstring s = PackPids(pids);
    med.tymed = TYMED_HGLOBAL;
    med.hGlobal = GlobalAlloc(GHND, (s.size() + 1) * sizeof(wchar_t));
    if (med.hGlobal) {
        wchar_t* dst = static_cast<wchar_t*>(GlobalLock(med.hGlobal));
        if (dst) {
            for (size_t i = 0; i < s.size(); ++i) dst[i] = s[i];
            dst[s.size()] = 0;
            GlobalUnlock(med.hGlobal);
        }
        owned = true;
    }
    med.pUnkForRelease = nullptr;
}

HRESULT SrDataObject::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_INVALIDARG;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IDataObject)) {
        *ppv = static_cast<IDataObject*>(this);
        AddRef();
        return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
}
ULONG SrDataObject::AddRef() { return ++refs; }
ULONG SrDataObject::Release() {
    ULONG r = --refs;
    if (r == 0) {
        if (owned && med.hGlobal) GlobalFree(med.hGlobal);
        delete this;
    }
    return r;
}

HRESULT SrDataObject::GetData(FORMATETC* pfe, STGMEDIUM* pmed) {
    if (!pfe || !pmed) return E_INVALIDARG;
    if (pfe->cfFormat != fmt.cfFormat || !(pfe->tymed & TYMED_HGLOBAL)) return DV_E_FORMATETC;
    ZeroMemory(pmed, sizeof(*pmed));
    pmed->tymed = TYMED_HGLOBAL;
    SIZE_T cb = med.hGlobal ? GlobalSize(med.hGlobal) : 0;
    pmed->hGlobal = GlobalAlloc(GHND, cb ? cb : sizeof(wchar_t));
    if (!pmed->hGlobal) return E_OUTOFMEMORY;
    if (cb) {
        BYTE* src = static_cast<BYTE*>(GlobalLock(med.hGlobal));
        BYTE* dst = static_cast<BYTE*>(GlobalLock(pmed->hGlobal));
        if (src && dst) memcpy(dst, src, cb);
        if (src) GlobalUnlock(med.hGlobal);
        if (dst) GlobalUnlock(pmed->hGlobal);
    }
    return S_OK;
}
HRESULT SrDataObject::GetDataHere(FORMATETC*, STGMEDIUM*) { return DV_E_FORMATETC; }
HRESULT SrDataObject::QueryGetData(FORMATETC* pfe) {
    if (!pfe) return E_INVALIDARG;
    if (pfe->cfFormat != fmt.cfFormat || !(pfe->tymed & TYMED_HGLOBAL)) return DV_E_FORMATETC;
    return S_OK;
}
HRESULT SrDataObject::GetCanonicalFormatEtc(FORMATETC*, FORMATETC* pfeOut) {
    if (pfeOut) pfeOut->ptd = nullptr;
    return S_OK;
}

// Aufzählung: genau ein Format, mehr braucht der interne Zug nicht.
struct OwnFormatEnum : public IEnumFORMATETC {
    ULONG refs = 1;
    ULONG pos = 0;
    FORMATETC item;

    explicit OwnFormatEnum(const FORMATETC& f) : item(f) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_INVALIDARG;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IEnumFORMATETC)) {
            *ppv = static_cast<IEnumFORMATETC*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override { ULONG r = --refs; if (r == 0) delete this; return r; }
    HRESULT STDMETHODCALLTYPE Next(ULONG c, FORMATETC* rg, ULONG* fetched) override {
        ULONG got = 0;
        while (got < c && pos < 1) { rg[got++] = item; ++pos; }
        if (fetched) *fetched = got;
        return got == c ? S_OK : S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE Reset() override { pos = 0; return S_OK; }
    HRESULT STDMETHODCALLTYPE Clone(IEnumFORMATETC** pp) override {
        if (!pp) return E_INVALIDARG;
        OwnFormatEnum* e = new OwnFormatEnum(item);
        e->pos = pos;
        *pp = e;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Skip(ULONG c) override { pos += c; return pos <= 1 ? S_OK : S_FALSE; }
};

HRESULT SrDataObject::EnumFormatEtc(DWORD dir, IEnumFORMATETC** ppEnum) {
    if (!ppEnum || dir != DATADIR_GET) return E_INVALIDARG;
    *ppEnum = new OwnFormatEnum(fmt);
    return S_OK;
}

// ---------------------------------------------------------------- Zugquelle

HRESULT SrDropSource::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_INVALIDARG;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IDropSource)) {
        *ppv = static_cast<IDropSource*>(this);
        AddRef();
        return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
}
ULONG SrDropSource::AddRef() { return ++refs; }
ULONG SrDropSource::Release() { ULONG r = --refs; if (r == 0) delete this; return r; }

HRESULT SrDropSource::QueryContinueDrag(BOOL esc, DWORD keyState) {
    if (esc) return DRAGDROP_ACTION_CANCEL;
    if (!(keyState & (MK_LBUTTON | MK_RBUTTON))) return DRAGDROP_ACTION_DROP;
    return DRAGDROP_ACTION_NONE;
}

// ---------------------------------------------------------------- Payload auslesen

static bool ReadOwnPayload(IDataObject* data, std::vector<unsigned long>& pidsOut) {
    if (!data) return false;
    FORMATETC fe = { SrDataObject::ClipFormat(), nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
    STGMEDIUM med = {};
    if (FAILED(data->QueryGetData(&fe))) return false;
    if (FAILED(data->GetData(&fe, &med))) return false;
    bool ok = false;
    if (med.tymed == TYMED_HGLOBAL && med.hGlobal) {
        const wchar_t* txt = static_cast<const wchar_t*>(GlobalLock(med.hGlobal));
        pidsOut = UnpackPids(txt);
        if (txt) GlobalUnlock(med.hGlobal);
        ok = !pidsOut.empty();
    }
    ReleaseStgMedium(&med);
    return ok;
}

static bool ReadFilePayload(IDataObject* data, std::vector<std::wstring>& filesOut) {
    if (!data) return false;
    FORMATETC fe = { CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
    STGMEDIUM med = {};
    if (FAILED(data->QueryGetData(&fe))) return false;
    if (FAILED(data->GetData(&fe, &med))) return false;
    bool ok = false;
    if (med.tymed == TYMED_HGLOBAL && med.hGlobal) {
        HDROP hdrop = static_cast<HDROP>(med.hGlobal);
        UINT n = DragQueryFileW(hdrop, 0xFFFFFFFFu, nullptr, 0);
        std::vector<wchar_t> buf(MAX_PATH * 4);
        for (UINT i = 0; i < n; ++i) {
            if (DragQueryFileW(hdrop, i, buf.data(), static_cast<UINT>(buf.size())) > 0) {
                filesOut.push_back(buf.data());
            }
        }
        ok = !filesOut.empty();
    }
    ReleaseStgMedium(&med);
    return ok;
}

// ---------------------------------------------------------------- Ablageziel

SrDropTarget::SrDropTarget(App* a, HWND h, bool asColumn, Zone z)
    : app(a), hwnd(h), columnMode(asColumn), zone(z) {}

HRESULT SrDropTarget::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_INVALIDARG;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IDropTarget)) {
        *ppv = static_cast<IDropTarget*>(this);
        AddRef();
        return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
}
ULONG SrDropTarget::AddRef() { return ++refs; }
ULONG SrDropTarget::Release() { ULONG r = --refs; if (r == 0) delete this; return r; }

static POINT ToClient(HWND hwnd, POINTL ptScreen) {
    POINT pt;
    pt.x = ptScreen.x;
    pt.y = ptScreen.y;
    ScreenToClient(hwnd, &pt);
    return pt;
}

// Eintritt: nur dann ein Ziel anzeigen, wenn wir mit dem Inhalt etwas anfangen können.
HRESULT SrDropTarget::DragEnter(IDataObject* data, DWORD keyState, POINTL pt, DWORD* pdwEffect) {
    if (!pdwEffect) return E_INVALIDARG;
    *pdwEffect = DROPEFFECT_NONE;
    if (!app) return S_OK;

    std::vector<unsigned long> pids;
    std::vector<std::wstring> files;
    const bool own = ReadOwnPayload(data, pids);
    const bool file = !own && ReadFilePayload(data, files);
    if (!own && !file) return S_OK;
    return DragOver(keyState, pt, pdwEffect);
}

HRESULT SrDropTarget::DragOver(DWORD, POINTL ptScreen, DWORD* pdwEffect) {
    if (!pdwEffect || !app) return E_INVALIDARG;
    Zone target = zone;
    if (columnMode) {
        POINT pt = ToClient(hwnd, ptScreen);
        target = ZoneForColumn(app->ColumnAtPoint(pt));
        // Am Rand der Tabelle weiterrollen, damit auch lange Listen erreichbar bleiben.
        RECT rc;
        GetClientRect(hwnd, &rc);
        if (pt.y >= 0 && pt.y < 24) SendMessageW(hwnd, WM_VSCROLL, SB_LINEUP, 0);
        else if (pt.y <= rc.bottom && pt.y > rc.bottom - 24) SendMessageW(hwnd, WM_VSCROLL, SB_LINEDOWN, 0);
    }
    if (target == Zone::None) {
        app->SetHover(Zone::None, -1);
        *pdwEffect = DROPEFFECT_NONE;
        return S_OK;
    }
    app->SetHover(target, ColumnForZone(target));
    // Ein eigener Zug verschiebt, von außen kommt eine Datei als Kopie.
    *pdwEffect = app->dragging ? DROPEFFECT_MOVE : DROPEFFECT_COPY;
    return S_OK;
}

HRESULT SrDropTarget::DragLeave() {
    if (app) app->SetHover(Zone::None, -1);
    return S_OK;
}

HRESULT SrDropTarget::Drop(IDataObject* data, DWORD, POINTL ptScreen, DWORD* pdwEffect) {
    if (!pdwEffect) return E_INVALIDARG;
    *pdwEffect = DROPEFFECT_NONE;
    if (!app) return S_OK;

    Zone target = zone;
    if (columnMode) target = ZoneForColumn(app->ColumnAtPoint(ToClient(hwnd, ptScreen)));
    app->FinishDragFeedback();

    if (target == Zone::None) {
        app->SetNote(Tr(L"Bitte genau auf die Spalte WLAN, LAN oder Standard ziehen - oder auf eine der drei Karten."), 8);
        return S_OK;
    }

    std::vector<unsigned long> pids;
    std::vector<std::wstring> files;
    if (ReadOwnPayload(data, pids)) {
        app->ApplyToRows(pids, target);
        *pdwEffect = DROPEFFECT_MOVE;
        return S_OK;
    }
    if (ReadFilePayload(data, files)) {
        app->ApplyToFileDrop(files, target);
        *pdwEffect = DROPEFFECT_COPY;
        return S_OK;
    }
    app->SetNote(L"Das kann RoutePerApp nicht zuordnen: bitte Programme aus der Liste oder .exe-Dateien aus dem Explorer ziehen.", 10);
    return S_OK;
}

// ---------------------------------------------------------------- Registrierung

void RegisterDropTargets(App* app) {
    if (!app) return;
    if (app->list) RegisterDragDrop(app->list, new SrDropTarget(app, app->list, true, Zone::None));
    for (int i = 1; i <= 3; ++i) {
        if (!app->cards[i]) continue;
        app->cardState[i].zone = static_cast<Zone>(i);
        app->cardState[i].hwnd = app->cards[i];
        RegisterDragDrop(app->cards[i], new SrDropTarget(app, app->cards[i], false, static_cast<Zone>(i)));
    }
}

void RevokeDropTargets(App* app) {
    if (!app) return;
    if (app->list) RevokeDragDrop(app->list);
    for (int i = 1; i <= 3; ++i) {
        if (app->cards[i]) RevokeDragDrop(app->cards[i]);
    }
}
