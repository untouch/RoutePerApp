// RoutePerApp - Sprache.
//
// Der deutsche Text im Quellcode ist der Schluessel, die Uebersetzung steckt in einer
// JSON-Datei (lang\en.json). Diese Datei wird beim Durchbau in die Datei eingebettet und
// beim Start geladen. Liegt im Datenverzeichnis oder neben der Datei eine gleichnamige
// Datei, ueberschreibt sie den eingebauten Text - so lassen sich Uebersetzungen
// nachtragen, ohne neu zu bauen.
//
// Fehlende Uebersetzungen lassen den deutschen Text stehen und werden gezaehlt; der
// Selbsttest prueft, dass kein sichtbarer Text ohne Uebersetzung dasteht.
#pragma once

#include <string>
#include <vector>

namespace lang {

// Eingebaute Uebersetzungen und optionale Dateien lesen. code: "de" oder "en".
// dataDir und exeDir werden nach language-Dateien durchsucht; problem bekommt den
// Grund, falls eine Datei gefunden, aber nicht gelesen werden konnte.
void Init(const std::wstring& code, const std::wstring& exeDir, const std::wstring& dataDir,
          std::wstring* problem);

// Sprache wechseln; laedt die passende Tabelle (eingebaut plus Datei).
void SetLanguage(const std::wstring& code);
const std::wstring& Language();

// Alle Sprachen, zwischen denen gewaehlt werden kann.
std::vector<std::wstring> Available();

// Uebersetzung fuer den Ausgangstext; ohne Treffer zurueck auf Deutsch. Der Text liegt
// in einem Vorrat, die Adresse bleibt also gueltig, auch nach einem Sprachwechsel.
const wchar_t* T(const wchar_t* source);

// Uebersetzten Text mit Werten fuellen. Die Vorlage enthaelt %d oder %s; die Sprache
// darf die Reihenfolge der Werte anpassen, weil jede Stelle ihre eigenen Angaben macht.
std::wstring Fill(const wchar_t* key);
std::wstring Fill(const wchar_t* key, const wchar_t* a);
std::wstring Fill(const wchar_t* key, const wchar_t* a, const wchar_t* b);
std::wstring Fill(const wchar_t* key, int n);
std::wstring Fill(const wchar_t* key, int n, const wchar_t* a);
std::wstring Fill(const wchar_t* key, const wchar_t* a, int n);
std::wstring Fill(const wchar_t* key, int n, int m);

// Texte, die in dieser Sitzung gebraucht, aber nicht uebersetzt wurden.
std::vector<std::wstring> MissingKeys();
size_t MissCount();
void ForgetMissing();

// Alle gebrauchten Texte ohne brauchbare Uebersetzung, unabhaengig davon, in
// welcher Sprache sie gebraucht wurden. Leere Werte zaehlen ebenfalls nicht.
std::vector<std::wstring> Untranslated();

// Schreiben aller gesammelten Texte als vorlage (Schluessel = Quelltext, Wert = "" oder
// bisheriger Wert), damit Uebersetzungen ausserhalb bearbeitet werden koennen.
bool WriteTemplate(const std::wstring& path, std::wstring* problem);

// JSON-Datei einlesen und als Uebersetzung verwenden. true, wenn sie gelesen wurde.
bool MergeFile(const std::wstring& path, std::wstring* problem);

// Name der Sprachdatei im Datenverzeichnis, zusaetzlich "language.<code>.json".
std::wstring FileName();

}  // namespace lang

#define Tr(x) ::lang::T(x)
