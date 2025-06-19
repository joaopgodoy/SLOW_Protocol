#ifndef SLOW_PACKET_H
#define SLOW_PACKET_H

#include <stdint.h>
#include <stddef.h>

#define MAX_PACKET_SIZE 1472
#define UUID_SIZE 16

typedef struct {
    uint8_t sid[UUID_SIZE];
    uint32_t sttl_flags;
    uint32_t seqnum;
    uint32_t acknum;
    uint16_t window;
    uint8_t fid;
    uint8_t fo;
    uint8_t data[MAX_PACKET_SIZE - 32];
    size_t data_len;
} SlowPacket;

void build_connect_packet(SlowPacket *p);
size_t build_packet(const SlowPacket *p, uint8_t *buffer);
int parse_packet(const uint8_t *buffer, size_t size, SlowPacket *p);
void print_packet_hex(const uint8_t *buffer, size_t size);

#endif
