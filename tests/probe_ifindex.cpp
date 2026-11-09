// Nachweis: IP_UNICAST_IF erwartet die Interface-Index in Netzwerk-Byte-Reihenfolge.
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <cstdio>

static void Try4(const char* tag, SOCKET s, unsigned long hostOrder) {
    unsigned long raw = hostOrder;
    int r = setsockopt(s, IPPROTO_IP, IP_UNICAST_IF, reinterpret_cast<const char*>(&raw), sizeof(raw));
    int e = r ? WSAGetLastError() : 0;
    unsigned long back = 0;
    int len = sizeof(back);
    int g = getsockopt(s, IPPROTO_IP, IP_UNICAST_IF, reinterpret_cast<char*>(&back), &len);
    printf("  %-30s set rc=%d err=%-5d get rc=%d wert=%lu (networkByte=%#lx)\n",
           tag, r, e, g, (g || !r) ? 0UL : back, htonl(raw));
}

int main() {
    WSADATA w;
    WSAStartup(MAKEWORD(2, 2), &w);

    SOCKET u = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    Try4("udp  idx=19  (Hostordnung)", u, 19);
    Try4("udp  ntohl(19) (Netzwerkordn)", u, ntohl(19));
    Try4("udp  ntohl(10)", u, ntohl(10));
    Try4("udp  ntohl(1)", u, ntohl(1));
    Try4("udp  ntohl(999)", u, ntohl(999));
    Try4("udp  0 (zurücksetzen)", u, 0);
    closesocket(u);

    SOCKET t = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    Try4("tcp  ntohl(19)", t, ntohl(19));
    closesocket(t);

    SOCKET v6 = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
    unsigned long raw = ntohl(19);
    int r = setsockopt(v6, IPPROTO_IPV6, IPV6_UNICAST_IF, reinterpret_cast<const char*>(&raw), sizeof(raw));
    unsigned long back = 0;
    int len = sizeof(back);
    int g = getsockopt(v6, IPPROTO_IPV6, IPV6_UNICAST_IF, reinterpret_cast<char*>(&back), &len);
    printf("  %-30s set rc=%d err=%-5d get rc=%d wert=%lu\n", "v6 ntohl(19)", r, r ? WSAGetLastError() : 0, g, back);
    closesocket(v6);

    WSACleanup();
    return 0;
}
