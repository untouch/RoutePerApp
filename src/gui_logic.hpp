// RoutePerApp - Logikschicht der Oberfläche.
// Bewusst ohne Fenster, ohne COM und ohne globalen Zustand: jede Funktion ist
// mit Unit-Tests prüfbar, und gui.cpp enthält dadurch nur noch Darstellung.
#pragma once

#ifndef UNICODE
#define UNICODE
#endif

#include <string>
#include <vector>

#include "core.hpp"
#include "lang.hpp"

namespace srgui {

// Die drei Zielbereiche, in die ein Prozess gezogen werden kann.
enum class Zone : int { None = 0, Wlan = 1, Lan = 2, Standard = 3 };

// Spalten der Prozesstabelle (feste Reihenfolge, auch in app.rc-artigen Texten).
enum Column {
    COL_NAME = 0,
    COL_PID = 1,
    COL_VIA = 2,
    COL_WLAN = 3,
    COL_LAN = 4,
    COL_DEFAULT = 5,
    COL_USER = 6,
    COL_COUNT = 7
};

Zone ZoneForColumn(int column);                 // nur 3/4/5 sind Zielbereiche
int ColumnForZone(Zone zone);                   // -1 für Zone::None
sr::RouteMode ZoneMode(Zone zone);
Zone ZoneForMode(sr::RouteMode mode);
const wchar_t* ZoneName(Zone zone);             // "WLAN", "LAN", "Standard"

// Spaltenkopf, z. B. "WLAN · WLAN 2" oder "LAN · nicht verbunden".
std::wstring ColumnHeader(Zone zone, const sr::AdapterInfo* adapter);

// Zelle in einer Zielbereichsspalte: leer, wenn der Prozess dort nicht läuft.
std::wstring ZoneCell(const sr::ProcRow& row, Zone zone);

// Hauptzelle mit Bildnamen und Verbindungszahl.
std::wstring NameCell(const sr::ProcRow& row);

// "über WLAN 2" / "kein Verkehr" / "Windows-Standard"
std::wstring ViaCell(const sr::ProcRow& row);

// Zeile für eine Adapterkarte: Titel plus bis zu vier Infozeilen.
struct CardText {
    std::wstring title;             // "WLAN 2"
    std::wstring kind;              // "WLAN · 5G-Mobilrouter"
    std::vector<std::wstring> lines;
    bool offline = false;
};
CardText BuildCard(const sr::AdapterInfo* adapter, Zone zone);

// Karte für den Bereich "Windows-Standard" (keine Bindung, Windows entscheidet).
CardText BuildStandardCard(size_t explicitCount, size_t unassignedCount);

// Einzeilige Zustandsmeldung unten in der Oberfläche.
std::wstring StatusLine(const std::wstring& note,
                        size_t visibleRows,
                        size_t networkRows,
                        bool elevated,
                        const std::wstring& engineError);

// Hinweis für den Moment, in dem etwas gezogen wird.
std::wstring DragHint(size_t draggedCount, Zone hover);

// Prüft eine aus dem Explorer abgelegte Datei und erklärt Ablehnungen.
bool DroppedFileAllowed(const std::wstring& path, std::wstring& why);
// Bildname als Regel-Muster aus einem Dateipfad ("chrome.exe").
std::wstring PatternFromPath(const std::wstring& path);

// Regel mit gleicher Muster/Ziel-Kombination finden (-1 = keine).
int FindEquivalentRule(const sr::Ruleset& rs, const std::wstring& pattern,
                       sr::RouteMode mode, const std::wstring& adapterGuid);

// Rücknahmekette für Zuordnungen (Strg+Z).
struct UndoStep {
    unsigned long long ruleId = 0;      // 0 = Regel wurde neu angelegt
    sr::Rule previous;                  // alte Regel (bei Neu: die neue Regel)
    bool wasNew = false;
    std::wstring label;                 // menschenlesbar für die Statuszeile
};

class UndoStack {
public:
    void Push(const UndoStep& step);
    bool Empty() const { return items_.empty(); }
    size_t Size() const { return items_.size(); }
    bool Pop(UndoStep& out);                       // letzte Ebene holen
    const std::wstring& TopLabel() const;

private:
    std::vector<UndoStep> items_;
    static const size_t kMaxDepth = 40;
};

// Fasst das Ergebnis des Sofort-Injizierens zusammen.
std::wstring InjectionSummary(int applied, int already, int failed, const std::wstring& firstError);

// Farb-/Symbolzustand einer Prozesszeile für die Darstellung.
enum class RowState : int { Neutral = 0, Forced = 1, Blocked = 2, Default = 3 };
RowState StateOfRow(const sr::ProcRow& row);
std::wstring RowStateText(const sr::ProcRow& row);   // Kurztext für Tooltips

// Filterfeld: kleinste sinnvolle Länge und Grobabgleich (für Tests).
bool QueryMatches(const std::wstring& query, const std::wstring& image, const std::wstring& user);

} // namespace srgui
