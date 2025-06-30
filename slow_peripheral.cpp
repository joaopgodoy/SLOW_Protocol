/*
 * slow_peripheral_commented.cpp
 * Autores: 
 *  Enzo Tonon Morente - 14568476 
 *  João Pedro Alves Notari Godoy - 14582076
 *  Letícia Barbosa Neves - 14582076
 * Descrição: Cliente UDP que implementa o protocolo SLOW (Parte 1 do trabalho)
 */

#include <iostream>
#include <iomanip>
#include <cstring>
#include <string>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <cstdint>
#include <vector> 
#include <algorithm>   // std::find_if
#include <optional>    // std::optional, std::nullopt
#include <chrono>      // std::chrono para controle de tempo
  
using namespace std;

// Tamanho do cabeçalho em bytes e tamanho máximo de dados por pacote
static const int HDR_SIZE = 32;
static const int DATA_MAX = 1440;

// Constantes de timeout e retransmissão
static const int TIMEOUT_MS = 2000;     // timeout de 2 segundo
static const int MAX_RETRIES = 3;       // máximo de retentativas

// Flags do protocolo SLOW (bit flags em h.sf)
static const uint32_t FLAG_C   = 1 << 4;  // Connect / Disconnect
static const uint32_t FLAG_R   = 1 << 3;  // Revive
static const uint32_t FLAG_ACK = 1 << 2;  // Acknowledgment
static const uint32_t FLAG_AR  = 1 << 1;  // Ack de Revive / Setup
static const uint32_t FLAG_MB  = 1 << 0;  // More Bit (fragmentação)

function<bool()> esperaAck;

// SID (Session ID): identificador único de sessão, 16 bytes
struct SID {
    uint8_t b[16];
    // Retorna SID "nulo" (todos zeros)
    static SID nil() {
        SID s{};
        memset(s.b, 0, 16);
        return s;
    }
    // Compara igualdade de 16 bytes
    bool isEqual(const SID& o) const {
        return memcmp(b, o.b, 16) == 0;
    }
};

// Estrutura de cabeçalho de controle do protocolo SLOWt flags em h.sf)
struct Header {
    SID     sid;    // Session ID
    uint32_t sf;    // Flags e campo de controle
    uint32_t seq;   // Número de sequência (enviado)
    uint32_t ack;   // Número do ack
    uint16_t wnd;   // Tamanho da janela
    uint8_t  fid;   // Fragment ID
    uint8_t  fo;    // Fragment Offset

    // Construtor inicializa campos padrão
    Header(): sid(SID::nil()), sf(0), seq(0), ack(0), wnd(0), fid(0), fo(0) {}
};

// Converte inteiro de 32 bits para 4 bytes
void pack32(uint32_t v, uint8_t* p) {
    for (int i = 0; i < 4; ++i)
        p[i] = (v >> (i * 8)) & 0xFF;
}

// Converte inteiro de 16 bits para 2 bytes
void pack16(uint16_t v, uint8_t* p) {
    for (int i = 0; i < 2; ++i)
        p[i] = (v >> (i * 8)) & 0xFF;
}

// Reconstrói inteiro de 32 bits a partir de 4 bytes
uint32_t unpack32(const uint8_t* p) {
    uint32_t v = 0;
    for (int i = 0; i < 4; ++i)
        v |= (uint32_t)p[i] << (i * 8);
    return v;
}

// Reconstrói inteiro de 16 bits a partir de 2 bytes
uint16_t unpack16(const uint8_t* p) {
    uint16_t v = 0;
    for (int i = 0; i < 2; ++i)
        v |= (uint16_t)p[i] << (i * 8);
    return v;
}

// escreve todos os campos no buffer de 32 bytes
void serialize(const Header& h, uint8_t* buf) {
    memcpy(buf,          h.sid.b,   16);     // SID
    pack32(h.sf,         buf + 16);           // Flags
    pack32(h.seq,        buf + 20);           // Sequence number
    pack32(h.ack,        buf + 24);           // Acknowledgment number
    pack16(h.wnd,        buf + 28);           // Window size
    buf[30] = h.fid;                         // Fragment ID
    buf[31] = h.fo;                          // Fragment offset
}

// reconstrói um Header a partir do buffer recebido.
void deserialize(Header& h, const uint8_t* buf) {
    memcpy(h.sid.b,      buf,           16);
    h.sf  = unpack32(buf + 16);
    h.seq = unpack32(buf + 20);
    h.ack = unpack32(buf + 24);
    h.wnd = unpack16(buf + 28);
    h.fid = buf[30];
    h.fo  = buf[31];
}

// Imprime Header
void printHeader(const Header& h, const string& label) {
    cout << "---- " << label << " ----\n";
    cout << "SID: ";
    for (int i = 0; i < 16; i++)
        cout << hex << setw(2) << setfill('0') << (int)h.sid.b[i];
    cout << dec << "\n";
    uint32_t flags =  h.sf & 0x1F;
    uint32_t sttl  = (h.sf >> 5) & 0x07FFFFFF;
    cout << "Flags: 0x" << hex << flags << dec << " ("<<flags<<")\n";
    cout << "STTL: "    << sttl  << "\n";
    cout << "SEQNUM: "  << h.seq  << "\n";
    cout << "ACKNUM: "  << h.ack  << "\n";
    cout << "WINDOW: "  << h.wnd  << "\n";
    cout << "FID: "     << (int)h.fid << "\n";
    cout << "FO: "      << (int)h.fo  << "\n\n";
}

//pacote que já foi enviado mas ainda não recebeu confirmação (ACK) 
struct PacoteEmTransmissao {
    uint8_t  buffer[HDR_SIZE + DATA_MAX]; /// buffer do pacote
    size_t   length;                      /// tamanho do pacote
    uint32_t seq;                         /// número da squencia do pacote
    size_t   dataSize;                    /// tamanho dos dados, mas sem o cabeçalho
    std::chrono::steady_clock::time_point tempoEnvio; /// timestamp do envio
    int tentativas;                       /// número de tentativas de envio
    
    PacoteEmTransmissao(const uint8_t* buf, size_t len, uint32_t sequence, size_t dSize) 
        : length(len), seq(sequence), dataSize(dSize), 
          tempoEnvio(std::chrono::steady_clock::now()), tentativas(1) {
        memcpy(buffer, buf, len);
    }
    
    // Atualiza timestamp para nova tentativa
    void atualizarTempo() {
        tempoEnvio = std::chrono::steady_clock::now();
        tentativas++;
    }
    
    // Verifica se passou do timeout (em milissegundos)
    bool tempoExpirado(int timeoutMs) const {
        auto agora = std::chrono::steady_clock::now();
        auto duracao = std::chrono::duration_cast<std::chrono::milliseconds>(agora - tempoEnvio);
        return duracao.count() >= timeoutMs;
    }
};

// Encapsula o socket e a lógica do protocolo SLOW.
class UDPPeripheral {
    int fd;                       // descritor do socket UDP
    sockaddr_in srv;              // Endereço do servidor
    Header lastHdr, prevHdr;      // último header recebido/enviado
    bool active = false;          // sessão ativa
    bool hasPrev = false;         // sessão armazenada para revive
    uint32_t nextSeq = 0;         // Próximo número de sequência a usar
    uint32_t lastCentralSeq = 0;  //  último seq recebido do servidor
    uint32_t window_size = 5 * DATA_MAX; // tamanho máximo da janela (5 × 1440 bytes)
    uint32_t bytesInFlight = 0;   // quantos bytes estão aguardando ACK
    vector<PacoteEmTransmissao> pacotesEmTransito; //fila de pacotes em transmissão

    // Calcula janela anunciada (até 16 bits)
    uint16_t advertisedWindow() const {
        if (bytesInFlight >= window_size)
            return 0; //janela está ocupada
        return static_cast<uint16_t>(min<uint32_t>(window_size - bytesInFlight, UINT16_MAX));
    }

    // Verifica e reenvia pacotes com timeout expirado
    void verificarTimeouts() {
        //para cada um dos pacotes em trânsito
        for (auto it = pacotesEmTransito.begin(); it != pacotesEmTransito.end(); ) {
            if (it->tempoExpirado(TIMEOUT_MS)) { //se expirou o tempo
                if (it->tentativas >= MAX_RETRIES) {
                    cout << "Pacote seq=" << it->seq << " descartado após " 
                         << MAX_RETRIES << " tentativas." << endl;
                    // Remove pacote que excedeu tentativas
                    bytesInFlight -= it->dataSize;
                    it = pacotesEmTransito.erase(it);
                } else {
                    cout << "Reenviando pacote seq=" << it->seq 
                         << " (tentativa " << (it->tentativas + 1) << ")" << endl;
                    
                    // Reenvia o pacote
                    if (sendto(fd, it->buffer, it->length, 0, 
                              (sockaddr*)&srv, sizeof(srv)) >= 0) {
                        it->atualizarTempo();
                        
                        // Imprime header do pacote reenviado
                        Header h;
                        deserialize(h, it->buffer);
                        printHeader(h, "REENVIADO - DATA");
                        
                        cout << "PAYLOAD (" << it->dataSize << " bytes): \""
                             << string((char*)(it->buffer + HDR_SIZE), 
                                     it->dataSize < 50 ? it->dataSize : 50) 
                             << (it->dataSize > 50 ? "..." : "") << "\"\n\n";
                    } else {
                        cerr << "Erro ao reenviar pacote seq=" << it->seq << endl;
                    }
                    ++it;
                }
            } else {
                ++it;
            }
        }
    }

    //recebeu o ack do pacote - remove ele da fila de pacotes em transmissão e retorna o seu tamanho
    uint32_t removerPacote(uint32_t ack){
        // Localiza o primeiro pacote cujo seq bate com ack
        auto it = find_if(pacotesEmTransito.begin(), pacotesEmTransito.end(),
                            [ack](const PacoteEmTransmissao& p) {
                                return p.seq == ack;
                            });

        if (it == pacotesEmTransito.end()) //nao encontrado
            return 0;            

        uint32_t tamanho = it->dataSize;
        bytesInFlight -= tamanho; // Atualiza bytes em voo
        pacotesEmTransito.erase(it);// remove da fila

        return tamanho; 
    }

    // Remove todos os pacotes com seq <= ack (ACK cumulativo)
    void removerPacotesAteAck(uint32_t ack) {        
        // Remove todos os pacotes com seq <= ack
        auto it = pacotesEmTransito.begin();
        while (it != pacotesEmTransito.end()) {
            if (it->seq <= ack) {
                bytesInFlight -= it->dataSize;
                it = pacotesEmTransito.erase(it);
            } else {
                ++it;
            }
        }
    }

    // Função auxiliar para enviar pacote e adicionar à fila
    bool enviarPacoteComTimeout(const uint8_t* buf, size_t len, uint32_t seq, size_t dataSize) {
        if (sendto(fd, buf, len, 0, (sockaddr*)&srv, sizeof(srv)) >= 0) {
            pacotesEmTransito.emplace_back(buf, len, seq, dataSize);
            bytesInFlight += dataSize;
            
            cout << "PAYLOAD (" << dataSize << " bytes): \""
                 << string((char*)(buf + HDR_SIZE), dataSize < 50 ? dataSize : 50) 
                 << (dataSize > 50 ? "..." : "") << "\"\n\n";
            
            return true;
        }
        return false;
    }

public:
    UDPPeripheral(): fd(-1) {}
    ~UDPPeripheral() { if (fd >= 0) close(fd); }

    // Inicializa o socket UDP e configura timeout de recv
    bool init(const char* host, int port) {
        fd = socket(AF_INET, SOCK_DGRAM, 0); // cria o socket UDP
        if (fd < 0) return false; 

        hostent* he = gethostbyname(host); //resolve hostname
        if (!he) return false;

        memset(&srv, 0, sizeof(srv));
        srv.sin_family = AF_INET;
        memcpy(&srv.sin_addr, he->h_addr, he->h_length);
        srv.sin_port = htons(port);
        
        timeval tv{5,0}; // ajusta timeout de recepção para 5 segundos

        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        return true;
    }

    // realiza o handshake inicial com o servidor (3-way handshake)
    bool connect() {
        if (active) return true; //se já estiver conectado não fazer nada

        // PASSO 1: Envia CONNECT
        Header h;
        h.seq = nextSeq++;
        h.wnd = advertisedWindow(); //janela atual
        h.sf |= FLAG_C; // flag connect

        uint8_t buf[HDR_SIZE];
        serialize(h, buf);
        printHeader(h, "Enviado - CONNECT (1/3)");
        if (sendto(fd, buf, HDR_SIZE, 0, (sockaddr*)&srv, sizeof(srv)) < HDR_SIZE)
            return false; 

        // PASSO 2: Aguarda SETUP do servidor
        uint8_t rbuf[HDR_SIZE + DATA_MAX];
        sockaddr_in sa; socklen_t sl = sizeof(sa);
        if (recvfrom(fd, rbuf, sizeof(rbuf), 0, (sockaddr*)&sa, &sl) < HDR_SIZE)
            return false;

        Header r;
        deserialize(r, rbuf);
        printHeader(r, "Recebido - SETUP (2/3)");

        if (r.ack != h.seq || !(r.sf & FLAG_AR)) return false; // verifica se ACK confirma nosso CONNECT
        
        // PASSO 3: Envia ACK final para completar 3-way handshake
        Header ack_final;
        ack_final.seq = nextSeq++;
        ack_final.ack = r.seq; // confirma o SETUP do servidor
        ack_final.wnd = advertisedWindow();
        ack_final.sf = FLAG_ACK; // apenas flag ACK

        uint8_t ack_buf[HDR_SIZE];
        serialize(ack_final, ack_buf);
        printHeader(ack_final, "Enviado - ACK (3/3)");
        if (sendto(fd, ack_buf, HDR_SIZE, 0, (sockaddr*)&srv, sizeof(srv)) < HDR_SIZE)
            return false;

        //ajusta estado interno
        prevHdr = r;
        active = hasPrev = true; //sessão ativa e com histório para revive
        lastCentralSeq = r.seq;
        nextSeq = r.seq + 1;
        window_size = r.wnd; //tamanho da janela do servidor

        return true; //3-way handshake bem sucedido
    }
    
    
// Envia mensagem, fragmentando se necessário e aguardando ACK
bool sendData(const string& msg) {
    if (!active) return false; //verifica se a sessão está ativa

    // Verifica timeouts antes de enviar novos dados
    verificarTimeouts();

    std::function<bool(const char*, size_t, uint8_t, uint8_t, bool)> sendFrag;

    // Função lambda para enviar cada fragmento
    sendFrag = [&](const char* data, size_t len, uint8_t fid, uint8_t fo, bool more) {
        //verifica se essa quantidade de bytes pode ser mandada baseada na janela atual
        if (bytesInFlight + len > window_size) {
            cout << "Janela cheia, esperando ACK..." << endl;
            esperaAck(); // espera ACK de fragmento anterior

            return sendFrag(data, len, fid, fo, more); // tenta enviar novamente
        }
        //monta o header baseado no último header recebido
        Header h = prevHdr;
        h.seq = nextSeq++; //número da sequência
        h.ack = lastCentralSeq;
        h.wnd = advertisedWindow(); //espaço livre na janela

        // Flags: sempre ACK; MB se ainda houver mais fragmentos
        h.sf = (h.sf & ~0x1F) | FLAG_ACK | (more ? FLAG_MB : 0);

        h.fid = fid; //qual mensagem o fragmento faz parte
        h.fo = fo; //indice do fragmento

        //realiza o envio do fragmento
        uint8_t buf[HDR_SIZE + DATA_MAX];
        serialize(h, buf);
        memcpy(buf + HDR_SIZE, data, len);
        printHeader(h, "Enviado - DATA");

        return enviarPacoteComTimeout(buf, HDR_SIZE + len, h.seq, len);
    };
    
    // Função lambda para esperar ACK de fragmento
    esperaAck = [&]() -> bool {
        auto tempoInicio = std::chrono::steady_clock::now();
        const int TIMEOUT_ESPERA_MS = 5000; // 5 segundos para esperar ACK
        
        while (true) {
            // Verifica timeouts periodicamente
            verificarTimeouts();
            
            uint8_t rbuf[HDR_SIZE];
            sockaddr_in sa; socklen_t sl = sizeof(sa);
            
            // Configura timeout curto para permitir verificação de timeouts
            timeval tv{0, 100000}; // 100ms
            setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
            
            int result = recvfrom(fd, rbuf, HDR_SIZE, 0, (sockaddr*)&sa, &sl);
            
            // Restaura timeout original
            timeval tv_orig{5, 0};
            setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv_orig, sizeof(tv_orig));
            
            if (result >= HDR_SIZE) {
                Header r; 
                deserialize(r, rbuf);
                printHeader(r, "RECEBIDO - ACK (DATA)");
                
                if (r.sf & FLAG_ACK) {
                    // Remove todos os pacotes confirmados até r.ack (ACK cumulativo)
                    removerPacotesAteAck(r.ack);
                    
                    lastCentralSeq = r.seq;
                    prevHdr = r;
                    nextSeq = lastCentralSeq + 1;
                    window_size = r.wnd;
                    
                    return true;
                }
            }
            
            // Verifica se passou do tempo limite total
            auto agora = std::chrono::steady_clock::now();
            auto duracao = std::chrono::duration_cast<std::chrono::milliseconds>(agora - tempoInicio);
            if (duracao.count() >= TIMEOUT_ESPERA_MS) {
                cout << "Timeout esperando ACK" << endl;
                return false;
            }
        }
    };

    // Fragmenta se a mensagem exceder DATA_MAX
    if (msg.size() > DATA_MAX) {
        uint8_t fid = nextSeq & 0xFF; //Indetificador único para todos os fragmentos
        uint8_t fo = 0; // Offset do fragmento (começa em 0)
        size_t off = 0; // Offset da mensagem (começa em 0)

        // até mandar todos os bytes
        while(off < msg.size()) {
            // tamanho do fragmento será o mínimo entre o tamanho máximo ou o tamanho restante da mensagem.
            size_t QuantidadeAMandar = min(msg.size() - off, static_cast<size_t>(DATA_MAX));

            //verifica se o tamanho do fragmento cabe na janela
            size_t DisponivelParaMandar = min(QuantidadeAMandar, (size_t)(window_size - bytesInFlight)); 

            if (DisponivelParaMandar == 0) {
                // Se não houver espaço, espera ACK de fragmento anterior
                if (!esperaAck()) return false; //se não receber ACK, retorna falso

                continue; //volta para o início do loop para tentar enviar novamente
            }

            bool more;
            if (off + DisponivelParaMandar < msg.size()){
                more = true; //se ainda houver mais fragmentos, seta a flag
            } else {
                more = false; //se não houver mais fragmentos, seta a flag como falsa
            }

            if (!sendFrag(msg.data() + off, DisponivelParaMandar, fid, fo, more)) return false; //envia a mensagem

            fo += 1; //novo fragmento
            off += DisponivelParaMandar; //incrementa o off

            // Se ainda há dados para enviar e a janela está cheia, espera ACK
            if (more && (bytesInFlight + min((size_t)DATA_MAX, (size_t)(msg.size() - off)) > window_size)) {
                if (!esperaAck()) return false;
            }
        }

        //ter certeza que temos o ACK finals    
        return esperaAck();

    } else {
        // mensagemn pequena é enviada em um único fragmento

        if (!sendFrag(msg.data(), msg.size(), 0, 0, false)) return false; //enviar a mensagem

        return esperaAck(); 

    }
}

    // Encerra a sessão (DISCONNECT)
    bool disconnect() {
        if (!active) return false; //se não estiver ativo da erro

        Header h = prevHdr;
        h.seq = nextSeq++;
        h.ack = lastCentralSeq;
        h.wnd = 0; // zera a janela
        h.sf = (h.sf & ~0x1F) | FLAG_C | FLAG_R | FLAG_ACK; // Flags CONNECT, REVIVE e ACK juntas sinalizam encerramento

        uint8_t buf[HDR_SIZE];
        serialize(h, buf);
        printHeader(h, "Enviado - DISCONNECT");
        if (sendto(fd, buf, HDR_SIZE, 0, (sockaddr*)&srv, sizeof(srv)) < HDR_SIZE) //envia o DISCONNECT
            return false;

        // Aguarda até 3 ACKs de desconexão
        for (int i = 0; i < 3; i++) {
            uint8_t rbuf[HDR_SIZE];
            sockaddr_in sa; socklen_t sl = sizeof(sa);

            if (recvfrom(fd, rbuf, HDR_SIZE, 0, (sockaddr*)&sa, &sl) >= HDR_SIZE) { //se recebeu um pacote
                Header r;
                deserialize(r, rbuf);
                printHeader(r, "Recebido - ACK(DISCONNECT)");

                if (r.sf & FLAG_ACK) { //verifica qual a flag do ACK
                    active = false; //desativa a sessão

                    return true;
                }
            }
        }
        return false; //ACK não foi recebido até 3 tentativas
    }

    // Revive (zero-way handshake) após desconexão: envia R+ACK + dados
    bool zeroWay(const string& msg) {
        if (!hasPrev) return false; //verifica se existe alguma sessão prévia

        // monta o cabeçalho do revive
        Header h = lastHdr;
        h.seq = nextSeq++;
        h.ack = lastCentralSeq;
        h.wnd = window_size; //tamanho da janela será a janela inteira, pois não tem bytes em "voo"
        h.sf = (h.sf & ~0x1F) | FLAG_R | FLAG_ACK;

        //manda o revive
        uint8_t buf[HDR_SIZE + DATA_MAX];
        serialize(h, buf);
        memcpy(buf + HDR_SIZE, msg.data(), msg.size());
        sendto(fd, buf, HDR_SIZE + msg.size(), 0, (sockaddr*)&srv, sizeof(srv));
        printHeader(h, "Enviado - REVIVE");

        //espera REIVE ACK do servidor
        uint8_t rbuf[HDR_SIZE + DATA_MAX];
        sockaddr_in sa; socklen_t sl = sizeof(sa);
        if (recvfrom(fd, rbuf, sizeof(rbuf), 0, (sockaddr*)&sa, &sl) < HDR_SIZE)
            return false;

        Header r;
        deserialize(r, rbuf);
        printHeader(r, "Recebido - ACK(REVIVE)");
        if (!(r.sf & FLAG_AR)) { //verifica se a flag é a correta
            cerr << "Revive falhou: ACK não recebido ou flag incorreta." << endl;
            return false; 
        }
        // Restaura estado após revive
        prevHdr = r;
        active = true;
        lastCentralSeq = r.seq;
        nextSeq = r.seq + 1;

        return true;
    }

    // Armazena último header para possível revive futuro
    void storeSession() {
        if (active) {
            lastHdr = prevHdr;
            hasPrev = true;
        }
    }

    bool canRevive() const { return hasPrev; }
    bool isActive() const { return active; }
};

int main() {
    UDPPeripheral client;

    // Inicializa socket e configura servidor
    if (!client.init("slow.gmelodie.com", 7033)) {
        cerr << "Erro ao inicializar socket." << endl;
        return 1;
    }

    // Handshake de conexão
    if (!client.connect()) {
        cerr << "Falha na conexao inicial." << endl;
        return 1;
    }

    cout << "Conectado ao servidor." << endl;

    // Loop de interação com o usuário para comandos
    string cmd;
    while (true) {
        cout << "\n> Comando (data/disconnect/revive/exit): ";
        cin >> cmd; 

        if (cmd == "data") {
            cin.ignore();
            string msg;
            cout << "Digite a mensagem: ";
            getline(cin, msg);

            if (!client.sendData(msg)) //enviar a mensagem
                cerr << "Erro ao enviar dados." << endl;

        } else if (cmd == "disconnect") {
            client.storeSession(); // armazena a sessão atual para possível revive

            if (client.disconnect()) //faz o disconnect
                cout << "Desconectado com sucesso." << endl;
            else {
                cout << "Falha ao desconectar." << endl;
            }
            
        } else if (cmd == "revive") {
            if (!client.canRevive()) {
                cout << "Nenhuma sessao armazenada." << endl;
                continue;
            }

            cin.ignore();
            string msg;
            cout << "Mensagem para enviar no revive: ";
            getline(cin, msg);

            if (client.zeroWay(msg))
                cout << "Sessao revivida." << endl;
            else
                cout << "Revive falhou." << endl;

        } else if (cmd == "exit") {
            if (client.isActive())
                client.disconnect();
            break;

        } else {
            cout << "Comando desconhecido." << endl;
        }
    }

    return 0;
}