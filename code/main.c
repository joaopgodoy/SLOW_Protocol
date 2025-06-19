#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <errno.h>
#include <sys/time.h>  // Para timeout opcional
#include "slow_packet.h"

#define SERVER_IP "slow.gmelodie.com"
#define SERVER_PORT 7033

int main() {
    int sockfd;
    struct sockaddr_in server_addr;
    uint8_t buffer[MAX_PACKET_SIZE];

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return 1;
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr);

    // Construir pacote Connect
    SlowPacket p;
    build_connect_packet(&p);
    
    // Serializar pacote
    size_t packet_size = build_packet(&p, buffer);

    printf("Enviando pacote Connect:\n");
    print_packet_hex(buffer, packet_size);

    ssize_t sent = sendto(sockfd, buffer, packet_size, 0,
                          (struct sockaddr *)&server_addr, sizeof(server_addr));

    if (sent < 0) {
        perror("sendto");
        close(sockfd);
        return 1;
    }

    printf("Pacote enviado (%zd bytes)\n", sent);

    close(sockfd);
    return 0;
}
