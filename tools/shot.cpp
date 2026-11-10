// Bildschirmbild des RoutePerApp-Fensters fuer die README.
//
// Eigener Weg statt eines Scripts: sucht das Fenster, malt es in einen Speicherbildschirm
// und schreibt eine PNG-Datei. Benutzt nur das eigene Fenster, es werden keine Nachrichten
// an fremde Programme gesendet.
//
//   build\shot.exe ausgabe.png [fenstertitel]
#include <windows.h>
#pragma warning(push, 0)     // die GDI+-Ueberschriften warnen selbst
#include <gdiplus.h>
#pragma warning(pop)
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#pragma comment(lib, "gdiplus.lib")

// Der PNG-Schreiber wird über seine Art gesucht, nicht über eine festgeschriebene Kennung.
static bool PngClsid(CLSID* out) {
    UINT anzahl = 0, groesse = 0;
    if (Gdiplus::GetImageEncodersSize(&anzahl, &groesse) != Gdiplus::Ok || groesse == 0) return false;
    std::vector<unsigned char> puffer(groesse);
    auto* infos = reinterpret_cast<Gdiplus::ImageCodecInfo*>(puffer.data());
    if (Gdiplus::GetImageEncoders(anzahl, groesse, infos) != Gdiplus::Ok) return false;
    for (UINT i = 0; i < anzahl; ++i) {
        if (lstrcmpW(infos[i].MimeType, L"image/png") == 0) { *out = infos[i].Clsid; return true; }
    }
    return false;
}

static bool SavePng(Gdiplus::Bitmap* bmp, const wchar_t* path) {
    CLSID kennung{};
    if (!PngClsid(&kennung)) { std::fputws(L"kein PNG-Schreiber gefunden", stderr); return false; }
    const Gdiplus::Status st = bmp->Save(path, &kennung, nullptr);
    if (st != Gdiplus::Ok) std::fwprintf(stderr, L"Speichern abgelehnt, Code %d\n", (int)st);
    return st == Gdiplus::Ok;
}

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) {
        std::fwprintf(stderr, L"benutzung: %s ausgabe.png\n", argv[0]);
        return 2;
    }

    HWND hwnd = FindWindowW(L"RoutePerApp.MainWindow", nullptr);
    if (!hwnd) { std::fputws(L"kein RoutePerApp-Fenster gefunden", stderr); return 1; }
    if (!IsWindowVisible(hwnd)) { std::fputws(L"Fenster ist versteckt", stderr); return 1; }

    RECT rc{};
    if (!GetWindowRect(hwnd, &rc) || rc.right <= rc.left || rc.bottom <= rc.top) {
        std::fputws(L"Fensterrechte nicht bekommen", stderr);
        return 1;
    }
    const int w = rc.right - rc.left, h = rc.bottom - rc.top;

    HDC screen = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(screen);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;          // von oben nach unten
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP dib = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!dib || !bits) { std::fputws(L"Speicherbildschirm fehlgeschlagen", stderr); return 1; }
    HBITMAP old = static_cast<HBITMAP>(SelectObject(mem, dib));

    // Erst direkt ins Fenster malen (funktioniert auch hinter anderen Fenstern), dann vom
    // Bildschirm holen, falls das Programm etwas zeichnet, das PrintWindow nicht mitnimmt.
    BOOL drawn = PrintWindow(hwnd, mem, 2 /* PW_RENDERFULLCONTENT */);
    if (!drawn)
        BitBlt(mem, 0, 0, w, h, screen, rc.left, rc.top, SRCCOPY);

    Gdiplus::GdiplusStartupInput in{};
    ULONG_PTR token = 0;
    if (Gdiplus::GdiplusStartup(&token, &in, nullptr) != Gdiplus::Ok) {
        std::fputws(L"GDI+ konnte nicht starten", stderr);
        return 1;
    }

    bool ok = false;
    {
        // Das Bild wird in ein eigenes GDI+-Bild kopiert. Ein direkt auf den Speicherbildschirm
        // gelegtes Bild wird vom PNG-Schreiber nicht immer angenommen.
        Gdiplus::Bitmap own(w, h, PixelFormat32bppARGB);
        Gdiplus::BitmapData bd{};
        const Gdiplus::Rect rechte(0, 0, w, h);
        if (own.LockBits(&rechte, Gdiplus::ImageLockModeWrite, PixelFormat32bppARGB, &bd) == Gdiplus::Ok) {
            for (int y = 0; y < h; ++y) {
                memcpy(static_cast<BYTE*>(bd.Scan0) + static_cast<size_t>(y) * bd.Stride,
                       static_cast<BYTE*>(bits) + static_cast<size_t>(y) * w * 4,
                       static_cast<size_t>(w) * 4);
            }
            own.UnlockBits(&bd);
        }
        ok = SavePng(&own, argv[1]);
    }
    Gdiplus::GdiplusShutdown(token);

    SelectObject(mem, old);
    DeleteObject(dib);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);

    if (!ok) { std::fputws(L"PNG konnte nicht geschrieben werden", stderr); return 1; }
    std::wprintf(L"Bild: %s  (%dx%d)\n", argv[1], w, h);
    return 0;
}
