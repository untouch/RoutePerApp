// Nachweis: erzwingt einen Adapter quer zum Zielnetz und prüft die Quelladresse.
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <cstdio>

static void TcpCase(const char* tag, const char* peer, unsigned short port, unsigned long ifIdx) {
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) { printf("  %-34s socket fehlt\n", tag); return; }
    if (ifIdx) {
        unsigned long raw = ntohl(ifIdx);
        int r = setsockopt(s, IPPROTO_IP, IP_UNICAST_IF, reinterpret_cast<const char*>(&raw), sizeof(raw));
        if (r) printf("  %-34s setsockopt err=%d\n", tag, WSAGetLastError());
    }
    sockaddr_in d;
    ZeroMemory(&d, sizeof(d));
    d.sin_family = AF_INET;
    d.sin_port = htons(port);
    inet_pton(AF_INET, peer, &d.sin_addr);

    u_long nonblock = 1;
    ioctlsocket(s, FIONBIO, &nonblock);
    int rc = connect(s, reinterpret_cast<const sockaddr*>(&d), sizeof(d));
    int err = rc ? WSAGetLastError() : 0;
    fd_set wf;
    FD_ZERO(&wf);
    FD_SET(s, &wf);
    timeval tv = { 2, 0 };
    int sel = select(0, nullptr, &wf, nullptr, &tv);
    unsigned long soerr = 0;
    int len = sizeof(soerr);
    getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&soerr), &len);
    if (rc == 0) soerr = 0;

    sockaddr_in loc;
    ZeroMemory(&loc, sizeof(loc));
    int ll = sizeof(loc);
    int gl = getsockname(s, reinterpret_cast<sockaddr*>(&loc), &ll);
    char txt[64] = "?";
    if (gl == 0) inet_ntop(AF_INET, &loc.sin_addr, txt, sizeof(txt));

    unsigned long back = 0;
    int bl = sizeof(back);
    int gb = getsockopt(s, IPPROTO_IP, IP_UNICAST_IF, reinterpret_cast<char*>(&back), &bl);

    printf("  %-34s idx=%-3lu peer=%s rc=%d wsa=%d sel=%d soerr=%-6d quelle=%s opt=%lu(get rc=%d)\n",
           tag, ifIdx, peer, rc, err, sel, soerr, txt, (gb || gl) ? 0UL : back, gb);
    closesocket(s);
}

static void UdpCase(const char* tag, const char* peer, unsigned short port, unsigned long ifIdx) {
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (ifIdx) {
        unsigned long raw = ntohl(ifIdx);
        setsockopt(s, IPPROTO_IP, IP_UNICAST_IF, reinterpret_cast<const char*>(&raw), sizeof(raw));
    }
    sockaddr_in d;
    ZeroMemory(&d, sizeof(d));
    d.sin_family = AF_INET;
    d.sin_port = htons(port);
    inet_pton(AF_INET, peer, &d.sin_addr);
    int rc = connect(s, reinterpret_cast<const sockaddr*>(&d), sizeof(d));
    sockaddr_in loc;
    ZeroMemory(&loc, sizeof(loc));
    int ll = sizeof(loc);
    int gl = getsockname(s, reinterpret_cast<sockaddr*>(&loc), &ll);
    char txt[64] = "?";
    if (gl == 0) inet_ntop(AF_INET, &loc.sin_addr, txt, sizeof(txt));
    printf("  %-34s idx=%-3lu peer=%s rc=%d wsa=%d quelle=%s\n",
           tag, ifIdx, peer, rc, rc ? WSAGetLastError() : 0, txt);
    closesocket(s);
}

static void V6Case(const char* tag, unsigned long idx, bool networkOrder) {
    SOCKET s = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
    unsigned long raw = networkOrder ? ntohl(idx) : idx;
    int r = setsockopt(s, IPPROTO_IPV6, IPV6_UNICAST_IF, reinterpret_cast<const char*>(&raw), sizeof(raw));
    printf("  %-34s idx=%-3lu netz=%d rc=%d err=%d\n", tag, idx, networkOrder ? 1 : 0, r, r ? WSAGetLastError() : 0);
    closesocket(s);
}

int main() {
    WSADATA w;
    WSAStartup(MAKEWORD(2, 2), &w);

    printf("TCP quer (Ziel im anderen Netz):\n");
    TcpCase("ohne Option -> Standard", "192.168.1.1", 80, 0);
    TcpCase("erzwungen WLAN(19)", "192.168.1.1", 80, 19);
    TcpCase("erzwungen LAN(10)", "192.168.1.1", 80, 10);
    TcpCase("ohne Option -> Standard", "192.168.178.1", 80, 0);
    TcpCase("erzwungen WLAN(19)", "192.168.178.1", 80, 19);
    TcpCase("erzwungen LAN(10)", "192.168.178.1", 80, 10);
    TcpCase("erzwungen LAN(10)", "93.184.215.69", 80, 10);
    TcpCase("erzwungen WLAN(19)", "93.184.215.69", 80, 19);

    printf("UDP-Quelladresse:\n");
    UdpCase("ohne Option", "192.168.178.1", 9, 0);
    UdpCase("erzwungen WLAN(19)", "192.168.178.1", 9, 19);
    UdpCase("erzwungen LAN(10)", "192.168.1.1", 9, 10);

    printf("IPv6-Option:\n");
    V6Case("v6 Netzwerkordnung", 19, true);
    V6Case("v6 Hostordnung", 19, false);
    V6Case("v6 Netzwerkordnung idx=10", 10, true);

    WSACleanup();
    return 0;
}
