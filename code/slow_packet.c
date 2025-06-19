#include "slow_packet.h"
#include <string.h>
#include <stdio.h>
#include <endian.h>

// REMOVER todas as funções de conversão - elas já existem em <endian.h>
// htole16(), htole32(), le16toh(), le32toh() já estão disponíveis!

void build_connect_packet(SlowPacket *p) {
    memset(p, 0, sizeof(SlowPacket));

    // SID Nil (16 bytes de zero)
    memset(p->sid, 0, UUID_SIZE);

    // sttl + flags
    uint32_t flags = 1U << 31;  // Bit 31 ativo → Connect
    p->sttl_flags = flags;

    p->seqnum = 0;
    p->acknum = 0;
    p->window = 1024;
    p->fid = 0;
    p->fo = 0;
    p->data_len = 0;
}

size_t build_packet(const SlowPacket *p, uint8_t *buffer) {
    size_t offset = 0;
    
    // SID (16 bytes)
    memcpy(buffer + offset, p->sid, UUID_SIZE);
    offset += UUID_SIZE;
    
    // STTL + FLAGS (4 bytes) - LITTLE ENDIAN
    uint32_t sttl_flags_le = htole32(p->sttl_flags);
    memcpy(buffer + offset, &sttl_flags_le, 4);
    offset += 4;
    
    // SEQNUM (4 bytes) - LITTLE ENDIAN
    uint32_t seqnum_le = htole32(p->seqnum);
    memcpy(buffer + offset, &seqnum_le, 4);
    offset += 4;
    
    // ACKNUM (4 bytes) - LITTLE ENDIAN
    uint32_t acknum_le = htole32(p->acknum);
    memcpy(buffer + offset, &acknum_le, 4);
    offset += 4;
    
    // WINDOW (2 bytes) - LITTLE ENDIAN
    uint16_t window_le = htole16(p->window);
    memcpy(buffer + offset, &window_le, 2);
    offset += 2;
    
    // FID (1 byte)
    buffer[offset++] = p->fid;
    
    // FO (1 byte)
    buffer[offset++] = p->fo;
    
    // DATA
    if (p->data_len > 0) {
        memcpy(buffer + offset, p->data, p->data_len);
        offset += p->data_len;
    }
    
    return offset;
}

int parse_packet(const uint8_t *buffer, size_t size, SlowPacket *p) {
    if (size < 32) return -1;
    
    size_t offset = 0;
    
    memcpy(p->sid, buffer + offset, UUID_SIZE);
    offset += UUID_SIZE;
    
    uint32_t sttl_flags_le;
    memcpy(&sttl_flags_le, buffer + offset, 4);
    p->sttl_flags = le32toh(sttl_flags_le);
    offset += 4;
    
    uint32_t seqnum_le;
    memcpy(&seqnum_le, buffer + offset, 4);
    p->seqnum = le32toh(seqnum_le);
    offset += 4;
    
    uint32_t acknum_le;
    memcpy(&acknum_le, buffer + offset, 4);
    p->acknum = le32toh(acknum_le);
    offset += 4;
    
    uint16_t window_le;
    memcpy(&window_le, buffer + offset, 2);
    p->window = le16toh(window_le);
    offset += 2;
    
    p->fid = buffer[offset++];
    p->fo = buffer[offset++];
    
    p->data_len = size - offset;
    if (p->data_len > 0) {
        memcpy(p->data, buffer + offset, p->data_len);
    }
    
    return 0;
}

void print_packet_hex(const uint8_t *buffer, size_t size) {
    for (size_t i = 0; i < size; i++) {
        printf("%02X ", buffer[i]);
    }
    printf("\n");
}
