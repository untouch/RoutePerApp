// Integrations-Tests: Hook-Bibliothek, Injektion und echte Adapterbindung.
// Der Kern wird hier wie im richtigen Programm nur eingebunden (nicht kopiert),
// damit auch das Verlinken mit hook.dll geprüft wird.
#include "../src/core.hpp"
#include "harness.hpp"

#include <windows.h>
#include <ws2tcpip.h>

#include <string>
#include <vector>

using namespace sr;

#pragma comment(lib, "ws2_32.lib")

// ---------------------------------------------------------------- Umgebung

static std::wstring ExeDir() {
    wchar_t buf[MAX_PATH * 4];
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH * 4);
    if (!n || n >= MAX_PATH * 4 - 1) return L".";
    std::wstring full(buf, n);
    size_t slash = full.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return L".";
    return full.substr(0, slash);
}

static std::wstring SelfImageLower() {
    wchar_t buf[MAX_PATH * 4];
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH * 4);
    std::wstring full = n ? std::wstring(buf, n) : std::wstring();
    size_t slash = full.find_last_of(L"\\/");
    return ToLowerW(slash == std::wstring::npos ? full : full.substr(slash + 1));
}

static std::wstring MakeTestDir(const wchar_t* name) {
    wchar_t base[MAX_PATH];
    GetTempPathW(MAX_PATH, base);
    std::wstring dir = std::wstring(base) + L"routeperapp-" + name + L"-" + std::to_wstring(GetCurrentProcessId());
    std::wstring err;
    EnsureDir(dir, err);
    return dir;
}

static AdapterInfo MakeAdapter(const wchar_t* alias, const wchar_t* guid, AdapterKind kind,
                               unsigned long idx, const wchar_t* ip, const wchar_t* gw, long long metric) {
    AdapterInfo a;
    a.alias = alias;
    a.guid = guid;
    a.kind = kind;
    a.ifIndex = idx;
    a.luid = idx;
    a.ipv4 = ip;
    a.gateway = gw;
    a.ifMetric = metric;
    a.isUp = true;
    return a;
}

// Ergebniszeilen des Zielprozesses auswerten.
static bool LineValue(const std::wstring& text, int round, const wchar_t* key, std::wstring& out) {
    std::wstring head = L"round=" + std::to_wstring(round);
    size_t at = text.find(head);
    if (at == std::wstring::npos) return false;
    size_t eol = text.find(L'\n', at);
    std::wstring line = text.substr(at, eol == std::wstring::npos ? std::wstring::npos : eol - at);
    std::wstring needle = L" ";
    needle += key;
    needle += L"=";
    size_t k = line.find(needle);
    if (k == std::wstring::npos) return false;
    k += needle.size();
    size_t e = line.find(L' ', k);
    out = line.substr(k, e == std::wstring::npos ? std::wstring::npos : e - k);
    return true;
}

// Warten bis die Ergebnisdatei einen bestimmten Text enthält.
static bool WaitForResult(const std::wstring& file, const wchar_t* needle, int attempts, std::wstring& content) {
    for (int i = 0; i < attempts; ++i) {
        std::wstring err;
        if (ReadTextFile(file, content, err) && content.find(needle) != std::wstring::npos) return true;
        Sleep(200);
    }
    return false;
}

// Warten bis die Hook-Bibliothek im Zielprozess einen bestimmten Index meldet.
static bool WaitHookIndex(unsigned long pid, const std::wstring& dll, unsigned long want, int attempts) {
    for (int i = 0; i < attempts; ++i) {
        std::wstring err;
        unsigned long got = 0;
        if (QueryHookIfIndex(pid, dll, got, err) && got == want) return true;
        Sleep(120);
    }
    return false;
}

// Ein UDP-Socket verbinden (es wird kein Paket gesendet) und den gesetzten Index ablesen.
static unsigned long ProbeUdpIfIndex(const sockaddr_in& dest, SOCKET* keepOut) {
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) return 0xFFFFFFFFu;
    connect(s, reinterpret_cast<const sockaddr*>(&dest), sizeof(dest));
    unsigned long idx = 0;
    int len = sizeof(idx);
    getsockopt(s, IPPROTO_IP, IP_UNICAST_IF, reinterpret_cast<char*>(&idx), &len);
    if (keepOut) *keepOut = s; else closesocket(s);
    return idx;
}

static bool SendUdpOnce(SOCKET s, const sockaddr_in& dest) {
    static const char payload[4] = { 's', 'r', 't', '!' };
    int r = sendto(s, payload, 4, 0, reinterpret_cast<const sockaddr*>(&dest), sizeof(dest));
    if (r != SOCKET_ERROR) return true;
    // Die Adapter in diesem Abschnitt sind frei erfunden, ein Paket kann deshalb nicht
    // herauskommen. Gezählt wird der Versuch: der Hook muss den Socket beim Senden
    // umhängen, ankommen muss nichts. Ohne Route ist der Versand trotzdem vorbei.
    const int e = WSAGetLastError();
    return e == WSAEHOSTUNREACH || e == WSAENETUNREACH || e == WSAEHOSTDOWN || e == WSAENETDOWN;
}

static unsigned long ReadIfIndex(SOCKET s) {
    unsigned long idx = 0;
    int len = sizeof(idx);
    getsockopt(s, IPPROTO_IP, IP_UNICAST_IF, reinterpret_cast<char*>(&idx), &len);
    return idx;
}

static sockaddr_in MakeV4(const wchar_t* text, unsigned short port) {
    sockaddr_in a;
    ZeroMemory(&a, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    std::string ascii;
    for (wchar_t c : std::wstring(text)) ascii.push_back(static_cast<char>(c));
    inet_pton(AF_INET, ascii.c_str(), &a.sin_addr);
    return a;
}

// ---------------------------------------------------------------- Vorbedingungen

static void TestCanInjectReasons() {
    Begin(L"Injektion: Vorbedingungen und Gründe");

    ProcInfo p;
    p.pid = 1234;
    p.image = L"chrome.exe";
    p.path = L"C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe";
    p.pathAccess = true;
    std::wstring why;

    CHECK_MSG(CanInjectProcess(p, true, true, why), L"normale Bedingung muss erlauben: " + why);
    CHECK_EQ_STR(why, L"");

    CHECK(!CanInjectProcess(p, false, true, why));
    CHECK_EQ_STR(why, L"RoutePerApp läuft ohne Administratorrechte");

    CHECK(!CanInjectProcess(p, true, false, why));
    CHECK_EQ_STR(why, L"hook.dll fehlt im Datenordner");

    ProcInfo sys = p; sys.pid = 4;
    CHECK(!CanInjectProcess(sys, true, true, why));
    CHECK_EQ_STR(why, L"Systemprozess wird nicht angefasst");

    ProcInfo self = p; self.image = L"RoutePerApp.EXE";
    CHECK(!CanInjectProcess(self, true, true, why));
    CHECK_EQ_STR(why, L"RoutePerApp bindet sich nicht selbst ein");

    ProcInfo guarded = p; guarded.pathAccess = false;
    CHECK(!CanInjectProcess(guarded, true, true, why));
    CHECK_EQ_STR(why, L"Prozessinformationen nicht lesbar (höhere Priorität)");

    EndSection();
}

static void TestErrorPaths() {
    Begin(L"Injektion: Fehlerpfade");

    const std::wstring hook = ExeDir() + L"\\hook.dll";
    const std::wstring missing = ExeDir() + L"\\gibt-es-nicht.dll";
    std::wstring err;

    CHECK_MSG(FileExists(hook), L"hook.dll fehlt im Buildordner: " + hook);

    // Nicht existierende DLL
    CHECK(!InjectDll(GetCurrentProcessId(), missing, err));
    CHECK_MSG(err.find(L"fehlt") != std::wstring::npos, L"Fehlermeldung unbrauchbar: " + err);

    unsigned long dummyIdx = 0;
    CHECK(!QueryHookIfIndex(GetCurrentProcessId(), missing, dummyIdx, err));

    // Unbekannter Prozess
    const unsigned long noPid = 0x7FFFFFFFu;
    bool loaded = true;
    CHECK(!IsModuleLoadedInProcess(noPid, L"hook.dll", loaded, err));
    CHECK(!loaded);

    CHECK(!InjectDll(noPid, hook, err));
    CHECK_MSG(!err.empty(), L"Fehlermeldung für toten Prozess fehlt");

    bool was = false;
    CHECK(!InjectAndActivate(noPid, hook, was, err));
    CHECK(!was);

    // DLL nicht im Prozess -> Fernaufruf muss sauber ablehnen
    unsigned long idx = 12345;
    CHECK(!QueryHookIfIndex(GetCurrentProcessId(), hook, idx, err));
    CHECK_EQ_INT(idx, 0);
    CHECK_MSG(!err.empty(), L"Fehlermeldung für nicht geladene DLL fehlt");
    wprintf(L"  Grund: %s\n", err.c_str());

    // Eigene PID darf nicht als "tot" abgelehnt werden, wenn die DLL geladen ist (siehe nächster Test)
    EndSection();
}

// ---------------------------------------------------------------- Hook im eigenen Prozess

static void TestHookInSelf() {
    Begin(L"Hook im eigenen Prozess: Entscheidung, Neu-Laden, Socket-Schutz");

    const std::wstring hook = ExeDir() + L"\\hook.dll";
    if (!FileExists(hook)) { Skip(L"hook.dll nicht gefunden"); EndSection(); return; }

    typedef DWORD(WINAPI* FnVoid)();
    const std::wstring dir = MakeTestDir(L"selfhook");
    SetEnvironmentVariableW(L"SRTROUTER_DATA", dir.c_str());

    // Regel: dieses Testprogramm -> WLAN-Rolle
    std::vector<AdapterInfo> ads = {
        MakeAdapter(L"WLAN 2", L"{11111111-1111-1111-1111-111111111111}", AdapterKind::Wlan, 19, L"192.168.1.171", L"192.168.1.1", 30),
        MakeAdapter(L"Main", L"{22222222-2222-2222-2222-222222222222}", AdapterKind::Lan, 10, L"192.168.178.132", L"192.168.178.1", 25)
    };
    Ruleset rs;
    Rule r;
    r.pattern = SelfImageLower();
    r.mode = RouteMode::Wlan;
    rs.AddTop(r);
    std::vector<std::wstring> problems;
    std::wstring err;
    CHECK_MSG(WriteResolved(MakePaths(dir), rs, ads, problems, err), L"resolved.json: " + err);

    HMODULE mod = LoadLibraryW(hook.c_str());
    CHECK_MSG(mod != nullptr, L"LoadLibrary hook.dll: " + LastErrText(GetLastError()));
    if (!mod) { EndSection(); return; }

    FnVoid startup = reinterpret_cast<FnVoid>(GetProcAddress(mod, "SrtStartup"));
    FnVoid version = reinterpret_cast<FnVoid>(GetProcAddress(mod, "SrtApiVersion"));
    FnVoid ifindex = reinterpret_cast<FnVoid>(GetProcAddress(mod, "SrtIfIndex"));
    FnVoid shutdown = reinterpret_cast<FnVoid>(GetProcAddress(mod, "SrtShutdown"));
    FnVoid patchCount = reinterpret_cast<FnVoid>(GetProcAddress(mod, "SrtPatchCount"));
    FnVoid patchMask = reinterpret_cast<FnVoid>(GetProcAddress(mod, "SrtPatchMask"));
    FnVoid unmatched = reinterpret_cast<FnVoid>(GetProcAddress(mod, "SrtUnmatchedCount"));
    FnVoid bindCount = reinterpret_cast<FnVoid>(GetProcAddress(mod, "SrtBindCount"));
    FnVoid bindFails = reinterpret_cast<FnVoid>(GetProcAddress(mod, "SrtBindFailCount"));
    CHECK(startup && version && ifindex && shutdown && patchCount && patchMask && unmatched && bindCount && bindFails);
    if (!startup || !version || !ifindex || !shutdown || !patchCount || !patchMask || !unmatched || !bindCount || !bindFails) { EndSection(); return; }

    CHECK_EQ_INT(version(), 1);
    CHECK_MSG(startup() == 1, L"SrtStartup lief nicht durch");
    CHECK_EQ_INT(ifindex(), 19);       // aufgelöst: Wlan -> ifIndex 19
    CHECK_MSG(startup() == 1, L"SrtStartup ist nicht wiederholbar");
    CHECK_EQ_INT(ifindex(), 19);
    // Importtabellen müssen umgehängt sein - auch bei Importen über Ordnungsnummern.
    CHECK_MSG(patchCount() >= 2, L"zu wenige Importe umgehängt");
    Note(L"umgehängte Importe: " + std::to_wstring(patchCount()) +
         L", Maske: " + std::to_wstring(patchMask()) +
         L", unbekannt gebliebene Importe: " + std::to_wstring(unmatched()));

    bool loaded = false;
    std::wstring err2;
    CHECK(IsModuleLoadedInProcess(GetCurrentProcessId(), L"hook.dll", loaded, err2));
    CHECK(loaded);

    WSADATA wsa;
    CHECK_EQ_INT(WSAStartup(MAKEWORD(2, 2), &wsa), 0);

    const sockaddr_in out = MakeV4(L"203.0.113.7", 53);     // Dokumentationsbereich, Paket verläuft nicht
    const sockaddr_in loop = MakeV4(L"127.0.0.1", 53);

    // 1) Neue UDP-Verbindung nach außen: Hook muss before connect den Index gesetzt haben
    CHECK_EQ_INT(ProbeUdpIfIndex(out, nullptr), 19);

    // 2) Loopback wird absichtlich nicht angefasst
    CHECK_EQ_INT(ProbeUdpIfIndex(loop, nullptr), 0);

    // 3) Selbst gebundener Socket wird nicht überschrieben
    SOCKET bound = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    CHECK(bound != INVALID_SOCKET);
    if (bound != INVALID_SOCKET) {
        sockaddr_in local;
        ZeroMemory(&local, sizeof(local));
        local.sin_family = AF_INET;
        inet_pton(AF_INET, "127.0.0.1", &local.sin_addr);
        local.sin_port = 0;
        CHECK_EQ_INT(bind(bound, reinterpret_cast<sockaddr*>(&local), sizeof(local)), 0);
        connect(bound, reinterpret_cast<const sockaddr*>(&out), sizeof(out));
        CHECK_EQ_INT(ReadIfIndex(bound), 0);
        closesocket(bound);
    }

    // 4) TCP-Socket: Familie aus Zieladresse, Index gesetzt
    SOCKET tcp = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    CHECK(tcp != INVALID_SOCKET);
    if (tcp != INVALID_SOCKET) {
        unsigned long idx = 0;
        int len = sizeof(idx);
        getsockopt(tcp, IPPROTO_IP, IP_UNICAST_IF, reinterpret_cast<char*>(&idx), &len);
        CHECK_EQ_INT(idx, 0);          // noch nicht verbunden -> nichts gesetzt
        closesocket(tcp);
    }

    // 5) Neu-Laden: Regel auf LAN umstellen, Index muss von selbst folgen
    Ruleset rs2;
    Rule r2;
    r2.pattern = SelfImageLower();
    r2.mode = RouteMode::Lan;
    rs2.AddTop(r2);
    CHECK_MSG(WriteResolved(MakePaths(dir), rs2, ads, problems, err), L"resolved.json (LAN): " + err);

    bool reloaded = false;
    for (int i = 0; i < 40 && !reloaded; ++i) {
        if (ifindex() == 10) reloaded = true;
        else Sleep(100);
    }
    CHECK_MSG(reloaded, L"Hook hat die geänderte resolved.json nicht übernommen");
    CHECK_EQ_INT(ProbeUdpIfIndex(out, nullptr), 10);

    // 6) Bereits benutzter Socket wird beim nächsten Senden umgehängt
    SOCKET live = INVALID_SOCKET;
    CHECK_EQ_INT(ProbeUdpIfIndex(out, &live), 10);
    CHECK(SendUdpOnce(live, out));
    CHECK_EQ_INT(ReadIfIndex(live), 10);

    Ruleset rs3;
    Rule r3;
    r3.pattern = SelfImageLower();
    r3.mode = RouteMode::Wlan;
    rs3.AddTop(r3);
    CHECK_MSG(WriteResolved(MakePaths(dir), rs3, ads, problems, err), L"resolved.json (WLAN): " + err);
    reloaded = false;
    for (int i = 0; i < 40 && !reloaded; ++i) {
        if (ifindex() == 19) reloaded = true;
        else Sleep(100);
    }
    CHECK_MSG(reloaded, L"zweiter Neu-Ladeprozess fehlgeschlagen");
    CHECK_EQ_INT(ReadIfIndex(live), 10);   // ohne erneutes Senden bleibt der Socket unberührt
    CHECK(SendUdpOnce(live, out));
    CHECK_EQ_INT(ReadIfIndex(live), 19);   // nach dem nächsten Senden gilt der neue Adapter
    closesocket(live);

    // 7) Loopback bleibt auch nach allen Wechseln unberührt
    CHECK_EQ_INT(ProbeUdpIfIndex(loop, nullptr), 0);

    // 8) Zählwerk: jede erwartete Bindung muss gelungen sein, kein Fehlversuch
    Note(L"umgehängte Importe: " + std::to_wstring(patchCount()) +
         L", gesetzte Bindungen: " + std::to_wstring(bindCount()) +
         L", Fehlversuche: " + std::to_wstring(bindFails()));
    CHECK(bindCount() >= 4);
    CHECK_EQ_INT(bindFails(), 0);

    shutdown();
    // FreeLibrary bleibt bewusst aus: die Importtabellen zeigen weiter auf die Bibliothek.
    WSACleanup();
    EndSection();
}

// ---------------------------------------------------------------- Echte Injektion

// Prüft eine Messrunde: die Quelladresse muss vom erzwungenen Adapter stammen.
// Bei UDP wird absichtlich ein Ziel im anderen Netz genommen - nur dann zeigt
// sich, ob die Schnittstelle wirklich gesetzt wurde oder Windows nur rutet.
static void CheckProbeRound(const std::wstring& text, int round, unsigned long wantIdx, const std::wstring& wantIp) {
    std::wstring v;
    CHECK_MSG(LineValue(text, round, L"udp_ifindex", v), L"udp_ifindex fehlt in Runde " + std::to_wstring(round));
    CHECK_EQ_INT(_wtoll(v.c_str()), wantIdx);
    CHECK_MSG(LineValue(text, round, L"udp_local", v), L"udp_local fehlt in Runde " + std::to_wstring(round));
    CHECK_EQ_STR(v, wantIp);
    if (LineValue(text, round, L"tcp_local", v) && v != L"0.0.0.0" && !v.empty()) {
        CHECK_EQ_STR(v, wantIp);
    } else {
        Note(L"TCP-Quelle in dieser Runde nicht vermittel - nur UDP geprüft");
    }
    if (LineValue(text, round, L"tcp_ifindex", v) && v != L"0") {
        CHECK_EQ_INT(_wtoll(v.c_str()), wantIdx);
    }
}

static void TestInjectTargetProcess() {
    Begin(L"Echte Injektion: srtarget.exe erst WLAN, dann LAN");

    const std::wstring hook = ExeDir() + L"\\hook.dll";
    const std::wstring target = ExeDir() + L"\\srtarget.exe";
    if (!FileExists(hook) || !FileExists(target)) { Skip(L"hook.dll oder srtarget.exe fehlt"); EndSection(); return; }

    std::vector<AdapterInfo> ads;
    std::wstring err;
    if (!EnumerateAdapters(ads, err)) { Skip(L"Adapterenaufzählung fehlgeschlagen"); EndSection(); return; }
    const AdapterInfo* wlan = FindAdapterByRole(ads, AdapterKind::Wlan);
    const AdapterInfo* lan = FindAdapterByRole(ads, AdapterKind::Lan);
    if (!wlan || !lan || !wlan->isUp || !lan->isUp || wlan->ipv4.empty() || lan->ipv4.empty()
        || wlan->gateway.empty() || lan->gateway.empty()) {
        Skip(L"zwei einsatzbereite Adapter (WLAN und Ethernet) mit Gateway werden gebraucht");
        EndSection();
        return;
    }
    wprintf(L"  WLAN=%s idx=%lu %s | LAN=%s idx=%lu %s\n", wlan->alias.c_str(), wlan->ifIndex,
            wlan->ipv4.c_str(), lan->alias.c_str(), lan->ifIndex, lan->ipv4.c_str());

    const std::wstring dir = MakeTestDir(L"inject");
    SetEnvironmentVariableW(L"SRTROUTER_DATA", dir.c_str());
    const Paths p = MakePaths(dir);
    const std::wstring outFile = dir + L"\\ergebnis.txt";
    const std::wstring waitFile = dir + L"\\r2.txt";
    DeleteFileW(outFile.c_str());
    DeleteFileW(waitFile.c_str());

    Ruleset rs;
    Rule r;
    r.pattern = L"srtarget.exe";
    r.mode = RouteMode::Wlan;
    rs.AddTop(r);
    std::vector<std::wstring> problems;
    CHECK_MSG(WriteResolved(p, rs, ads, problems, err), L"resolved.json: " + err);

    // TCP-Partner jeweils im Netz des Zieladapters, UDP-Partner absichtlich quer:
    // nur wenn die Schnittstelle wirklich erzwungen wird, erscheint die erwartete Quelladresse.
    std::wstring cmd = L"\"" + target + L"\" \"" + outFile + L"\" 2 \"" + waitFile + L"\" \""
                     + wlan->gateway + L";" + lan->gateway + L"\" 80 300 \""
                     + lan->gateway + L";" + wlan->gateway + L"\"";

    STARTUPINFOW si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));
    std::wstring mutableCmd = cmd;
    BOOL created = CreateProcessW(target.c_str(), &mutableCmd[0], nullptr, nullptr, FALSE,
                                  CREATE_SUSPENDED, nullptr, nullptr, &si, &pi);
    CHECK_MSG(created, L"Zielprozess starten: " + LastErrText(GetLastError()));
    if (!created) { EndSection(); return; }

    bool wasAlready = true;
    CHECK_MSG(InjectAndActivate(pi.dwProcessId, hook, wasAlready, err), L"Injektion: " + err);
    CHECK(!wasAlready);

    wasAlready = false;
    CHECK_MSG(InjectAndActivate(pi.dwProcessId, hook, wasAlready, err), L"zweite Aktivierung: " + err);
    CHECK(wasAlready);

    bool loaded = false;
    CHECK(IsModuleLoadedInProcess(pi.dwProcessId, L"hook.dll", loaded, err));
    CHECK(loaded);

    unsigned long idx = 0;
    CHECK_MSG(QueryHookIfIndex(pi.dwProcessId, hook, idx, err), L"Fernabfrage: " + err);
    CHECK_EQ_INT(idx, wlan->ifIndex);

    unsigned long mask = 0;
    CHECK_MSG(QueryHookPatchMask(pi.dwProcessId, hook, mask, err), L"Fernabfrage Maske: " + err);
    CHECK_MSG(mask != 0, L"im Zielprozess wurde kein einziges Importfeld umgehängt");
    Note(L"Maske der Abfangstellen im Zielprozess: " + std::to_wstring(mask));

    ResumeThread(pi.hThread);

    std::wstring text;
    bool sawRound1 = WaitForResult(outFile, L"round=1", 90, text);
    CHECK_MSG(sawRound1, L"Runde 1 wurde nicht gemeldet. Inhalt: " + text);
    if (sawRound1) {
        CheckProbeRound(text, 1, wlan->ifIndex, wlan->ipv4);
    }

    // Jetzt auf LAN umstellen und warten, bis die Hook-Bibliothek im Zielprozess umgeschaltet hat
    Ruleset rs2;
    Rule r2;
    r2.pattern = L"srtarget.exe";
    r2.mode = RouteMode::Lan;
    rs2.AddTop(r2);
    CHECK_MSG(WriteResolved(p, rs2, ads, problems, err), L"resolved.json (LAN): " + err);

    bool switched = WaitHookIndex(pi.dwProcessId, hook, lan->ifIndex, 40);
    CHECK_MSG(switched, L"Zielprozess hat nicht auf LAN umgeschaltet");

    if (switched) {
        HANDLE wf = CreateFileW(waitFile.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        CHECK_MSG(wf != INVALID_HANDLE_VALUE, L"Wartedatei: " + LastErrText(GetLastError()));
        if (wf != INVALID_HANDLE_VALUE) CloseHandle(wf);

        bool sawDone = WaitForResult(outFile, L"done", 90, text);
        CHECK_MSG(sawDone, L"Runde 2 wurde nicht beendet. Inhalt: " + text);
        if (sawDone) {
            CheckProbeRound(text, 2, lan->ifIndex, lan->ipv4);
        }
    }

    wprintf(L"  Zielausgabe:\n");
    size_t pos = 0;
    while (pos < text.size()) {
        size_t e = text.find(L'\n', pos);
        std::wstring line = text.substr(pos, e == std::wstring::npos ? std::wstring::npos : e - pos);
        pos = (e == std::wstring::npos) ? text.size() : e + 1;
        if (!line.empty()) wprintf(L"    %s\n", line.c_str());
    }

    DWORD waitRes = WaitForSingleObject(pi.hProcess, 20000);
    CHECK_MSG(waitRes == WAIT_OBJECT_0, L"Zielprozess läuft zu lange");
    DWORD code = 0xFFFF;
    GetExitCodeProcess(pi.hProcess, &code);
    CHECK_EQ_INT(code, 0);

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    EndSection();
}

// ---------------------------------------------------------------- Haupt

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    wprintf(L"RoutePerApp Injektions-Tests\n");

    TestCanInjectReasons();
    TestErrorPaths();
    TestHookInSelf();
    TestInjectTargetProcess();

    wprintf(L"\n=========================================\n");
    wprintf(L"Prüfungen: %d   Fehler: %d   übersprungen: %d\n", g_checks, g_failed, g_skipped);
    wprintf(L"=========================================\n");
    return g_failed == 0 ? 0 : 1;
}
