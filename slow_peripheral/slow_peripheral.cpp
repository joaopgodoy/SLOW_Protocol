/*
 * slow_peripheral_commented.cpp
 * Autores: 
 *  Enzo Tonon Morente - 14568476 
 *  João Pedro Alves Notari Godoy -
 *  Letícia Barbosa Neves -
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
 
 using namespace std;
 
 // Tamanho do cabeçalho em bytes e tamanho máximo de dados por pacote
 static const int HDR_SIZE = 32;
 static const int DATA_MAX = 1440;
 
 // Flags do protocolo SLOW (bit flags em h.sf)
 static const uint32_t FLAG_C   = 1 << 4;  // Connect / Disconnect
 static const uint32_t FLAG_R   = 1 << 3;  // Revive
 static const uint32_t FLAG_ACK = 1 << 2;  // Acknowledgment
 static const uint32_t FLAG_AR  = 1 << 1;  // Ack de Revive / Setup
 static const uint32_t FLAG_MB  = 1 << 0;  // More Bit (fragmentação)
 
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
 
 // Estrutura de cabeçalho de controle do protocolo SLOW
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
     cout << "==== " << label << " ====" << endl;
     cout << "SID: ";
     for (int i = 0; i < 16; i++)
         cout << hex << setw(2) << setfill('0') << (int)h.sid.b[i];
     cout << dec << endl;
     cout << "Flags: " << (h.sf & 0x1F) << endl;
     cout << "SEQ: "  << h.seq << endl;
     cout << "ACK: "  << h.ack << endl;
     cout << "WND: "  << h.wnd << endl;
     cout << "FID: "  << (int)h.fid << endl;
     cout << "FO: "   << (int)h.fo << endl << endl;
 }
 
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
 
     // Calcula janela anunciada (até 16 bits)
     uint16_t advertisedWindow() const {
         if (bytesInFlight >= window_size)
             return 0;
         return static_cast<uint16_t>(min<uint32_t>(window_size - bytesInFlight, UINT16_MAX));
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
 
     // realiza o handshake inicial com o servidor
     bool connect() {
        if (active) return true; //se já estiver conectado não fazer nada

        //cria o header com a flag connect
        Header h;
        h.seq = nextSeq++;
        h.wnd = advertisedWindow(); //janela atual
        h.sf |= FLAG_C; // flag connect

        //envia o header
        uint8_t buf[HDR_SIZE];
        serialize(h, buf);
        sendto(fd, buf, HDR_SIZE, 0, (sockaddr*)&srv, sizeof(srv)); //manda o connect
        printHeader(h, "CONNECT");

        // aguarda SETUP
        uint8_t rbuf[HDR_SIZE + DATA_MAX]; // buffer capaz de armazenar o cabeçalho mais até DATA_MAX bytes de payload.
        sockaddr_in sa; socklen_t sl = sizeof(sa);
        if (recvfrom(fd, rbuf, sizeof(rbuf), 0, (sockaddr*)&sa, &sl) < HDR_SIZE) //bloqueia (até 5 s) aguardando um pacote de resposta
            // não foi recebido cabeçalho completo (falha)
            return false;

        Header r;
        deserialize(r, rbuf); //converter header
        printHeader(r, "SETUP");

        if (!(r.sf & FLAG_AR)) return false; // Verifica flag AR de resposta
    
        prevHdr = r;
        active = hasPrev = true; //sessão ativa e com histório para revive
        lastCentralSeq = r.seq;
        nextSeq = r.seq + 1;

        return true; //handshake bem sucedido
     }
 
     // Envia mensagem, fragmentando se necessário e aguardando ACK
     bool sendData(const string& msg) {
         if (!active) return false; //verifica se a sessão está ativa
 
         // Função lambda para enviar cada fragmento
         auto sendFrag = [&](const char* data, size_t len, uint8_t fid, uint8_t fo, bool more) {
            //verifica se essa quantidade de bytes pode ser mandada baseada na janela atual
            if (bytesInFlight + len > window_size) return false;

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
            printHeader(h, "DATA");

            if (sendto(fd, buf, HDR_SIZE + len, 0, (sockaddr*)&srv, sizeof(srv)) >= 0){
                bytesInFlight += len;

                return true;
            }

            return false;
         };
        
        // Função lambda para esperar ACK de fragmento
        auto esperaAck = [&]() -> bool {
            uint8_t rbuf[HDR_SIZE];
            sockaddr_in sa; socklen_t sl = sizeof(sa);
            if (recvfrom(fd, rbuf, HDR_SIZE, 0, (sockaddr*)&sa, &sl) < HDR_SIZE)
                return false;

            Header r; deserialize(r, rbuf);
            printHeader(r, "Pacote Recebido (ACK DATA)");
            
            if (!(r.sf & FLAG_ACK)) return false;

            // Atualiza controle de fluxo e janela
            bytesInFlight   = 0;
            lastCentralSeq  = r.seq;
            prevHdr         = r;
            nextSeq         = lastCentralSeq + 1;
            window_size     = r.wnd; // Atualiza a janela com o valor mais recente do servidor
            
            return true;
        };
 
         // Fragmenta se a mensagem exceder DATA_MAX
         if (msg.size() > DATA_MAX) {
            uint8_t fid = nextSeq & 0xFF; //Indetificador único para todos os fragmentos
            
            // for inicia no primeiro byte da mensagem (off = 0); no primeiro fragmento (fo = 0) e vai até o final da mensagem (off < msg.size())
            for (size_t off = 0, fo = 0; off < msg.size();) {
                // tamanho do fragmento será o mínimo entre o tamanho máximo ou o tamanho restante da mensagem.
                size_t len = min(msg.size() - off, static_cast<size_t>(DATA_MAX));

                //verifica se o tamanho do fragmento cabe na janela
                size_t EspacoJanela = min(len, (size_t)(window_size - bytesInFlight)); 

                if (EspacoJanela == 0) {
                    // Se não houver espaço, espera ACK de fragmento anterior
                    if (!esperaAck()) return false; //se não receber ACK, retorna falso

                    continue; //volta para o início do loop para tentar enviar novamente
                }

                bool more = (off + len < msg.size()); //se será necessário ter mais fragmentos ou não

                if (!sendFrag(msg.data() + off, len, fid, fo, more)) return false; //envia a mensagem

                fo += 1;
                off += len; //incrementa o off

                // Se ainda há dados para enviar e a janela está cheia, espera ACK
                if (more && (bytesInFlight + min((size_t)DATA_MAX, (size_t)(msg.size() - off)) > window_size)) {
                    if (!esperaAck()) return false;
                }
            }

            //ter certeza que temos o ACK finals    
            return esperaAck();

         } else {
            // mensagemn pequena é enviada em um único fragmento

            if (msg.size() <= window_size){ //verificar se é possível mandar a mensagem dentro da janela
                if (!sendFrag(msg.data(), msg.size(), 0, 0, false)) return false; //enviar a mensagem

                return esperaAck(); 
            }
            else{ //mensagem maior que a janela, mas menor que o DATA_MAX]
                cerr << "Mensagem muito grande para a janela atual." << endl;
                return false;
            }
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
         sendto(fd, buf, HDR_SIZE, 0, (sockaddr*)&srv, sizeof(srv)); //envia o DISCONNECT
         printHeader(h, "DISCONNECT");
 
         // Aguarda até 3 ACKs de desconexão
         for (int i = 0; i < 3; i++) {
             uint8_t rbuf[HDR_SIZE];
             sockaddr_in sa; socklen_t sl = sizeof(sa);
             if (recvfrom(fd, rbuf, HDR_SIZE, 0, (sockaddr*)&sa, &sl) >= HDR_SIZE) {
                 Header r;
                 deserialize(r, rbuf);
                 printHeader(r, "ACK DISCONNECT");
                 if (r.sf & FLAG_ACK) { //flag ACK recebido
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
        printHeader(h, "REVIVE");

        //espera REIVE ACK do servidor
        uint8_t rbuf[HDR_SIZE + DATA_MAX];
        sockaddr_in sa; socklen_t sl = sizeof(sa);
        if (recvfrom(fd, rbuf, sizeof(rbuf), 0, (sockaddr*)&sa, &sl) < HDR_SIZE)
            return false;

        Header r;
        deserialize(r, rbuf);
        printHeader(r, "REVIVE ACK");
        if (!(r.sf & FLAG_AR)) return false; //verifica se a flag é a correta

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
 
     cout << "[OK] Conectado ao servidor." << endl;
 
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
                cout << "Fala ao desconecatar." << endl;
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
 