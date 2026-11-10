#include "core.hpp"
#include "lang.hpp"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <tlhelp32.h>
#include <shlobj.h>
#include <shellapi.h>
#include <sddl.h>

#include <algorithm>
#include <cwctype>
#include <ctime>
#include <initializer_list>
#include <map>
#include <memory>
#include <set>

#include <aclapi.h>
#include <accctrl.h>
#include <wincrypt.h>

#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "ole32.lib")

namespace sr {

// ---------------------------------------------------------------- Kleine Helfer

std::wstring ToLowerW(std::wstring s) {
    for (wchar_t& c : s) c = static_cast<wchar_t>(towlower(c));
    return s;
}

std::wstring TrimW(const std::wstring& s) {
    size_t a = 0, b = s.size();
    while (a < b && iswspace(s[a])) ++a;
    while (b > a && iswspace(s[b - 1])) --b;
    return s.substr(a, b - a);
}

std::wstring LastErrText(unsigned long err) {
    wchar_t* buf = nullptr;
    DWORD n = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                             FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, err, 0,
                             reinterpret_cast<LPWSTR>(&buf), 0, nullptr);
    std::wstring out;
    if (n && buf) {
        out = TrimW(buf);
    } else {
        wchar_t tmp[64];
        swprintf(tmp, 64, L"Fehler %lu", err);
        out = tmp;
    }
    if (buf) LocalFree(buf);
    return out;
}

std::wstring ModeLabel(RouteMode m) {
    switch (m) {
    case RouteMode::Wlan:   return Tr(L"WLAN (5G)");
    case RouteMode::Lan:    return Tr(L"LAN (Fritzbox)");
    case RouteMode::Adapter:return Tr(L"fester Adapter");
    default:                return L"Standard";
    }
}

std::wstring KindLabel(AdapterKind k) {
    switch (k) {
    case AdapterKind::Wlan: return L"WLAN";
    case AdapterKind::Lan:  return L"Ethernet";
    default:                return Tr(L"sonstiger Adapter");
    }
}

bool WildcardMatch(const std::wstring& patternIn, const std::wstring& textIn) {
    const std::wstring p = ToLowerW(patternIn);
    const std::wstring t = ToLowerW(textIn);
    size_t pi = 0, ti = 0;
    long long star = -1, starNext = 0;
    while (ti < t.size()) {
        if (pi < p.size() && (p[pi] == L'?' || p[pi] == t[ti])) { ++pi; ++ti; continue; }
        if (pi < p.size() && p[pi] == L'*') { star = static_cast<long long>(pi); ++pi; starNext = ti; continue; }
        if (star >= 0) { pi = static_cast<size_t>(star) + 1; ti = static_cast<size_t>(++starNext); continue; }
        return false;
    }
    while (pi < p.size() && p[pi] == L'*') ++pi;
    return pi == p.size();
}

bool ContainsKeyword(const std::wstring& text, const std::vector<std::wstring>& keywords) {
    const std::wstring t = ToLowerW(text);
    for (const std::wstring& k : keywords) if (!k.empty() && t.find(ToLowerW(k)) != std::wstring::npos) return true;
    return false;
}

std::wstring FormatRate(long long bytesPerSec) {
    wchar_t buf[64];
    if (bytesPerSec <= 0) return Tr(L"0 B/s");
    double v = static_cast<double>(bytesPerSec);
    const wchar_t* unit = L"B/s";
    if (v >= 1024.0 * 1024.0 * 1024.0) { v /= 1024.0 * 1024.0 * 1024.0; unit = L"GB/s"; }
    else if (v >= 1024.0 * 1024.0) { v /= 1024.0 * 1024.0; unit = L"MB/s"; }
    else if (v >= 1024.0) { v /= 1024.0; unit = L"KB/s"; }
    if (v >= 100.0) swprintf(buf, 64, L"%.0f %s", v, unit);
    else swprintf(buf, 64, L"%.1f %s", v, unit);
    return buf;
}

std::wstring FormatBytes(unsigned long long bytes) {
    wchar_t buf[64];
    if (bytes == 0) return Tr(L"0 B");
    double v = static_cast<double>(bytes);
    const wchar_t* unit = L"B";
    if (v >= 1024.0 * 1024.0 * 1024.0) { v /= 1024.0 * 1024.0 * 1024.0; unit = L"GB"; }
    else if (v >= 1024.0 * 1024.0) { v /= 1024.0 * 1024.0; unit = L"MB"; }
    else if (v >= 1024.0) { v /= 1024.0; unit = L"KB"; }
    if (v >= 100.0) swprintf(buf, 64, L"%.0f %s", v, unit);
    else swprintf(buf, 64, L"%.1f %s", v, unit);
    return buf;
}

std::wstring FormatGuidShort(const std::wstring& guid) {
    std::wstring g = guid;
    if (!g.empty() && g.front() == L'{') g.erase(g.begin());
    if (!g.empty() && g.back() == L'}') g.pop_back();
    if (g.size() > 8) return g.substr(0, 8) + L"\u2026";
    return g;
}

bool ParseGuid(const std::wstring& text, std::wstring& normalized) {
    std::wstring t = TrimW(text);
    if (t.empty()) { normalized.clear(); return false; }
    if (t.size() != 38) return false;
    if (t.front() != L'{' || t.back() != L'}') return false;
    int dashPos[5] = { 9, 14, 19, 24, -1 };
    for (int i = 0; i < 4; ++i) if (t[dashPos[i]] != L'-') return false;
    for (int i = 1; i < 37; ++i) {
        if (i == 9 || i == 14 || i == 19 || i == 24) continue;
        wchar_t c = t[i];
        bool hex = (c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f') || (c >= L'A' && c <= L'F');
        if (!hex) return false;
    }
    normalized = t;
    for (wchar_t& c : normalized) c = static_cast<wchar_t>(towlower(c));
    return true;
}

std::wstring NowIsoLocal() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t buf[32];
    swprintf(buf, 32, L"%04d-%02d-%02d %02d:%02d:%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return buf;
}

// ---------------------------------------------------------------- Klassifizierung

// NDIS_PHYSICAL_MEDIUM (ntddndis.h) - die echten Werte des SDK:
//   0 Unspecified, 1 WirelessLan, 8 WirelessWan, 9 Native802_11, 10 Bluetooth,
//   14 802_3 (= Ethernet), 15 802_5, 19 Other. Nicht gesetzt meldet -1.
static const unsigned long kIfTypeEthernet = 6;         // IF_TYPE_ETHERNET_CSMACD
static const unsigned long kIfTypeIeee80211 = 71;       // IF_TYPE_IEEE80211

static bool IsWifiMedium(long m) {
    return m == NdisPhysicalMediumWirelessLan || m == NdisPhysicalMediumNative802_11 ||
           m == NdisPhysicalMediumWirelessWan;
}
static bool IsCableMedium(long m) {
    return m == NdisPhysicalMedium802_3 || m == NdisPhysicalMedium802_5;
}

AdapterKind ClassifyMedium(unsigned long ifType, long physMedium, int connType,
                           const std::wstring& alias, const std::wstring& desc) {
    (void)connType;   // NET_IF_CONNECTION_TYPE unterscheidet WLAN nicht -> bewusst unbenutzt

    if (ifType == kIfTypeIeee80211 || IsWifiMedium(physMedium)) return AdapterKind::Wlan;
    if (physMedium == NdisPhysicalMediumBluetooth) return AdapterKind::Other;

    std::wstring d = ToLowerW(desc);
    // Beschreibung ist bei Funkadaptern oft eindeutiger als die gemeldete Medium-Kennung
    if (d.find(L"wireless") != std::wstring::npos || d.find(L"wi-fi") != std::wstring::npos ||
        d.find(L"wifi") != std::wstring::npos || d.find(L"802.11") != std::wstring::npos)
        return AdapterKind::Wlan;

    if (ifType == kIfTypeEthernet && (IsCableMedium(physMedium) || physMedium < 0 ||
                                      physMedium == NdisPhysicalMediumUnspecified)) {
        // Alias "WLAN" darf nicht als Ethernet durchgehen
        std::wstring a = ToLowerW(alias);
        if (a.find(L"wlan") != std::wstring::npos || a.find(L"wi-fi") != std::wstring::npos ||
            a.find(L"wifi") != std::wstring::npos)
            return AdapterKind::Other;
        return AdapterKind::Lan;
    }
    if (d.find(L"ethernet") != std::wstring::npos || d.find(L"gigabit") != std::wstring::npos ||
        d.find(L"gbe") != std::wstring::npos)
        return AdapterKind::Lan;
    return AdapterKind::Other;
}

bool LooksVirtual(const std::wstring& desc, const std::wstring& alias, long physMedium, unsigned long accessType) {
    static const std::vector<std::wstring> kVirtualHints = {
        L"tap", L"tailscale", L"virtual", L"vpn", L"wireguard", L"openvpn", L"anyconnect",
        L"hyper-v", L"vmware", L"vmnet", L"virtualbox", L"docker", L"wsl", L"utun",
        L"wan miniport", L"wansl", L"bluetooth", L"kaspersky", L"data escort",
        L"pseudo", L"loopback", L"isatap", L"teredo", L"ppp", L"l2tp", L"sandboxie",
        L"personal area", L"npcap", L"zero tier", L"zerotier", L"packet filter", L"ndisfilter"
    };
    if (ContainsKeyword(desc, kVirtualHints)) return true;
    if (ContainsKeyword(alias, kVirtualHints)) return true;
    // Echte Chips melden Funk (1/9) oder Kabel (14/15). Alles andere ist ein Software-Adapter.
    if (!IsWifiMedium(physMedium) && !IsCableMedium(physMedium)) return true;
    // NET_IF_ACCESS_BROADCAST == 2 ist das, was echte Leitungen melden
    if (accessType != NET_IF_ACCESS_BROADCAST) return true;
    return false;
}

bool IsLinkLocalIpv4(const std::wstring& ip) {
    return ip.rfind(L"169.254.", 0) == 0;
}

// ---------------------------------------------------------------- Regeln

MatchResult MatchRule(const Rule& rule, const std::wstring& image, const std::wstring& path) {
    MatchResult r;
    if (!rule.enabled) return r;
    std::wstring pat = TrimW(rule.pattern);
    if (pat.empty()) { r.invalid = true; return r; }
    std::wstring stripped = pat;
    bool onlyWildcards = true;
    for (wchar_t c : stripped) { if (c != L'*' && c != L'?') { onlyWildcards = false; break; } }
    if (onlyWildcards) { r.invalid = true; return r; }   // "match everything" versehentlich verhindern

    bool pathStyle = pat.find(L'\\') != std::wstring::npos || pat.find(L'/') != std::wstring::npos;
    if (pathStyle) {
        if (path.empty()) return r;                      // ohne Pfad kann ein Pfadmuster nicht prüfen
        r.matched = WildcardMatch(pat, path);
    } else {
        r.matched = WildcardMatch(pat, image);
        // "*jetbrains*" soll auch dann treffen, wenn nur der Installationspfad dazu passt
        if (!r.matched && !path.empty()) r.matched = WildcardMatch(pat, path);
    }
    if (r.matched && !TrimW(rule.pathPattern).empty()) {
        if (path.empty()) r.matched = false;
        else r.matched = WildcardMatch(TrimW(rule.pathPattern), path);
    }
    return r;
}

static unsigned long long NextRuleId(const Ruleset& rs) {
    unsigned long long maxId = 0;
    for (const Rule& r : rs.rules) if (r.id > maxId) maxId = r.id;
    return maxId + 1;
}

unsigned long long Ruleset::AddTop(Rule r) {
    r.id = NextRuleId(*this);
    if (r.created == 0) {
        SYSTEMTIME st; GetSystemTime(&st);
        FILETIME ft; SystemTimeToFileTime(&st, &ft);
        r.created = (static_cast<long long>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
    }
    rules.insert(rules.begin(), r);
    return r.id;
}

// Regel inklusive ihrer alten id wieder oben einreihen (für "Rückgängig").
bool Ruleset::InsertTop(const Rule& r) {
    if (r.id == 0 || r.pattern.empty()) return false;
    if (IndexOf(r.id) >= 0) return false;
    rules.insert(rules.begin(), r);
    return true;
}

bool Ruleset::Remove(unsigned long long id) {
    for (size_t i = 0; i < rules.size(); ++i) {
        if (rules[i].id == id) { rules.erase(rules.begin() + i); return true; }
    }
    return false;
}

int Ruleset::IndexOf(unsigned long long id) const {
    for (size_t i = 0; i < rules.size(); ++i) if (rules[i].id == id) return static_cast<int>(i);
    return -1;
}

bool Ruleset::Move(unsigned long long id, int newIndex) {
    int cur = IndexOf(id);
    if (cur < 0) return false;
    if (newIndex < 0) newIndex = 0;
    if (newIndex >= static_cast<int>(rules.size())) newIndex = static_cast<int>(rules.size()) - 1;
    if (newIndex == cur) return true;
    Rule r = rules[cur];
    rules.erase(rules.begin() + cur);
    rules.insert(rules.begin() + newIndex, r);
    return true;
}

bool Ruleset::Update(unsigned long long id, const Rule& values) {
    int i = IndexOf(id);
    if (i < 0) return false;
    Rule r = values;
    r.id = id;
    r.created = rules[i].created;
    rules[i] = r;
    return true;
}

const Rule* Ruleset::Find(unsigned long long id) const {
    int i = IndexOf(id);
    return i < 0 ? nullptr : &rules[i];
}

void Ruleset::Resolve(const std::wstring& image, const std::wstring& path, Resolved& out) const {
    out = Resolved{};
    for (const Rule& r : rules) {
        MatchResult m = MatchRule(r, image, path);
        if (m.matched) {
            out.mode = r.mode;
            out.ruleId = r.id;
            out.adapterGuid = (r.mode == RouteMode::Adapter) ? r.adapterGuid : std::wstring();
            return;
        }
    }
    out.mode = defaultMode;
    out.adapterGuid = (defaultMode == RouteMode::Adapter) ? defaultAdapterGuid : std::wstring();
    out.fromDefault = (defaultMode != RouteMode::None);
}

std::vector<std::wstring> Ruleset::Validate() const {
    std::vector<std::wstring> warn;
    std::set<unsigned long long> seen;
    for (const Rule& r : rules) {
        if (r.id == 0) warn.push_back(Tr(L"Regel ohne ID ignoriert: ") + r.pattern);
        else if (!seen.insert(r.id).second) warn.push_back(Tr(L"Doppelte Regel-ID ") + std::to_wstring(r.id));
        MatchResult m = MatchRule(r, L"x.exe", L"C:\\x.exe");
        std::wstring pat = TrimW(r.pattern);
        bool onlyWildcards = !pat.empty();
        for (wchar_t c : pat) { if (c != L'*' && c != L'?') { onlyWildcards = false; break; } }
        if (pat.empty() || onlyWildcards) warn.push_back(Tr(L"Regel hat ein unbrauchbares Muster: \"") + r.pattern + L"\"");
        if (r.mode == RouteMode::Adapter) {
            std::wstring g;
            if (!ParseGuid(r.adapterGuid, g)) warn.push_back(Tr(L"Regel ") + pat + Tr(L" hat keine gültige Adapter-GUID"));
        }
        (void)m;
    }
    return warn;
}

JsonPtr Ruleset::ToJson() const {
    JsonPtr root = Json::MakeObject();
    root->set(L"version", Json::MakeNumber(version));
    root->set(L"defaultMode", Json::MakeNumber(static_cast<double>(static_cast<int>(defaultMode))));
    root->set(L"defaultAdapterGuid", Json::MakeString(defaultAdapterGuid));
    JsonPtr arr = Json::MakeArray();
    for (const Rule& r : rules) {
        JsonPtr o = Json::MakeObject();
        o->set(L"id", Json::MakeNumber(static_cast<double>(r.id)));
        o->set(L"pattern", Json::MakeString(r.pattern));
        o->set(L"path", Json::MakeString(r.pathPattern));
        o->set(L"mode", Json::MakeNumber(static_cast<double>(static_cast<int>(r.mode))));
        o->set(L"adapterGuid", Json::MakeString(r.adapterGuid));
        o->set(L"note", Json::MakeString(r.note));
        o->set(L"enabled", Json::MakeBool(r.enabled));
        o->set(L"created", Json::MakeNumber(static_cast<double>(r.created)));
        arr->push(o);
    }
    root->set(L"rules", arr);
    return root;
}

bool Ruleset::FromJson(const JsonPtr& root, Ruleset& out, std::vector<std::wstring>& warnings) {
    if (!root || !root->isObject()) { warnings.push_back(Tr(L"Wurzel ist kein Objekt")); return false; }
    out = Ruleset{};
    out.version = root->intOr(L"version", 1);
    int dm = root->intOr(L"defaultMode", 0);
    if (dm < 0 || dm > 3) { warnings.push_back(Tr(L"Ungültiger Standardmodus, auf Standard gesetzt")); dm = 0; }
    out.defaultMode = static_cast<RouteMode>(dm);
    std::wstring dg;
    if (out.defaultMode == RouteMode::Adapter) {
        dg = root->strOr(L"defaultAdapterGuid", L"");
        std::wstring norm;
        if (!ParseGuid(dg, norm)) { warnings.push_back(Tr(L"Standard-Adapter-GUID ungültig, Modus auf Standard")); out.defaultMode = RouteMode::None; }
        else out.defaultAdapterGuid = norm;
    }
    const JsonPtr* rules = root->find(L"rules");
    if (!rules || !(*rules) || !(*rules)->isArray()) { warnings.push_back(Tr(L"Feld 'rules' fehlt")); return true; }
    for (const JsonPtr& item : (*rules)->arr) {
        if (!item || !item->isObject()) { warnings.push_back(Tr(L"Regel-Eintrag ist kein Objekt, übersprungen")); continue; }
        Rule r;
        r.pattern = TrimW(item->strOr(L"pattern", L""));
        r.pathPattern = TrimW(item->strOr(L"path", L""));
        r.note = item->strOr(L"note", L"");
        r.adapterGuid = item->strOr(L"adapterGuid", L"");
        r.enabled = item->boolOr(L"enabled", true);
        double d = 0;
        if (item->getNum(L"id", d)) r.id = static_cast<unsigned long long>(d);
        if (item->getNum(L"created", d)) r.created = static_cast<long long>(d);
        int mode = item->intOr(L"mode", 0);
        if (mode < 0 || mode > 3) { warnings.push_back(Tr(L"Regel \"") + r.pattern + Tr(L"\" hat ungültigen Modus, übersprungen")); continue; }
        r.mode = static_cast<RouteMode>(mode);
        if (r.pattern.empty()) { warnings.push_back(Tr(L"Regel ohne Muster übersprungen")); continue; }
        bool onlyWildcards = true;
        for (wchar_t c : r.pattern) { if (c != L'*' && c != L'?') { onlyWildcards = false; break; } }
        if (onlyWildcards) { warnings.push_back(Tr(L"Regel \"") + r.pattern + Tr(L"\" ist zu breit, übersprungen")); continue; }
        if (r.mode == RouteMode::Adapter) {
            std::wstring norm;
            if (!ParseGuid(r.adapterGuid, norm)) { warnings.push_back(Tr(L"Regel \"") + r.pattern + Tr(L"\" ohne gültige GUID, übersprungen")); continue; }
            r.adapterGuid = norm;
        }
        if (r.id == 0) r.id = NextRuleId(out);
        out.rules.push_back(r);
    }
    return true;
}

// ---------------------------------------------------------------- Pfade & Dateien

// Frühere Versionen legten alles unter „Common AppData\SoftRouter“ an. Beim ersten Start
// nach der Umbenennung werden die Dateien übernommen, sonst stehen die Zuordnungen mit
// einem Mal leer da.
static void AdoptOldDataDir(const wchar_t* base) {
    const std::wstring now = std::wstring(base) + L"\\RoutePerApp";
    const std::wstring old = std::wstring(base) + L"\\SoftRouter";
    if (GetFileAttributesW(now.c_str()) != INVALID_FILE_ATTRIBUTES) return;
    if (GetFileAttributesW(old.c_str()) == INVALID_FILE_ATTRIBUTES) return;
    if (!CreateDirectoryW(now.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) return;
    static const wchar_t* names[] = { L"rules.json", L"rules.json.bak", L"resolved.json",
                                      L"resolved.json.bak", L"settings.json" };
    for (auto name : names) {
        const std::wstring from = old + L"\\" + name, to = now + L"\\" + name;
        if (GetFileAttributesW(from.c_str()) != INVALID_FILE_ATTRIBUTES &&
            GetFileAttributesW(to.c_str()) == INVALID_FILE_ATTRIBUTES)
            CopyFileW(from.c_str(), to.c_str(), TRUE);
    }
}

Paths DefaultPaths() {
    wchar_t base[MAX_PATH] = L"";
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_COMMON_APPDATA, nullptr, SHGFP_TYPE_CURRENT, base))) {
        wcscpy_s(base, L".");
    }
    std::wstring dir = std::wstring(base) + L"\\RoutePerApp";
    AdoptOldDataDir(base);
    return MakePaths(dir);
}

Paths MakePaths(const std::wstring& dir) {
    Paths p;
    p.dir = dir;
    p.rulesFile = dir + L"\\rules.json";
    p.resolvedFile = dir + L"\\resolved.json";
    p.hookDir = dir + L"\\hooks";      // eigener Ordner, nur mit Administratorrechten aenderbar
    p.hookDll = p.hookDir + L"\\hook.dll";
    p.hookDll32 = p.hookDir + L"\\hook32.dll";
    p.logFile = dir + L"\\routeperapp.log";
    return p;
}

bool EnsureDir(const std::wstring& dir, std::wstring& err) {
    if (GetFileAttributesW(dir.c_str()) != INVALID_FILE_ATTRIBUTES) return true;
    // Oberordner zuerst
    size_t pos = dir.find_last_of(L"\\/");
    if (pos != std::wstring::npos && pos > 1) {
        std::wstring parent = dir.substr(0, pos);
        if (!EnsureDir(parent, err)) return false;
    }
    if (!CreateDirectoryW(dir.c_str(), nullptr)) {
        DWORD e = GetLastError();
        if (e != ERROR_ALREADY_EXISTS) { err = Tr(L"Ordner ") + dir + L": " + LastErrText(e); return false; }
    }
    return true;
}

bool FileExists(const std::wstring& file) {
    DWORD a = GetFileAttributesW(file.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

// ---------------------------------------------------------------- Hook-Dateien absichern

std::wstring CurrentExeDir() {
    wchar_t puffer[MAX_PATH * 4];
    const DWORD n = GetModuleFileNameW(nullptr, puffer, ARRAYSIZE(puffer));
    if (n == 0 || n >= ARRAYSIZE(puffer)) return std::wstring();
    std::wstring voll(puffer, n);
    const size_t slash = voll.find_last_of(L'\\');
    if (slash == std::wstring::npos) return std::wstring();
    return voll.substr(0, slash);
}

static wchar_t HexZiffer(int wert) { return wert < 10 ? wchar_t(L'0' + wert) : wchar_t(L'a' + wert - 10); }

// SHA-256 einer Datei als Hex-Kette. Der Vergleich zeigt, ob eine Hook-Bibliothek noch die
// ist, die mit dem Programm ausgeliefert wurde.
bool HashFile(const std::wstring& file, std::wstring& hex, std::wstring& err) {
    hex.clear();
    const HANDLE h = CreateFileW(file.c_str(), GENERIC_READ,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                 nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) { err = LastErrText(GetLastError()); return false; }

    HCRYPTPROV anbieter = 0;
    HCRYPTHASH hash = 0;
    bool ok = false;
    if (CryptAcquireContextW(&anbieter, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) {
        if (CryptCreateHash(anbieter, CALG_SHA_256, 0, 0, &hash)) {
            ok = true;
            std::vector<char> block(64 * 1024);
            for (;;) {
                DWORD gelesener = 0;
                if (!ReadFile(h, block.data(), DWORD(block.size()), &gelesener, nullptr)) { ok = false; break; }
                if (gelesener == 0) break;
                if (!CryptHashData(hash, reinterpret_cast<const BYTE*>(block.data()), gelesener, 0)) { ok = false; break; }
            }
            if (ok) {
                BYTE wert[32];
                DWORD laenge = sizeof(wert);
                if (CryptGetHashParam(hash, HP_HASHVAL, wert, &laenge, 0)) {
                    std::wstring kette;
                    for (DWORD i = 0; i < laenge; ++i) {
                        kette += HexZiffer(wert[i] >> 4);
                        kette += HexZiffer(wert[i] & 15);
                    }
                    hex = kette;
                } else {
                    ok = false;
                }
            }
        }
        CryptDestroyHash(hash);
        CryptReleaseContext(anbieter, 0);
    }
    CloseHandle(h);
    if (!ok && err.empty()) err = Tr(L"Hashbildung fehlgeschlagen");
    return ok;
}

// Ein Ordner, in dem normale Programme nur lesen und ausfuehren duerfen. Die Dateien darin
// werden von einem Programm mit Administratorrechten benutzt und in andere Programme
// eingeschleust; ein gewoehnlicher Programmablauf darf sie nicht austauschen.
//
// Bewusst fehlt ein Eintrag fuer den Erzeuger: der Mensch anmeldet sich mit einem Konto, das
// zur Gruppe Administratoren gehoert. Sein normales Programm laeuft ohne erhohte Rechte und
// darf deshalb hier nicht schreiben duerfen, obwohl derselbe Mensch es duerfte.
bool HardenFolder(const std::wstring& dir, std::wstring& err) {
    PSID systemSid = nullptr, adminSid = nullptr, benutzerSid = nullptr;
    SID_IDENTIFIER_AUTHORITY nt = SECURITY_NT_AUTHORITY;
    const bool sidOk =
        AllocateAndInitializeSid(&nt, 1, SECURITY_LOCAL_SYSTEM_RID, 0, 0, 0, 0, 0, 0, 0, &systemSid) &&
        AllocateAndInitializeSid(&nt, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS,
                                 0, 0, 0, 0, 0, 0, &adminSid) &&
        AllocateAndInitializeSid(&nt, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_USERS,
                                 0, 0, 0, 0, 0, 0, &benutzerSid);
    if (!sidOk) {
        if (systemSid) FreeSid(systemSid);
        if (adminSid) FreeSid(adminSid);
        if (benutzerSid) FreeSid(benutzerSid);
        err = Tr(L"Benutzerkennungen nicht vorbereitet: ") + LastErrText(GetLastError());
        return false;
    }

    const ACCESS_MASK lesen = FILE_READ_DATA | FILE_READ_EA | FILE_READ_ATTRIBUTES |
                              FILE_EXECUTE | SYNCHRONIZE | READ_CONTROL;
    struct Vorgabe { PSID sid; ACCESS_MASK rechte; };
    const Vorgabe vorgaben[3] = { { systemSid, FILE_ALL_ACCESS },
                                  { adminSid, FILE_ALL_ACCESS },
                                  { benutzerSid, lesen } };

    EXPLICIT_ACCESSW zeilen[3]{};
    int anzahl = 0;
    for (const Vorgabe& v : vorgaben) {
        EXPLICIT_ACCESSW& z = zeilen[anzahl++];
        z.grfAccessPermissions = v.rechte;
        z.grfAccessMode = GRANT_ACCESS;
        z.grfInheritance = SUB_CONTAINERS_AND_OBJECTS_INHERIT;
        z.Trustee.TrusteeForm = TRUSTEE_IS_SID;
        z.Trustee.ptstrName = reinterpret_cast<LPWSTR>(v.sid);
    }

    PACL rechte = nullptr;
    DWORD r = SetEntriesInAclW(anzahl, zeilen, nullptr, &rechte);
    if (r != ERROR_SUCCESS) {
        err = Tr(L"Zugriffsliste nicht gebaut: ") + LastErrText(r);
        FreeSid(systemSid); FreeSid(adminSid); FreeSid(benutzerSid);
        return false;
    }

    // Der Ordner erbt die Rechte aus den Programmdaten. Mit der geschuetzten Liste fallen
    // diese geerbten Schreibrechte weg, nur die eigene Liste gilt.
    r = SetNamedSecurityInfoW(const_cast<LPWSTR>(dir.c_str()), SE_FILE_OBJECT,
                              DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
                              nullptr, nullptr, rechte, nullptr);
    bool ok = (r == ERROR_SUCCESS);
    if (!ok) err = Tr(L"Ordnerschutz nicht gesetzt: ") + LastErrText(r);

    // Vorhandene Dateien tragen noch die alte Liste, sie werden einzeln nachgezogen.
    WIN32_FIND_DATAW f{};
    const HANDLE suche = FindFirstFileW((dir + L"\\*").c_str(), &f);
    if (suche != INVALID_HANDLE_VALUE) {
        do {
            if (f.cFileName == std::wstring(L".") || f.cFileName == std::wstring(L"..")) continue;
            if (f.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            const std::wstring voll = dir + L"\\" + f.cFileName;
            if (SetNamedSecurityInfoW(const_cast<LPWSTR>(voll.c_str()), SE_FILE_OBJECT,
                                      DACL_SECURITY_INFORMATION, nullptr, nullptr, rechte,
                                      nullptr) != ERROR_SUCCESS)
                ok = false;
        } while (FindNextFileW(suche, &f));
        FindClose(suche);
    }

    LocalFree(rechte);
    FreeSid(systemSid); FreeSid(adminSid); FreeSid(benutzerSid);
    return ok;
}

// Prueft, ob der Ordner fuer normale Programme keinen Schreibzugriff mehr enthaelt.
bool FolderIsHardened(const std::wstring& dir) {
    PSID systemSid = nullptr, adminSid = nullptr;
    SID_IDENTIFIER_AUTHORITY nt = SECURITY_NT_AUTHORITY;
    AllocateAndInitializeSid(&nt, 1, SECURITY_LOCAL_SYSTEM_RID, 0, 0, 0, 0, 0, 0, 0, &systemSid);
    AllocateAndInitializeSid(&nt, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS,
                             0, 0, 0, 0, 0, 0, &adminSid);

    PACL rechte = nullptr;
    PSECURITY_DESCRIPTOR paket = nullptr;
    const DWORD r = GetNamedSecurityInfoW(const_cast<LPWSTR>(dir.c_str()), SE_FILE_OBJECT,
                                          DACL_SECURITY_INFORMATION, nullptr, nullptr, &rechte,
                                          nullptr, &paket);
    if (r != ERROR_SUCCESS) {
        if (systemSid) FreeSid(systemSid);
        if (adminSid) FreeSid(adminSid);
        return false;
    }
    const ACCESS_MASK schreiben = FILE_ADD_FILE | FILE_WRITE_DATA | FILE_WRITE_ATTRIBUTES |
                                  FILE_WRITE_EA | DELETE | FILE_ADD_SUBDIRECTORY;
    bool offen = false;
    if (rechte) {
        for (unsigned i = 0; i < rechte->AceCount; ++i) {
            ACCESS_ALLOWED_ACE* ace = nullptr;
            if (!GetAce(rechte, i, reinterpret_cast<void**>(&ace)) || !ace) continue;
            if (ace->Header.AceType != ACCESS_ALLOWED_ACE_TYPE) continue;
            if ((ace->Mask & schreiben) == 0) continue;
            PSID wer = reinterpret_cast<PSID>(&ace->SidStart);
            if ((systemSid && EqualSid(wer, systemSid)) || (adminSid && EqualSid(wer, adminSid))) continue;
            offen = true;
            break;
        }
    }
    LocalFree(paket);
    if (systemSid) FreeSid(systemSid);
    if (adminSid) FreeSid(adminSid);
    return !offen;
}

bool ReadTextFile(const std::wstring& file, std::wstring& out, std::wstring& err) {
    out.clear();
    HANDLE h = CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) { err = LastErrText(GetLastError()); return false; }
    std::string bytes;
    char buf[8192];
    DWORD got = 0;
    bool ok = true;
    while (true) {
        if (!ReadFile(h, buf, sizeof(buf), &got, nullptr)) { err = LastErrText(GetLastError()); ok = false; break; }
        if (got == 0) break;
        bytes.append(buf, got);
        if (bytes.size() > 32u * 1024u * 1024u) { err = Tr(L"Datei zu groß"); ok = false; break; }
    }
    CloseHandle(h);
    if (!ok) return false;
    if (bytes.size() >= 3 && (unsigned char)bytes[0] == 0xEF && (unsigned char)bytes[1] == 0xBB && (unsigned char)bytes[2] == 0xBF)
        bytes.erase(bytes.begin(), bytes.begin() + 3);
    out = WFromU8(bytes);
    return true;
}

bool WriteTextFileAtomic(const std::wstring& file, const std::wstring& content, std::wstring& err) {
    size_t pos = file.find_last_of(L"\\/");
    if (pos != std::wstring::npos) {
        std::wstring dir = file.substr(0, pos);
        if (!dir.empty() && !EnsureDir(dir, err)) return false;
    }
    wchar_t tail[32];
    swprintf(tail, 32, L".%lu.tmp", GetCurrentProcessId());
    const std::wstring tmp = file + tail;
    const std::wstring bak = file + L".bak";

    std::string bytes = "\xEF\xBB\xBF" + U8FromW(content);   // BOM: für Werkzeuge/Notfall lesbar
    HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) { err = Tr(L"Temporäre Datei ") + tmp + L": " + LastErrText(GetLastError()); return false; }
    bool ok = true;
    size_t off = 0;
    while (off < bytes.size()) {
        DWORD want = static_cast<DWORD>(std::min<size_t>(bytes.size() - off, 65536));
        DWORD done = 0;
        if (!WriteFile(h, bytes.data() + off, want, &done, nullptr) || done != want) {
            err = Tr(L"Schreiben ") + tmp + L": " + LastErrText(GetLastError());
            ok = false;
            break;
        }
        off += done;
    }
    if (ok) {
        if (!FlushFileBuffers(h)) { err = Tr(L"Flush: ") + LastErrText(GetLastError()); ok = false; }
    }
    CloseHandle(h);
    if (!ok) { DeleteFileW(tmp.c_str()); return false; }

    if (FileExists(file)) {
        // Sicherung nur anlegen, wenn sie nicht schon den gleichen Inhalt hat
        std::wstring curText, curErr;
        bool same = false;
        if (ReadTextFile(file, curText, curErr)) same = (curText == content);
        if (!same) {
            if (FileExists(bak)) SetFileAttributesW(bak.c_str(), FILE_ATTRIBUTE_NORMAL);
            CopyFileW(file.c_str(), bak.c_str(), FALSE);
        }
    }

    for (int attempt = 0; attempt < 6; ++attempt) {
        if (MoveFileExW(tmp.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return true;
        DWORD e = GetLastError();
        err = Tr(L"Verschieben nach ") + file + L": " + LastErrText(e);
        if (e != ERROR_SHARING_VIOLATION && e != ERROR_ACCESS_DENIED) break;
        Sleep(25);
    }
    DeleteFileW(tmp.c_str());
    return false;
}

bool GetFileMTime(const std::wstring& file, long long& out) {
    HANDLE h = CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    FILETIME ft;
    BOOL ok = GetFileTime(h, nullptr, nullptr, &ft);
    CloseHandle(h);
    if (!ok) return false;
    out = (static_cast<long long>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
    return true;
}

// Prüfsumme über den Text – der Zeitstempel einer Datei wird vom System nur in groben
// Schritten nachgetragen, zwei Kurzschreibungen bekommen leicht denselben Wert. Zur
// Sicherheit wird deshalb zusätzlich der Inhalt verglichen.
static unsigned long long HashText64(const std::wstring& s) {
    unsigned long long h = 14695981039346656037ULL;      // FNV-1a
    for (wchar_t c : s) {
        h ^= static_cast<unsigned long long>(static_cast<unsigned short>(c));
        h *= 1099511628211ULL;
    }
    return h;
}

// merkt sich den gerade geschriebenen/gelesenen Stand von rules.json
static void RememberRulesHash(const Paths& p, unsigned long long& hash) {
    std::wstring text, err;
    hash = (FileExists(p.rulesFile) && ReadTextFile(p.rulesFile, text, err)) ? HashText64(text) : 0;
}

bool LoadRuleset(const Paths& p, Ruleset& out, std::vector<std::wstring>& warnings, std::wstring& err) {
    err.clear();
    if (!FileExists(p.rulesFile)) {
        out = Ruleset();
        return true;   // Erststart: leeres Regelwerk ist kein Fehler
    }
    std::wstring text, readErr;
    if (!ReadTextFile(p.rulesFile, text, readErr)) {
        err = L"rules.json konnte nicht gelesen werden: " + readErr;
        out = Ruleset();
        return false;
    }
    JsonPtr root;
    JsonParseResult pr = JsonParse(text, root);
    if (!pr.ok) {
        // Defekte Datei: Sicherung behalten und Original zur Analyse sichern.
        wchar_t stamp[64];
        swprintf(stamp, 64, L".defekt-%llu", static_cast<unsigned long long>(time(nullptr)));
        const std::wstring broken = p.rulesFile + stamp;
        std::wstring werr;
        WriteTextFileAtomic(broken, text, werr);
        warnings.push_back(L"rules.json war beschädigt (Zeile " + std::to_wstring(pr.line) + L"): " + pr.error);
        warnings.push_back(Tr(L"Kaputte Version gesichert als ") + broken);
        if (FileExists(p.rulesFile + L".bak")) {
            std::wstring bakText, bakErr;
            if (ReadTextFile(p.rulesFile + L".bak", bakText, bakErr)) {
                JsonPtr bakRoot;
                JsonParseResult bpr = JsonParse(bakText, bakRoot);
                if (bpr.ok && Ruleset::FromJson(bakRoot, out, warnings)) {
                    warnings.push_back(L"Regelwerk aus Sicherung rules.json.bak wiederhergestellt");
                    return true;
                }
            }
        }
        out = Ruleset();
        err = L"rules.json beschädigt";
        return false;
    }
    if (!Ruleset::FromJson(root, out, warnings)) {
        out = Ruleset();
        err = L"rules.json hat ein ungültiges Format";
        return false;
    }
    for (const std::wstring& w : out.Validate()) warnings.push_back(w);
    return true;
}

bool SaveRuleset(const Paths& p, const Ruleset& rs, std::wstring& err) {
    if (!EnsureDir(p.dir, err)) return false;
    const std::wstring text = JsonSerialize(rs.ToJson(), true) + L"\r\n";
    return WriteTextFileAtomic(p.rulesFile, text, err);
}

// ---------------------------------------------------------------- Aufgelöstes Regelwerk

const AdapterInfo* FindAdapterByGuid(const std::vector<AdapterInfo>& adapters, const std::wstring& guid) {
    std::wstring norm;
    if (!ParseGuid(guid, norm)) return nullptr;
    for (const AdapterInfo& a : adapters) {
        std::wstring g = ToLowerW(a.guid);
        if (g == norm) return &a;
    }
    return nullptr;
}

const AdapterInfo* FindAdapterByRole(const std::vector<AdapterInfo>& adapters, AdapterKind kind) {
    const AdapterInfo* best = nullptr;
    for (const AdapterInfo& a : adapters) {
        if (a.kind != kind) continue;
        if (!a.isUp) continue;
        if (!best) { best = &a; continue; }
        if (a.isVirtual != best->isVirtual) { if (!a.isVirtual) best = &a; continue; }
        if (a.ifMetric < best->ifMetric) best = &a;
        if (a.ifMetric == best->ifMetric && a.linkSpeedBps > best->linkSpeedBps) best = &a;
    }
    if (best) return best;
    for (const AdapterInfo& a : adapters) {           // ikinci Chance: Adapter ohne Link
        if (a.kind != kind) continue;
        if (best && (a.isVirtual || a.ifMetric >= best->ifMetric)) continue;
        if (!best || !a.isVirtual) best = &a;
    }
    return best;
}

static std::wstring GuidForMode(const std::vector<AdapterInfo>& adapters,
                                const Resolved& res, std::wstring* problem, const std::wstring& what) {
    switch (res.mode) {
    case RouteMode::Wlan: {
        const AdapterInfo* a = FindAdapterByRole(adapters, AdapterKind::Wlan);
        if (!a) { if (problem) *problem = Tr(L"Kein WLAN-Adapter gefunden für ") + what; return L""; }
        return ToLowerW(a->guid);          // im resolved.json immer kleingeschrieben
    }
    case RouteMode::Lan: {
        const AdapterInfo* a = FindAdapterByRole(adapters, AdapterKind::Lan);
        if (!a) { if (problem) *problem = Tr(L"Kein Ethernet-Adapter gefunden für ") + what; return L""; }
        return ToLowerW(a->guid);
    }
    case RouteMode::Adapter: {
        std::wstring norm;
        if (!ParseGuid(res.adapterGuid, norm)) { if (problem) *problem = Tr(L"Ungültige GUID für ") + what; return L""; }
        const AdapterInfo* a = FindAdapterByGuid(adapters, norm);
        if (!a) { if (problem) *problem = Tr(L"Adapter ") + FormatGuidShort(norm) + Tr(L" nicht vorhanden für ") + what; return L""; }
        return norm;
    }
    default: return L"";
    }
}

JsonPtr BuildResolvedJson(const Ruleset& rs, const std::vector<AdapterInfo>& adapters,
                          long long generation, std::vector<std::wstring>& problems) {
    JsonPtr root = Json::MakeObject();
    root->set(L"version", Json::MakeNumber(1));
    root->set(L"generation", Json::MakeNumber(static_cast<double>(generation)));
    root->set(L"updated", Json::MakeString(NowIsoLocal()));

    JsonPtr def = Json::MakeObject();
    def->set(L"mode", Json::MakeNumber(static_cast<double>(static_cast<int>(rs.defaultMode))));
    def->set(L"guid", Json::MakeString(L""));
    if (rs.defaultMode != RouteMode::None) {
        Resolved r;
        r.mode = rs.defaultMode;
        r.adapterGuid = rs.defaultAdapterGuid;
        std::wstring problem;
        std::wstring g = GuidForMode(adapters, r, &problem, L"Standardregel");
        if (g.empty() && !problem.empty()) problems.push_back(problem);
        def->set(L"guid", Json::MakeString(g));
    }
    root->set(L"default", def);

    // Höchste Priorität gewinnt; Dopplungen (gleiches Bild + Pfad) werden verworfen.
    std::set<std::wstring> seen;
    JsonPtr arr = Json::MakeArray();
    for (const Rule& r : rs.rules) {
        if (!r.enabled) continue;
        Resolved res;
        res.mode = r.mode;
        res.adapterGuid = r.adapterGuid;
        std::wstring guid = GuidForMode(adapters, res, nullptr, Tr(L"Regel \"") + r.pattern + L"\"");
        if (res.mode != RouteMode::None && guid.empty()) {
            std::wstring problem;
            GuidForMode(adapters, res, &problem, Tr(L"Regel \"") + r.pattern + L"\"");
            if (!problem.empty()) problems.push_back(problem);
        }
        std::wstring key = ToLowerW(r.pattern) + L"|";
        if (seen.count(key)) continue;
        seen.insert(key);
        JsonPtr o = Json::MakeObject();
        o->set(L"image", Json::MakeString(ToLowerW(r.pattern)));
        o->set(L"path", Json::MakeString(ToLowerW(r.pathPattern)));
        o->set(L"mode", Json::MakeNumber(static_cast<double>(static_cast<int>(r.mode))));
        o->set(L"guid", Json::MakeString(guid));
        arr->push(o);
    }
    root->set(L"entries", arr);

    // Adapter-Tabelle für den Hook: GUID -> Interface-Index (IP_UNICAST_IF braucht den Index)
    JsonPtr ad = Json::MakeArray();
    for (const AdapterInfo& a : adapters) {
        if (a.guid.empty()) continue;
        JsonPtr o = Json::MakeObject();
        o->set(L"guid", Json::MakeString(ToLowerW(a.guid)));
        o->set(L"ifIndex", Json::MakeNumber(static_cast<double>(a.ifIndex)));
        o->set(L"alias", Json::MakeString(a.alias));
        o->set(L"kind", Json::MakeNumber(static_cast<double>(static_cast<int>(a.kind))));
        o->set(L"up", Json::MakeBool(a.isUp));
        ad->push(o);
    }
    root->set(L"adapters", ad);
    return root;
}

// Zeitmarke einer Datei als eine Zahl (100-Nanosekunden-Seit 1601).
static unsigned long long Stamp(const FILETIME& t) {
    return (static_cast<unsigned long long>(t.dwHighDateTime) << 32) | t.dwLowDateTime;
}

// Schreiben und dabei sicherstellen, dass die Zeitmarke der Datei gegenueber der
// alten Datei faellt. Gebundene Programme entscheiden ueber diese Marke, ob sie
// erneut lesen; zwei Schreiben koennen auf dieselbe Marke fallen, weil die Uhr
// nur etwa alle 16 ms weitergeht. Dann bliebe eine aenderte Entscheidung blind.
static bool WriteStamped(const std::wstring& file, const std::wstring& text,
                         std::wstring& err) {
    FILETIME vorher = {};
    const HANDLE alt = CreateFileW(file.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                   nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (alt != INVALID_HANDLE_VALUE) {
        GetFileTime(alt, nullptr, nullptr, &vorher);
        CloseHandle(alt);
    }
    if (!WriteTextFileAtomic(file, text, err)) return false;

    if (vorher.dwHighDateTime || vorher.dwLowDateTime) {
        const HANDLE h = CreateFileW(file.c_str(), FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                     nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            FILETIME jetzt = {};
            GetFileTime(h, nullptr, nullptr, &jetzt);
            if (Stamp(jetzt) <= Stamp(vorher)) {
                const unsigned long long weiter = Stamp(vorher) + 10000;   // 1 ms spaeter
                FILETIME ziel{};
                ziel.dwLowDateTime = static_cast<DWORD>(weiter);
                ziel.dwHighDateTime = static_cast<DWORD>(weiter >> 32);
                SetFileTime(h, nullptr, nullptr, &ziel);
            }
            CloseHandle(h);
        }
    }
    return true;
}

bool WriteResolved(const Paths& p, const Ruleset& rs, const std::vector<AdapterInfo>& adapters,
                   std::vector<std::wstring>& problems, std::wstring& err) {
    problems.clear();
    JsonPtr doc = BuildResolvedJson(rs, adapters, 0, problems);
    const std::wstring text = JsonSerialize(doc, false) + L"\r\n";
    return WriteStamped(p.resolvedFile, text, err);
}

bool ParseResolvedDoc(const std::wstring& text, JsonPtr& out) {
    JsonParseResult r = JsonParse(text, out);
    return r.ok;
}

bool ResolvedIfIndex(const JsonPtr& doc, const std::wstring& guid, unsigned long& ifIndex) {
    ifIndex = 0;
    std::wstring norm;
    if (!ParseGuid(guid, norm)) return false;
    if (!doc || !doc->isObject()) return false;
    const JsonPtr* ads = doc->find(L"adapters");
    if (!ads || !(*ads) || !(*ads)->isArray()) return false;
    for (const JsonPtr& a : (*ads)->arr) {
        if (!a || !a->isObject()) continue;
        if (ToLowerW(a->strOr(L"guid", L"")) != norm) continue;
        double d = 0;
        if (!a->getNum(L"ifIndex", d) || d <= 0) return false;
        ifIndex = static_cast<unsigned long>(d);
        return true;
    }
    return false;
}

ResolvedLookupResult ResolvedLookup(const JsonPtr& doc, const std::wstring& imageLower, const std::wstring& pathLower) {
    ResolvedLookupResult res;
    if (!doc || !doc->isObject()) return res;
    const JsonPtr* entries = doc->find(L"entries");
    if (entries && (*entries) && (*entries)->isArray()) {
        for (const JsonPtr& e : (*entries)->arr) {
            if (!e || !e->isObject()) continue;
            std::wstring img = e->strOr(L"image", L"");
            if (ToLowerW(img) != imageLower) continue;
            std::wstring pat = e->strOr(L"path", L"");
            if (!pat.empty()) {
                if (pathLower.empty()) continue;
                if (!WildcardMatch(pat, pathLower)) continue;
            }
            res.found = true;
            res.mode = static_cast<RouteMode>(e->intOr(L"mode", 0));
            res.guid = e->strOr(L"guid", L"");
            return res;
        }
    }
    const JsonPtr* def = doc->find(L"default");
    if (def && (*def) && (*def)->isObject()) {
        int m = (*def)->intOr(L"mode", 0);
        std::wstring g = (*def)->strOr(L"guid", L"");
        if (m != 0 && !g.empty()) {
            res.found = true;
            res.mode = static_cast<RouteMode>(m);
            res.guid = g;
        }
    }
    return res;
}

// ---------------------------------------------------------------- Adapter

// IP-Adresse (Binär) -> lesbarer Text. inet_ntop arbeitet mit char*, hier immer ASCII.
static std::wstring IpAddrText(int family, const void* addr) {
    char buf[64] = {};
    if (!inet_ntop(family, addr, buf, sizeof(buf))) return std::wstring();
    return WFromU8(buf);
}

static std::wstring WFromAscii(const char* text) {
    if (!text) return std::wstring();
    std::wstring out;
    for (const char* p = text; *p; ++p) out.push_back(static_cast<wchar_t>(static_cast<unsigned char>(*p)));
    return out;
}

bool EnumerateAdapters(std::vector<AdapterInfo>& out, std::wstring& err) {
    out.clear();

    // 1) MIB_IF_TABLE2: Medium, Typ, Link, Zähler
    struct IfTableDeleter { void operator()(PMIB_IF_TABLE2 p) const { if (p) FreeMibTable(p); } };
    PMIB_IF_TABLE2 raw = nullptr;
    DWORD r = GetIfTable2(&raw);
    std::unique_ptr<MIB_IF_TABLE2, IfTableDeleter> ift(raw);
    if (r != NO_ERROR) {
        err = Tr(L"GetIfTable2: ") + LastErrText(r);
        if (r != ERROR_ACCESS_DENIED) return false;   // ohne Rechte reicht GetAdaptersAddresses
    }

    struct RowExtra {
        unsigned long ifType = 0;
        long physMedium = -1;
        unsigned long accessType = 0;
        bool up = false;
        unsigned long long rx = 0, tx = 0, linkSpeed = 0;
        bool have = false;
    };
    std::map<unsigned long long, RowExtra> byLuid;
    if (ift) {
        for (DWORD i = 0; i < ift->NumEntries; ++i) {
            const MIB_IF_ROW2& row = ift->Table[i];
            RowExtra x;
            x.ifType = static_cast<unsigned long>(row.Type);
            x.physMedium = static_cast<long>(row.PhysicalMediumType);
            x.accessType = static_cast<unsigned long>(row.AccessType);
            x.up = (row.MediaConnectState == MediaConnectStateConnected) ||
                   (row.OperStatus >= MIB_IF_OPER_STATUS_OPERATIONAL);
            x.rx = row.InOctets;
            x.tx = row.OutOctets;
            x.linkSpeed = row.ReceiveLinkSpeed != NET_IF_LINK_SPEED_UNKNOWN ? row.ReceiveLinkSpeed : row.TransmitLinkSpeed;
            x.have = true;
            byLuid[row.InterfaceLuid.Value] = x;
        }
    }

    // 2) GetAdaptersAddresses: GUID, IP, Gateway, Name
    ULONG flags = GAA_FLAG_INCLUDE_GATEWAYS | GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
    ULONG size = 0;
    DWORD rr = GetAdaptersAddresses(AF_UNSPEC, flags, nullptr, nullptr, &size);
    if (rr != ERROR_BUFFER_OVERFLOW && rr != NO_ERROR) { err = Tr(L"GetAdaptersAddresses: ") + LastErrText(rr); return false; }
    std::vector<char> buf(size ? size : 1);
    IP_ADAPTER_ADDRESSES* aa = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data());
    rr = GetAdaptersAddresses(AF_UNSPEC, flags, nullptr, aa, &size);
    if (rr != NO_ERROR) { err = Tr(L"GetAdaptersAddresses: ") + LastErrText(rr); return false; }

    for (IP_ADAPTER_ADDRESSES* p = aa; p; p = p->Next) {
        if (p->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
        AdapterInfo a;
        a.ifIndex = p->IfIndex;
        a.luid = p->Luid.Value;
        a.alias = p->FriendlyName ? p->FriendlyName : L"";
        a.description = p->Description ? p->Description : L"";
        a.guid = WFromAscii(p->AdapterName);   // AdapterName ist ANSI und enthält "{GUID}"
        auto it = byLuid.find(a.luid);
        RowExtra x;
        if (it != byLuid.end()) x = it->second;
        if (!x.have) {
            x.ifType = static_cast<unsigned long>(p->IfType);
            x.up = (p->OperStatus == IfOperStatusUp);
            x.linkSpeed = p->TransmitLinkSpeed;
        }
        a.kind = ClassifyMedium(x.ifType ? x.ifType : static_cast<unsigned long>(p->IfType), x.physMedium,
                                static_cast<int>(p->ConnectionType), a.alias, a.description);
        a.isVirtual = LooksVirtual(a.description, a.alias, x.physMedium, x.accessType);
        a.isUp = x.up || (p->OperStatus == IfOperStatusUp);
        a.rxBytes = x.rx;
        a.txBytes = x.tx;
        a.linkSpeedBps = x.linkSpeed ? x.linkSpeed : p->TransmitLinkSpeed;

        for (IP_ADAPTER_UNICAST_ADDRESS* u = p->FirstUnicastAddress; u; u = u->Next) {
            if (!u->Address.lpSockaddr) continue;
            if (u->Address.lpSockaddr->sa_family == AF_INET) {
                std::wstring s = IpAddrText(AF_INET, &reinterpret_cast<sockaddr_in*>(u->Address.lpSockaddr)->sin_addr);
                if (s.empty()) continue;
                if (a.ipv4.empty() || !IsLinkLocalIpv4(a.ipv4)) {
                    if (!IsLinkLocalIpv4(s)) a.ipv4 = s;
                    else if (a.ipv4.empty()) a.ipv4 = s;
                }
            }
        }
        for (IP_ADAPTER_GATEWAY_ADDRESS_LH* g = p->FirstGatewayAddress; g; g = g->Next) {
            if (!g->Address.lpSockaddr) continue;
            if (g->Address.lpSockaddr->sa_family == AF_INET) {
                std::wstring s = IpAddrText(AF_INET, &reinterpret_cast<sockaddr_in*>(g->Address.lpSockaddr)->sin_addr);
                if (!s.empty() && a.gateway.empty()) a.gateway = s;
            }
        }

        // 3) InterfaceMetric: welcher Adapter gewinnt standardmäßig
        MIB_IPINTERFACE_ROW ir = {};
        ir.Family = AF_INET;
        ir.InterfaceLuid.Value = a.luid;
        if (GetIpInterfaceEntry(&ir) == NO_ERROR) a.ifMetric = ir.Metric;

        out.push_back(a);
    }

    // 4) Default-Route markieren
    PMIB_IPFORWARD_TABLE2 fwd = nullptr;
    if (GetIpForwardTable2(AF_INET, &fwd) == NO_ERROR) {
        struct FwdDeleter { void operator()(PMIB_IPFORWARD_TABLE2 p) const { if (p) FreeMibTable(p); } };
        std::unique_ptr<MIB_IPFORWARD_TABLE2, FwdDeleter> f(fwd);
        for (DWORD i = 0; i < fwd->NumEntries; ++i) {
            const MIB_IPFORWARD_ROW2& row = fwd->Table[i];
            if (row.DestinationPrefix.PrefixLength != 0) continue;
            for (AdapterInfo& a : out) {
                if (a.luid == row.InterfaceLuid.Value) {
                    a.hasDefaultRoute = true;
                    if (a.gateway.empty() && row.NextHop.si_family == AF_INET) {
                        std::wstring s = IpAddrText(AF_INET, &row.NextHop.Ipv4.sin_addr);
                        if (!s.empty()) a.gateway = s;
                    }
                }
            }
        }
    }

    std::sort(out.begin(), out.end(), [](const AdapterInfo& l, const AdapterInfo& r) {
        auto rank = [](const AdapterInfo& a) {
            int k = (a.kind == AdapterKind::Wlan || a.kind == AdapterKind::Lan) ? 0 : 1;
            int up = a.isUp ? 0 : 1;
            int virt = a.isVirtual ? 1 : 0;
            return k * 100 + up * 10 + virt;
        };
        int rl = rank(l), rr2 = rank(r);
        if (rl != rr2) return rl < rr2;
        if (l.ifMetric != r.ifMetric) return l.ifMetric < r.ifMetric;
        return l.alias < r.alias;
    });
    return true;
}

void UpdateRates(std::vector<AdapterInfo>& adapters, const std::vector<AdapterInfo>& prev, long long dtMs) {
    if (dtMs <= 0) dtMs = 1;
    for (AdapterInfo& a : adapters) {
        const AdapterInfo* old = nullptr;
        for (const AdapterInfo& p : prev) {
            if (p.luid && p.luid == a.luid) { old = &p; break; }
            if (!p.luid && p.ifIndex == a.ifIndex) { old = &p; break; }
        }
        if (!old) { a.rxRate = 0; a.txRate = 0; continue; }
        long long dr = static_cast<long long>(a.rxBytes) - static_cast<long long>(old->rxBytes);
        long long dt = static_cast<long long>(a.txBytes) - static_cast<long long>(old->txBytes);
        if (dr < 0) dr = 0;
        if (dt < 0) dt = 0;
        a.rxRate = dr * 1000 / dtMs;
        a.txRate = dt * 1000 / dtMs;
    }
}

unsigned long AdapterIndexForLocalIp(const std::vector<AdapterInfo>& adapters, const std::wstring& ip) {
    if (ip.empty()) return 0;
    for (const AdapterInfo& a : adapters) if (!a.ipv4.empty() && a.ipv4 == ip) return a.ifIndex;
    return 0;
}

// ---------------------------------------------------------------- Prozesse

bool EnumerateProcesses(std::vector<ProcInfo>& out, std::wstring& err) {
    out.clear();
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) { err = LastErrText(GetLastError()); return false; }
    PROCESSENTRY32W pe = {};
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            ProcInfo p;
            p.pid = pe.th32ProcessID;
            p.ppid = pe.th32ParentProcessID;
            p.image = pe.szExeFile;
            HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, p.pid);
            if (h) {
                wchar_t buf[MAX_PATH * 4];
                DWORD n = static_cast<DWORD>(sizeof(buf) / sizeof(buf[0]));
                if (QueryFullProcessImageNameW(h, 0, buf, &n)) { p.path = buf; p.pathAccess = true; }
                // 32-Bit-Programme brauchen eine 32-Bit-Hook-Bibliothek.
                typedef BOOL(WINAPI* FnWow64)(HANDLE, PBOOL);
                static FnWow64 wow64 = reinterpret_cast<FnWow64>(
                    GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "IsWow64Process"));
                if (wow64) {
                    BOOL wow = FALSE;
                    if (wow64(h, &wow)) p.is32Bit = (wow != FALSE);
                }
                HANDLE tok = nullptr;
                if (OpenProcessToken(h, TOKEN_QUERY, &tok)) {
                    DWORD need = 0;
                    GetTokenInformation(tok, TokenUser, nullptr, 0, &need);
                    if (need) {
                        std::vector<char> tbuf(need);
                        if (GetTokenInformation(tok, TokenUser, tbuf.data(), need, &need)) {
                            auto* tu = reinterpret_cast<TOKEN_USER*>(tbuf.data());
                            wchar_t name[256], dom[256];
                            DWORD cn = 256, dn = 256;
                            SID_NAME_USE use;
                            if (LookupAccountSidW(nullptr, tu->User.Sid, name, &cn, dom, &dn, &use))
                                p.user = dom[0] ? (std::wstring(dom) + L"\\" + name) : name;
                        }
                    }
                    CloseHandle(tok);
                }
                CloseHandle(h);
            }
            out.push_back(p);
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return true;
}

bool EnumerateTcpConnections(std::vector<TcpConn>& out, std::wstring& err) {
    out.clear();
    const ADDRESS_FAMILY fams[2] = { AF_INET, AF_INET6 };
    for (ADDRESS_FAMILY fam : fams) {
        ULONG size = 0;
        DWORD r = GetExtendedTcpTable(nullptr, &size, FALSE, fam, TCP_TABLE_OWNER_PID_ALL, 0);
        if (r != ERROR_INSUFFICIENT_BUFFER && r != NO_ERROR) { err = Tr(L"GetExtendedTcpTable: ") + LastErrText(r); continue; }
        std::vector<char> buf(size ? size : 4);
        r = GetExtendedTcpTable(buf.data(), &size, FALSE, fam, TCP_TABLE_OWNER_PID_ALL, 0);
        if (r != NO_ERROR) { err = Tr(L"GetExtendedTcpTable: ") + LastErrText(r); continue; }
        if (fam == AF_INET) {
            PMIB_TCPTABLE_OWNER_PID v4 = reinterpret_cast<PMIB_TCPTABLE_OWNER_PID>(buf.data());
            for (DWORD i = 0; i < v4->dwNumEntries; ++i) {
                const MIB_TCPROW_OWNER_PID& row = v4->table[i];
                TcpConn c;
                c.pid = row.dwOwningPid;
                c.localAddr = IpAddrText(AF_INET, &row.dwLocalAddr);
                c.remoteAddr = IpAddrText(AF_INET, &row.dwRemoteAddr);
                c.remotePort = ntohs(static_cast<unsigned short>(row.dwRemotePort & 0xFFFF));
                c.state = row.dwState;
                out.push_back(c);
            }
        } else {
            PMIB_TCP6TABLE_OWNER_PID v6 = reinterpret_cast<PMIB_TCP6TABLE_OWNER_PID>(buf.data());
            for (DWORD i = 0; i < v6->dwNumEntries; ++i) {
                const MIB_TCP6ROW_OWNER_PID& row = v6->table[i];
                TcpConn c;
                c.pid = row.dwOwningPid;
                c.localAddr = IpAddrText(AF_INET6, row.ucLocalAddr);
                c.remoteAddr = IpAddrText(AF_INET6, row.ucRemoteAddr);
                c.remotePort = ntohs(static_cast<unsigned short>(row.dwRemotePort & 0xFFFF));
                c.state = row.dwState;
                c.isIPv6 = true;
                out.push_back(c);
            }
        }
    }
    return true;
}

bool IsWow64ProcessById(unsigned long pid, bool& is32, std::wstring& err) {
    is32 = false;
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) { err = LastErrText(GetLastError()); return false; }
    BOOL wow64 = FALSE;
    BOOL ok = IsWow64Process(h, &wow64);
    DWORD e = GetLastError();
    CloseHandle(h);
    if (!ok) { err = LastErrText(e); return false; }
    is32 = wow64 != FALSE;
    return true;
}

bool IsProcessRunning(unsigned long pid, const std::wstring& expectImageLower) {
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return false;
    bool ok = true;
    if (!expectImageLower.empty()) {
        wchar_t buf[MAX_PATH * 4];
        DWORD n = static_cast<DWORD>(sizeof(buf) / sizeof(buf[0]));
        if (QueryFullProcessImageNameW(h, 0, buf, &n)) {
            const std::wstring full = buf;
            size_t pos = full.find_last_of(L"\\/");
            std::wstring name = (pos == std::wstring::npos) ? full : full.substr(pos + 1);
            ok = ToLowerW(name) == expectImageLower;
        }
    }
    CloseHandle(h);
    return ok;
}

// ---------------------------------------------------------------- Zeilen, Sortierung, Filter

static const AdapterInfo* RoleAd(const std::vector<AdapterInfo>& adapters, RouteMode mode) {
    if (mode == RouteMode::Wlan) return FindAdapterByRole(adapters, AdapterKind::Wlan);
    if (mode == RouteMode::Lan) return FindAdapterByRole(adapters, AdapterKind::Lan);
    return nullptr;
}

std::vector<ProcRow> BuildRows(const std::vector<ProcInfo>& procs,
                               const std::vector<TcpConn>& conns,
                               const std::vector<AdapterInfo>& adapters,
                               const Ruleset& rs,
                               const std::vector<unsigned long>& injectedPids) {
    std::map<unsigned long, int> connCount;
    std::map<unsigned long, std::wstring> connVia;
    for (const TcpConn& c : conns) {
        if (c.localAddr.rfind(L"127.", 0) == 0 || c.localAddr == L"::1") {
            connCount[c.pid] += 0;   // Loopback zählt nicht als Leitungs-Nutzung
            continue;
        }
        connCount[c.pid] += 1;
        if (connVia[c.pid].empty()) {
            unsigned long idx = AdapterIndexForLocalIp(adapters, c.localAddr);
            if (idx) {
                for (const AdapterInfo& a : adapters) if (a.ifIndex == idx) { connVia[c.pid] = a.alias; break; }
            }
        }
    }
    std::set<unsigned long> injected(injectedPids.begin(), injectedPids.end());

    std::vector<ProcRow> rows;
    rows.reserve(procs.size());
    for (const ProcInfo& p : procs) {
        ProcRow r;
        r.pid = p.pid;
        r.ppid = p.ppid;
        r.image = p.image;
        r.path = p.path;
        r.user = p.user;
        r.pathAccess = p.pathAccess;
        r.is32Bit = p.is32Bit;
        auto it = connCount.find(p.pid);
        r.conns = (it == connCount.end()) ? 0 : it->second;
        r.hasNetwork = r.conns > 0;
        auto vit = connVia.find(p.pid);
        if (vit != connVia.end()) r.viaAdapter = vit->second;
        Resolved res;
        rs.Resolve(p.image, p.path, res);
        r.mode = res.mode;
        r.ruleId = res.ruleId;
        r.fromDefault = res.fromDefault;
        if (res.mode == RouteMode::Adapter) {
            const AdapterInfo* a = FindAdapterByGuid(adapters, res.adapterGuid);
            r.modeGuid = a ? a->guid : res.adapterGuid;
        } else {
            const AdapterInfo* a = RoleAd(adapters, res.mode);
            r.modeGuid = a ? a->guid : L"";
        }
        r.injected = injected.count(p.pid) > 0;
        rows.push_back(r);
    }
    return rows;
}

void SortRows(std::vector<ProcRow>& rows, SortField field, bool ascending) {
    auto cmp = [&](const ProcRow& a, const ProcRow& b) {
        int c = 0;
        switch (field) {
        case SortField::Name: {
            // Prozesse mit Netzwerk zuerst, dann alphabetisch
            if (a.hasNetwork != b.hasNetwork) c = a.hasNetwork ? -1 : 1;
            if (c == 0) c = _wcsicmp(a.image.c_str(), b.image.c_str());
            if (c == 0) c = (a.pid < b.pid) ? -1 : (a.pid > b.pid ? 1 : 0);
            break;
        }
        case SortField::Pid: c = (a.pid < b.pid) ? -1 : (a.pid > b.pid ? 1 : 0); break;
        case SortField::Via: c = _wcsicmp(a.viaAdapter.c_str(), b.viaAdapter.c_str()); break;
        case SortField::Mode: c = (static_cast<int>(a.mode) - static_cast<int>(b.mode)); break;
        case SortField::Conns: c = (a.conns - b.conns); break;
        case SortField::User: c = _wcsicmp(a.user.c_str(), b.user.c_str()); break;
        }
        if (c == 0) c = (a.pid < b.pid) ? -1 : (a.pid > b.pid ? 1 : 0);
        return ascending ? (c < 0) : (c > 0);
    };
    std::stable_sort(rows.begin(), rows.end(), cmp);
}

void FilterRows(const std::vector<ProcRow>& rows, const std::wstring& query, bool onlyNetwork,
                std::vector<unsigned long>& pidOut) {
    pidOut.clear();
    const std::wstring q = ToLowerW(TrimW(query));
    for (const ProcRow& r : rows) {
        if (onlyNetwork && r.conns <= 0) continue;
        if (!q.empty()) {
            bool hit = ToLowerW(r.image).find(q) != std::wstring::npos
                    || ToLowerW(r.path).find(q) != std::wstring::npos
                    || ToLowerW(r.user).find(q) != std::wstring::npos;
            if (!hit) {
                wchar_t digits[16];
                swprintf(digits, 16, L"%lu", r.pid);
                if (std::wstring(digits).find(q) == std::wstring::npos) continue;
                hit = true;
            }
            if (!hit) continue;
        }
        pidOut.push_back(r.pid);
    }
}

DropTarget DropTargetForColumn(int column) {
    DropTarget t;
    switch (column) {
    case 3: t.valid = true; t.mode = RouteMode::Wlan; break;
    case 4: t.valid = true; t.mode = RouteMode::Lan; break;
    case 5: t.valid = true; t.mode = RouteMode::None; break;
    default: break;
    }
    return t;
}

// ---------------------------------------------------------------- Injektion

bool IsModuleLoadedInProcess(unsigned long pid, const std::wstring& moduleBaseLower, bool& loaded, std::wstring& err) {
    loaded = false;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snap == INVALID_HANDLE_VALUE) {
        DWORD e = GetLastError();
        if (e == ERROR_BAD_LENGTH || e == ERROR_ACCESS_DENIED) return true;   // unbekannt, nicht als Fehler werten
        err = LastErrText(e);
        return false;
    }
    MODULEENTRY32W me = {};
    me.dwSize = sizeof(me);
    if (Module32FirstW(snap, &me)) {
        do {
            if (ToLowerW(me.szModule) == moduleBaseLower) { loaded = true; break; }
        } while (Module32NextW(snap, &me));
    }
    CloseHandle(snap);
    return true;
}

bool InjectDll(unsigned long pid, const std::wstring& dllPath, std::wstring& err) {
    if (!FileExists(dllPath)) { err = Tr(L"Hook-Bibliothek fehlt: ") + dllPath; return false; }

    bool is32 = false;
    std::wstring e2;
    if (IsWow64ProcessById(pid, is32, e2) && is32) { err = Tr(L"32-Bit-Prozess: nur 64-Bit-Anbindung möglich"); return false; }

    HANDLE h = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION |
                           PROCESS_VM_WRITE | PROCESS_VM_READ, FALSE, pid);
    if (!h) {
        DWORD e = GetLastError();
        err = (e == ERROR_ACCESS_DENIED) ? Tr(L"Zugriff verweigert (Prozess läuft höher privilegiert oder ist geschützt)")
                                         : LastErrText(e);
        return false;
    }
    bool ok = false;
    size_t chars = dllPath.size() + 1;
    SIZE_T bytes = chars * sizeof(wchar_t);
    void* remote = VirtualAllocEx(h, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote) {
        err = Tr(L"VirtualAllocEx: ") + LastErrText(GetLastError());
    } else if (!WriteProcessMemory(h, remote, dllPath.c_str(), bytes, nullptr)) {
        err = Tr(L"WriteProcessMemory: ") + LastErrText(GetLastError());
    } else {
        HMODULE user32 = GetModuleHandleW(L"kernel32.dll");
        FARPROC fn = GetProcAddress(user32, "LoadLibraryW");
        if (!fn) {
            err = Tr(L"LoadLibraryW nicht gefunden");
        } else {
            HANDLE th = CreateRemoteThread(h, nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(fn), remote, 0, nullptr);
            if (!th) {
                err = Tr(L"CreateRemoteThread: ") + LastErrText(GetLastError());
            } else {
                DWORD wait = WaitForSingleObject(th, 10000);
                DWORD exitCode = 0;
                GetExitCodeThread(th, &exitCode);
                CloseHandle(th);
                if (wait != WAIT_OBJECT_0) {
                    err = Tr(L"Injektion braucht zu lange (Timeout)");
                } else if (exitCode == 0) {
                    err = Tr(L"LoadLibrary schlug im Zielprozess fehl (Virenschutz? geschützter Prozess?)");
                } else {
                    ok = true;
                }
            }
        }
    }
    if (remote) VirtualFreeEx(h, remote, 0, MEM_RELEASE);
    CloseHandle(h);
    return ok;
}

// Basisadresse eines Moduls im Zielprozess (für Remote-Aufrufe eigener Exporte).
static std::wstring LowerBaseName(const std::wstring& path) {
    size_t pos = path.find_last_of(L"\\/");
    return ToLowerW(pos == std::wstring::npos ? path : path.substr(pos + 1));
}

static bool RemoteModuleBase(unsigned long pid, const std::wstring& modulePath,
                             unsigned long long& baseOut, std::wstring& err) {
    baseOut = 0;
    std::wstring want = LowerBaseName(modulePath);
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snap == INVALID_HANDLE_VALUE) { err = Tr(L"Modulauflistung: ") + LastErrText(GetLastError()); return false; }
    bool found = false;
    MODULEENTRY32W me = {};
    me.dwSize = sizeof(me);
    if (Module32FirstW(snap, &me)) {
        do {
            if (ToLowerW(me.szModule) == want) {
                baseOut = static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(me.modBaseAddr));
                found = true;
                break;
            }
        } while (Module32NextW(snap, &me));
    }
    CloseHandle(snap);
    if (!found) { err = Tr(L"Modul nicht im Prozess gefunden: ") + want; return false; }
    return true;
}

// Ruft einen Export der eigenen hook.dll im Zielprozess auf: kein Parameter, DWORD-Rückgabe.
// Die hook.dll wird dazu lokal gespiegelt (DllMain der Hook-Bibliothek tut nichts), um die
// Relative Adresse des Exports zu bestimmen und im Zielprozess zu der dortigen Basis zu addieren.
static bool CallRemoteHookProc(unsigned long pid, const std::wstring& dllPath, const char* procName,
                               unsigned long& resultOut, std::wstring& err) {
    resultOut = 0;
    if (!FileExists(dllPath)) { err = Tr(L"Hook-Bibliothek fehlt: ") + dllPath; return false; }

    unsigned long long remoteBase = 0;
    if (!RemoteModuleBase(pid, dllPath, remoteBase, err)) return false;

    static HMODULE local = nullptr;
    if (!local) {
        local = LoadLibraryW(dllPath.c_str());
        if (!local) { err = Tr(L"Lokales Laden der Hook-Bibliothek: ") + LastErrText(GetLastError()); return false; }
    }
    FARPROC fn = GetProcAddress(local, procName);
    if (!fn) { err = std::wstring(Tr(L"Export ")) + WFromAscii(procName) + L" fehlt in hook.dll"; return false; }
    unsigned long long rva = static_cast<unsigned long long>(
        reinterpret_cast<uintptr_t>(fn) - reinterpret_cast<uintptr_t>(local));

    HANDLE h = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION |
                           PROCESS_VM_READ, FALSE, pid);
    if (!h) { err = LastErrText(GetLastError()); return false; }
    bool ok = false;
    HANDLE th = CreateRemoteThread(h, nullptr, 0,
                                   reinterpret_cast<LPTHREAD_START_ROUTINE>(
                                       reinterpret_cast<unsigned char*>(static_cast<uintptr_t>(remoteBase)) + rva),
                                   nullptr, 0, nullptr);
    if (!th) {
        err = Tr(L"CreateRemoteThread: ") + LastErrText(GetLastError());
    } else {
        DWORD wait = WaitForSingleObject(th, 8000);
        DWORD code = 0;
        GetExitCodeThread(th, &code);
        CloseHandle(th);
        if (wait != WAIT_OBJECT_0) err = Tr(L"Remote-Aufruf braucht zu lange (Timeout)");
        else { resultOut = static_cast<unsigned long>(code); ok = true; }
    }
    CloseHandle(h);
    return ok;
}

// Aktiviert die Hooks in einem bereits injizierten Prozess (wiederholbar, unschädlich).
bool ActivateHookInProcess(unsigned long pid, const std::wstring& dllPath, std::wstring& err) {
    unsigned long r = 0;
    if (!CallRemoteHookProc(pid, dllPath, "SrtStartup", r, err)) return false;
    if (r != 1) { err = Tr(L"Hook-Aktivierung meldete Rückgabe ") + std::to_wstring(r); return false; }
    return true;
}

// Diagnose: aktueller Interface-Index, den die Hook-Bibliothek in diesem Prozess setzt.
bool QueryHookIfIndex(unsigned long pid, const std::wstring& dllPath, unsigned long& ifIndex, std::wstring& err) {
    return CallRemoteHookProc(pid, dllPath, "SrtIfIndex", ifIndex, err);
}

bool QueryHookPatchMask(unsigned long pid, const std::wstring& dllPath, unsigned long& mask, std::wstring& err) {
    return CallRemoteHookProc(pid, dllPath, "SrtPatchMask", mask, err);
}

// Injizieren falls nötig und aktivieren; warAlready sagt, ob die DLL schon geladen war.
bool InjectAndActivate(unsigned long pid, const std::wstring& dllPath, bool& warAlready, std::wstring& err) {
    warAlready = false;
    std::wstring ignored;
    bool loaded = false;
    IsModuleLoadedInProcess(pid, LowerBaseName(dllPath), loaded, ignored);
    warAlready = loaded;
    if (!loaded) {
        if (!InjectDll(pid, dllPath, err)) return false;
    }
    return ActivateHookInProcess(pid, dllPath, err);
}

bool CanInjectProcess(const ProcInfo& p, bool elevated, bool hookExists, std::wstring& why) {
    if (!hookExists) {
        why = p.is32Bit ? L"hook32.dll fehlt im Datenordner (32-Bit-Programm)"
                        : L"hook.dll fehlt im Datenordner";
        return false;
    }
    if (!elevated) { why = Tr(L"RoutePerApp läuft ohne Administratorrechte"); return false; }
    if (p.pid <= 4) { why = Tr(L"Systemprozess wird nicht angefasst"); return false; }
    if (ToLowerW(p.image) == L"routeperapp.exe") { why = L"RoutePerApp bindet sich nicht selbst ein"; return false; }
    if (!p.pathAccess) { why = Tr(L"Prozessinformationen nicht lesbar (höhere Priorität)"); return false; }
    return true;
}

bool IsProcessElevatedSelf(bool& elevated, std::wstring& err) {
    elevated = false;
    HANDLE tok = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) { err = LastErrText(GetLastError()); return false; }
    TOKEN_ELEVATION el = {};
    DWORD need = 0;
    if (!GetTokenInformation(tok, TokenElevation, &el, sizeof(el), &need)) {
        err = LastErrText(GetLastError());
        CloseHandle(tok);
        return false;
    }
    elevated = el.TokenIsElevated != 0;
    CloseHandle(tok);
    return true;
}

bool RelaunchElevated(std::wstring& err) {
    wchar_t path[MAX_PATH * 4];
    DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH * 4);
    if (!n || n >= MAX_PATH * 4 - 1) { err = Tr(L"Pfad der eigenen Datei nicht bestimmbar"); return false; }
    HINSTANCE r = ShellExecuteW(nullptr, L"runas", path, nullptr, nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(r) <= 32) { err = LastErrText(static_cast<DWORD>(reinterpret_cast<INT_PTR>(r))); return false; }
    return true;
}

// ---------------------------------------------------------------- Engine

Engine::Engine(Paths p) : paths(std::move(p)) {}

bool Engine::Init(std::wstring& err) {
    std::vector<std::wstring> warn;
    bool ok = LoadRuleset(paths, rules, warn, err);
    problems = warn;
    IsProcessElevatedSelf(elevated, lastError);
    GetFileMTime(paths.rulesFile, lastRulesMTime);
    RememberRulesHash(paths, lastRulesHash);
    if (elevated) EnsureDir(paths.dir, lastError);
    return ok;
}

void Engine::RefreshAdapters() {
    std::vector<AdapterInfo> prev = adapters;
    std::wstring err;
    std::vector<AdapterInfo> next;
    if (!EnumerateAdapters(next, err)) { lastError = err; return; }
    LARGE_INTEGER f;
    QueryPerformanceCounter(&f);
    static LARGE_INTEGER prevTick = [] { LARGE_INTEGER x; QueryPerformanceCounter(&x); return x; }();
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    long long ms = static_cast<long long>((f.QuadPart - prevTick.QuadPart) * 1000 / freq.QuadPart);
    prevTick = f;
    UpdateRates(next, prev, ms > 0 ? ms : 1000);
    adapters.swap(next);
}

void Engine::RefreshProcesses() {
    std::wstring err;
    if (!EnumerateProcesses(procs, err)) lastError = err;
    if (!EnumerateTcpConnections(conns, err)) lastError = err;
    // Injektionsliste aufräumen: tote Prozesse hinaus
    std::set<unsigned long> alive;
    for (const ProcInfo& p : procs) alive.insert(p.pid);
    std::vector<unsigned long> keep;
    for (unsigned long pid : injected) if (alive.count(pid)) keep.push_back(pid);
    injected.swap(keep);
}

void Engine::RebuildRows() {
    rows = BuildRows(procs, conns, adapters, rules, injected);
    for (AdapterInfo& a : adapters) { a.assignedCount = 0; a.activeCount = 0; }
    for (const ProcRow& r : rows) {
        if (r.mode == RouteMode::None) continue;
        for (AdapterInfo& a : adapters) {
            if (!r.modeGuid.empty() && ToLowerW(a.guid) == ToLowerW(r.modeGuid)) {
                a.assignedCount++;
                if (r.conns > 0) a.activeCount++;
                break;
            }
        }
    }
}

bool Engine::Publish() {
    std::wstring err;
    bool ok = WriteResolved(paths, rules, adapters, problems, err);
    if (!ok) lastError = err;
    else { ++generation; lastError.clear(); }
    return ok;
}

bool Engine::SaveAndPublish(std::wstring& err) {
    if (!SaveRuleset(paths, rules, err)) return false;
    GetFileMTime(paths.rulesFile, lastRulesMTime);
    RememberRulesHash(paths, lastRulesHash);
    if (!Publish()) { err = lastError; return false; }
    return true;
}

bool Engine::ReloadIfChanged(std::wstring& err) {
    // Der Zeitstempel allein genügt nicht: das System trägt ihn nur in groben Schritten
    // nach, zwei kurz hintereinander geschriebene Dateien sehen gleich aus. Deshalb wird
    // der Inhalt herangezogen – sonst würde eine von Hand geänderte rules.json übersehen.
    std::wstring text, rerr;
    if (!ReadTextFile(paths.rulesFile, text, rerr)) return false;   // Datei (noch) nicht da
    const unsigned long long h = HashText64(text);
    if (h == lastRulesHash) return false;
    lastRulesHash = h;
    GetFileMTime(paths.rulesFile, lastRulesMTime);
    std::vector<std::wstring> warn;
    if (!LoadRuleset(paths, rules, warn, err)) return false;
    problems = warn;
    return true;
}

bool Engine::Assign(const std::wstring& image, const std::wstring& path, RouteMode mode,
                    const std::wstring& adapterGuid, std::wstring& err) {
    if (TrimW(image).empty()) { err = Tr(L"Prozessname fehlt"); return false; }
    // Bestehende Regel für denselben Prozess überschreiben: eine Zuordnung pro Prozess.
    const std::wstring imgLower = ToLowerW(TrimW(image));
    for (size_t i = 0; i < rules.rules.size(); ++i) {
        Rule& r = rules.rules[i];
        if (!r.enabled) continue;
        std::wstring pat = ToLowerW(r.pattern);
        bool sameImage = (pat == imgLower);
        bool samePathRule = (!path.empty() && !r.pathPattern.empty() && ToLowerW(r.pathPattern) == ToLowerW(path));
        if (sameImage || samePathRule) {
            r.mode = mode;
            r.adapterGuid = (mode == RouteMode::Adapter) ? adapterGuid : L"";
            if (mode == RouteMode::None) rules.rules.erase(rules.rules.begin() + i);
            return SaveAndPublish(err);
        }
    }
    Rule r;
    r.pattern = imgLower;
    r.mode = mode;
    r.adapterGuid = (mode == RouteMode::Adapter) ? adapterGuid : L"";
    r.note = Tr(L"per Drag & Drop zugeordnet");
    if (!path.empty()) {
        // Bei gleichem Bildnamen an anderem Ort: Pfad präzisieren
        bool otherPathExists = false;
        for (const ProcInfo& p : procs) {
            if (ToLowerW(p.image) == imgLower && !p.path.empty() && ToLowerW(p.path) != ToLowerW(path)) { otherPathExists = true; break; }
        }
        if (otherPathExists) r.pathPattern = ToLowerW(path);
    }
    if (mode == RouteMode::None) return SaveAndPublish(err);   // nichts zu tun, aber speichern
    rules.AddTop(r);
    return SaveAndPublish(err);
}

bool Engine::ClearAssignment(unsigned long long ruleId, std::wstring& err) {
    if (!rules.Remove(ruleId)) { err = Tr(L"Regel nicht gefunden"); return false; }
    return SaveAndPublish(err);
}

int Engine::ReinjectMatching(std::wstring& err) {
    int already = 0, failed = 0;
    return ReinjectMatching(err, already, failed);
}

// Meldungen nur einmal anzeigen, der Prüfschlauf läuft häufig.
static void Merken(std::vector<std::wstring>& liste, const std::wstring& text) {
    for (const std::wstring& s : liste) if (s == text) return;
    liste.push_back(text);
}

// Bringt die Hook-Bibliotheken in den eigenen Unterordner und vergleicht jede Datei mit dem
// Exemplar neben der Programmdatei. Nur was danach gleich ist, wird angebunden: ein Programm
// mit Administratorrechten darf keine Bibliothek einschleusen, die jemand anderes geändert hat.
bool Engine::VerifyHooks(const std::wstring& trustedDir, std::wstring& err) {
    err.clear();
    hooksTrusted = false;

    std::wstring ordner;
    if (!EnsureDir(paths.hookDir, ordner)) { err = ordner; lastError = ordner; return false; }

    // Dateien der alten Ablage direkt im Datenordner: die kann jeder Benutzer ersetzen,
    // deshalb weg damit. Der Unterordner unten ist der einzige Ort, aus dem angebunden wird.
    static const wchar_t* namen[] = { L"hook.dll", L"hook32.dll" };
    for (int i = 0; i < 2; ++i) {
        const std::wstring alt = paths.dir + L"\\" + namen[i];
        if (FileExists(alt)) DeleteFileW(alt.c_str());
    }

    const std::wstring ziel[2] = { paths.hookDll, paths.hookDll32 };
    for (int i = 0; i < 2; ++i) {
        const std::wstring vorlage = trustedDir.empty() ? std::wstring() : trustedDir + L"\\" + namen[i];
        if (vorlage.empty() || !FileExists(vorlage)) {
            if (err.empty()) err = std::wstring(Tr(L"Hook-Bibliothek fehlt neben der Programmdatei: ")) + namen[i];
            lastError = err;
            return false;
        }
        std::wstring fehler, unveraendert, aktuell;
        if (!HashFile(vorlage, unveraendert, fehler)) {
            if (err.empty()) err = std::wstring(namen[i]) + L": " + fehler;
            lastError = err;
            return false;
        }
        bool erneuern = true;
        if (FileExists(ziel[i]) && HashFile(ziel[i], aktuell, fehler) && aktuell == unveraendert)
            erneuern = false;
        if (!erneuern) continue;

        if (!CopyFileW(vorlage.c_str(), ziel[i].c_str(), FALSE)) {
            const DWORD code = GetLastError();
            std::wstring warum = LastErrText(code);
            if (code == ERROR_SHARING_VIOLATION || code == ERROR_ACCESS_DENIED)
                warum += Tr(L" - RoutePerApp beenden und die gebundenen Programme neu starten");
            if (err.empty()) err = std::wstring(namen[i]) + Tr(L" konnte nicht erneuert werden: ") + warum;
            lastError = err;
            return false;
        }
        if (!HashFile(ziel[i], aktuell, fehler) || aktuell != unveraendert) {
            if (err.empty()) err = std::wstring(Tr(L"Hook-Bibliothek passt nicht zur Datei neben dem Programm: ")) + namen[i];
            lastError = err;
            return false;
        }
    }
    if (!err.empty()) { lastError = err; return false; }

    // Erst jetzt, nach dem Kopieren: ohne Administratorrechte ist der Schutz nicht zu setzen,
    // und ohne Administratorrechte wird auch nichts angebunden.
    if (elevated) {
        std::wstring schutz;
        if (!HardenFolder(paths.hookDir, schutz))
            Merken(problems, Tr(L"Der Hook-Ordner bleibt angreifbar: ") + schutz);
    }
    hooksTrusted = true;
    return true;
}

int Engine::ReinjectMatching(std::wstring& err, int& alreadyOut, int& failedOut) {
    err.clear();
    alreadyOut = 0;
    failedOut = 0;
    int done = 0;
    if (!elevated) { err = Tr(L"ohne Administratorrechte keine Anbindung möglich"); return 0; }
    if (!VerifyHooks(CurrentExeDir(), err)) return 0;
    const bool hook64 = FileExists(paths.hookDll);
    const bool hook32 = FileExists(paths.hookDll32);
    std::wstring firstError;
    for (const ProcRow& r : rows) {
        if (r.mode == RouteMode::None) continue;
        if (r.pid <= 4) continue;
        if (ToLowerW(r.image) == L"routeperapp.exe") continue;
        std::wstring why;
        ProcInfo pi;
        pi.pid = r.pid;
        pi.image = r.image;
        pi.path = r.path;
        pi.pathAccess = r.pathAccess;
        pi.is32Bit = r.is32Bit;
        if (!CanInjectProcess(pi, elevated, r.is32Bit ? hook32 : hook64, why)) continue;
        const std::wstring dll = r.is32Bit ? paths.hookDll32 : paths.hookDll;
        bool wasAlready = false;
        std::wstring e;
        if (InjectAndActivate(r.pid, dll, wasAlready, e)) {
            if (!r.injected) injected.push_back(r.pid);
            if (wasAlready) ++alreadyOut;      //DLL war schon drin, nur reaktiviert
            else ++done;                       // zählt nur neue Injektionen
        } else {
            ++failedOut;
            if (firstError.empty()) firstError = r.image + L": " + e;
        }
    }
    err = firstError;
    return done;
}

std::wstring Engine::HookDllFor(bool is32BitProgram) const {
    return is32BitProgram ? paths.hookDll32 : paths.hookDll;
}

const AdapterInfo* Engine::RoleAdapter(AdapterKind kind) const {
    return FindAdapterByRole(adapters, kind);
}

} // namespace sr
