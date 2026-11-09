// RoutePerApp Test-Zielprozess (srtarget.exe)
//
// Dient den Integrations-tests als "opfer": es baut Netzwerkverbindungen auf und
// schreibt nach jeder Runde in eine Ergebnisdatei, welche Netzwerkschnittstelle
// der Socket tatsächlich benutzt. So lässt sich prüfen, ob die Hook-Bibliothek
// IP_UNICAST_IF / IPV6_UNICAST_IF wirklich gesetzt hat.
//
// Aufruf:
//   srtarget.exe <ergebnisdatei> [runden] [wartedatei] [peer-ip[;peer-ip...]] [peer-port] [startverzögerung_ms]
//
// Mehrere Peer-Adressen (mit ';' getrennt) gelten rundenweise; die letzte Adresse
// wird für alle weiteren Runden wiederverwendet.
//
// Nach jeder Runde (ab Runde 2) wartet der Prozess, bis <wartedatei> existiert.
// Der Test kann dadurch mitten im Lauf die Zuordnung ändern und die Wirkung der
// nächsten Verbindung messen.

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <mstcpip.h>
#include <string>
#include <vector>

#include <string>

#ifndef IP_UNICAST_IF
#define IP_UNICAST_IF 21
#endif
#ifndef IPV6_UNICAST_IF
#define IPV6_UNICAST_IF 40
#endif

static std::string Ipv4Text(const in_addr& a) {
    char buf[64] = "";
    if (!inet_ntop(AF_INET, &a, buf, sizeof(buf))) return "?";
    return std::string(buf);
}

static void WriteWholeFile(const std::wstring& path, const std::string& text) {
    // FILE_SHARE_READ: der Test liest parallel mit.
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD done = 0;
    WriteFile(h, text.data(), static_cast<DWORD>(text.size()), &done, nullptr);
    CloseHandle(h);
}

static bool WaitForFile(const std::wstring& file, DWORD timeoutMs) {
    DWORD start = GetTickCount();
    while (GetTickCount() - start < timeoutMs) {
        DWORD a = GetFileAttributesW(file.c_str());
        if (a != INVALID_FILE_ATTRIBUTES) return true;
        Sleep(100);
    }
    return false;
}

struct Probe {
    int ifIndex = 0;            // von der Schnittstelle, die der Socket nutzt
    std::string local;          // gewählte Quelladresse
    std::string result = "-";   // Verbindungsausgang
};

static void ReadSocketInfo(SOCKET s, Probe& p) {
    unsigned long idx = 0;
    int len = static_cast<int>(sizeof(idx));
    if (getsockopt(s, IPPROTO_IP, IP_UNICAST_IF, reinterpret_cast<char*>(&idx), &len) == 0) {
        p.ifIndex = static_cast<int>(idx);
    }
    sockaddr_storage ss;
    ZeroMemory(&ss, sizeof(ss));
    len = static_cast<int>(sizeof(ss));
    if (getsockname(s, reinterpret_cast<sockaddr*>(&ss), &len) == 0 && ss.ss_family == AF_INET) {
        p.local = Ipv4Text(reinterpret_cast<const sockaddr_in*>(&ss)->sin_addr);
    }
}

// TCP: nicht blockierend verbinden, damit ein hängender SYN-Testlauf nicht alles aufhält.
static Probe ProbeTcp(const sockaddr_in& peer) {
    Probe p;
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) { p.result = "socket:" + std::to_string(WSAGetLastError()); return p; }
    u_long on = 1;
    ioctlsocket(s, FIONBIO, &on);
    int r = connect(s, reinterpret_cast<const sockaddr*>(&peer), static_cast<int>(sizeof(peer)));
    if (r == 0) {
        p.result = "verbunden";
    } else {
        int err = WSAGetLastError();
        if (err == WSAEWOULDBLOCK) {
            fd_set w;
            FD_ZERO(&w);
            FD_SET(s, &w);
            timeval tv;
            tv.tv_sec = 2;
            tv.tv_usec = 500000;
            int sel = select(0, nullptr, &w, nullptr, &tv);
            if (sel == 1) {
                int so = 0, l = sizeof(so);
                getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&so), &l);
                p.result = (so == 0) ? "verbunden" : "fehler:" + std::to_string(so);
            } else if (sel == 0) {
                p.result = "timeout";
            } else {
                p.result = "select:" + std::to_string(WSAGetLastError());
            }
        } else {
            p.result = "fehler:" + std::to_string(err);
        }
    }
    ReadSocketInfo(s, p);
    closesocket(s);
    return p;
}

// UDP: WSAConnect verschickt nichts, legt aber Quelladresse und Schnittstelle fest.
// Danach ein winziges Datagramm an den Discard-Port, um den send-Pfad zu beschäftigen.
static Probe ProbeUdp(const sockaddr_in& peer, u_short discardPort) {
    Probe p;
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) { p.result = "socket:" + std::to_string(WSAGetLastError()); return p; }
    sockaddr_in dest = peer;
    dest.sin_port = htons(peer.sin_port);
    int r = WSAConnect(s, reinterpret_cast<const sockaddr*>(&dest), static_cast<int>(sizeof(dest)),
                       nullptr, nullptr, nullptr, nullptr);
    if (r != 0) { p.result = "wsaconnect:" + std::to_string(WSAGetLastError()); }
    else {
        static const char payload[4] = { 's', 'r', 't', '.' };
        int sent = send(s, payload, 4, 0);
        p.result = (sent >= 0) ? "gesendet" : "send:" + std::to_string(WSAGetLastError());
    }
    sockaddr_in disc = dest;
    disc.sin_port = htons(discardPort);
    sendto(s, "x", 1, 0, reinterpret_cast<const sockaddr*>(&disc), static_cast<int>(sizeof(disc)));
    ReadSocketInfo(s, p);
    closesocket(s);
    return p;
}

static void ParsePeerList(const std::wstring& text, u_short port, std::vector<sockaddr_in>& out) {
    std::wstring token;
    for (size_t i = 0; i <= text.size(); ++i) {
        if (i == text.size() || text[i] == L';') {
            if (!token.empty()) {
                std::string ascii;
                for (wchar_t c : token) ascii.push_back(static_cast<char>(c));
                sockaddr_in p;
                ZeroMemory(&p, sizeof(p));
                p.sin_family = AF_INET;
                p.sin_port = htons(port);
                if (inet_pton(AF_INET, ascii.c_str(), &p.sin_addr) == 1) out.push_back(p);
                token.clear();
            }
        } else {
            token.push_back(text[i]);
        }
    }
}

static const sockaddr_in& PickPeer(const std::vector<sockaddr_in>& list, int round) {
    size_t pi = static_cast<size_t>(round - 1);
    if (pi >= list.size()) pi = list.size() - 1;
    return list[pi];
}

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) {
        const wchar_t* msg = L"Benutzung: srtarget.exe <ergebnisdatei> [runden] [wartedatei] [tcp-peer] [port] [verzögerung] [udp-peer]\r\n";
        DWORD g = 0;
        WriteConsoleW(GetStdHandle(STD_OUTPUT_HANDLE), msg, static_cast<DWORD>(lstrlenW(msg)), &g, nullptr);
        return 2;
    }
    const std::wstring outFile = argv[1];
    int rounds = (argc > 2) ? _wtoi(argv[2]) : 1;
    if (rounds < 1) rounds = 1;
    if (rounds > 8) rounds = 8;
    const std::wstring waitFile = (argc > 3) ? argv[3] : std::wstring();
    const std::wstring peerIp = (argc > 4) ? argv[4] : L"93.184.215.69";
    u_short port = static_cast<u_short>((argc > 5) ? _wtoi(argv[5]) : 80);
    DWORD delay = (argc > 6) ? static_cast<DWORD>(_wtoi(argv[6])) : 0;
    const std::wstring udpPeerIp = (argc > 7) ? argv[7] : std::wstring();

    std::string text = "srtarget v1\r\n";
    text += "pid=" + std::to_string(GetCurrentProcessId()) + "\r\n";
    WriteWholeFile(outFile, text);

    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        text += "fatal=wsastartup\r\ndone\r\n";
        WriteWholeFile(outFile, text);
        return 3;
    }

    std::vector<sockaddr_in> peers;
    ParsePeerList(peerIp, port, peers);
    if (peers.empty()) {
        text += "fatal=peer-ip\r\ndone\r\n";
        WriteWholeFile(outFile, text);
        WSACleanup();
        return 4;
    }
    std::vector<sockaddr_in> udpPeers;
    ParsePeerList(udpPeerIp.empty() ? peerIp : udpPeerIp, 9, udpPeers);
    if (udpPeers.empty()) udpPeers = peers;

    if (delay) Sleep(delay);

    for (int round = 1; round <= rounds; ++round) {
        if (round > 1 && !waitFile.empty()) {
            if (!WaitForFile(waitFile, 25000)) {
                text += "round=" + std::to_string(round) + " waited=timeout\r\n";
                WriteWholeFile(outFile, text);
            }
        }
        const sockaddr_in& peer = PickPeer(peers, round);
        const sockaddr_in& udpPeer = PickPeer(udpPeers, round);
        Probe tcp = ProbeTcp(peer);
        Probe udp = ProbeUdp(udpPeer, 9);
        text += "round=" + std::to_string(round) +
                " peer=" + Ipv4Text(peer.sin_addr) + ":" + std::to_string(port) +
                " udp_peer=" + Ipv4Text(udpPeer.sin_addr) + ":9" +
                " tcp_ifindex=" + std::to_string(tcp.ifIndex) +
                " tcp_local=" + tcp.local +
                " tcp=" + tcp.result +
                " udp_ifindex=" + std::to_string(udp.ifIndex) +
                " udp_local=" + udp.local +
                " udp=" + udp.result + "\r\n";
        WriteWholeFile(outFile, text);
    }

    text += "done\r\n";
    WriteWholeFile(outFile, text);
    Sleep(120);                       // Datei wirklich abschließen, bevor der Prozess endet
    WSACleanup();
    return 0;
}
