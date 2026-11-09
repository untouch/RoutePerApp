// Gemeinsame Test-Harness für die RoutePerApp-Testprogramme.
// Bewusst ohne Framework: jeder Test zählt Prüfungen und Fehler mit.
#pragma once

#include <string>
#include <cstdio>
#include <cstring>

static int g_checks = 0;
static int g_failed = 0;
static int g_skipped = 0;
static int g_sectionFails = 0;
static std::wstring g_section;

#define WIDEN2(x) L ## x
#define WIDEN(x) WIDEN2(x)
#define LR(x) WIDEN(#x)

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

static void Begin(const wchar_t* name) {
    g_section = name;
    g_sectionFails = 0;
    wprintf(L"\n== %s ==\n", name);
}

static void EndSection() {
    if (g_sectionFails == 0) wprintf(L"  ok\n");
    else wprintf(L"  %d Fehler\n", g_sectionFails);
}

// Für Prüfpunkte, die diese Umgebung nicht liefern kann (kein Adapter, keine Rechte).
static void Skip(const wchar_t* why) {
    ++g_skipped;
    wprintf(L"  übersprungen: %s\n", why);
}

static void Note(const wchar_t* text) {
    wprintf(L"  Hinweis: %s\n", text);
}

static void Note(const std::wstring& text) {
    Note(text.c_str());
}
