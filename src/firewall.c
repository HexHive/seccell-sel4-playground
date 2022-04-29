#include "firewall.h"

uint16_t get_header_checksum(struct ip_packet *p) {
    uint32_t header_length = (sizeof(struct ip_packet) - 65536) / 16;
    uint32_t checksum = 0;
    for (size_t j = 0; j < header_length; j++) {
        if (j == 5) continue;  // Corresponds to the checksum itself
        checksum += ((uint16_t *)p)[j];
    }
    uint8_t carry = checksum >> 16;
    checksum &= 0xffff;
    checksum += carry;
    checksum += checksum >> 16;
    checksum &= 0xffff;
    return checksum;
}

static bool valid_address(uint32_t ip) {
    struct firewall_entry *f;
    HASH_FIND_INT(firewall, &ip, f);
    return f == NULL;
}

bool process_firewall(struct ip_packet *p) {
    return (p->header_checksum == get_header_checksum(p) && valid_address(p->source_address));
}
