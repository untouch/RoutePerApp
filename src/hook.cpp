// RoutePerApp Hook-Bibliothek (hook.dll)
//
// Wird vom RoutePerApp in einen Zielprozess eingeschleust. Sie ersetzt die
// Winsock-Aufrufe des Prozesses über dessen Importtabellen (kein Inline-Hook)
// und setzt vor jeder neuen Verbindung die gewünschte Netzwerkschnittstelle
// (IP_UNICAST_IF / IPV6_UNICAST_IF). Neue Verbindungen des Prozesses laufen dann
// über den eingestellten Adapter; bereits bestehende Verbindungen bleiben unberührt.
//
// Grundsätze:
//  - Nichts darf den Zielprozess zum Absturz bringen: jeder Schritt ist
//    abgefangen, im Fehlerfall läuft der normale Originalaufruf weiter.
//  - Die Entscheidung steht in %PROGRAMDATA%\RoutePerApp\resolved.json und wird
//    neu eingelesen, sobald sich die Datei ändert. Eine Zuordnung wirkt dadurch
//    sofort für alle neuen Verbindungen, ohne erneute Injektion.
//  - Sockets, an die der Prozess selbst eine lokale Adresse gebunden hat, werden
//    absichtlich nicht verändert, ebenso wenig Loopback-Verkehr.

#define WIN32_LEAN_AND_MEAN
#include "core.hpp"

#include <mstcpip.h>

#ifndef IP_UNICAST_IF
#define IP_UNICAST_IF 31
#endif
#ifndef IPV6_UNICAST_IF
#define IPV6_UNICAST_IF 31
#endif
#ifndef SO_DOMAIN
#define SO_DOMAIN 48
#endif

namespace srhook {

using namespace sr;      // Kernfunktionen und Typen aus core.hpp

// ---------------------------------------------------------------- Signaturen

typedef int (PASCAL *FnConnect)(SOCKET, const sockaddr*, int);
typedef int (PASCAL *FnWSAConnect)(SOCKET, const sockaddr*, int, LPWSABUF, LPWSABUF, LPQOS, LPQOS);
typedef int (PASCAL *FnSend)(SOCKET, const char*, int, int);
typedef int (PASCAL *FnSendTo)(SOCKET, const char*, int, int, const sockaddr*, int);
typedef int (PASCAL *FnSendMsg)(SOCKET, const WSAMSG*, int);
typedef int (PASCAL *FnWSASend)(SOCKET, LPWSABUF, DWORD, LPDWORD, DWORD, LPWSAOVERLAPPED, LPWSAOVERLAPPED_COMPLETION_ROUTINE);
typedef int (PASCAL *FnWSASendTo)(SOCKET, LPWSABUF, DWORD, LPDWORD, DWORD, const sockaddr*, int, LPWSAOVERLAPPED, LPWSAOVERLAPPED_COMPLETION_ROUTINE);
typedef int (PASCAL *FnWSASendMsg)(SOCKET, LPWSAMSG, DWORD, LPDWORD, LPWSAOVERLAPPED, LPWSAOVERLAPPED_COMPLETION_ROUTINE);
typedef int (PASCAL *FnCloseSocket)(SOCKET);

enum HookSlot {
    H_CONNECT = 0, H_WSACONNECT, H_SEND, H_SENDTO, H_SENDMSG,
    H_WSASEND, H_WSASENDTO, H_WSASENDMSG, H_CLOSESOCKET, H_COUNT
};

struct HookDef {
    const char* importName;
    void* replacement;
};

struct SockRec {
    SOCKET s;
    bool used;
    bool checkedBind;
    bool skip;                  // Socket ist fest gebunden -> nichts einstellen
    unsigned long long gen;     // bereits gesetzter Entscheidungsstand
};

// ---------------------------------------------------------------- Zustand

static void* g_real[H_COUNT] = {};
static SockRec g_socks[512] = {};

static Paths g_paths;
static JsonPtr g_doc;                           // letzter guter resolved.json-Stand
static unsigned long long g_generation = 1;     // steigt bei jedem Neu-Load
static unsigned long long gFileTime = 0;
static unsigned long long gFileSize = 0;
static unsigned long g_ifIndex = 0;             // 0 = nichts einstellen

static std::wstring g_imageLower;
static std::wstring g_pathLower;

static HMODULE g_self = nullptr;
static HMODULE g_ws2 = nullptr;
static bool g_started = false;

static SRWLOCK g_docLock = SRWLOCK_INIT;
static SRWLOCK g_sockLock = SRWLOCK_INIT;
static INIT_ONCE g_once = INIT_ONCE_STATIC_INIT;
static HANDLE g_stop = nullptr;             // beendet den Überwachungs-Thread

// Zählern für Diagnose und Oberfläche: wie viel wurde tatsächlich umgehängt?
static long g_patchedThunks = 0;            // Importeinträge, die auf uns zeigen
static long g_patchedModules = 0;           // Module mit mindestens einem Eintrag
static long g_slotPatched[H_COUNT] = {};    // je Abfangstelle: Anzahl der Einträge
static long g_unmatched = 0;                // Einträge in ws2_32, die wir nicht kannten
static long g_bindSets = 0;                 // erfolgreich gesetzte Schnittstellen
static long g_bindFails = 0;                // Fehlversuche beim Setzen
static long g_lastBindErr = 0;              // letzte Winsock-Fehlernummer dabei

// ---------------------------------------------------------------- Helfer

static std::wstring LowerBaseName(const std::wstring& path) {
    size_t pos = path.find_last_of(L"\\/");
    return ToLowerW(pos == std::wstring::npos ? path : path.substr(pos + 1));
}

static bool IsLoopback(const sockaddr* a) {
    if (!a) return false;
    if (a->sa_family == AF_INET) {
        unsigned long host = ntohl(reinterpret_cast<const sockaddr_in*>(a)->sin_addr.s_addr);
        return (host & 0xFF000000UL) == 0x7F000000UL;            // 127.0.0.0/8
    }
    if (a->sa_family == AF_INET6) {
        static const unsigned char loop[16] = { 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1 };
        const in6_addr* v6 = &reinterpret_cast<const sockaddr_in6*>(a)->sin6_addr;
        return memcmp(v6, loop, 16) == 0;
    }
    return false;
}

static bool FileStamp(const std::wstring& file, unsigned long long& writeTime, unsigned long long& size) {
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (!GetFileAttributesExW(file.c_str(), GetFileExInfoStandard, &fad)) return false;
    writeTime = (static_cast<unsigned long long>(fad.ftLastWriteTime.dwHighDateTime) << 32) |
                 static_cast<unsigned long long>(fad.ftLastWriteTime.dwLowDateTime);
    size = (static_cast<unsigned long long>(fad.nFileSizeHigh) << 32) |
            static_cast<unsigned long long>(fad.nFileSizeLow);
    return true;
}

// liest resolved.json neu, falls sich die Datei verändert hat
static void RefreshDecision() {
    unsigned long long mt = 0, sz = 0;
    if (!FileStamp(g_paths.resolvedFile, mt, sz)) return;
    if (mt == gFileTime && sz == gFileSize) return;

    std::wstring text, err;
    if (!ReadTextFile(g_paths.resolvedFile, text, err)) return;
    JsonPtr doc;
    if (!ParseResolvedDoc(text, doc)) { gFileTime = mt; gFileSize = sz; return; }  // kaputt: alter Stand bleibt

    ResolvedLookupResult lr = ResolvedLookup(doc, g_imageLower, g_pathLower);
    unsigned long idx = 0;
    if (lr.found && lr.mode != RouteMode::None && !lr.guid.empty()) ResolvedIfIndex(doc, lr.guid, idx);

    AcquireSRWLockExclusive(&g_docLock);
    g_doc = doc;
    g_ifIndex = idx;
    ++g_generation;
    gFileTime = mt;
    gFileSize = sz;
    ReleaseSRWLockExclusive(&g_docLock);
}

static unsigned long CurrentIfIndex() {
    AcquireSRWLockShared(&g_docLock);
    unsigned long v = g_ifIndex;
    unsigned long long gen = g_generation;
    ReleaseSRWLockShared(&g_docLock);
    (void)gen;
    return v;
}

static unsigned long long CurrentGeneration() {
    AcquireSRWLockShared(&g_docLock);
    unsigned long long v = g_generation;
    ReleaseSRWLockShared(&g_docLock);
    return v;
}

// Hat der Prozess an diesen Socket schon eine eigene lokale Adresse gebunden?
static bool SocketIsBound(SOCKET s) {
    sockaddr_storage ss;
    ZeroMemory(&ss, sizeof(ss));
    int len = static_cast<int>(sizeof(ss));
    if (getsockname(s, reinterpret_cast<sockaddr*>(&ss), &len) != 0) return false;   // noch nicht gebunden
    if (ss.ss_family == AF_INET) {
        return reinterpret_cast<sockaddr_in*>(&ss)->sin_addr.s_addr != 0;
    }
    if (ss.ss_family == AF_INET6) {
        static const unsigned char any[16] = { 0 };
        return memcmp(&reinterpret_cast<sockaddr_in6*>(&ss)->sin6_addr, any, 16) != 0;
    }
    return false;
}

static size_t SlotHash(SOCKET s) {
    size_t v = static_cast<size_t>(static_cast<unsigned long long>(static_cast<uintptr_t>(s)));
    v *= 2654435761u;
    return v % (sizeof(g_socks) / sizeof(g_socks[0]));
}

// Slot für einen Socket finden (create = anlegen falls nötig). Lock muss gehalten werden.
static SockRec* FindSlot(SOCKET s, bool create) {
    size_t h = SlotHash(s);
    const size_t n = sizeof(g_socks) / sizeof(g_socks[0]);
    SockRec* rec = nullptr;
    for (size_t i = 0; i < 8; ++i) {
        SockRec& r = g_socks[(h + i) % n];
        if (r.used && r.s == s) return &r;
        if (!r.used && !rec) rec = &r;
    }
    if (!create) return nullptr;
    if (!rec) rec = &g_socks[h];            // Tabelle voll: ältester Platz wird überschrieben
    rec->used = true;
    rec->s = s;
    rec->gen = 0;
    rec->checkedBind = false;
    rec->skip = false;
    return rec;
}

// entscheidet, ob für diesen Socket die Schnittstelle neu gesetzt werden muss
static bool NeedsApply(SOCKET s, short family) {
    unsigned long long gen = CurrentGeneration();
    bool result = false;
    AcquireSRWLockExclusive(&g_sockLock);
    SockRec* rec = FindSlot(s, true);
    if (rec) {
        if (!rec->checkedBind) {
            rec->checkedBind = true;
            rec->skip = SocketIsBound(s);
        }
        if (!rec->skip && rec->gen != gen) { rec->gen = gen; result = true; }
    }
    ReleaseSRWLockExclusive(&g_sockLock);
    (void)family;
    return result;
}

// Setzt die Schnittstelle für den Versand.
// Wichtig: IP_UNICAST_IF will die Interface-Index in Netzwerk-Byte-Reihenfolge
// (der Wert wird wie eine Adresse im reservierten Block 0.x.x.x gelesen),
// IPV6_UNICAST_IF dagegen in Host-Reihenfolge. Beides wurde auf diesem Rechner
// nachgemessen: vertauschte Reihenfolge liefert WSAEADDRNOTAVAIL.
static bool ApplyToSocket(SOCKET s, short family, unsigned long idx) {
    short fam = family;
    if (fam != AF_INET && fam != AF_INET6) {
        int dom = 0, len = sizeof(dom);
        if (getsockopt(s, SOL_SOCKET, SO_DOMAIN, reinterpret_cast<char*>(&dom), &len) == 0) fam = static_cast<short>(dom);
    }
    unsigned long v4 = ntohl(idx);
    unsigned long v6 = idx;
    char* p4 = reinterpret_cast<char*>(&v4);
    char* p6 = reinterpret_cast<char*>(&v6);
    const int len4 = static_cast<int>(sizeof(v4));
    const int len6 = static_cast<int>(sizeof(v6));
    if (fam == AF_INET6) {
        if (setsockopt(s, IPPROTO_IPV6, IPV6_UNICAST_IF, p6, len6) == 0) { InterlockedIncrement(&g_bindSets); return true; }
        InterlockedIncrement(&g_bindFails);
        g_lastBindErr = WSAGetLastError();
        return false;
    }
    if (fam == AF_INET) {
        if (setsockopt(s, IPPROTO_IP, IP_UNICAST_IF, p4, len4) == 0) { InterlockedIncrement(&g_bindSets); return true; }
        InterlockedIncrement(&g_bindFails);
        g_lastBindErr = WSAGetLastError();
        return false;
    }
    bool a = setsockopt(s, IPPROTO_IP, IP_UNICAST_IF, p4, len4) == 0;
    if (!a) { g_lastBindErr = WSAGetLastError(); }
    bool b = setsockopt(s, IPPROTO_IPV6, IPV6_UNICAST_IF, p6, len6) == 0;
    if (!b) { g_lastBindErr = WSAGetLastError(); }
    if (a && b) { InterlockedIncrement(&g_bindSets); return true; }
    InterlockedIncrement(&g_bindFails);
    return false;
}

// Nach einem Fehlversuch den Vermerk zurücknehmen, damit beim nächsten
// Verbindungsaufbau erneut gesetzt wird.
static void AllowRetry(SOCKET s) {
    AcquireSRWLockExclusive(&g_sockLock);
    SockRec* rec = FindSlot(s, true);
    if (rec) rec->gen = 0;
    ReleaseSRWLockExclusive(&g_sockLock);
}

// Ein geschlossener Socket ist kein Socket: der Wert kann von Windows wieder
// vergeben werden, und ein neuer Socket mit demselben Wert braucht eine neue
// Bindung. Deshalb den Vermerk vollständig entfernen.
static void ForgetSocket(SOCKET s) {
    AcquireSRWLockExclusive(&g_sockLock);
    SockRec* rec = FindSlot(s, false);
    if (rec) {
        rec->used = false;
        rec->s = 0;
        rec->gen = 0;
        rec->checkedBind = false;
        rec->skip = false;
    }
    ReleaseSRWLockExclusive(&g_sockLock);
}

// Vermerk setzen, damit der Versand-Pfad nicht erneut setzt.
static void MarkApplied(SOCKET s) {
    unsigned long long gen = CurrentGeneration();
    AcquireSRWLockExclusive(&g_sockLock);
    SockRec* rec = FindSlot(s, true);
    if (rec) { rec->checkedBind = true; rec->skip = false; rec->gen = gen; }
    ReleaseSRWLockExclusive(&g_sockLock);
}

// Verbindungsaufbau: hier wird bewusst jedes Mal gesetzt (connect ist selten),
// damit ein wieder verwendeter Socket-Wert nie einen alten Vermerk erbt.
// Keine C++-Objekte in dieser Funktion.
static void MaybeApplyOnConnect(SOCKET s, const sockaddr* dest) {
    if (IsLoopback(dest)) return;
    unsigned long idx = CurrentIfIndex();
    if (!idx) return;
    short fam = dest ? static_cast<short>(dest->sa_family) : 0;
    if (SocketIsBound(s)) {
        AcquireSRWLockExclusive(&g_sockLock);
        SockRec* rec = FindSlot(s, true);
        if (rec) { rec->checkedBind = true; rec->skip = true; rec->gen = 0; }
        ReleaseSRWLockExclusive(&g_sockLock);
        return;
    }
    if (ApplyToSocket(s, fam, idx)) MarkApplied(s);
    else AllowRetry(s);
}

// Versand-Pfad: nur setzen, wenn für diese Entscheidungsstufe noch nichts geschah.
// Keine C++-Objekte in dieser Funktion.
static void MaybeApply(SOCKET s, const sockaddr* dest) {
    if (IsLoopback(dest)) return;
    unsigned long idx = CurrentIfIndex();
    if (!idx) return;
    short fam = dest ? static_cast<short>(dest->sa_family) : 0;
    if (!NeedsApply(s, fam)) return;
    if (!ApplyToSocket(s, fam, idx)) AllowRetry(s);
}

// ---------------------------------------------------------------- Hooks

static int PASCAL HookConnect(SOCKET s, const sockaddr* name, int len) {
    __try { MaybeApplyOnConnect(s, name); } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return g_real[H_CONNECT] ? reinterpret_cast<FnConnect>(g_real[H_CONNECT])(s, name, len) : SOCKET_ERROR;
}

static int PASCAL HookWSAConnect(SOCKET s, const sockaddr* name, int len, LPWSABUF caller, LPWSABUF callee,
                                 LPQOS groupQos, LPQOS sockQos) {
    __try { MaybeApplyOnConnect(s, name); } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return g_real[H_WSACONNECT] ? reinterpret_cast<FnWSAConnect>(g_real[H_WSACONNECT])(s, name, len, caller, callee, groupQos, sockQos) : SOCKET_ERROR;
}

static int PASCAL HookSend(SOCKET s, const char* buf, int len, int flags) {
    __try { MaybeApply(s, nullptr); } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return g_real[H_SEND] ? reinterpret_cast<FnSend>(g_real[H_SEND])(s, buf, len, flags) : SOCKET_ERROR;
}

static int PASCAL HookSendTo(SOCKET s, const char* buf, int len, int flags, const sockaddr* to, int toLen) {
    __try { MaybeApply(s, to); } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return g_real[H_SENDTO] ? reinterpret_cast<FnSendTo>(g_real[H_SENDTO])(s, buf, len, flags, to, toLen) : SOCKET_ERROR;
}

static int PASCAL HookSendMsg(SOCKET s, const WSAMSG* msg, int flags) {
    __try { MaybeApply(s, msg ? reinterpret_cast<const sockaddr*>(msg->name) : nullptr); } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return g_real[H_SENDMSG] ? reinterpret_cast<FnSendMsg>(g_real[H_SENDMSG])(s, msg, flags) : SOCKET_ERROR;
}

static int PASCAL HookWSASend(SOCKET s, LPWSABUF bufs, DWORD count, LPDWORD sent, DWORD flags,
                              LPWSAOVERLAPPED ov, LPWSAOVERLAPPED_COMPLETION_ROUTINE cb) {
    __try { MaybeApply(s, nullptr); } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return g_real[H_WSASEND] ? reinterpret_cast<FnWSASend>(g_real[H_WSASEND])(s, bufs, count, sent, flags, ov, cb) : SOCKET_ERROR;
}

static int PASCAL HookWSASendTo(SOCKET s, LPWSABUF bufs, DWORD count, LPDWORD sent, DWORD flags,
                                const sockaddr* to, int toLen, LPWSAOVERLAPPED ov,
                                LPWSAOVERLAPPED_COMPLETION_ROUTINE cb) {
    __try { MaybeApply(s, to); } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return g_real[H_WSASENDTO] ? reinterpret_cast<FnWSASendTo>(g_real[H_WSASENDTO])(s, bufs, count, sent, flags, to, toLen, ov, cb) : SOCKET_ERROR;
}

static int PASCAL HookWSASendMsg(SOCKET s, LPWSAMSG msg, DWORD flags, LPDWORD sent, LPWSAOVERLAPPED ov,
                                 LPWSAOVERLAPPED_COMPLETION_ROUTINE cb) {
    __try { MaybeApply(s, msg ? reinterpret_cast<const sockaddr*>(msg->name) : nullptr); } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return g_real[H_WSASENDMSG] ? reinterpret_cast<FnWSASendMsg>(g_real[H_WSASENDMSG])(s, msg, flags, sent, ov, cb) : SOCKET_ERROR;
}

static int PASCAL HookCloseSocket(SOCKET s) {
    int r = g_real[H_CLOSESOCKET] ? reinterpret_cast<FnCloseSocket>(g_real[H_CLOSESOCKET])(s) : SOCKET_ERROR;
    __try { ForgetSocket(s); } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return r;
}

static const HookDef g_defs[H_COUNT] = {
    { "connect",     reinterpret_cast<void*>(&HookConnect) },
    { "WSAConnect",  reinterpret_cast<void*>(&HookWSAConnect) },
    { "send",        reinterpret_cast<void*>(&HookSend) },
    { "sendto",      reinterpret_cast<void*>(&HookSendTo) },
    { "sendmsg",     reinterpret_cast<void*>(&HookSendMsg) },
    { "WSASend",     reinterpret_cast<void*>(&HookWSASend) },
    { "WSASendTo",   reinterpret_cast<void*>(&HookWSASendTo) },
    { "WSASendMsg",  reinterpret_cast<void*>(&HookWSASendMsg) },
    { "closesocket", reinterpret_cast<void*>(&HookCloseSocket) },
};

// ---------------------------------------------------------------- Importtabellen

static HMODULE SelfModule() {
    HMODULE h = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&g_once), &h);
    return h;
}

static bool InModule(void* p, HMODULE mod, ULONG size) {
    if (!mod || size == 0) return false;
    BYTE* v = reinterpret_cast<BYTE*>(p);
    BYTE* lo = reinterpret_cast<BYTE*>(mod);
    return v >= lo && v < lo + size;
}

static ULONG ModuleSize(HMODULE mod) {
    MODULEINFO mi;
    if (GetModuleInformation(GetCurrentProcess(), mod, &mi, sizeof(mi))) return mi.SizeOfImage;
    return 0;
}

// Zu welcherAdresse in ws2_32 gehört welcher Slot? Über die Adresse statt über den
// Namen: Übersetzt werden auch Importe, die nur über eine Ordnungsnummer gehen -
// ws2_32.lib macht das bei Winsock-Funktionen regelmäßig.
static int SlotForName(const char* name) {
    for (int i = 0; i < H_COUNT; ++i) if (_stricmp(name, g_defs[i].importName) == 0) return i;
    return -1;
}

// Zu welcher Adresse in ws2_32 gehört welcher Slot? Über die Adresse statt über den
// Namen: so werden auch Importe erfasst, die nur über eine Ordnungsnummer laufen -
// ws2_32.lib macht das bei Winsock-Funktionen regelmäßig.
static int SlotForAddress(const void* addr) {
    for (int i = 0; i < H_COUNT; ++i) if (g_real[i] && g_real[i] == addr) return i;
    return -1;
}

static bool PatchModuleIAT(HMODULE mod) {
    if (!mod || !g_ws2) return false;
    BYTE* base = reinterpret_cast<BYTE*>(mod);
    ULONG modSize = ModuleSize(mod);
    if (modSize == 0) return false;
    PIMAGE_DOS_HEADER dos = reinterpret_cast<PIMAGE_DOS_HEADER>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    if (dos->e_lfanew <= 0 || dos->e_lfanew + (LONG)sizeof(IMAGE_NT_HEADERS) > (LONG)modSize) return false;
    PIMAGE_NT_HEADERS nt = reinterpret_cast<PIMAGE_NT_HEADERS>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return false;
    IMAGE_DATA_DIRECTORY dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (dir.VirtualAddress == 0 || dir.Size == 0 || dir.VirtualAddress >= modSize) return false;
    const ULONG wsSize = ModuleSize(g_ws2);
    const ULONG selfSize = ModuleSize(g_self);
    PIMAGE_IMPORT_DESCRIPTOR desc = reinterpret_cast<PIMAGE_IMPORT_DESCRIPTOR>(base + dir.VirtualAddress);
    bool patched = false;
    for (int guard = 0; desc->Name && guard < 512; ++desc, ++guard) {
        if (dir.VirtualAddress >= modSize) break;
        const char* dllName = reinterpret_cast<const char*>(base + desc->Name);
        if (_stricmp(dllName, "ws2_32.dll") != 0) continue;
        if (!desc->FirstThunk || desc->FirstThunk >= modSize) continue;
        PIMAGE_THUNK_DATA iat = reinterpret_cast<PIMAGE_THUNK_DATA>(base + desc->FirstThunk);
        // Namenstabelle gibt es nur, wenn das Importverzeichnis nicht vorgebunden wurde.
        const bool haveNames = (desc->OriginalFirstThunk != 0 && desc->OriginalFirstThunk < modSize);
        PIMAGE_THUNK_DATA orig = haveNames ? reinterpret_cast<PIMAGE_THUNK_DATA>(base + desc->OriginalFirstThunk) : nullptr;
        for (int gi = 0; iat->u1.Function && gi < 4096; ++iat, ++gi, ++orig) {
            void* current = reinterpret_cast<void*>(iat->u1.Function);
            if (InModule(current, g_self, selfSize)) continue;      // bereits gehookt
            if (!InModule(current, g_ws2, wsSize)) continue;        // zeigt nicht in ws2_32
            int slot = SlotForAddress(current);
            if (slot < 0 && haveNames
                && !IMAGE_SNAP_BY_ORDINAL64(orig->u1.Ordinal)
                && orig->u1.AddressOfData < modSize) {
                // Adressvergleich reicht nicht immer (weitere Export-Schicht, Provider-Kette):
                // hilfsweise über den importierten Namen entscheiden.
                PIMAGE_IMPORT_BY_NAME imp = reinterpret_cast<PIMAGE_IMPORT_BY_NAME>(base + orig->u1.AddressOfData);
                slot = SlotForName(reinterpret_cast<const char*>(imp->Name));
            }
            if (slot < 0) { InterlockedIncrement(&g_unmatched); continue; }
            DWORD old = 0;
            if (VirtualProtect(&iat->u1.Function, sizeof(iat->u1.Function), PAGE_READWRITE, &old)) {
                iat->u1.Function = reinterpret_cast<ULONG_PTR>(g_defs[slot].replacement);
                DWORD back = 0;
                VirtualProtect(&iat->u1.Function, sizeof(iat->u1.Function), old, &back);
                InterlockedIncrement(&g_patchedThunks);
                InterlockedIncrement(&g_slotPatched[slot]);
                patched = true;
            }
        }
    }
    if (patched) InterlockedIncrement(&g_patchedModules);
    return patched;
}

static void PatchAllModules() {
    HMODULE mods[512];
    DWORD needed = 0;
    if (!EnumProcessModulesEx(GetCurrentProcess(), mods, sizeof(mods), &needed, LIST_MODULES_ALL)) return;
    size_t count = needed / sizeof(HMODULE);
    const size_t cap = sizeof(mods) / sizeof(mods[0]);
    if (count > cap) count = cap;
    for (size_t i = 0; i < count; ++i) {
        HMODULE m = mods[i];
        if (!m || m == g_self) continue;
        if (m == g_ws2) continue;
        PatchModuleIAT(m);
    }
}

// ---------------------------------------------------------------- Start

// Das Patchen läuft in einer eigenen Funktion ohne C++-Objekte, damit der
// SEH-Rahmen vom Compiler akzeptiert wird und ein fehlerhafter Tabellenkopf
// den Zielprozess nicht treffen kann.
static bool TryPatchAll() {
    __try {
        PatchAllModules();
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static DWORD WINAPI WatcherThread(LPVOID) {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_IDLE);
    size_t tick = 0;
    // wartet 400 ms und bricht ab, wenn SrtShutdown aufgerufen wurde
    while (WaitForSingleObject(g_stop, 400) == WAIT_TIMEOUT) {
        __try { RefreshDecision(); } __except (EXCEPTION_EXECUTE_HANDLER) {}
        ++tick;
        if (tick % 4 == 0) TryPatchAll();          // neue DLLs im Zielprozess erwischen
    }
    return 0;
}

static void StartupOnce() {
    g_paths = DefaultPaths();
    // Tests und Sondersituationen: Umgebungsvariable SRTROUTER_DATA lenkt den Datenordner.
    wchar_t envDir[MAX_PATH * 4] = L"";
    DWORD nenv = GetEnvironmentVariableW(L"SRTROUTER_DATA", envDir, MAX_PATH * 4 - 1);
    if (nenv > 0 && nenv < MAX_PATH * 4 - 1) g_paths = MakePaths(std::wstring(envDir, nenv));

    wchar_t file[MAX_PATH * 2] = L"";
    DWORD n = GetModuleFileNameW(nullptr, file, MAX_PATH * 2 - 1);
    std::wstring full = (n == 0) ? std::wstring() : std::wstring(file, n);
    g_pathLower = ToLowerW(full);
    g_imageLower = LowerBaseName(full);

    g_self = SelfModule();
    g_ws2 = GetModuleHandleW(L"ws2_32.dll");
    if (!g_ws2) g_ws2 = LoadLibraryW(L"ws2_32.dll");
    if (!g_ws2) return;
    for (int i = 0; i < H_COUNT; ++i) g_real[i] = GetProcAddress(g_ws2, g_defs[i].importName);

    RefreshDecision();
    TryPatchAll();
    if (!g_stop) g_stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (g_stop) {
        HANDLE th = CreateThread(nullptr, 0, WatcherThread, nullptr, 0, nullptr);
        if (th) CloseHandle(th);
    }
    g_started = true;
}

static BOOL CALLBACK InitOnceCb(PINIT_ONCE, PVOID, PVOID*) {
    StartupOnce();
    return TRUE;
}

static void EnsureStarted() {
    InitOnceExecuteOnce(&g_once, InitOnceCb, nullptr, nullptr);
}

} // namespace srhook

// ---------------------------------------------------------------- Einstiegspunkte

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);       // keine Arbeit unter der Loader-Sperre
    }
    return TRUE;
}

extern "C" {

// Vom Injektor als zweiter Remote-Thread aufgerufen: aktiviert die Hooks.
__declspec(dllexport) DWORD WINAPI SrtStartup(void) {
    srhook::EnsureStarted();
    return srhook::g_started ? 1u : 0u;
}

// Diagnose für den RoutePerApp: aktiver Interface-Index dieses Prozesses (0 = keine Bindung)
__declspec(dllexport) DWORD WINAPI SrtIfIndex(void) {
    srhook::EnsureStarted();
    return static_cast<DWORD>(srhook::CurrentIfIndex());
}

// Diagnose: Fassung der Hook-Schnittstelle
__declspec(dllexport) DWORD WINAPI SrtApiVersion(void) {
    return 1u;
}

// Diagnose: wie viele Importeinträge wurden in diesem Prozess umgehängt?
__declspec(dllexport) DWORD WINAPI SrtPatchCount(void) {
    srhook::EnsureStarted();
    return static_cast<DWORD>(srhook::g_patchedThunks);
}

// Diagnose: welche Abfangstellen sind aktiv? Bit i entspricht Stelle i
// (0 connect, 1 WSAConnect, 2 send, 3 sendto, 4 sendmsg, 5 WSASend, 6 WSASendTo,
// 7 WSASendMsg, 8 closesocket).
__declspec(dllexport) DWORD WINAPI SrtPatchMask(void) {
    srhook::EnsureStarted();
    DWORD mask = 0;
    for (int i = 0; i < srhook::H_COUNT; ++i) if (srhook::g_slotPatched[i] > 0) mask |= (1u << i);
    return mask;
}

// Diagnose: Importe in ws2_32, die keiner Abfangstelle zugeordnet werden konnten.
__declspec(dllexport) DWORD WINAPI SrtUnmatchedCount(void) {
    return static_cast<DWORD>(srhook::g_unmatched);
}

// Diagnose: wie oft wurde eine Schnittstelle erfolgreich / erfolglos gesetzt?
__declspec(dllexport) DWORD WINAPI SrtBindCount(void) {
    return static_cast<DWORD>(srhook::g_bindSets);
}

__declspec(dllexport) DWORD WINAPI SrtBindFailCount(void) {
    return static_cast<DWORD>(srhook::g_bindFails);
}

// Beendet den Überwachungs-Thread. Die Importtabellen bleiben dabei unverändert,
// ein Entladen der Bibliothek ist nach dem ersten Start nicht mehr vorgesehen.
__declspec(dllexport) DWORD WINAPI SrtShutdown(void) {
    if (srhook::g_stop) SetEvent(srhook::g_stop);
    return 1u;
}

} // extern "C"
