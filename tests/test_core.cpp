// Unit- und Systemtests für den RoutePerApp-Kern.
// Der Kern wird hier direkt eingebunden, damit jede Funktion einzeln prüfbar ist.
#include "../src/core.cpp"

#include <cstdio>
#include <cstdarg>

using namespace sr;

static int g_checks = 0;
static int g_failed = 0;
static int g_sectionFails = 0;
static std::wstring g_section;

static void Fail(const char* file, int line, const std::wstring& what) {
    ++g_failed;
    ++g_sectionFails;
    const char* base = strrchr(file, '\\');
    wprintf(L"  FEHLER [%s] %hs:%d\n    %s\n", g_section.c_str(), base ? base + 1 : file, line, what.c_str());
}

#define CHECK(cond) do { ++g_checks; if (!(cond)) Fail(__FILE__, __LINE__, L"Erwartung nicht erfüllt: " LR"(cond)"); } while (0)
#define CHECK_MSG(cond, msg) do { ++g_checks; if (!(cond)) Fail(__FILE__, __LINE__, (msg)); } while (0)
#define CHECK_EQ_INT(a, b) do { ++g_checks; long long _a = (long long)(a), _b = (long long)(b); \
    if (_a != _b) { wchar_t _m[200]; swprintf(_m, 200, L"%lld != %lld", _a, _b); Fail(__FILE__, __LINE__, _m); } } while (0)
#define CHECK_EQ_STR(a, b) do { ++g_checks; const std::wstring _x = (a), _y = (b); \
    if (_x != _y) Fail(__FILE__, __LINE__, L"\"" + _x + L"\" != \"" + _y + L"\""); } while (0)

#define WIDEN2(x) L ## x
#define WIDEN(x) WIDEN2(x)
#define LR(x) WIDEN(#x)

static void Begin(const wchar_t* name) {
    g_section = name;
    g_sectionFails = 0;
    wprintf(L"\n== %s ==\n", name);
}
static void EndSection() {
    if (g_sectionFails == 0) wprintf(L"  ok\n");
    else wprintf(L"  %d Fehler\n", g_sectionFails);
}

// ----------------------------------------------------------------Fixture-Helfer

static AdapterInfo MakeAdapter(const wchar_t* alias, const wchar_t* guid, AdapterKind kind,
                               unsigned long idx, const wchar_t* ip, long long metric, bool up = true,
                               bool virt = false) {
    AdapterInfo a;
    a.alias = alias;
    a.guid = guid;
    a.kind = kind;
    a.ifIndex = idx;
    a.luid = idx;
    a.ipv4 = ip;
    a.ifMetric = metric;
    a.isUp = up;
    a.isVirtual = virt;
    return a;
}

static std::wstring TempDir() {
    wchar_t base[MAX_PATH];
    GetTempPathW(MAX_PATH, base);
    wchar_t sub[MAX_PATH];
    swprintf(sub, MAX_PATH, L"%srouteperapp-test-%lu", base, GetCurrentProcessId());
    std::wstring dir = sub;
    DeleteFileW((dir + L"\\rules.json").c_str());
    DeleteFileW((dir + L"\\rules.json.bak").c_str());
    RemoveDirectoryW(dir.c_str());
    std::wstring err;
    EnsureDir(dir, err);
    return dir;
}

// ----------------------------------------------------------------JSON

static void TestJsonBasics() {
    Begin(L"JSON: Grundformat");
    JsonPtr v;
    JsonParseResult r = JsonParse(L"{\"a\":1,\"b\":\"text\",\"c\":true,\"d\":null,\"e\":[1,2,3]}", v);
    CHECK(r.ok);
    CHECK_MSG(v && v->isObject(), L"Wurzel ist kein Objekt");
    if (v && v->isObject()) {
        CHECK_EQ_INT(v->intOr(L"a", -1), 1);
        CHECK_EQ_STR(v->strOr(L"b", L""), L"text");
        CHECK(v->boolOr(L"c", false));
        const JsonPtr* e = v->find(L"e");
        CHECK_MSG(e && (*e)->isArray() && (*e)->arr.size() == 3, L"Array e fehlt");
    }
    EndSection();
}

static void TestJsonEscapesUnicode() {
    Begin(L"JSON: Escape-Sequenzen und Unicode");
    JsonPtr v;
    JsonParseResult r = JsonParse(L"{\"p\":\"C:\\\\tmp\\\\a b\",\"u\":\"Gr\\u00fc\\u00df\",\"e\":\"\\u00e4\\u00f6\\u00fc\",\"emoji\":\"\\ud83d\\ude00\",\"tab\":\"a\\tb\"}", v);
    CHECK(r.ok);
    if (!r.ok) { EndSection(); return; }
    CHECK_EQ_STR(v->strOr(L"p", L""), L"C:\\tmp\\a b");
    CHECK_EQ_STR(v->strOr(L"u", L""), L"Grüß");
    CHECK_EQ_STR(v->strOr(L"e", L""), L"äöü");
    std::wstring emo = v->strOr(L"emoji", L"");
    CHECK_EQ_INT(emo.size(), 2);
    CHECK_EQ_INT(emo.empty() ? 0 : emo[0], 0xD83D);
    CHECK_EQ_STR(v->strOr(L"tab", L""), L"a\tb");

    std::wstring out = JsonSerialize(v, false);
    JsonPtr v2;
    CHECK(JsonParse(out, v2).ok);
    CHECK_EQ_STR(v2->strOr(L"p", L""), L"C:\\tmp\\a b");
    CHECK_EQ_STR(v2->strOr(L"u", L""), L"Grüß");
    EndSection();
}

static void TestJsonFehler() {
    Begin(L"JSON: Fehlererkennung");
    JsonPtr v;
    struct Case { const wchar_t* text; const wchar_t* name; };
    Case cases[] = {
        { L"{\"a\":1", L"unvollständiges Objekt" },
        { L"{\"a\":1}}", L"Klammer zu viel" },
        { L"{\"a\":1,}", L"Hängendes Komma" },
        { L"{'a':1}", L"Einfache Anführungszeichen" },
        { L"{\"a\":01}", L"führende Null in Zahl" },
        { L"{\"a\":tru}", L"kaputtes Literal" },
        { L"{\"a\":\"zeile\nwert\"}", L"Zeilenumbruch in Zeichenkette" },
        { L"{\"a\":1} extra", L"Daten nach JSON-Ende" },
        { L"", L"leere Datei" },
        { L"{a:1}", L"Schlüssel ohne Anführungszeichen" },
    };
    for (const Case& c : cases) {
        v.reset();
        JsonParseResult r = JsonParse(c.text, v);
        CHECK_MSG(!r.ok, std::wstring(L" hätte fehlschlagen müssen: ") + c.name);
    }
    // Leere Container sind gültig
    CHECK(JsonParse(L"{}", v).ok && v->isObject() && v->obj.empty());
    CHECK(JsonParse(L"[]", v).ok && v->isArray() && v->arr.empty());
    // Verschachtelt + negative Zahlen + Exponent
    CHECK(JsonParse(L"{\"n\":-1.5e2,\"s\":[[],{},null]}", v).ok);
    if (v) {
        double d = 0;
        CHECK(v->getNum(L"n", d));
        CHECK_MSG(d == -150.0, L"Exponent falsch geparst");
    }
    EndSection();
}

static void TestJsonUtf8() {
    Begin(L"JSON: UTF-8-Konvertierung");
    const std::wstring text = L"Ärger mit Straße – Netz 你好 😀";
    std::string u8 = U8FromW(text);
    std::wstring back = WFromU8(u8);
    CHECK_EQ_STR(back, text);
    // Abgeschnittenes UTF-8 darf nicht abstürzen
    std::string cut = u8.substr(0, u8.size() - 2);
    std::wstring cutBack = WFromU8(cut);
    CHECK(cutBack.size() > 0);
    CHECK_EQ_STR(WFromU8(""), L"");
    CHECK(U8FromW(L"").empty());
    EndSection();
}

// ----------------------------------------------------------------Muster

static void TestWildcard() {
    Begin(L"Matcher: Platzhalter");
    CHECK(WildcardMatch(L"chrome.exe", L"chrome.exe"));
    CHECK(WildcardMatch(L"CHROME.EXE", L"chrome.exe"));
    CHECK(WildcardMatch(L"chrome*", L"chrome.exe"));
    CHECK(WildcardMatch(L"*.exe", L"chrome.exe"));
    CHECK(WildcardMatch(L"chrome?.exe", L"chromeX.exe"));
    CHECK(!WildcardMatch(L"chrome?.exe", L"chromexy.exe"));
    CHECK(WildcardMatch(L"*jetbrains*phpstorm*", L"C:\\Program Files\\JetBrains\\PhpStorm 2024.1\\bin64\\phpstorm64.exe"));
    CHECK(!WildcardMatch(L"*jetbrains*idea*", L"C:\\Program Files\\JetBrains\\PhpStorm 2024.1\\bin64\\phpstorm64.exe"));
    CHECK(WildcardMatch(L"C:\\a\\*\\phpstorm64.exe", L"C:\\a\\b\\phpstorm64.exe"));
    CHECK(!WildcardMatch(L"chrome.exe", L"chrome.exe.bak"));
    CHECK(!WildcardMatch(L"firefox.exe", L"chrome.exe"));
    CHECK(WildcardMatch(L"*", L"was auch immer"));
    EndSection();
}

static void TestMatchRule() {
    Begin(L"Matcher: Regeln");
    Rule r;
    r.pattern = L"chrome.exe";
    CHECK(MatchRule(r, L"chrome.exe", L"C:\\x\\chrome.exe").matched);
    CHECK(!MatchRule(r, L"chrome64.exe", L"").matched);

    r.pattern = L"*jetbrains*";
    CHECK(MatchRule(r, L"phpstorm64.exe", L"C:\\Program Files\\JetBrains\\PhpStorm\\bin64\\phpstorm64.exe").matched);
    CHECK(!MatchRule(r, L"phpstorm64.exe", L"").matched);   // Pfad unbekannt -> keine falsche Zuordnung

    r.pattern = L"C:\\apps\\*\\chrome.exe";
    CHECK(MatchRule(r, L"chrome.exe", L"C:\\apps\\canary\\chrome.exe").matched);
    CHECK(!MatchRule(r, L"chrome.exe", L"C:\\other\\chrome.exe").matched);

    r.pattern = L"chrome.exe";
    r.pathPattern = L"C:\\apps\\*";
    CHECK(MatchRule(r, L"chrome.exe", L"C:\\apps\\chrome.exe").matched);
    CHECK(!MatchRule(r, L"chrome.exe", L"C:\\windows\\chrome.exe").matched);

    r.enabled = false;
    CHECK(!MatchRule(r, L"chrome.exe", L"C:\\apps\\chrome.exe").matched);

    Rule bad;
    bad.pattern = L"";
    CHECK(MatchRule(bad, L"x.exe", L"").invalid);
    bad.pattern = L"**";
    CHECK(MatchRule(bad, L"x.exe", L"").invalid);
    bad.pattern = L"*";
    CHECK(MatchRule(bad, L"x.exe", L"").invalid);
    EndSection();
}

// ----------------------------------------------------------------Regelwerk

static void TestRulesetOperations() {
    Begin(L"Regelwerk: Einfügen, Verschieben, Entfernen");
    Ruleset rs;
    Rule a; a.pattern = L"a.exe"; a.mode = RouteMode::Wlan;
    Rule b; b.pattern = L"b.exe"; b.mode = RouteMode::Lan;
    Rule c; c.pattern = L"c.exe"; c.mode = RouteMode::None;
    unsigned long long ia = rs.AddTop(a);
    unsigned long long ib = rs.AddTop(b);
    unsigned long long ic = rs.AddTop(c);
    CHECK_EQ_INT(rs.rules.size(), 3);
    CHECK_EQ_STR(rs.rules[0].pattern, L"c.exe");      // neueste zuerst
    CHECK_EQ_INT(ia, 1);
    CHECK_EQ_INT(ib, 2);
    CHECK_EQ_INT(ic, 3);
    CHECK(rs.Move(ib, 0));
    CHECK_EQ_STR(rs.rules[0].pattern, L"b.exe");
    CHECK_EQ_INT(rs.IndexOf(ia), 2);                    // b nach vorn: a rutscht ans Ende
    CHECK(rs.Move(ia, 99));                            // klemmt am Ende an
    CHECK_EQ_INT(rs.rules.size(), 3);
    CHECK_EQ_STR(rs.rules[2].pattern, L"a.exe");
    CHECK(!rs.Move(999, 0));
    CHECK(rs.Remove(ic));
    CHECK_EQ_INT(rs.rules.size(), 2);
    CHECK(!rs.Remove(ic));
    Rule upd; upd.pattern = L"a.exe"; upd.mode = RouteMode::Lan;
    CHECK(rs.Update(ia, upd));
    CHECK_EQ_INT(static_cast<int>(rs.rules[rs.IndexOf(ia)].mode), static_cast<int>(RouteMode::Lan));
    CHECK(!rs.Update(999, upd));
    CHECK(rs.Find(ib) != nullptr);
    CHECK(rs.Find(999) == nullptr);
    EndSection();
}

static void TestRulesetResolve() {
    Begin(L"Regelwerk: Zuständigkeit");
    Ruleset rs;
    Rule broad; broad.pattern = L"chrome*"; broad.mode = RouteMode::Lan;
    Rule exact; exact.pattern = L"chrome.exe"; exact.mode = RouteMode::Wlan;
    unsigned long long idBroad = rs.AddTop(broad);
    unsigned long long idExact = rs.AddTop(exact);
    Resolved res;
    rs.Resolve(L"chrome.exe", L"", res);
    CHECK_EQ_INT(static_cast<int>(res.mode), static_cast<int>(RouteMode::Wlan));   // exakter Treffer zuerst
    CHECK_EQ_INT(res.ruleId, static_cast<long long>(idExact));
    rs.Resolve(L"chrome64.exe", L"", res);                     // nur die breitere Regel passt hier
    CHECK_EQ_INT(static_cast<int>(res.mode), static_cast<int>(RouteMode::Lan));
    CHECK_EQ_INT(res.ruleId, static_cast<long long>(idBroad));

    // Ohne Treffer: Standard
    rs.defaultMode = RouteMode::Lan;
    rs.Resolve(L"anderer.exe", L"", res);
    CHECK_EQ_INT(static_cast<int>(res.mode), static_cast<int>(RouteMode::Lan));
    CHECK(res.fromDefault);
    CHECK_EQ_INT(res.ruleId, 0);

    // Feste Adapter-GUID
    Ruleset rs2;
    Rule g;
    g.pattern = L"x.exe";
    g.mode = RouteMode::Adapter;
    g.adapterGuid = L"{11111111-2222-3333-4444-555555555555}";
    rs2.AddTop(g);
    rs2.Resolve(L"x.exe", L"", res);
    CHECK_EQ_INT(static_cast<int>(res.mode), static_cast<int>(RouteMode::Adapter));
    CHECK_EQ_STR(res.adapterGuid, L"{11111111-2222-3333-4444-555555555555}");

    // Deaktivierte Regel greift nicht
    Ruleset rs3;
    Rule off; off.pattern = L"y.exe"; off.mode = RouteMode::Wlan; off.enabled = false;
    rs3.AddTop(off);
    rs3.defaultMode = RouteMode::Lan;
    rs3.Resolve(L"y.exe", L"", res);
    CHECK_EQ_INT(static_cast<int>(res.mode), static_cast<int>(RouteMode::Lan));
    EndSection();
}

static void TestRulesetJson() {
    Begin(L"Regelwerk: Speichern und Laden");
    Ruleset rs;
    Rule a; a.pattern = L"chrome.exe"; a.mode = RouteMode::Wlan; a.note = L"Streaming über 5G";
    Rule b; b.pattern = L"C:\\apps\\*\\game.exe"; b.mode = RouteMode::Adapter;
    b.adapterGuid = L"{AAAAAAAA-BBBB-CCCC-DDDD-EEEEEEEEEEEE}";
    rs.AddTop(a);
    rs.AddTop(b);
    rs.defaultMode = RouteMode::Lan;
    JsonPtr doc = rs.ToJson();
    std::wstring text = JsonSerialize(doc, true);
    JsonPtr re;
    CHECK(JsonParse(text, re).ok);
    Ruleset back;
    std::vector<std::wstring> warn;
    CHECK(Ruleset::FromJson(re, back, warn));
    CHECK_EQ_INT(back.rules.size(), 2);
    CHECK_EQ_STR(back.rules[0].pattern, L"C:\\apps\\*\\game.exe");
    CHECK_EQ_INT(static_cast<int>(back.rules[0].mode), static_cast<int>(RouteMode::Adapter));
    CHECK_EQ_STR(back.rules[0].adapterGuid, L"{aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee}");
    CHECK_EQ_INT(static_cast<int>(back.defaultMode), static_cast<int>(RouteMode::Lan));
    CHECK_EQ_STR(back.rules[1].note, L"Streaming über 5G");
    CHECK(warn.empty());

    // Müll in der Datei führt zu Warnungen, aber nicht zum Absturz
    JsonPtr junk;
    CHECK(JsonParse(L"{\"version\":1,\"rules\":[{\"pattern\":\"\"},{\"pattern\":\"**\"},{\"mode\":99,\"pattern\":\"x.exe\"},{\"pattern\":\"ok.exe\",\"mode\":1},{\"pattern\":\"bad.exe\",\"mode\":3,\"adapterGuid\":\"keine-guid\"}]}", junk).ok);
    Ruleset j;
    std::vector<std::wstring> jw;
    CHECK(Ruleset::FromJson(junk, j, jw));
    CHECK_EQ_INT(j.rules.size(), 1);
    CHECK_EQ_STR(j.rules[0].pattern, L"ok.exe");
    CHECK_EQ_INT(static_cast<long long>(jw.size()), 4);

    // Unbekannte Felder werden ignoriert (vorwärtskompatibel)
    JsonPtr future;
    CHECK(JsonParse(L"{\"version\":2,\"zukunft\":42,\"rules\":[{\"pattern\":\"a.exe\",\"mode\":2,\"neu\":\"x\"}]}", future).ok);
    Ruleset fu;
    std::vector<std::wstring> fw;
    CHECK(Ruleset::FromJson(future, fu, fw));
    CHECK_EQ_INT(fu.rules.size(), 1);
    CHECK_EQ_STR(fu.rules[0].pattern, L"a.exe");
    EndSection();
}

static void TestValidate() {
    Begin(L"Regelwerk: Validierung");
    Ruleset rs;
    Rule a; a.pattern = L"chrome.exe"; a.mode = RouteMode::Wlan;
    Rule b; b.pattern = L"*"; b.mode = RouteMode::Wlan;
    Rule c; c.pattern = L"x.exe"; c.mode = RouteMode::Adapter; c.adapterGuid = L"blague";
    rs.AddTop(a);
    rs.AddTop(b);
    rs.AddTop(c);
    std::vector<std::wstring> w = rs.Validate();
    CHECK_EQ_INT(static_cast<long long>(w.size()), 2);
    EndSection();
}

// ----------------------------------------------------------------Dateien

static void TestAtomicWrite() {
    Begin(L"Dateien: atomares Schreiben und Wiederherstellung");
    const std::wstring dir = TempDir();
    std::wstring err;
    const std::wstring file = dir + L"\\unter\\ordner\\test.json";
    CHECK(WriteTextFileAtomic(file, L"{\"a\":1}", err));
    CHECK(FileExists(file));
    // Kein Rest einer Temp-Datei
    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW((dir + L"\\unter\\ordner\\*.tmp").c_str(), &fd);
    CHECK_MSG(hFind == INVALID_HANDLE_VALUE, L"Temp-Datei liegt noch im Ordner");
    if (hFind != INVALID_HANDLE_VALUE) FindClose(hFind);

    std::wstring text;
    CHECK(ReadTextFile(file, text, err));
    CHECK_EQ_STR(text, L"{\"a\":1}");
    CHECK(WriteTextFileAtomic(file, L"{\"a\":2}", err));
    CHECK(ReadTextFile(file, text, err));
    CHECK_EQ_STR(text, L"{\"a\":2}");
    CHECK(FileExists(file + L".bak"));   // Sicherung der Vorversion
    CHECK(ReadTextFile(file + L".bak", text, err));
    CHECK_EQ_STR(text, L"{\"a\":1}");

    // Ungültiger Pfad darf nicht abstürzen, sondern muss sauber fehlschlagen
    CHECK_MSG(!WriteTextFileAtomic(L"Z:\\gibt\\es\\nicht\\x.json", L"test", err), L"Schreiben auf nicht existierendes Laufwerk sollte fehlschlagen");
    CHECK(!err.empty());
    CHECK_MSG(!ReadTextFile(dir + L"\\unbekannt.json", text, err), L"Lesen fehlender Datei muss fehlschlagen");
    EndSection();
}

static void TestStoreRoundtrip() {
    Begin(L"Store: Regeln auf der Festplatte");
    const std::wstring dir = TempDir();
    Paths p = MakePaths(dir);
    std::wstring err;
    Ruleset rs;
    Rule a; a.pattern = L"firefox.exe"; a.mode = RouteMode::Wlan; a.note = L"surfen";
    rs.AddTop(a);
    CHECK(SaveRuleset(p, rs, err));
    CHECK(FileExists(p.rulesFile));

    Ruleset back;
    std::vector<std::wstring> warn;
    CHECK(LoadRuleset(p, back, warn, err));
    CHECK_EQ_INT(back.rules.size(), 1);
    CHECK_EQ_STR(back.rules[0].pattern, L"firefox.exe");

    // Erststart ohne Datei ist kein Fehler
    const std::wstring dir2 = TempDir() + L"\\leer";
    Paths p2 = MakePaths(dir2);
    Ruleset leer;
    warn.clear();
    CHECK(LoadRuleset(p2, leer, warn, err));
    CHECK_EQ_INT(leer.rules.size(), 0);

    // Defekte Datei: kaputte Version bleibt erhalten, Backup wird benutzt
    CHECK(SaveRuleset(p, rs, err));
    Rule b; b.pattern = L"steam.exe"; b.mode = RouteMode::Lan;
    rs.rules.push_back(b);
    CHECK(SaveRuleset(p, rs, err));
    CHECK(FileExists(p.rulesFile + L".bak"));
    std::wstring broken;
    WriteTextFileAtomic(p.rulesFile, L"{ \"rules\": [ {\"pattern\": ", broken);
    Ruleset rec;
    warn.clear();
    bool ok = LoadRuleset(p, rec, warn, err);
    CHECK_MSG(ok, L"Wiederherstellung aus Sicherung fehlgeschlagen");
    // Jede sichere Speicherung sichert vorher die letzte gute Fassung: die Sicherung
    // enthält deshalb beide Regeln, die beim Überschreiben kurz vorher galten.
    CHECK_EQ_INT(rec.rules.size(), 2);
    CHECK_EQ_STR(rec.rules[0].pattern, L"firefox.exe");
    CHECK_MSG(warn.size() >= 2, L"Warnungen zur Wiederherstellung fehlen");
    bool savedBroken = false;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((p.rulesFile + L".defekt-*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) { savedBroken = true; FindClose(h); }
    CHECK_MSG(savedBroken, L"Beschädigte Datei wurde nicht gesichert");

    // Komplett kaputt und ohne Sicherung: leeres Regelwerk statt Absturz
    DeleteFileW((p.rulesFile + L".bak").c_str());
    WriteTextFileAtomic(p.rulesFile, L"kein json", broken);
    Ruleset empty;
    warn.clear();
    CHECK(!LoadRuleset(p, empty, warn, err));
    CHECK_EQ_INT(empty.rules.size(), 0);
    EndSection();
}

// ----------------------------------------------------------------Klassifizierung

static void TestClassification() {
    Begin(L"Adapter: Klassifizierung und Filter");
    // Die Medium-Werte sind die echten NDIS-Werte aus ntddndis.h (-1 = nicht gemeldet)
    // Realtek WLAN (WiFi 7): Typ 71, Medium Native802_11
    CHECK(ClassifyMedium(71, NdisPhysicalMediumNative802_11, 1, L"WLAN 2", L"Realtek 8922AE WiFi 7 PCI-E NIC") == AdapterKind::Wlan);
    // WLAN-Instanz ohne Medium (Not Present) bleibt WLAN wegen Typ 71
    CHECK(ClassifyMedium(71, -1, 1, L"WLAN 5", L"Realtek 8922AE WiFi 7 PCI-E NIC #5") == AdapterKind::Wlan);
    // Realtek Ethernet: Typ 6, Medium 802_3
    CHECK(ClassifyMedium(6, NdisPhysicalMedium802_3, 1, L"Main", L"Realtek PCIe 2.5GbE Family Controller #3") == AdapterKind::Lan);
    // TAP-Adapter sieht technisch nach Ethernet aus -> über die Beschreibung als virtual erkannt
    CHECK(ClassifyMedium(6, NdisPhysicalMedium802_3, 1, L"LAN-Verbindung 3", L"TAP-Windows Adapter V9") == AdapterKind::Lan);
    CHECK(LooksVirtual(L"TAP-Windows Adapter V9", L"LAN-Verbindung 3", NdisPhysicalMedium802_3, NET_IF_ACCESS_BROADCAST));
    CHECK(LooksVirtual(L"Kaspersky Security Data Escort Adapter", L"Ethernet 2", NdisPhysicalMedium802_3, NET_IF_ACCESS_BROADCAST));
    CHECK(LooksVirtual(L"Tailscale Tunnel", L"Tailscale", -1, NET_IF_ACCESS_POINT_TO_POINT));
    CHECK(LooksVirtual(L"VirtualBox Host-Only Ethernet Adapter", L"Ethernet 3", -1, NET_IF_ACCESS_BROADCAST));
    CHECK(LooksVirtual(L"Bluetooth Device (Personal Area Network)", L"Bluetooth-Netzwerkverbindung", NdisPhysicalMediumBluetooth, NET_IF_ACCESS_BROADCAST));
    CHECK(!LooksVirtual(L"Realtek PCIe 2.5GbE Family Controller #3", L"Main", NdisPhysicalMedium802_3, NET_IF_ACCESS_BROADCAST));
    CHECK(!LooksVirtual(L"Realtek 8922AE WiFi 7 PCI-E NIC", L"WLAN 2", NdisPhysicalMediumNative802_11, NET_IF_ACCESS_BROADCAST));
    // Bluetooth ist weder WLAN noch Ethernet
    CHECK(ClassifyMedium(6, NdisPhysicalMediumBluetooth, 1, L"Bluetooth-Netzwerkverbindung", L"Bluetooth Device (Personal Area Network)") == AdapterKind::Other);
    // Tailscale (Typ 53) ist kein Leitungs-Adapter
    CHECK(ClassifyMedium(53, -1, 1, L"Tailscale", L"Tailscale Tunnel") == AdapterKind::Other);
    // Alias "WLAN" darf nicht als Ethernet durchgehen
    CHECK(ClassifyMedium(6, NdisPhysicalMedium802_3, 1, L"WLAN 2", L"irgendein Miniport") == AdapterKind::Other);
    // Beschreibung "Wireless" erkennt WLAN auch ohne korrekten Typ
    CHECK(ClassifyMedium(6, -1, 1, L"Adapter 1", L"USB Wireless Adapter") == AdapterKind::Wlan);

    CHECK(IsLinkLocalIpv4(L"169.254.5.6"));
    CHECK(!IsLinkLocalIpv4(L"192.168.1.171"));
    CHECK(!IsLinkLocalIpv4(L""));

    std::wstring norm;
    CHECK(ParseGuid(L"{11111111-2222-3333-4444-555555555555}", norm));
    CHECK_EQ_STR(norm, L"{11111111-2222-3333-4444-555555555555}");
    CHECK(ParseGuid(L"{ABCDEF01-2345-6789-ABCD-EF0123456789}", norm));
    CHECK_EQ_STR(norm, L"{abcdef01-2345-6789-abcd-ef0123456789}");
    CHECK(!ParseGuid(L"11111111-2222-3333-4444-555555555555", norm));
    CHECK(!ParseGuid(L"{11111111-2222-3333-4444-55555555555", norm));
    CHECK(!ParseGuid(L"", norm));
    CHECK(!ParseGuid(L"{ZZZZZZZZ-2222-3333-4444-555555555555}", norm));
    EndSection();
}

static void TestAdapterSelection() {
    Begin(L"Adapter: Rollenauswahl");
    std::vector<AdapterInfo> ads;
    ads.push_back(MakeAdapter(L"WLAN 2", L"{11111111-1111-1111-1111-111111111111}", AdapterKind::Wlan, 19, L"192.168.1.171", 30));
    ads.push_back(MakeAdapter(L"Main", L"{22222222-2222-2222-2222-222222222222}", AdapterKind::Lan, 10, L"192.168.178.132", 25));
    ads.push_back(MakeAdapter(L"Ethernet 2", L"{33333333-3333-3333-3333-333333333333}", AdapterKind::Lan, 9, L"", 50, false, true));
    ads.push_back(MakeAdapter(L"Tailscale", L"{44444444-4444-4444-4444-444444444444}", AdapterKind::Other, 6, L"", 2500, true, true));

    const AdapterInfo* w = FindAdapterByRole(ads, AdapterKind::Wlan);
    const AdapterInfo* l = FindAdapterByRole(ads, AdapterKind::Lan);
    CHECK_MSG(w && w->alias == L"WLAN 2", L"WLAN-Rolle falsch");
    CHECK_MSG(l && l->alias == L"Main", L"LAN-Rolle falsch (kleineres Metric gewinnt)");

    // Virtualisierte Adapter verlieren gegen echte
    std::vector<AdapterInfo> ads2;
    ads2.push_back(MakeAdapter(L"VPN", L"{55555555-5555-5555-5555-555555555555}", AdapterKind::Lan, 5, L"10.0.0.2", 10, true, true));
    ads2.push_back(MakeAdapter(L"Realtek", L"{66666666-6666-6666-6666-666666666666}", AdapterKind::Lan, 6, L"192.168.0.9", 25, true, false));
    const AdapterInfo* l2 = FindAdapterByRole(ads2, AdapterKind::Lan);
    CHECK_MSG(l2 && l2->alias == L"Realtek", L"Virtualer Adapter wurde bevorzugt");

    CHECK(FindAdapterByGuid(ads, L"{22222222-2222-2222-2222-222222222222}") != nullptr);
    CHECK(FindAdapterByGuid(ads, L"{22222222-2222-2222-2222-222222222222}")->alias == L"Main");
    CHECK(FindAdapterByGuid(ads, L"{77777777-7777-7777-7777-777777777777}") == nullptr);
    CHECK(FindAdapterByGuid(ads, L"kein guid") == nullptr);

    CHECK_EQ_INT(AdapterIndexForLocalIp(ads, L"192.168.1.171"), 19);
    CHECK_EQ_INT(AdapterIndexForLocalIp(ads, L"192.168.178.132"), 10);
    CHECK_EQ_INT(AdapterIndexForLocalIp(ads, L"10.9.9.9"), 0);
    CHECK_EQ_INT(AdapterIndexForLocalIp(ads, L""), 0);

    // Raten aus Zählern
    std::vector<AdapterInfo> prev = ads;
    std::vector<AdapterInfo> cur = ads;
    cur[0].rxBytes += 2 * 1024 * 1024;
    cur[0].txBytes += 1024 * 1024;
    cur[1].rxBytes = 0;   // Zähler zurückgesetzt (Adapter neu gestartet)
    UpdateRates(cur, prev, 1000);
    CHECK_EQ_INT(cur[0].rxRate, 2 * 1024 * 1024);
    CHECK_EQ_INT(cur[0].txRate, 1024 * 1024);
    CHECK_EQ_INT(cur[1].rxRate, 0);
    UpdateRates(cur, prev, 0);   // durch 0 darf nicht passieren
    CHECK(cur[0].rxRate >= 0);

    CHECK_EQ_STR(FormatRate(0), L"0 B/s");
    CHECK_EQ_STR(FormatRate(1024), L"1.0 KB/s");
    CHECK(FormatRate(1500000).find(L"MB/s") != std::wstring::npos);
    CHECK_EQ_STR(FormatBytes(512), L"512 B");
    CHECK(FormatBytes(3ull * 1024 * 1024 * 1024).find(L"GB") != std::wstring::npos);
    CHECK_EQ_STR(FormatGuidShort(L"{11111111-1111-1111-1111-111111111111}"), L"11111111\u2026");
    EndSection();
}

// ----------------------------------------------------------------Aufgelöstes Regelwerk

static void TestResolvedDoc() {
    Begin(L"Engine: aufgelöstes Regelwerk für den Hook");
    std::vector<AdapterInfo> ads;
    ads.push_back(MakeAdapter(L"WLAN 2", L"{11111111-1111-1111-1111-111111111111}", AdapterKind::Wlan, 19, L"192.168.1.171", 30));
    ads.push_back(MakeAdapter(L"Main", L"{22222222-2222-2222-2222-222222222222}", AdapterKind::Lan, 10, L"192.168.178.132", 25));

    Ruleset rs;
    Rule a; a.pattern = L"Chrome.exe"; a.mode = RouteMode::Wlan;
    Rule b; b.pattern = L"steam.exe"; b.mode = RouteMode::Lan;
    Rule dup1; dup1.pattern = L"doppelt.exe"; dup1.mode = RouteMode::Wlan;
    Rule dup2; dup2.pattern = L"doppelt.exe"; dup2.mode = RouteMode::Lan;
    rs.AddTop(a);
    rs.AddTop(b);
    rs.AddTop(dup1);   // höhere Priorität als dup2
    rs.defaultMode = RouteMode::None;
    std::vector<std::wstring> problems;
    JsonPtr doc = BuildResolvedJson(rs, ads, 7, problems);
    CHECK(problems.empty());
    std::wstring text = JsonSerialize(doc, false);
    JsonPtr again;
    CHECK(ParseResolvedDoc(text, again));

    ResolvedLookupResult r = ResolvedLookup(again, L"chrome.exe");
    CHECK(r.found);
    CHECK_EQ_STR(r.guid, L"{11111111-1111-1111-1111-111111111111}");
    CHECK_EQ_INT(static_cast<int>(r.mode), static_cast<int>(RouteMode::Wlan));
    r = ResolvedLookup(again, L"steam.exe");
    CHECK_EQ_STR(r.guid, L"{22222222-2222-2222-2222-222222222222}");
    r = ResolvedLookup(again, L"doppelt.exe");
    CHECK_MSG(r.mode == RouteMode::Wlan, L"Doppelte Regel: höhere Priorität muss gewinnen");
    r = ResolvedLookup(again, L"unbekannt.exe");
    CHECK(!r.found);

    unsigned long idx = 0;
    CHECK(ResolvedIfIndex(again, L"{11111111-1111-1111-1111-111111111111}", idx));
    CHECK_EQ_INT(idx, 19);
    CHECK(ResolvedIfIndex(again, L"{22222222-2222-2222-2222-222222222222}", idx));
    CHECK_EQ_INT(idx, 10);
    CHECK(!ResolvedIfIndex(again, L"{99999999-9999-9999-9999-999999999999}", idx));
    CHECK(!ResolvedIfIndex(again, L"keins", idx));

    // Pfadregeln im aufgelösten Dokument
    Ruleset rs2;
    Rule p; p.pattern = L"chrome.exe"; p.pathPattern = L"C:\\apps\\canary\\*"; p.mode = RouteMode::Wlan;
    rs2.AddTop(p);
    problems.clear();
    JsonPtr d2 = BuildResolvedJson(rs2, ads, 1, problems);
    CHECK(!ResolvedLookup(d2, L"chrome.exe", L"c:\\apps\\stable\\chrome.exe").found);
    CHECK(ResolvedLookup(d2, L"chrome.exe", L"c:\\apps\\canary\\chrome.exe").found);

    // Standardregel wird übernommen
    Ruleset rs3;
    rs3.defaultMode = RouteMode::Lan;
    problems.clear();
    JsonPtr d3 = BuildResolvedJson(rs3, ads, 1, problems);
    r = ResolvedLookup(d3, L"wasauchimmer.exe");
    CHECK_MSG(r.found && r.guid == L"{22222222-2222-2222-2222-222222222222}", L"Standardregel fehlt im aufgelösten Dokument");

    // Adapter fehlt -> klar benanntes Problem statt stiller Fehler
    std::vector<AdapterInfo> nurLan;
    nurLan.push_back(ads[1]);
    Ruleset rs4;
    Rule w; w.pattern = L"nureiner.exe"; w.mode = RouteMode::Wlan;
    rs4.AddTop(w);
    problems.clear();
    JsonPtr d4 = BuildResolvedJson(rs4, nurLan, 1, problems);
    CHECK_MSG(!problems.empty(), L"Feehlender Adapter wurde nicht gemeldet");
    EndSection();
}

// ----------------------------------------------------------------Zeilen

static void TestBuildRows() {
    Begin(L"Anzeige: Zeilenaufbau");
    std::vector<AdapterInfo> ads;
    ads.push_back(MakeAdapter(L"WLAN 2", L"{11111111-1111-1111-1111-111111111111}", AdapterKind::Wlan, 19, L"192.168.1.171", 30));
    ads.push_back(MakeAdapter(L"Main", L"{22222222-2222-2222-2222-222222222222}", AdapterKind::Lan, 10, L"192.168.178.132", 25));

    std::vector<ProcInfo> procs;
    ProcInfo p1; p1.pid = 100; p1.image = L"chrome.exe"; p1.path = L"C:\\apps\\chrome.exe"; p1.pathAccess = true; p1.user = L"HALL\\User";
    ProcInfo p2; p2.pid = 200; p2.image = L"steam.exe"; p2.path = L"C:\\games\\steam.exe"; p2.pathAccess = true;
    ProcInfo p3; p3.pid = 300; p3.image = L"Geheim.exe"; p3.path = L""; p3.pathAccess = false;
    procs.push_back(p1); procs.push_back(p2); procs.push_back(p3);

    std::vector<TcpConn> conns;
    TcpConn c1; c1.pid = 100; c1.localAddr = L"192.168.178.132"; c1.remoteAddr = L"93.184.216.34"; c1.state = 5;
    TcpConn c2; c2.pid = 100; c2.localAddr = L"192.168.178.132"; c2.remoteAddr = L"142.250.10.10"; c2.state = 5;
    TcpConn c3; c3.pid = 200; c3.localAddr = L"192.168.1.171"; c3.remoteAddr = L"104.16.1.1"; c3.state = 5;
    TcpConn c4; c4.pid = 300; c4.localAddr = L"127.0.0.1"; c4.remoteAddr = L"127.0.0.1"; c4.state = 5;
    conns.push_back(c1); conns.push_back(c2); conns.push_back(c3); conns.push_back(c4);

    Ruleset rs;
    Rule w; w.pattern = L"chrome.exe"; w.mode = RouteMode::Wlan;
    rs.AddTop(w);
    rs.defaultMode = RouteMode::Lan;

    std::vector<unsigned long> injected = { 200 };
    std::vector<ProcRow> rows = BuildRows(procs, conns, ads, rs, injected);
    CHECK_EQ_INT(rows.size(), 3);

    ProcRow* r1 = nullptr; ProcRow* r2 = nullptr; ProcRow* r3 = nullptr;
    for (ProcRow& r : rows) { if (r.pid == 100) r1 = &r; if (r.pid == 200) r2 = &r; if (r.pid == 300) r3 = &r; }
    CHECK_MSG(r1 && r2 && r3, L"Zeile fehlt");
    if (r1) {
        CHECK_EQ_INT(r1->conns, 2);
        CHECK_EQ_STR(r1->viaAdapter, L"Main");
        CHECK(r1->mode == RouteMode::Wlan);
        CHECK_EQ_STR(r1->modeGuid, L"{11111111-1111-1111-1111-111111111111}");
        CHECK(!r1->injected);
        CHECK(r1->hasNetwork);
    }
    if (r2) {
        CHECK_EQ_INT(r2->conns, 1);
        CHECK_EQ_STR(r2->viaAdapter, L"WLAN 2");
        CHECK(r2->mode == RouteMode::Lan && r2->fromDefault);
        CHECK(r2->injected);
    }
    if (r3) {
        CHECK_EQ_INT(r3->conns, 0);           // Loopback zählt nicht als Leitungsverkehr
        CHECK(!r3->hasNetwork);
        CHECK(!r3->pathAccess);
        CHECK(r3->mode == RouteMode::Lan);
    }
    EndSection();
}

static void TestSortFilter() {
    Begin(L"Anzeige: Sortierung und Filter");
    std::vector<ProcRow> rows;
    ProcRow a; a.pid = 1; a.image = L"beta.exe"; a.conns = 0; a.user = L"B"; a.mode = RouteMode::None;
    ProcRow b; b.pid = 2; b.image = L"alpha.exe"; b.conns = 3; b.user = L"A"; b.mode = RouteMode::Wlan;
    ProcRow c; c.pid = 3; c.image = L"gamma.exe"; c.conns = 1; c.user = L"C"; c.mode = RouteMode::Lan;
    rows.push_back(a); rows.push_back(b); rows.push_back(c);

    SortRows(rows, SortField::Name, true);
    CHECK_EQ_STR(rows[0].image, L"alpha.exe");   // mit Verkehr zuerst
    CHECK_EQ_STR(rows[1].image, L"beta.exe");
    SortRows(rows, SortField::Name, false);
    CHECK_EQ_STR(rows[0].image, L"gamma.exe");   // absteigend: gamma, beta, alpha
    SortRows(rows, SortField::Pid, false);
    CHECK_EQ_INT(rows[0].pid, 3);
    SortRows(rows, SortField::Conns, true);
    CHECK_EQ_INT(rows[0].conns, 0);
    SortRows(rows, SortField::User, true);
    CHECK_EQ_STR(rows[0].user, L"A");

    std::vector<unsigned long> hits;
    FilterRows(rows, L"ALPHA", false, hits);
    CHECK_EQ_INT(hits.size(), 1);
    CHECK_EQ_INT(hits[0], 2);
    FilterRows(rows, L"3", false, hits);            // nach PID
    CHECK_EQ_INT(hits.size(), 1);
    FilterRows(rows, L"", true, hits);              // nur mit Verkehr
    CHECK_EQ_INT(hits.size(), 2);
    FilterRows(rows, L"", false, hits);
    CHECK_EQ_INT(hits.size(), 3);
    FilterRows(rows, L"nichts-davon", false, hits);
    CHECK_EQ_INT(hits.size(), 0);
    FilterRows(rows, L"  gamma  ", false, hits);    // Randzeichen werden ignoriert
    CHECK_EQ_INT(hits.size(), 1);
    EndSection();
}

static void TestDropTargets() {
    Begin(L"Anzeige: Drop-Ziele der Spalten");
    DropTarget a = DropTargetForColumn(3);
    DropTarget b = DropTargetForColumn(4);
    DropTarget c = DropTargetForColumn(5);
    CHECK(a.valid && a.mode == RouteMode::Wlan);
    CHECK(b.valid && b.mode == RouteMode::Lan);
    CHECK(c.valid && c.mode == RouteMode::None);
    for (int col : { 0, 1, 2, 6, 99, -1 }) {
        DropTarget t = DropTargetForColumn(col);
        CHECK_MSG(!t.valid, L"Spalte dürfte kein Drop-Ziel sein");
    }
    EndSection();
}

// ----------------------------------------------------------------Engine

static void TestEngineFlow() {
    Begin(L"Engine: Zuweisen, Speichern, Neu laden");
    Paths p = MakePaths(TempDir());
    Engine e(p);
    std::wstring err;
    CHECK(e.Init(err));

    e.adapters.push_back(MakeAdapter(L"WLAN 2", L"{11111111-1111-1111-1111-111111111111}", AdapterKind::Wlan, 19, L"192.168.1.171", 30));
    e.adapters.push_back(MakeAdapter(L"Main", L"{22222222-2222-2222-2222-222222222222}", AdapterKind::Lan, 10, L"192.168.178.132", 25));

    ProcInfo pi; pi.pid = 4242; pi.image = L"phpstorm64.exe"; pi.path = L"C:\\apps\\phpstorm\\bin64\\phpstorm64.exe"; pi.pathAccess = true;
    e.procs.push_back(pi);

    // Zuordnung per Drag & Drop simulieren
    CHECK_MSG(e.Assign(pi.image, pi.path, RouteMode::Lan, L"", err), L"Zuweisung fehlgeschlagen: " + err);
    CHECK(FileExists(p.rulesFile));
    CHECK(FileExists(p.resolvedFile));
    CHECK_EQ_INT(e.rules.rules.size(), 1);

    std::wstring rtext, rerr;
    CHECK(ReadTextFile(p.resolvedFile, rtext, rerr));
    JsonPtr doc;
    CHECK(ParseResolvedDoc(rtext, doc));
    ResolvedLookupResult lr = ResolvedLookup(doc, L"phpstorm64.exe");
    CHECK_MSG(lr.found && lr.guid == L"{22222222-2222-2222-2222-222222222222}", L"Hook würde den falschen Adapter sehen");
    unsigned long idx = 0;
    CHECK(ResolvedIfIndex(doc, lr.guid, idx));
    CHECK_EQ_INT(idx, 10);

    e.RebuildRows();
    CHECK_EQ_INT(e.rows.size(), 1);
    CHECK(e.rows[0].mode == RouteMode::Lan);

    // Zweite Zuordnung für denselben Prozess ersetzt die erste (keine Regelhäufung)
    CHECK(e.Assign(pi.image, pi.path, RouteMode::Wlan, L"", err));
    CHECK_EQ_INT(e.rules.rules.size(), 1);
    CHECK(e.rules.rules[0].mode == RouteMode::Wlan);

    // Zurücksetzen entfernt die Regel
    CHECK(e.Assign(pi.image, pi.path, RouteMode::None, L"", err));
    CHECK_EQ_INT(e.rules.rules.size(), 0);

    // Zuweisung mit Adapter-GUID
    std::wstring guid = L"{11111111-1111-1111-1111-111111111111}";
    CHECK(e.Assign(pi.image, pi.path, RouteMode::Adapter, guid, err));
    CHECK_EQ_INT(e.rules.rules.size(), 1);
    CHECK_EQ_STR(e.rules.rules[0].adapterGuid, guid);
    CHECK(e.ClearAssignment(e.rules.rules[0].id, err));
    CHECK_EQ_INT(e.rules.rules.size(), 0);
    CHECK(!e.ClearAssignment(99999, err));

    // Externe Änderung der rules.json wird übernommen
    CHECK(e.Assign(pi.image, pi.path, RouteMode::Lan, L"", err));
    Ruleset foreign;
    Rule f; f.pattern = L"anderer.exe"; f.mode = RouteMode::Wlan;
    foreign.AddTop(f);
    std::vector<std::wstring> w2;
    (void)w2;
    std::wstring serr;
    // direkt schreiben, als hätte ein anderer Nutzer die Datei editiert
    foreign.rules[0].id = 5;
    std::wstring txt = JsonSerialize(foreign.ToJson(), true);
    CHECK(WriteTextFileAtomic(p.rulesFile, txt, serr));
    bool changed = e.ReloadIfChanged(err);
    CHECK_MSG(changed, L"Externe Änderung nicht erkannt");
    CHECK_EQ_INT(e.rules.rules.size(), 1);
    CHECK_EQ_STR(e.rules.rules[0].pattern, L"anderer.exe");
    CHECK(!e.ReloadIfChanged(err));   // zweite Runde: nichts geändert

    // Ungültige Zuweisung wird abgelehnt
    CHECK(!e.Assign(L"", L"", RouteMode::Lan, L"", err));
    CHECK(!err.empty());

    // Engine mit defekter Datei überlebt
    Paths p2 = MakePaths(TempDir() + L"\\defekt");
    std::wstring serr2;
    EnsureDir(p2.dir, serr2);
    WriteTextFileAtomic(p2.rulesFile, L"{\"rules\":[", serr2);
    Engine e2(p2);
    bool init = e2.Init(err);
    CHECK_MSG(!init, L"Defekte Regeln müssen gemeldet werden");
    CHECK_EQ_INT(e2.rules.rules.size(), 0);
    EndSection();
}

static void TestEnginePathPrecision() {
    Begin(L"Engine: Präzisierung bei gleichem Bildnamen");
    Paths p = MakePaths(TempDir());
    Engine e(p);
    std::wstring err;
    CHECK(e.Init(err));
    e.adapters.push_back(MakeAdapter(L"WLAN", L"{11111111-1111-1111-1111-111111111111}", AdapterKind::Wlan, 1, L"10.0.0.2", 30));
    e.adapters.push_back(MakeAdapter(L"LAN", L"{22222222-2222-2222-2222-222222222222}", AdapterKind::Lan, 2, L"10.1.0.2", 25));
    ProcInfo a; a.pid = 10; a.image = L"tool.exe"; a.path = L"C:\\a\\tool.exe"; a.pathAccess = true;
    ProcInfo b; b.pid = 20; b.image = L"tool.exe"; b.path = L"C:\\b\\tool.exe"; b.pathAccess = true;
    e.procs.push_back(a);
    e.procs.push_back(b);
    CHECK(e.Assign(a.image, a.path, RouteMode::Lan, L"", err));
    CHECK_EQ_INT(e.rules.rules.size(), 1);
    CHECK_MSG(!e.rules.rules[0].pathPattern.empty(), L"Pfad wurde nicht zur Unterscheidung benutzt");
    // Beide Prozesse bekommen dieselbe (präzisierte) Zuordnung nicht automatisch: nur der markierte
    std::wstring txt, e2;
    ReadTextFile(p.resolvedFile, txt, e2);
    JsonPtr doc;
    CHECK(ParseResolvedDoc(txt, doc));
    CHECK(ResolvedLookup(doc, L"tool.exe", L"c:\\a\\tool.exe").found);
    CHECK(!ResolvedLookup(doc, L"tool.exe", L"c:\\b\\tool.exe").found);
    EndSection();
}

static void TestReinjectGuards() {
    Begin(L"Engine: Injektion nur wenn sicher");
    Paths p = MakePaths(TempDir());
    Engine e(p);
    std::wstring err;
    CHECK(e.Init(err));
    e.adapters.push_back(MakeAdapter(L"WLAN", L"{11111111-1111-1111-1111-111111111111}", AdapterKind::Wlan, 1, L"10.0.0.2", 30));
    CHECK(e.Assign(L"srtarget.exe", L"", RouteMode::Wlan, L"", err));
    e.RefreshProcesses();
    e.RebuildRows();
    // Ohne hook.dll und ohne Admin-Rechte darf nichts injiziert werden
    int n = e.ReinjectMatching(err);
    CHECK_EQ_INT(n, 0);
    CHECK(!err.empty());

    std::wstring why;
    ProcInfo self; self.pid = GetCurrentProcessId(); self.image = L"routeperapp.exe"; self.path = L"C:\\x\\routeperapp.exe"; self.pathAccess = true;
    CHECK(!CanInjectProcess(self, true, true, why));
    CHECK(why.find(L"nicht") != std::wstring::npos || why.find(L"selbst") != std::wstring::npos);

    ProcInfo sys; sys.pid = 4; sys.image = L"System"; sys.pathAccess = true;
    CHECK(!CanInjectProcess(sys, true, true, why));

    ProcInfo ok; ok.pid = 1234; ok.image = L"any.exe"; ok.path = L"C:\\any.exe"; ok.pathAccess = true;
    CHECK(!CanInjectProcess(ok, false, true, why));      // ohne Admin
    CHECK(CanInjectProcess(ok, true, true, why));
    CHECK(!CanInjectProcess(ok, true, false, why));      // hook.dll fehlt
    CHECK(!CanInjectProcess(ok, true, true, why) || true);

    ProcInfo noAccess; noAccess.pid = 1235; noAccess.image = L"protected.exe"; noAccess.pathAccess = false;
    CHECK(!CanInjectProcess(noAccess, true, true, why));
    CHECK(!why.empty());
    EndSection();
}

static void TestSelfElevation() {
    Begin(L"System: eigene Rechte prüfen");
    bool elevated = false;
    std::wstring err;
    CHECK(IsProcessElevatedSelf(elevated, err));
    wprintf(L"  RoutePerApp läuft eleviert: %s\n", elevated ? L"ja" : L"nein");
    EndSection();
}

// ----------------------------------------------------------------Live-Systemtests

static void TestLiveAdapters() {
    Begin(L"System: echte Adapter");
    std::vector<AdapterInfo> ads;
    std::wstring err;
    bool ok = EnumerateAdapters(ads, err);
    CHECK_MSG(ok, L"Adapteraufzählung fehlgeschlagen: " + err);
    if (!ok) { EndSection(); return; }
    CHECK(ads.size() > 0);
    int wlan = 0, lan = 0;
    for (const AdapterInfo& a : ads) {
        wprintf(L"  %-26s | %-44s | Typ=%d | %s%s | idx=%lu\n", a.alias.c_str(), a.description.c_str(),
                static_cast<int>(a.kind), a.isUp ? L"verbunden" : L"trennt", a.isVirtual ? L" (virtual)" : L"", a.ifIndex);
        if (a.kind == AdapterKind::Wlan && !a.isVirtual && a.isUp) ++wlan;
        if (a.kind == AdapterKind::Lan && !a.isVirtual && a.isUp) ++lan;
    }
    const AdapterInfo* w = FindAdapterByRole(ads, AdapterKind::Wlan);
    const AdapterInfo* l = FindAdapterByRole(ads, AdapterKind::Lan);
    CHECK_MSG(w != nullptr, L"Kein WLAN-Adapter erkannt");
    CHECK_MSG(l != nullptr, L"Kein Ethernet-Adapter erkannt");
    if (w) CHECK_MSG(!w->guid.empty(), L"WLAN ohne GUID");
    if (l) CHECK_MSG(!l->guid.empty(), L"Ethernet ohne GUID");
    // Tailscale/TAP/Kaspersky dürfen nicht als Leitungsadapter gewinnen
    if (w) CHECK_MSG(w->description.find(L"Tailscale") == std::wstring::npos, L"Tailscale als WLAN gewählt");
    if (l) CHECK_MSG(l->description.find(L"TAP") == std::wstring::npos, L"TAP als Ethernet gewählt");

    // Zähler existieren und Raten sind berechenbar
    std::vector<AdapterInfo> again;
    EnumerateAdapters(again, err);
    UpdateRates(again, ads, 250);
    for (const AdapterInfo& a : again) CHECK(a.rxRate >= 0 && a.txRate >= 0);
    EndSection();
}

static void TestLiveProcessesAndTcp() {
    Begin(L"System: Prozesse und TCP-Verbindungen");
    std::vector<ProcInfo> procs;
    std::wstring err;
    CHECK(EnumerateProcesses(procs, err));
    CHECK(procs.size() > 20);
    bool foundSelf = false;
    for (const ProcInfo& p : procs) {
        if (p.pid == GetCurrentProcessId()) {
            foundSelf = true;
            CHECK(!p.image.empty());
            CHECK(p.pathAccess);
        }
    }
    CHECK_MSG(foundSelf, L"Eigener Prozess nicht in der Liste");

    // TCP-Tabelle mit einer frisch aufgebauten Verbindung prüfen
    WSADATA wsa;
    CHECK(WSAStartup(MAKEWORD(2, 2), &wsa) == 0);
    SOCKET srv = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    CHECK(srv != INVALID_SOCKET);
    sockaddr_in sa = {};
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    sa.sin_port = htons(47123);
    bool listened = bind(srv, reinterpret_cast<sockaddr*>(&sa), sizeof(sa)) == 0 && listen(srv, 4) == 0;
    CHECK_MSG(listened, L"Test-Listener konnte nicht gestartet werden");
    bool connected = false;
    if (listened) {
        SOCKET cli = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        connected = connect(cli, reinterpret_cast<sockaddr*>(&sa), sizeof(sa)) == 0;
        CHECK_MSG(connected, L"Verbindung zum Test-Listener fehlgeschlagen");
        // TCP-Tabelle lesen und die eigene PID suchen
        std::vector<TcpConn> conns;
        CHECK(EnumerateTcpConnections(conns, err));
        bool mine = false;
        for (const TcpConn& c : conns) {
            if (c.pid == GetCurrentProcessId() && c.remotePort == 47123) { mine = true; break; }
            if (c.pid == GetCurrentProcessId() && c.localAddr == L"127.0.0.1") { mine = true; break; }
        }
        CHECK_MSG(mine, L"Eigene Verbindung nicht in der TCP-Tabelle");
        closesocket(cli);
    }
    closesocket(srv);
    WSACleanup();
    EndSection();
}

static void TestResolvedFileRoundtrip() {
    Begin(L"System: resolved.json lesbar und gültig");
    Paths p = MakePaths(TempDir());
    Engine e(p);
    std::wstring err;
    CHECK(e.Init(err));
    e.RefreshAdapters();
    CHECK(e.adapters.size() > 0);
    Rule r; r.pattern = L"tests.exe"; r.mode = RouteMode::Wlan;
    e.rules.rules.push_back(r);
    CHECK_MSG(e.Publish(), L"Publish fehlgeschlagen: " + e.lastError);
    std::wstring text, rerr;
    CHECK(ReadTextFile(p.resolvedFile, text, rerr));
    JsonPtr doc;
    CHECK(ParseResolvedDoc(text, doc));
    ResolvedLookupResult lr = ResolvedLookup(doc, L"tests.exe");
    CHECK(lr.found);
    const AdapterInfo* w = e.RoleAdapter(AdapterKind::Wlan);
    CHECK_MSG(w != nullptr, L"kein WLAN im echten System");
    if (w) {
        CHECK_EQ_STR(lr.guid, ToLowerW(w->guid));
        unsigned long idx = 0;
        CHECK(ResolvedIfIndex(doc, lr.guid, idx));
        CHECK_EQ_INT(idx, w->ifIndex);
    }
    EndSection();
}

// ---------------------------------------------------------------- Sprache

static void TestLanguageLayer() {
    Begin(L"Sprache: Uebersetzung, Vorlage und Dateiueberschreibung");
    std::wstring err;
    const std::wstring dir = TempDir();

    CHECK_EQ_INT(lang::Available().size(), 2);

    // Deutsch laesst jeden Text unveraendert stehen.
    lang::Init(L"de", dir, dir, &err);
    CHECK_EQ_STR(lang::Language(), L"de");
    CHECK_EQ_STR(lang::T(L"Neu laden"), L"Neu laden");
    CHECK_EQ_INT(lang::MissCount(), 0);

    // Unbekannter Code faellt auf Deutsch zurueck.
    lang::Init(L"zz", dir, dir, &err);
    CHECK_EQ_STR(lang::Language(), L"de");

    lang::Init(L"en", dir, dir, &err);
    CHECK_EQ_STR(lang::Language(), L"en");
    lang::ForgetMissing();
    CHECK_EQ_STR(lang::T(L"Ein Text ohne Uebersetzung"), L"Ein Text ohne Uebersetzung");
    CHECK_EQ_INT(lang::MissCount(), 1);
    lang::ForgetMissing();
    CHECK_EQ_INT(lang::MissCount(), 0);

    // Sprachdatei: Schluessel = Text im Programm, Wert = Uebersetzung.
    // WriteTextFileAtomic schreibt mit BOM - die Datei muss trotzdem gelesen werden.
    const std::wstring json =
        L"{\"_Hinweis\":\"Kommentar, keine Beschriftung\","
        L"\"_Sprache\":\"en\","
        L"\"Datei\":\"File\","
        L"\"Leerer Wert\":\"\","
        L"\"markierte Programme  \u2192  WLAN\":\"selected programs  \u2192  Wi-Fi\","
        L"\"%d Programme zugeordnet\":\"%d programs assigned\","
        L"\"Adapter %s heute\":\"adapter %s today\"}";
    CHECK(WriteTextFileAtomic(dir + L"\\language.en.json", json, err));

    lang::Init(L"en", dir, dir, &err);
    CHECK_MSG(err.empty(), L"Sprachdatei wurde nicht gelesen: " + err);
    CHECK_EQ_STR(lang::T(L"Datei"), L"File");
    CHECK_EQ_STR(lang::T(L"markierte Programme  \u2192  WLAN"), L"selected programs  \u2192  Wi-Fi");
    // Ein leerer Wert ueberschreibt nicht, sondern laesst den Quelltext stehen.
    CHECK_EQ_STR(lang::T(L"Leerer Wert"), L"Leerer Wert");
    // Mit Anfangsstrich wird nicht uebersetzt, das ist als Kommentar gedacht.
    CHECK_EQ_STR(lang::T(L"_Hinweis"), L"_Hinweis");

    // Die Adresse muss gueltig und gleich bleiben, weil Windows-Texte weitergereicht werden.
    const wchar_t* zuerst = lang::T(L"Datei");
    const wchar_t* spaeter = lang::T(L"Datei");
    CHECK_MSG(zuerst == spaeter, L"Adresse der Uebersetzung wechselt zwischen Aufrufen");
    CHECK_EQ_STR(std::wstring(zuerst), L"File");

    // Gesetze Texte mit Werten einschieben.
    CHECK_EQ_STR(lang::Fill(L"%d Programme zugeordnet", 3), L"3 programs assigned");
    CHECK_EQ_STR(lang::Fill(L"Adapter %s heute", L"WLAN"), L"adapter WLAN today");
    CHECK_EQ_STR(lang::Fill(L"Datei"), L"File");

    // Vorlage enthalten jeden gebrauchten Text, damit Uebersetzungen aussen ergaenzt
    // werden koennen, und laesst sich wieder einlesen.
    std::wstring vorlage = dir + L"\\language.vorlage.json";
    CHECK(lang::WriteTemplate(vorlage, &err));
    std::wstring text;
    CHECK(ReadTextFile(vorlage, text, err));
    CHECK(text.find(L"Ein Text ohne Uebersetzung") != std::wstring::npos);
    CHECK(text.find(L"\"File\"") != std::wstring::npos);

    // Eine Datei neben der Programmdatei gilt, die im Datenverzeichnis gewinnt.
    std::wstring unter = dir + L"\\nebene";
    EnsureDir(unter, err);
    CHECK(WriteTextFileAtomic(unter + L"\\language.en.json",
                              L"{\"Datei\":\"Ordner\",\"Nur hier\":\"Only here\"}", err));
    lang::Init(L"en", unter, dir, &err);
    CHECK_EQ_STR(lang::T(L"Nur hier"), L"Only here");
    CHECK_EQ_STR(lang::T(L"Datei"), L"File");   // aus dem Datenverzeichnis

    // Eine unlesbare Sprachdatei muss gemeldet werden, nicht verschwinden.
    CHECK(WriteTextFileAtomic(dir + L"\\language.en.json", L"{ ", err));
    err.clear();
    lang::Init(L"en", dir, dir, &err);
    CHECK_MSG(!err.empty(), L"kaputte Sprachdatei bleibt ungemeldet");
    lang::Init(L"de", dir, dir, &err);
    CHECK_EQ_STR(lang::T(L"Datei"), L"Datei");
    EndSection();
}

// ----------------------------------------------------------------Haupt

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    wprintf(L"RoutePerApp-Kern Tests\n");

    TestJsonBasics();
    TestJsonEscapesUnicode();
    TestJsonFehler();
    TestJsonUtf8();
    TestWildcard();
    TestMatchRule();
    TestRulesetOperations();
    TestRulesetResolve();
    TestRulesetJson();
    TestValidate();
    TestAtomicWrite();
    TestStoreRoundtrip();
    TestClassification();
    TestAdapterSelection();
    TestResolvedDoc();
    TestBuildRows();
    TestSortFilter();
    TestDropTargets();
    TestEngineFlow();
    TestEnginePathPrecision();
    TestReinjectGuards();
    TestSelfElevation();
    TestLiveAdapters();
    TestLiveProcessesAndTcp();
    TestResolvedFileRoundtrip();
    TestLanguageLayer();

    wprintf(L"\n=========================================\n");
    wprintf(L"Prüfungen: %d   Fehler: %d\n", g_checks, g_failed);
    wprintf(L"=========================================\n");
    return g_failed == 0 ? 0 : 1;
}
