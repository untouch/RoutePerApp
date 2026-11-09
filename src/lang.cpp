// RoutePerApp - Sprache, siehe lang.hpp.
#include "lang.hpp"
#include "json.hpp"
#include "resource.hpp"

#include <windows.h>

#include <deque>
#include <map>
#include <set>

namespace lang {
namespace {

std::wstring g_code = L"de";
std::wstring g_exeDir, g_dataDir;
std::map<std::wstring, std::wstring> g_map;             // Quelltext -> Uebersetzung
std::deque<std::wstring> g_pool;                        // Reden hält Adressen konstant
std::map<std::wstring, const wchar_t*> g_hold;
std::vector<std::wstring> g_used;                       // alle gebrauchten Schluessel
std::set<std::wstring> g_usedSeen;
std::vector<std::wstring> g_missing;                    // ohne Uebersetzung geblieben
std::set<std::wstring> g_missingSeen;

// Eingebaute Tabellen: eine Ressource je Sprache.
struct Table { const wchar_t* code; WORD res; };
const Table kTables[] = {
    { L"en", IDR_LANG_EN },
};

std::wstring FromUtf8(const char* data, size_t len) {
    if (!data || !len) return std::wstring();
    const int need = MultiByteToWideChar(CP_UTF8, 0, data, static_cast<int>(len), nullptr, 0);
    if (need <= 0) return std::wstring();
    std::wstring out(static_cast<size_t>(need), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, data, static_cast<int>(len), &out[0], need);
    return out;
}

std::string ToUtf8(const std::wstring& text) {
    if (text.empty()) return std::string();
    const int need = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                                         nullptr, 0, nullptr, nullptr);
    if (need <= 0) return std::string();
    std::string out(static_cast<size_t>(need), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                        &out[0], need, nullptr, nullptr);
    return out;
}

bool ReadAll(const std::wstring& path, std::wstring& text) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    std::string bytes;
    char chunk[16384];
    DWORD got = 0;
    for (;;) {
        if (!ReadFile(h, chunk, sizeof chunk, &got, nullptr) || !got) break;
        bytes.append(chunk, got);
    }
    CloseHandle(h);
    text = FromUtf8(bytes.data(), bytes.size());
    if (!text.empty() && text[0] == L'\xFEFF') text.erase(0, 1);
    return !text.empty();
}

bool WriteAll(const std::wstring& path, const std::wstring& text) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    const std::string bytes = ToUtf8(L"\xFEFF" + text);
    DWORD done = 0;
    const BOOL ok = WriteFile(h, bytes.data(), static_cast<DWORD>(bytes.size()), &done, nullptr);
    CloseHandle(h);
    return ok == TRUE;
}

const Table* Find(const wchar_t* code) {
    for (const Table& t : kTables) if (std::wcscmp(t.code, code) == 0) return &t;
    return nullptr;
}

void MergeJson(const std::wstring& text) {
    sr::JsonPtr root;
    sr::JsonParseResult r = sr::JsonParse(text, root);
    if (!r.ok || !root || !root->isObject()) return;
    for (const auto& kv : root->obj) {
        if (kv.second && kv.second->isString() && !kv.second->str.empty() &&
            !kv.first.empty() && kv.first[0] != L'_') {
            g_map[kv.first] = kv.second->str;
        }
    }
}

void LoadEmbedded(const wchar_t* code) {
    const Table* t = Find(code);
    if (!t) return;
    HMODULE mod = GetModuleHandleW(nullptr);
    HRSRC res = FindResourceW(mod, MAKEINTRESOURCEW(t->res), RT_RCDATA);
    if (!res) return;
    HGLOBAL loaded = LoadResource(mod, res);
    if (!loaded) return;
    const void* data = LockResource(loaded);
    const DWORD size = SizeofResource(mod, res);
    MergeJson(FromUtf8(static_cast<const char*>(data), size));
}

// Adresse fuer einen Text, der zur Laufzeit entstanden ist (aus der Datei oder einer
// Uebersetzung). Der Vorrat wird nie verkleinert, die Adresse bleibt also brauchbar.
const wchar_t* Keep(const std::wstring& text) {
    const auto it = g_hold.find(text);
    if (it != g_hold.end()) return it->second;
    g_pool.push_back(text);
    const wchar_t* where = g_pool.back().c_str();
    g_hold.emplace(g_pool.back(), where);
    return where;
}

}  // namespace

std::wstring FileName() { return L"language." + g_code + L".json"; }

void Init(const std::wstring& code, const std::wstring& exeDir, const std::wstring& dataDir,
          std::wstring* problem) {
    g_exeDir = exeDir;
    g_dataDir = dataDir;
    SetLanguage(code.empty() ? L"de" : code);
    std::wstring ignored;
    MergeFile(g_exeDir + L"\\" + FileName(), &ignored);
    MergeFile(g_dataDir + L"\\" + FileName(), problem);
}

void SetLanguage(const std::wstring& code) {
    const wchar_t* want = code.empty() ? L"de" : code.c_str();
    g_code = Find(want) ? want : L"de";
    g_map.clear();
    if (g_code != L"de") LoadEmbedded(g_code.c_str());
}

const std::wstring& Language() { return g_code; }

std::vector<std::wstring> Available() {
    std::vector<std::wstring> out{L"de"};
    for (const Table& t : kTables) out.push_back(t.code);
    return out;
}

const wchar_t* T(const wchar_t* source) {
    if (!source) return L"";
    const std::wstring key(source);
    if (g_usedSeen.insert(key).second) g_used.push_back(key);
    if (g_code == L"de") return source;
    const auto it = g_map.find(key);
    if (it != g_map.end()) return Keep(it->second);
    if (g_missingSeen.insert(key).second) g_missing.push_back(key);
    return source;
}

std::vector<std::wstring> MissingKeys() { return g_missing; }
size_t MissCount() { return g_missing.size(); }
void ForgetMissing() { g_missing.clear(); g_missingSeen.clear(); }

bool MergeFile(const std::wstring& path, std::wstring* problem) {
    std::wstring text;
    if (!ReadAll(path, text)) return false;
    sr::JsonPtr root;
    sr::JsonParseResult r = sr::JsonParse(text, root);
    if (!r.ok || !root || !root->isObject()) {
        if (problem) *problem = L"Sprachdatei " + path + L" ist nicht lesbar";
        return false;
    }
    MergeJson(text);
    return true;
}

bool WriteTemplate(const std::wstring& path, std::wstring* problem) {
    sr::JsonPtr doc = sr::Json::MakeObject();
    doc->set(L"_Hinweis", sr::Json::MakeString(
        L"Schluessel = Text im Programm, Wert = Uebersetzung. Leere Werte bedeuten: es "
        L"wird der deutsche Text benutzt. Diese Datei als language.en.json in das "
        L"Datenverzeichnis legen und RoutePerApp neu starten."));
    doc->set(L"_Sprache", sr::Json::MakeString(g_code));
    doc->set(L"_Anzahl", sr::Json::MakeNumber(static_cast<double>(g_used.size())));
    for (const std::wstring& key : g_used) {
        const auto it = g_map.find(key);
        doc->set(key, sr::Json::MakeString(it == g_map.end() ? std::wstring() : it->second));
    }
    if (!WriteAll(path, sr::JsonSerialize(doc, true) + L"\r\n")) {
        if (problem) *problem = L"Vorlage konnte nicht geschrieben werden: " + path;
        return false;
    }
    return true;
}

}  // namespace lang
