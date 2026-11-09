#include "gui_logic.hpp"

#include <algorithm>

using namespace sr;

namespace srgui {

// ---------------------------------------------------------------- Zonen und Spalten

Zone ZoneForColumn(int column) {
    const DropTarget t = DropTargetForColumn(column);
    if (!t.valid) return Zone::None;
    switch (t.mode) {
    case RouteMode::Wlan: return Zone::Wlan;
    case RouteMode::Lan:  return Zone::Lan;
    default:              return Zone::Standard;
    }
}

int ColumnForZone(Zone zone) {
    switch (zone) {
    case Zone::Wlan:     return COL_WLAN;
    case Zone::Lan:      return COL_LAN;
    case Zone::Standard: return COL_DEFAULT;
    default:             return -1;
    }
}

RouteMode ZoneMode(Zone zone) {
    switch (zone) {
    case Zone::Wlan:     return RouteMode::Wlan;
    case Zone::Lan:      return RouteMode::Lan;
    case Zone::Standard: return RouteMode::None;
    default:             return RouteMode::None;
    }
}

Zone ZoneForMode(RouteMode mode) {
    switch (mode) {
    case RouteMode::Wlan: return Zone::Wlan;
    case RouteMode::Lan:  return Zone::Lan;
    default:              return Zone::None;
    }
}

const wchar_t* ZoneName(Zone zone) {
    switch (zone) {
    case Zone::Wlan:     return L"WLAN";
    case Zone::Lan:      return L"LAN";
    case Zone::Standard: return L"Standard";
    default:             return L"";
    }
}

std::wstring ColumnHeader(Zone zone, const AdapterInfo* adapter) {
    std::wstring head = ZoneName(zone);
    if (zone == Zone::Standard) return head + L" · Windows";
    if (!adapter) return head + L" · nicht vorhanden";
    if (!adapter->isUp) return head + L" · " + adapter->alias + L" (getrennt)";
    return head + L" · " + adapter->alias;
}

// ---------------------------------------------------------------- Tabellenzellen

std::wstring ZoneCell(const ProcRow& row, Zone zone) {
    switch (zone) {
    case Zone::Wlan:
        return row.mode == RouteMode::Wlan ? L"\x25CF WLAN" : L"";
    case Zone::Lan:
        return row.mode == RouteMode::Lan ? L"\x25CF LAN" : L"";
    case Zone::Standard:
        // Nur eine ausdr\xFCckliche Regel z\xE4hlt als "Standard", nicht das Fehlen einer Regel.
        return (row.ruleId != 0 && row.mode == RouteMode::None) ? L"\x25CF Standard" : L"";
    default:
        return L"";
    }
}

std::wstring NameCell(const ProcRow& row) {
    std::wstring text = row.image;
    if (row.conns > 0) {
        text += L"  \xB7 ";
        text += std::to_wstring(row.conns);
    }
    return text;
}

std::wstring ViaCell(const ProcRow& row) {
    if (!row.viaAdapter.empty()) return L"\xFC" L"ber " + row.viaAdapter;
    if (row.conns > 0) return L"Adapter unklar";
    return L"kein Verkehr";
}

// ---------------------------------------------------------------- Adapterkarten

CardText BuildCard(const AdapterInfo* adapter, Zone zone) {
    CardText c;
    if (!adapter) {
        c.title = ZoneName(zone);
        c.kind = L"kein Adapter erkannt";
        c.lines.push_back(L"Bitte Adapter einschalten und neu laden.");
        c.offline = true;
        return c;
    }
    c.title = adapter->alias.empty() ? adapter->description : adapter->alias;
    c.offline = !adapter->isUp;

    std::wstring kind = KindLabel(adapter->kind);
    if (adapter->kind == AdapterKind::Wlan) kind += L" \x2013 5G-Mobilrouter";
    else if (adapter->kind == AdapterKind::Lan) kind += L" \x2013 Fritzbox / Ethernet";
    if (adapter->isVirtual) kind += L" (virtuell)";
    c.kind = kind;

    std::wstring net = adapter->ipv4.empty() ? L"keine IPv4-Adresse" : adapter->ipv4;
    if (!adapter->gateway.empty()) net += L"   Gateway " + adapter->gateway;
    if (adapter->ifMetric > 0) net += L"   Metrik " + std::to_wstring(adapter->ifMetric);
    c.lines.push_back(net);

    std::wstring counts = std::to_wstring(adapter->assignedCount) + L" zugeordnet";
    counts += L" \x2013 " + std::to_wstring(adapter->activeCount) + L" mit Verkehr";
    c.lines.push_back(counts);

    std::wstring rate = L"\x2193 " + FormatRate(adapter->rxRate) + L"   \x2191 " + FormatRate(adapter->txRate);
    c.lines.push_back(rate);
    return c;
}

// ---------------------------------------------------------------- Status und Hinweise

CardText BuildStandardCard(size_t explicitCount, size_t unassignedCount) {
    CardText c;
    c.title = L"Windows-Standard";
    c.kind = L"ohne Bindung \x2013 Windows entscheidet";
    c.lines.push_back(std::to_wstring(explicitCount) + L" bewusst ohne Bindung");
    c.lines.push_back(std::to_wstring(unassignedCount) + L" ohne eigene Regel");
    c.lines.push_back(L"hierher ziehen \x2013 Zuordnung aufheben");
    c.offline = false;
    return c;
}

std::wstring StatusLine(const std::wstring& note, size_t visibleRows, size_t networkRows,
                        bool elevated, const std::wstring& engineError) {
    std::wstring s = note;
    if (s.empty()) {
        s = std::to_wstring(visibleRows) + L" Prozesse sichtbar, " +
            std::to_wstring(networkRows) + L" davon mit Netzwerkverbindung";
    }
    if (!elevated) s += L"  \x2013  ohne Administratorrechte: Zuordnung m\xFC" L"glich, Bindung erst nach Neustart als Administrator";
    if (!engineError.empty()) s += L"  \x2013  " + engineError;
    return s;
}

std::wstring DragHint(size_t draggedCount, Zone hover) {
    std::wstring what = draggedCount == 1 ? L"1 Programm" : std::to_wstring(draggedCount) + L" Programme";
    if (hover == Zone::None) return what + L" losgelassen \x2013 auf WLAN, LAN oder Standard fallen lassen";
    return what + L" auf " + ZoneName(hover) + L" fallen lassen";
}

// ---------------------------------------------------------------- Dateien und Regeln

bool DroppedFileAllowed(const std::wstring& path, std::wstring& why) {
    why.clear();
    if (path.empty()) { why = L"Keine Datei \xFC" L"bergeben."; return false; }
    const std::wstring lower = ToLowerW(path);
    if (lower.size() > 4 && lower.substr(lower.size() - 4) == L".lnk") {
        why = L"Verkn\xFCpfungen bitte als ZIEL-Datei ablegen (die .exe selbst ziehen).";
        return false;
    }
    if (lower.size() < 4 || lower.substr(lower.size() - 4) != L".exe") {
        why = L"Nur vollst\xE4ndige Programmdateien (.exe) k\xF6nnen zugeordnet werden.";
        return false;
    }
    if (!FileExists(path)) { why = L"Datei existiert nicht (mehr): " + path; return false; }
    return true;
}

std::wstring PatternFromPath(const std::wstring& path) {
    const size_t pos = path.find_last_of(L"\\/");
    return ToLowerW(pos == std::wstring::npos ? path : path.substr(pos + 1));
}

int FindEquivalentRule(const Ruleset& rs, const std::wstring& pattern, RouteMode mode,
                       const std::wstring& adapterGuid) {
    const std::wstring want = ToLowerW(pattern);
    const std::wstring guid = ToLowerW(adapterGuid);
    for (size_t i = 0; i < rs.rules.size(); ++i) {
        const Rule& r = rs.rules[i];
        if (!r.enabled) continue;
        if (ToLowerW(r.pattern) != want) continue;
        if (r.mode != mode) continue;
        if (ToLowerW(r.adapterGuid) != guid) continue;
        return static_cast<int>(i);
    }
    return -1;
}

// ---------------------------------------------------------------- Rücknahme

void UndoStack::Push(const UndoStep& step) {
    items_.push_back(step);
    if (items_.size() > kMaxDepth) items_.erase(items_.begin());
}

bool UndoStack::Pop(UndoStep& out) {
    if (items_.empty()) return false;
    out = items_.back();
    items_.pop_back();
    return true;
}

const std::wstring& UndoStack::TopLabel() const {
    static const std::wstring empty;
    if (items_.empty()) return empty;
    return items_.back().label;
}

// ---------------------------------------------------------------- Injektion und Zeilenzustand

std::wstring InjectionSummary(int applied, int already, int failed, const std::wstring& firstError) {
    std::wstring s = std::to_wstring(applied) + L" sofort gebunden";
    if (already) s += L", " + std::to_wstring(already) + L" waren schon gebunden";
    if (failed) {
        s += L", " + std::to_wstring(failed) + L" fehlgeschlagen";
        if (!firstError.empty()) s += L" (" + firstError + L")";
    }
    return s;
}

RowState StateOfRow(const ProcRow& row) {
    if (!row.pathAccess) return RowState::Blocked;
    if (row.mode == RouteMode::Wlan || row.mode == RouteMode::Lan) return RowState::Forced;
    if (row.ruleId != 0) return RowState::Default;
    return RowState::Neutral;
}

std::wstring RowStateText(const ProcRow& row) {
    switch (StateOfRow(row)) {
    case RowState::Blocked: return L"Pfad nicht lesbar";
    case RowState::Forced:  return L"fest an Adapter gebunden";
    case RowState::Default: return L"ausdr\xFC" L"cklich Windows-Standard";
    default:                return L"keine Zuordnung";
    }
}

bool QueryMatches(const std::wstring& query, const std::wstring& image, const std::wstring& user) {
    if (query.empty()) return true;
    const std::wstring q = ToLowerW(query);
    if (ToLowerW(image).find(q) != std::wstring::npos) return true;
    if (!user.empty() && ToLowerW(user).find(q) != std::wstring::npos) return true;
    for (wchar_t ch : q) { if (ch < L'0' || ch > L'9') return false; }
    return true;    // reine Ziffern: Suchfeld ist auch f\xFCr PID gedacht
}

} // namespace srgui
