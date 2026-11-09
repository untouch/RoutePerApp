// RoutePerApp - Kernschicht: Adapter, Prozesse, Regeln, Zuständigkeit, Injektion.
// GUI-frei und ohne globalen Zustand -> jede Funktion ist einzeln testbar.
#pragma once

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
// winsock2.h muss vor windows.h/winsock.h eingebunden werden.
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <psapi.h>

#include <string>
#include <vector>
#include <memory>

#include "json.hpp"

namespace sr {

// ---------------------------------------------------------------- Typen

enum class RouteMode : int {
    None = 0,     // keine Zuordnung (Windows-Standardrouting)
    Wlan = 1,     // 5G-Mobilrouter
    Lan = 2,      // Fritzbox / Ethernet
    Adapter = 3   // fester Adapter per GUID
};

enum class AdapterKind : int { Other = 0, Wlan = 1, Lan = 2 };

struct AdapterInfo {
    unsigned long ifIndex = 0;
    unsigned long long luid = 0;
    std::wstring guid;              // "{00000000-...}" (AdapterName)
    std::wstring alias;             // z.B. "Main", "WLAN 2"
    std::wstring description;       // z.B. "Realtek PCIe 2.5GbE Family Controller"
    AdapterKind kind = AdapterKind::Other;
    bool isUp = false;
    bool isVirtual = false;
    bool hasDefaultRoute = false;
    std::wstring ipv4;
    std::wstring gateway;
    unsigned long long linkSpeedBps = 0;
    unsigned long long rxBytes = 0;
    unsigned long long txBytes = 0;
    long long ifMetric = 0;         // InterfaceMetric (kleiner = bevorzugt)
    long long rxRate = 0;           // Bytes/s, von Engine berechnet
    long long txRate = 0;
    int assignedCount = 0;          // Prozesse, die laut Regeln hier laufen
    int activeCount = 0;            // davon gerade mit Verbindung
};

struct Rule {
    unsigned long long id = 0;
    std::wstring pattern;           // Bildname ("chrome.exe") oder Muster mit * / ?
    std::wstring pathPattern;       // optional: vollständiger Pfad-Muster
    RouteMode mode = RouteMode::None;
    std::wstring adapterGuid;       // nur bei mode == Adapter
    std::wstring note;
    bool enabled = true;
    long long created = 0;
};

struct Resolved {
    RouteMode mode = RouteMode::None;
    std::wstring adapterGuid;       // Aufgelöste GUID ("" = keine)
    unsigned long long ruleId = 0;  // 0 = Default/keine
    bool fromDefault = false;
    std::wstring adapterAlias;      // Anzeigename
};

struct ProcInfo {
    unsigned long pid = 0;
    unsigned long ppid = 0;
    std::wstring image;             // "chrome.exe"
    std::wstring path;              // "" wenn kein Zugriff
    std::wstring user;
    bool pathAccess = false;
    bool is32Bit = false;
};

struct TcpConn {
    unsigned long pid = 0;
    std::wstring localAddr;
    std::wstring remoteAddr;
    unsigned long remotePort = 0;
    unsigned long state = 0;
    bool isIPv6 = false;
};

struct ProcRow {
    unsigned long pid = 0;
    unsigned long ppid = 0;
    std::wstring image;
    std::wstring path;
    std::wstring user;
    int conns = 0;
    std::wstring viaAdapter;        // Adapter, über den aktuell Verkehr läuft
    RouteMode mode = RouteMode::None;
    std::wstring modeGuid;
    unsigned long long ruleId = 0;
    bool fromDefault = false;
    bool injected = false;
    bool pathAccess = false;
    bool is32Bit = false;
    bool hasNetwork = false;
};

enum class SortField : int { Name, Pid, Via, Mode, Conns, User };

// ---------------------------------------------------------------- Reine Hilfsfunktionen

std::wstring ToLowerW(std::wstring s);
std::wstring TrimW(const std::wstring& s);
std::wstring LastErrText(unsigned long err);
std::wstring ModeLabel(RouteMode m);
std::wstring KindLabel(AdapterKind k);
bool WildcardMatch(const std::wstring& pattern, const std::wstring& text); // case-insensitiv, * und ?
bool ContainsKeyword(const std::wstring& text, const std::vector<std::wstring>& keywords);
std::wstring FormatRate(long long bytesPerSec);
std::wstring FormatBytes(unsigned long long bytes);
std::wstring FormatGuidShort(const std::wstring& guid);
bool ParseGuid(const std::wstring& text, std::wstring& normalized); // prüft + normalisiert {} und Groß/Klein
std::wstring NowIsoLocal();

// Klassifizierungs- und Filterlogik (rein, damit mit echten Adapterdaten testbar)
// ifType: MIB_IF_ROW2.Type, physMedium: NDIS_PHYSICAL_MEDIUM, accessType: MIB_IF_ACCESS_TYPE
AdapterKind ClassifyMedium(unsigned long ifType, long physMedium, int connType,
                           const std::wstring& alias, const std::wstring& desc);
bool LooksVirtual(const std::wstring& desc, const std::wstring& alias,
                  long physMedium, unsigned long accessType);
bool IsLinkLocalIpv4(const std::wstring& ip); // 169.254.* -> kein echtes Netz

// ---------------------------------------------------------------- Regeln

struct MatchResult {
    bool matched = false;
    bool invalid = false;   // Muster unbrauchbar (leer / nur Wildcards)
};

MatchResult MatchRule(const Rule& rule, const std::wstring& image, const std::wstring& path);

class Ruleset {
public:
    int version = 1;
    std::vector<Rule> rules;                        // Index 0 = höchste Priorität
    RouteMode defaultMode = RouteMode::None;
    std::wstring defaultAdapterGuid;

    unsigned long long AddTop(Rule r);
    bool InsertTop(const Rule& r);   // Regel mit vorhandener id wieder einsetzen (für Rückgängig)
    bool Remove(unsigned long long id);
    bool Move(unsigned long long id, int newIndex);
    bool Update(unsigned long long id, const Rule& values);
    const Rule* Find(unsigned long long id) const;
    int IndexOf(unsigned long long id) const;
    void Resolve(const std::wstring& image, const std::wstring& path, Resolved& out) const;
    std::vector<std::wstring> Validate() const;   // Menschenlesbare Warnungen

    JsonPtr ToJson() const;
    static bool FromJson(const JsonPtr& root, Ruleset& out, std::vector<std::wstring>& warnings);
};

// ---------------------------------------------------------------- Pfade + Dateizugriff

struct Paths {
    std::wstring dir;
    std::wstring rulesFile;
    std::wstring resolvedFile;
    std::wstring hookDll;       // 64-Bit-Hook für 64-Bit-Programme
    std::wstring hookDll32;     // 32-Bit-Hook für 32-Bit-Programme
    std::wstring logFile;
};

Paths DefaultPaths();
Paths MakePaths(const std::wstring& dir);
bool EnsureDir(const std::wstring& dir, std::wstring& err);
bool ReadTextFile(const std::wstring& file, std::wstring& out, std::wstring& err);
bool WriteTextFileAtomic(const std::wstring& file, const std::wstring& content, std::wstring& err);
bool FileExists(const std::wstring& file);
bool GetFileMTime(const std::wstring& file, long long& out);

bool LoadRuleset(const Paths& p, Ruleset& out, std::vector<std::wstring>& warnings, std::wstring& err);
bool SaveRuleset(const Paths& p, const Ruleset& rs, std::wstring& err);

// ---------------------------------------------------------------- Aufgelöstes Regelwerk (Hook liest das)

struct ResolvedLookupResult {
    bool found = false;
    RouteMode mode = RouteMode::None;
    std::wstring guid;
};

JsonPtr BuildResolvedJson(const Ruleset& rs, const std::vector<AdapterInfo>& adapters,
                          long long generation, std::vector<std::wstring>& problems);
bool WriteResolved(const Paths& p, const Ruleset& rs, const std::vector<AdapterInfo>& adapters,
                   std::vector<std::wstring>& problems, std::wstring& err);
ResolvedLookupResult ResolvedLookup(const JsonPtr& doc, const std::wstring& imageLower,
                                    const std::wstring& pathLower = std::wstring());
bool ParseResolvedDoc(const std::wstring& text, JsonPtr& out);
// Wandert von einer Adapter-GUID zur Interface-Index (braucht der Hook für IP_UNICAST_IF).
bool ResolvedIfIndex(const JsonPtr& doc, const std::wstring& guid, unsigned long& ifIndex);

// ---------------------------------------------------------------- Systemzugriff

bool EnumerateAdapters(std::vector<AdapterInfo>& out, std::wstring& err);
void UpdateRates(std::vector<AdapterInfo>& adapters, const std::vector<AdapterInfo>& prev, long long dtMs);
unsigned long AdapterIndexForLocalIp(const std::vector<AdapterInfo>& adapters, const std::wstring& ip);
const AdapterInfo* FindAdapterByGuid(const std::vector<AdapterInfo>& adapters, const std::wstring& guid);
const AdapterInfo* FindAdapterByRole(const std::vector<AdapterInfo>& adapters, AdapterKind kind);

bool EnumerateProcesses(std::vector<ProcInfo>& out, std::wstring& err);
bool EnumerateTcpConnections(std::vector<TcpConn>& out, std::wstring& err);
bool IsWow64ProcessById(unsigned long pid, bool& is32, std::wstring& err);
bool IsProcessRunning(unsigned long pid, const std::wstring& expectImageLower);

std::vector<ProcRow> BuildRows(const std::vector<ProcInfo>& procs,
                               const std::vector<TcpConn>& conns,
                               const std::vector<AdapterInfo>& adapters,
                               const Ruleset& rs,
                               const std::vector<unsigned long>& injectedPids);
void SortRows(std::vector<ProcRow>& rows, SortField field, bool ascending);
void FilterRows(const std::vector<ProcRow>& rows, const std::wstring& query, bool onlyNetwork,
                std::vector<unsigned long>& pidOut);

// Ziel einer Drag-&-Drop-Aktion in einer bestimmten Tabelle
struct DropTarget { bool valid = false; RouteMode mode = RouteMode::None; };
DropTarget DropTargetForColumn(int column);

// ---------------------------------------------------------------- Injektion (assistentisch, prüfbar)

bool IsModuleLoadedInProcess(unsigned long pid, const std::wstring& moduleBaseLower,
                             bool& loaded, std::wstring& err);
bool InjectDll(unsigned long pid, const std::wstring& dllPath, std::wstring& err);
// Aktiviert die Hooks in einem bereits injizierten Prozess (jederzeit wiederholbar).
bool ActivateHookInProcess(unsigned long pid, const std::wstring& dllPath, std::wstring& err);
// Diagnose: Interface-Index, den die Hook-Bibliothek in diesem Prozess gerade setzt (0 = keiner).
bool QueryHookIfIndex(unsigned long pid, const std::wstring& dllPath, unsigned long& ifIndex, std::wstring& err);

// Fragt ab, welche Abfangstellen in einem Prozess wirksam sind (Bitmaske, 0 = nichts).
bool QueryHookPatchMask(unsigned long pid, const std::wstring& dllPath, unsigned long& mask, std::wstring& err);
// Injizieren falls nötig und aktivieren. warAlready = DLL war schon im Prozess.
bool InjectAndActivate(unsigned long pid, const std::wstring& dllPath, bool& warAlready, std::wstring& err);
// Prüft Vorbedingungen und liefert einen erklärbaren Grund, warum nicht.
bool CanInjectProcess(const ProcInfo& p, bool elevated, bool hookExists, std::wstring& why);

bool IsProcessElevatedSelf(bool& elevated, std::wstring& err);
bool RelaunchElevated(std::wstring& err);

// ---------------------------------------------------------------- Engine (dünn, zustandsbehaftet)

class Engine {
public:
    explicit Engine(Paths p);

    bool Init(std::wstring& err);

    Paths paths;
    Ruleset rules;
    std::vector<AdapterInfo> adapters;
    std::vector<ProcInfo> procs;
    std::vector<TcpConn> conns;
    std::vector<unsigned long> injected;
    std::vector<ProcRow> rows;
    long long generation = 0;
    long long lastRulesMTime = 0;
    unsigned long long lastRulesHash = 0;   // Inhalt von rules.json (zeitstempel ist zu grob)
    std::wstring lastError;
    std::vector<std::wstring> problems;
    bool elevated = false;

    void RefreshAdapters();
    void RefreshProcesses();
    void RebuildRows();
    bool Publish();                       // resolved.json schreiben
    bool SaveAndPublish(std::wstring& err);
    bool ReloadIfChanged(std::wstring& err); // externe Änderung der rules.json übernehmen
    bool Assign(const std::wstring& image, const std::wstring& path, RouteMode mode,
                const std::wstring& adapterGuid, std::wstring& err);
    bool ClearAssignment(unsigned long long ruleId, std::wstring& err);
    int ReinjectMatching(std::wstring& err);   // alle laufenden Prozesse mit Regel sofort versehen
    int ReinjectMatching(std::wstring& err, int& alreadyOut, int& failedOut);
    std::wstring HookDllFor(bool is32BitProgram) const;  // passende Hook-Bibliothek zum Prozess
    const AdapterInfo* RoleAdapter(AdapterKind kind) const;
    std::wstring ResolvedPath() const { return paths.resolvedFile; }
};

} // namespace sr
