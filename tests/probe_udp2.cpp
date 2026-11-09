// Vergleich: WSAConnect vs. connect, Bindung vor/nach dem Verbindungsaufbau.
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <cstdio>

static const unsigned short kPort = 9;

static void Set(SOCKET s, unsigned long idx) {
    unsigned long raw = ntohl(idx);
    int r = setsockopt(s, IPPROTO_IP, IP_UNICAST_IF, reinterpret_cast<const char*>(&raw), sizeof(raw));
    if (r) printf("      setsockopt err=%d\n", WSAGetLastError());
}

static void Report(const char* tag, SOCKET s) {
    sockaddr_in loc;
    ZeroMemory(&loc, sizeof(loc));
    int ll = sizeof(loc);
    int gl = getsockname(s, reinterpret_cast<sockaddr*>(&loc), &ll);
    char txt[64] = "?";
    if (gl == 0) inet_ntop(AF_INET, &loc.sin_addr, txt, sizeof(txt));
    unsigned long back = 0;
    int bl = sizeof(back);
    int gb = getsockopt(s, IPPROTO_IP, IP_UNICAST_IF, reinterpret_cast<char*>(&back), &bl);
    char peer[64] = "?";
    sockaddr_in pr;
    ZeroMemory(&pr, sizeof(pr));
    int pl = sizeof(pr);
    if (getpeername(s, reinterpret_cast<sockaddr*>(&pr), &pl) == 0) inet_ntop(AF_INET, &pr.sin_addr, peer, sizeof(peer));
    printf("  %-42s quelle=%-15s partner=%-15s opt=%lu (get rc=%d)\n", tag, txt, peer, (gb || gl) ? 0UL : back, gb);
}

int main() {
    WSADATA w;
    WSAStartup(MAKEWORD(2, 2), &w);

    const char* peerIp = "192.168.178.1";     // absichtlich im anderen Netz (Ethernet)
    const unsigned long idx = 19;             // erzwingen: WLAN

    sockaddr_in d;
    ZeroMemory(&d, sizeof(d));
    d.sin_family = AF_INET;
    d.sin_port = htons(kPort);
    inet_pton(AF_INET, peerIp, &d.sin_addr);

    SOCKET a = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    Set(a, idx);
    printf("  a) WSAConnect nach Bindung:\n");
    int ra = WSAConnect(a, reinterpret_cast<const sockaddr*>(&d), sizeof(d), nullptr, nullptr, nullptr, nullptr);
    printf("      WSAConnect rc=%d err=%d\n", ra, ra ? WSAGetLastError() : 0);
    Report("  nach WSAConnect", a);
    send(a, "x", 1, 0);
    Report("  nach send", a);
    closesocket(a);

    SOCKET b = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    printf("  b) Bindung erst nach WSAConnect:\n");
    int rb = WSAConnect(b, reinterpret_cast<const sockaddr*>(&d), sizeof(d), nullptr, nullptr, nullptr, nullptr);
    printf("      WSAConnect rc=%d err=%d\n", rb, rb ? WSAGetLastError() : 0);
    Report("  nach WSAConnect", b);
    Set(b, idx);
    send(b, "x", 1, 0);
    Report("  nach Bindung + send", b);
    closesocket(b);

    SOCKET c = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    Set(c, idx);
    printf("  c) connect (kein WSAConnect) nach Bindung:\n");
    int rc = connect(c, reinterpret_cast<const sockaddr*>(&d), sizeof(d));
    printf("      connect rc=%d err=%d\n", rc, rc ? WSAGetLastError() : 0);
    Report("  nach connect", c);
    send(c, "x", 1, 0);
    Report("  nach send", c);
    closesocket(c);

    SOCKET e = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    Set(e, idx);
    printf("  e) WSAIoctl- bzw. sendto-Weg ohne Verbindung:\n");
    int rs = sendto(e, "x", 1, 0, reinterpret_cast<const sockaddr*>(&d), sizeof(d));
    printf("      sendto rc=%d err=%d\n", rs, (rs == SOCKET_ERROR) ? WSAGetLastError() : 0);
    Report("  nach sendto", e);
    closesocket(e);

    WSACleanup();
    return 0;
}
