#include "../src/core.cpp"
#include <cstdio>
using namespace sr;

static void P(const wchar_t* pat, const wchar_t* txt) {
    bool r = WildcardMatch(pat, txt);
    wprintf(L"%-28s vs %-30s -> %d\n", pat, txt, (int)r);
}

int main() {
    P(L"chrome*", L"chromium.exe");
    P(L"chrome*", L"chrome.exe");
    P(L"chrome*", L"chrome");
    P(L"*", L"abc");
    P(L"a*", L"ab");
    P(L"*a", L"ba");
    P(L"a*b", L"acb");
    P(L"*e*", L"jetbrains");
    P(L"x*", L"xyz");
    wprintf(L"lower p=%s t=%s\n", ToLowerW(L"Chrome*").c_str(), ToLowerW(L"Chromium.exe").c_str());
    return 0;
}
