// Tests für die Logikschicht der Oberfläche (src\gui_logic.cpp) und die
// zugehörigen Kern-Ergänzungen. Ohne Fenster, deshalb vollständig automatisierbar.
#include "harness.hpp"

#include <string>
#include <vector>

#include "../src/gui_logic.hpp"      // zieht core.hpp und damit die richtige Include-Reihenfolge
#include "../src/core.hpp"

using namespace sr;
using namespace srgui;

static std::wstring TempDir(const wchar_t* tag) {
    wchar_t base[MAX_PATH];
    GetTempPathW(MAX_PATH, base);
    std::wstring dir = std::wstring(base) + L"routeperapp-" + tag;
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

static AdapterInfo MakeAdapter(unsigned long idx, const wchar_t* alias, AdapterKind kind, bool up,
                              const wchar_t* ip, const wchar_t* gw) {
    AdapterInfo a;
    a.ifIndex = idx;
    a.alias = alias;
    a.description = alias;
    a.kind = kind;
    a.isUp = up;
    a.ipv4 = ip;
    a.gateway = gw;
    a.ifMetric = 25;
    return a;
}

// ---------------------------------------------------------------- Zonen, Spalten, Köpfe

static void TestZones() {
    Begin(L"Zuordnungszonen und Spalten");

    CHECK_EQ_INT((int)ZoneForColumn(COL_NAME), (int)Zone::None);
    CHECK_EQ_INT((int)ZoneForColumn(COL_PID), (int)Zone::None);
    CHECK_EQ_INT((int)ZoneForColumn(COL_VIA), (int)Zone::None);
    CHECK_EQ_INT((int)ZoneForColumn(COL_WLAN), (int)Zone::Wlan);
    CHECK_EQ_INT((int)ZoneForColumn(COL_LAN), (int)Zone::Lan);
    CHECK_EQ_INT((int)ZoneForColumn(COL_DEFAULT), (int)Zone::Standard);
    CHECK_EQ_INT((int)ZoneForColumn(COL_USER), (int)Zone::None);
    CHECK_EQ_INT((int)ZoneForColumn(-1), (int)Zone::None);
    CHECK_EQ_INT((int)ZoneForColumn(99), (int)Zone::None);

    CHECK_EQ_INT(ColumnForZone(Zone::Wlan), COL_WLAN);
    CHECK_EQ_INT(ColumnForZone(Zone::Lan), COL_LAN);
    CHECK_EQ_INT(ColumnForZone(Zone::Standard), COL_DEFAULT);
    CHECK_EQ_INT(ColumnForZone(Zone::None), -1);

    CHECK_EQ_INT((int)ZoneMode(Zone::Wlan), (int)RouteMode::Wlan);
    CHECK_EQ_INT((int)ZoneMode(Zone::Lan), (int)RouteMode::Lan);
    CHECK_EQ_INT((int)ZoneMode(Zone::Standard), (int)RouteMode::None);
    CHECK_EQ_INT((int)ZoneForMode(RouteMode::Wlan), (int)Zone::Wlan);
    CHECK_EQ_INT((int)ZoneForMode(RouteMode::Lan), (int)Zone::Lan);
    CHECK_EQ_INT((int)ZoneForMode(RouteMode::None), (int)Zone::None);
    CHECK_EQ_INT((int)ZoneForMode(RouteMode::Adapter), (int)Zone::None);

    const AdapterInfo up = MakeAdapter(19, L"WLAN 2", AdapterKind::Wlan, true, L"192.168.1.171", L"192.168.1.1");
    const AdapterInfo down = MakeAdapter(10, L"Main", AdapterKind::Lan, false, L"", L"");

    CHECK_EQ_STR(ColumnHeader(Zone::Wlan, &up), L"WLAN \xB7 WLAN 2");
    CHECK_EQ_STR(ColumnHeader(Zone::Lan, &down), L"LAN \xB7 Main (getrennt)");
    CHECK_EQ_STR(ColumnHeader(Zone::Lan, nullptr), L"LAN \xB7 nicht vorhanden");
    CHECK_EQ_STR(ColumnHeader(Zone::Standard, nullptr), L"Standard \xB7 Windows");

    EndSection();
}

// ---------------------------------------------------------------- Tabellenzellen

static void TestCells() {
    Begin(L"Zellen der Prozesstabelle");

    ProcRow r;
    r.image = L"chrome.exe";
    r.conns = 3;
    CHECK_EQ_STR(NameCell(r), L"chrome.exe  \xB7 3");
    r.conns = 0;
    CHECK_EQ_STR(NameCell(r), L"chrome.exe");

    r.viaAdapter = L"WLAN 2";
    CHECK_EQ_STR(ViaCell(r), L"\xFC" L"ber WLAN 2");
    r.viaAdapter.clear();
    r.conns = 2;
    CHECK_EQ_STR(ViaCell(r), L"Adapter unklar");
    r.conns = 0;
    CHECK_EQ_STR(ViaCell(r), L"kein Verkehr");

    r.mode = RouteMode::Wlan;
    r.ruleId = 7;
    CHECK_EQ_STR(ZoneCell(r, Zone::Wlan), L"\x25CF WLAN");
    CHECK_EQ_STR(ZoneCell(r, Zone::Lan), L"");
    CHECK_EQ_STR(ZoneCell(r, Zone::Standard), L"");

    r.mode = RouteMode::Lan;
    CHECK_EQ_STR(ZoneCell(r, Zone::Lan), L"\x25CF LAN");
    CHECK_EQ_STR(ZoneCell(r, Zone::Wlan), L"");

    // Ausdruck "Standard" nur mit eigener Regel, nicht bei fehlender Zuordnung.
    r.mode = RouteMode::None;
    r.ruleId = 9;
    CHECK_EQ_STR(ZoneCell(r, Zone::Standard), L"\x25CF Standard");
    r.ruleId = 0;
    CHECK_EQ_STR(ZoneCell(r, Zone::Standard), L"");
    CHECK_EQ_STR(ZoneCell(r, Zone::Wlan), L"");

    EndSection();
}

// ---------------------------------------------------------------- Adapterkarten

static void TestCards() {
    Begin(L"Adapterkarten");

    AdapterInfo a = MakeAdapter(19, L"WLAN 2", AdapterKind::Wlan, true, L"192.168.1.171", L"192.168.1.1");
    a.assignedCount = 4;
    a.activeCount = 2;
    a.rxRate = 1024;
    a.txRate = 2048;
    const CardText c = BuildCard(&a, Zone::Wlan);
    CHECK_EQ_STR(c.title, L"WLAN 2");
    CHECK(!c.offline);
    CHECK_EQ_INT((int)c.lines.size(), 3);
    CHECK(c.lines[0].find(L"192.168.1.171") != std::wstring::npos);
    CHECK(c.lines[0].find(L"Gateway 192.168.1.1") != std::wstring::npos);
    CHECK(c.lines[1].find(L"4 zugeordnet") != std::wstring::npos);
    CHECK(c.lines[1].find(L"2 mit Verkehr") != std::wstring::npos);
    CHECK(c.lines[2].find(L"KB/s") != std::wstring::npos);
    CHECK(c.kind.find(L"WLAN") != std::wstring::npos);
    CHECK(c.kind.find(L"5G-Mobilrouter") != std::wstring::npos);

    const AdapterInfo lan = MakeAdapter(10, L"Main", AdapterKind::Lan, true, L"192.168.178.132", L"192.168.178.1");
    const CardText cl = BuildCard(&lan, Zone::Lan);
    CHECK(cl.kind.find(L"Fritzbox") != std::wstring::npos);

    const AdapterInfo down = MakeAdapter(10, L"Main", AdapterKind::Lan, false, L"", L"");
    const CardText cd = BuildCard(&down, Zone::Lan);
    CHECK(cd.offline);
    CHECK(cd.lines[0].find(L"keine IPv4") != std::wstring::npos);

    const CardText cn = BuildCard(nullptr, Zone::Wlan);
    CHECK(cn.offline);
    CHECK_EQ_STR(cn.title, L"WLAN");
    CHECK_EQ_STR(cn.kind, L"kein Adapter erkannt");

    EndSection();
}

// ---------------------------------------------------------------- Status und Zieh-Hinweis

static void TestStatusTexts() {
    Begin(L"Statuszeile und Zieh-Hinweis");

    const std::wstring plain = StatusLine(L"", 40, 6, true, L"");
    CHECK(plain.find(L"40 Prozesse sichtbar") != std::wstring::npos);
    CHECK(plain.find(L"6 davon mit Netzwerkverbindung") != std::wstring::npos);
    CHECK(plain.find(L"Administratorrechte") == std::wstring::npos);

    const std::wstring note = StatusLine(L"chrome.exe \x2192 WLAN", 40, 6, true, L"");
    CHECK_EQ_STR(note, L"chrome.exe \x2192 WLAN");

    const std::wstring notAdmin = StatusLine(L"", 1, 0, false, L"");
    CHECK(notAdmin.find(L"ohne Administratorrechte") != std::wstring::npos);

    const std::wstring withErr = StatusLine(L"", 1, 0, true, L"Adapter nicht lesbar");
    CHECK(withErr.find(L"Adapter nicht lesbar") != std::wstring::npos);

    CHECK_EQ_STR(DragHint(1, Zone::None), L"1 Programm losgelassen \x2013 auf WLAN, LAN oder Standard fallen lassen");
    CHECK(DragHint(3, Zone::Wlan).find(L"3 Programme auf WLAN") != std::wstring::npos);

    EndSection();
}

// ---------------------------------------------------------------- Ablage aus dem Explorer

static void TestDroppedFiles() {
    Begin(L"Ablage von Programmdateien");

    std::wstring why;
    CHECK(!DroppedFileAllowed(L"", why));
    CHECK(!why.empty());
    CHECK(!DroppedFileAllowed(L"C:\\x\\app.exe.lnk", why));
    CHECK(why.find(L"Verkn\xFCpfung") != std::wstring::npos);
    CHECK(!DroppedFileAllowed(L"C:\\x\\notepad.txt", why));
    CHECK(why.find(L".exe") != std::wstring::npos);
    CHECK(!DroppedFileAllowed(L"C:\\x\\feHLT.exe", why));
    CHECK(why.find(L"existiert nicht") != std::wstring::npos);

    const std::wstring dir = TempDir(L"gui-drop");
    const std::wstring file = dir + L"\\echtes-Programm.EXE";
    HANDLE h = CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    CHECK(h != INVALID_HANDLE_VALUE);
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    CHECK_MSG(DroppedFileAllowed(file, why), why);
    CHECK_EQ_STR(PatternFromPath(file), L"echtes-programm.exe");
    CHECK_EQ_STR(PatternFromPath(L"C:\\a\\b\\Chrome.exe"), L"chrome.exe");
    CHECK_EQ_STR(PatternFromPath(L"notepad.exe"), L"notepad.exe");
    DeleteFileW(file.c_str());
    RemoveDirectoryW(dir.c_str());

    EndSection();
}

// ---------------------------------------------------------------- Regeln und Rücknahme

static void TestRuleLookupAndUndo() {
    Begin(L"Regel-Wiederkennung und Rücknahmekette");

    Ruleset rs;
    Rule a; a.pattern = L"chrome.exe"; a.mode = RouteMode::Wlan;
    Rule b; b.pattern = L"firefox.exe"; b.mode = RouteMode::Lan;
    Rule c; c.pattern = L"team.exe"; c.mode = RouteMode::Adapter; c.adapterGuid = L"{A1}"; c.enabled = false;
    rs.AddTop(a);
    rs.AddTop(b);
    rs.AddTop(c);

    // AddTop fügt vorn ein: die Liste steht in umgekehrter Reihenfolge der Eingabe.
    CHECK_EQ_INT(FindEquivalentRule(rs, L"chrome.exe", RouteMode::Wlan, L""), 2);
    CHECK_EQ_INT(FindEquivalentRule(rs, L"CHROME.EXE", RouteMode::Wlan, L""), 2);
    CHECK_EQ_INT(FindEquivalentRule(rs, L"chrome.exe", RouteMode::Lan, L""), -1);
    CHECK_EQ_INT(FindEquivalentRule(rs, L"firefox.exe", RouteMode::Lan, L""), 1);
    CHECK_EQ_INT(FindEquivalentRule(rs, L"team.exe", RouteMode::Adapter, L"{a1}"), -1);  // deaktiviert zählt nicht
    CHECK_EQ_INT(FindEquivalentRule(rs, L"team.exe", RouteMode::Adapter, L"{A1}"), -1);

    Ruleset r2;
    Rule d; d.pattern = L"team.exe"; d.mode = RouteMode::Adapter; d.adapterGuid = L"{A1}";
    r2.AddTop(d);
    CHECK_EQ_INT(FindEquivalentRule(r2, L"team.exe", RouteMode::Adapter, L"{a1}"), 0);   // GUID egal geschrieben
    CHECK_EQ_INT(FindEquivalentRule(r2, L"team.exe", RouteMode::Adapter, L"{B7}"), -1);  // anderer Adapter
    CHECK_EQ_INT(FindEquivalentRule(r2, L"team.exe", RouteMode::Wlan, L""), -1);

    UndoStack us;
    UndoStep emptyStep;
    CHECK(us.Empty());
    CHECK(!us.Pop(emptyStep));
    UndoStep s1; s1.ruleId = 1; s1.label = L"eins";
    UndoStep s2; s2.ruleId = 2; s2.label = L"zwei";
    us.Push(s1);
    us.Push(s2);
    UndoStep s3; s3.wasNew = true; s3.ruleId = 0; s3.label = L"drei";
    us.Push(s3);
    CHECK_EQ_INT((int)us.Size(), 3);
    CHECK_EQ_STR(us.TopLabel(), L"drei");
    UndoStep out;
    CHECK(us.Pop(out));
    CHECK(out.wasNew);
    CHECK_EQ_STR(out.label, L"drei");
    CHECK(us.Pop(out));
    CHECK_EQ_INT((int)out.ruleId, 2);
    CHECK(us.Pop(out));
    CHECK_EQ_INT((int)out.ruleId, 1);
    CHECK(us.Empty());
    CHECK_EQ_STR(us.TopLabel(), L"");

    for (int i = 0; i < 60; ++i) { UndoStep s; s.ruleId = (unsigned long long)i; us.Push(s); }
    CHECK_EQ_INT((int)us.Size(), 40);          // Kette wächst nicht unbegrenzt
    CHECK(us.Pop(out));
    CHECK_EQ_INT((int)out.ruleId, 59);

    EndSection();
}

// ---------------------------------------------------------------- Zeilenzustand und Suche

static void TestRowStatesAndQuery() {
    Begin(L"Zustand der Zeile und Suchfeld");

    ProcRow r;
    r.image = L"a.exe";
    r.pathAccess = true;
    CHECK_EQ_INT((int)StateOfRow(r), (int)RowState::Neutral);
    CHECK_EQ_STR(RowStateText(r), L"keine Zuordnung");

    r.mode = RouteMode::Lan;
    CHECK_EQ_INT((int)StateOfRow(r), (int)RowState::Forced);
    CHECK(RowStateText(r).find(L"Adapter") != std::wstring::npos);

    r.mode = RouteMode::None;
    r.ruleId = 5;
    CHECK_EQ_INT((int)StateOfRow(r), (int)RowState::Default);

    r.ruleId = 0;
    r.pathAccess = false;
    CHECK_EQ_INT((int)StateOfRow(r), (int)RowState::Blocked);
    CHECK(RowStateText(r).find(L"Pfad nicht lesbar") != std::wstring::npos);

    CHECK(QueryMatches(L"", L"anything", L""));
    CHECK(QueryMatches(L"CHRo", L"chrome.exe", L""));
    CHECK(QueryMatches(L"doe", L"x.exe", L"B\\Doe"));
    CHECK(!QueryMatches(L"xyz", L"chrome.exe", L"B\\Doe"));
    CHECK(QueryMatches(L"1234", L"chrome.exe", L""));

    EndSection();
}

// ---------------------------------------------------------------- Kern-Ergänzungen

static void TestCoreAdditions() {
    Begin(L"Kern: 32-Bit-Hook und Injektionszähler");

    const Paths p = MakePaths(L"C:\\data");
    CHECK_EQ_STR(p.hookDll, L"C:\\data\\hook.dll");
    CHECK_EQ_STR(p.hookDll32, L"C:\\data\\hook32.dll");

    ProcInfo pr;
    pr.pid = 4242;
    pr.image = L"spiel.exe";
    pr.pathAccess = true;
    std::wstring why;
    pr.is32Bit = true;
    CHECK(!CanInjectProcess(pr, true, false, why));
    CHECK(why.find(L"hook32.dll") != std::wstring::npos);
    CHECK(CanInjectProcess(pr, true, true, why));
    pr.is32Bit = false;
    CHECK(!CanInjectProcess(pr, true, false, why));
    CHECK(why.find(L"hook.dll fehlt") != std::wstring::npos);

    const Paths tmp = MakePaths(TempDir(L"gui-engine"));
    Engine e(tmp);
    int already = 0, failed = 0;
    const int done = e.ReinjectMatching(why, already, failed);
    CHECK_EQ_INT(done, 0);
    CHECK_EQ_INT(already, 0);
    CHECK_EQ_INT(failed, 0);
    CHECK(!why.empty());                                   // ohne Rechte kommt eine Begründung
    CHECK_EQ_STR(e.HookDllFor(false), tmp.hookDll);
    CHECK_EQ_STR(e.HookDllFor(true), tmp.hookDll32);

    // Der eigene Prozess wird als 64-Bit erkannt und ist lesbar.
    std::vector<ProcInfo> procs;
    std::wstring err;
    CHECK_MSG(EnumerateProcesses(procs, err), err);
    const DWORD self = GetCurrentProcessId();
    const ProcInfo* mine = nullptr;
    for (const ProcInfo& pi : procs) if (pi.pid == self) { mine = &pi; break; }
    CHECK_MSG(mine != nullptr, L"eigener Prozess nicht in der Aufzählung");
    if (mine) {
        CHECK(mine->pathAccess);
        CHECK(!mine->path.empty());
        CHECK(!mine->is32Bit);                             // dieser Testbau ist 64-Bit
    }

    // Adapterbestand dieser Maschine (für die Kartenansicht relevant).
    std::vector<AdapterInfo> ads;
    std::wstring aerr;
    if (!EnumerateAdapters(ads, aerr)) Skip(L"Adapter-Aufzählung nicht möglich");
    else Note(L"Adapter gefunden: " + std::to_wstring(ads.size()));

    RemoveDirectoryW(tmp.dir.c_str());
    EndSection();
}

int wmain() {
    wprintf(L"RoutePerApp Oberfl\xE4" L"chen-Tests\n");
    TestZones();
    TestCells();
    TestCards();
    TestStatusTexts();
    TestDroppedFiles();
    TestRuleLookupAndUndo();
    TestRowStatesAndQuery();
    TestCoreAdditions();
    wprintf(L"\n=========================================\n");
    wprintf(L"Pr\xFC" L"fungen: %d   Fehler: %d   \xFC" L"bersprungen: %d\n", g_checks, g_failed, g_skipped);
    wprintf(L"=========================================\n");
    return g_failed ? 1 : 0;
}
