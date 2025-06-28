/*
Enzo Tonon Morente
João Pedro Alves Notari Godoy
Letícia Barbosa Neves
*/
// Descrição: Cliente UDP que implementa o protocolo SLOW (Parte 1 do trabalho)

#include <iostream>
#include <iomanip>
#include <cstring>
#include <string>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <cstdint>

using namespace std;

// Constantes do protocolo
static const int HDR_SIZE = 32; 
static const int DATA_MAX = 1440;
static const uint32_t FLAG_C = 1 << 4;
static const uint32_t FLAG_R = 1 << 3;
static const uint32_t FLAG_ACK = 1 << 2;
static const uint32_t FLAG_AR = 1 << 1;
static const uint32_t FLAG_MB = 1 << 0;

// SID (Session ID)
struct SID {
    uint8_t b[16];
    static SID nil() {
        SID s{}; memset(s.b, 0, 16);
        return s;
    }
    bool isEqual(const SID& o) const {
        return memcmp(b, o.b, 16) == 0;
    }
};

// Header do protocolo
struct Header {
    SID sid;
    uint32_t sf, seq, ack;
    uint16_t wnd;
    uint8_t fid, fo;
    Header(): sid(SID::nil()), sf(0), seq(0), ack(0), wnd(0), fid(0), fo(0) {}
};

void pack32(uint32_t v, uint8_t* p) {
    for (int i = 0; i < 4; ++i) p[i] = v >> (i * 8);
}
void pack16(uint16_t v, uint8_t* p) {
    for (int i = 0; i < 2; ++i) p[i] = v >> (i * 8);
}
uint32_t unpack32(const uint8_t* p) {
    uint32_t v = 0;
    for (int i = 0; i < 4; ++i) v |= (uint32_t)p[i] << (i * 8);
    return v;
}
uint16_t unpack16(const uint8_t* p) {
    uint16_t v = 0;
    for (int i = 0; i < 2; ++i) v |= (uint16_t)p[i] << (i * 8);
    return v;
}

void serialize(const Header& h, uint8_t* buf) {
    memcpy(buf, h.sid.b, 16);
    pack32(h.sf, buf + 16);
    pack32(h.seq, buf + 20);
    pack32(h.ack, buf + 24);
    pack16(h.wnd, buf + 28);
    buf[30] = h.fid;
    buf[31] = h.fo;
}

void deserialize(Header& h, const uint8_t* buf) {
    memcpy(h.sid.b, buf, 16);
    h.sf  = unpack32(buf + 16);
    h.seq = unpack32(buf + 20);
    h.ack = unpack32(buf + 24);
    h.wnd = unpack16(buf + 28);
    h.fid = buf[30];
    h.fo  = buf[31];
}

void printHeader(const Header& h, const string& label) {
    cout << "==== " << label << " ====\nSID: ";
    for (int i = 0; i < 16; i++) cout << hex << setw(2) << setfill('0') << (int)h.sid.b[i];
    cout << dec << "\nFlags: " << (h.sf & 0x1F)
         << "\nSEQ: " << h.seq << "\nACK: " << h.ack
         << "\nWND: " << h.wnd << "\nFID: " << (int)h.fid
         << "\nFO: " << (int)h.fo << "\n\n";
}

class UDPPeripheral {
    int fd;
    sockaddr_in srv;
    Header lastHdr, prevHdr;
    bool active = false, hasPrev = false;
    uint32_t nextSeq = 0, lastCentralSeq = 0;
    uint32_t window_size = 5 * DATA_MAX, bytesInFlight = 0;

    uint16_t advertisedWindow() const {
        return (window_size > bytesInFlight)
            ? static_cast<uint16_t>(min<uint32_t>(window_size - bytesInFlight, UINT16_MAX))
            : 0;
    }

public:
    UDPPeripheral(): fd(-1) {}
    ~UDPPeripheral() { if (fd >= 0) close(fd); }

    bool init(const char* host, int port) {
        if ((fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) return false;
        hostent* he = gethostbyname(host);
        if (!he) return false;
        memset(&srv, 0, sizeof(srv));
        srv.sin_family = AF_INET;
        memcpy(&srv.sin_addr, he->h_addr, he->h_length);
        srv.sin_port = htons(port);
        timeval tv{5,0};
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        return true;
    }

    bool connect() {
        Header h; h.seq = nextSeq++; h.wnd = advertisedWindow(); h.sf |= FLAG_C;
        uint8_t buf[HDR_SIZE]; serialize(h, buf);
        sendto(fd, buf, HDR_SIZE, 0, (sockaddr*)&srv, sizeof(srv));
        printHeader(h, "CONNECT");

        uint8_t rbuf[HDR_SIZE + DATA_MAX];
        sockaddr_in sa; socklen_t sl = sizeof(sa);
        if (recvfrom(fd, rbuf, sizeof(rbuf), 0, (sockaddr*)&sa, &sl) < HDR_SIZE) return false;

        Header r; deserialize(r, rbuf);
        printHeader(r, "SETUP");

        if (!(r.sf & FLAG_AR)) return false;
        prevHdr = r;
        active = hasPrev = true;
        lastCentralSeq = r.seq;
        nextSeq = r.seq + 1;
        return true;
    }

    bool sendData(const string& msg) {
        if (!active) return false;

        auto sendFrag = [&](const char* data, size_t len, uint8_t fid, uint8_t fo, bool more) {
            if (bytesInFlight + len > window_size) return false;
            Header h = prevHdr;
            h.seq = nextSeq++; h.ack = lastCentralSeq;
            h.wnd = advertisedWindow();
            h.sf = (h.sf & ~0x1F) | FLAG_ACK | (more ? FLAG_MB : 0);
            h.fid = fid; h.fo = fo;

            uint8_t buf[HDR_SIZE + DATA_MAX];
            serialize(h, buf);
            memcpy(buf + HDR_SIZE, data, len);
            printHeader(h, "DATA");
            bytesInFlight += len;
            return sendto(fd, buf, HDR_SIZE + len, 0, (sockaddr*)&srv, sizeof(srv)) >= 0;
        };

        if (msg.size() > DATA_MAX) {
            uint8_t fid = nextSeq & 0xFF;
            for (size_t off = 0, fo = 0; off < msg.size();) {
                size_t len = min(msg.size() - off, (size_t)DATA_MAX);
                bool more = off + len < msg.size();
                if (!sendFrag(msg.data() + off, len, fid, fo++, more)) return false;
                off += len;
            }
        } else {
            if (!sendFrag(msg.data(), msg.size(), 0, 0, false)) return false;
        }

        uint8_t rbuf[HDR_SIZE];
        sockaddr_in sa; socklen_t sl = sizeof(sa);
        if (recvfrom(fd, rbuf, HDR_SIZE, 0, (sockaddr*)&sa, &sl) < HDR_SIZE) return false;

        Header r; deserialize(r, rbuf);
        printHeader(r, "ACK DATA");
        if (!(r.sf & FLAG_ACK)) return false;

        bytesInFlight = 0;
        lastCentralSeq = r.seq;
        prevHdr = r;
        nextSeq = r.seq + 1;
        return true;
    }

    bool disconnect() {
        if (!active) return false;
        Header h = prevHdr;
        h.seq = nextSeq++; h.ack = lastCentralSeq;
        h.wnd = 0;
        h.sf = (h.sf & ~0x1F) | FLAG_C | FLAG_R | FLAG_ACK;

        uint8_t buf[HDR_SIZE];
        serialize(h, buf);
        sendto(fd, buf, HDR_SIZE, 0, (sockaddr*)&srv, sizeof(srv));
        printHeader(h, "DISCONNECT");

        for (int i = 0; i < 3; i++) {
            uint8_t rbuf[HDR_SIZE];
            sockaddr_in sa; socklen_t sl = sizeof(sa);
            if (recvfrom(fd, rbuf, HDR_SIZE, 0, (sockaddr*)&sa, &sl) >= HDR_SIZE) {
                Header r; deserialize(r, rbuf);
                printHeader(r, "ACK DISCONNECT");
                if (r.sf & FLAG_ACK) { active = false; return true; }
            }
        }
        return false;
    }

    bool zeroWay(const string& msg) {
        if (!hasPrev) return false;

        Header h = lastHdr;
        h.seq = nextSeq++; h.ack = lastCentralSeq;
        h.wnd = window_size;
        h.sf = (h.sf & ~0x1F) | FLAG_R | FLAG_ACK;

        uint8_t buf[HDR_SIZE + DATA_MAX];
        serialize(h, buf);
        memcpy(buf + HDR_SIZE, msg.data(), msg.size());
        sendto(fd, buf, HDR_SIZE + msg.size(), 0, (sockaddr*)&srv, sizeof(srv));
        printHeader(h, "REVIVE");

        uint8_t rbuf[HDR_SIZE + DATA_MAX];
        sockaddr_in sa; socklen_t sl = sizeof(sa);
        if (recvfrom(fd, rbuf, sizeof(rbuf), 0, (sockaddr*)&sa, &sl) < HDR_SIZE) return false;

        Header r; deserialize(r, rbuf);
        printHeader(r, "REVIVE ACK");

        if (!(r.sf & FLAG_AR)) return false;
        prevHdr = r;
        active = true;
        lastCentralSeq = r.seq;
        nextSeq = r.seq + 1;
        return true;
    }

    void storeSession() { if (active) { lastHdr = prevHdr; hasPrev = true; } }
    bool canRevive() const { return hasPrev; }
    bool isActive() const { return active; }
};

int main() {
    UDPPeripheral client;
    if (!client.init("slow.gmelodie.com", 7033)) {
        cerr << "Erro ao inicializar socket.\n";
        return 1;
    }

    if (!client.connect()) {
        cerr << "Falha na conexão inicial.\n";
        return 1;
    }

    cout << "[OK] Conectado ao servidor.\n";

    string cmd;
    while (true) {
        cout << "\n> Comando (data/disconnect/revive/exit): ";
        cin >> cmd;
        if (cmd == "data") {
            cin.ignore();
            string msg;
            cout << "Digite a mensagem: ";
            getline(cin, msg);
            if (!client.sendData(msg)) cerr << "Erro ao enviar dados.\n";
        } else if (cmd == "disconnect") {
            client.storeSession();
            if (client.disconnect()) cout << "Desconectado com sucesso.\n";
        } else if (cmd == "revive") {
            if (!client.canRevive()) {
                cout << "Nenhuma sessão armazenada.\n";
                continue;
            }
            cin.ignore();
            string msg;
            cout << "Mensagem para enviar no revive: ";
            getline(cin, msg);
            if (client.zeroWay(msg)) cout << "Sessão revivida.\n";
            else cout << "Revive falhou.\n";
        } else if (cmd == "exit") {
            if (client.isActive()) client.disconnect();
            break;
        } else {
            cout << "Comando desconhecido.\n";
        }
    }

    return 0;
}
