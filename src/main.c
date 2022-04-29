#include <assert.h>
#include <seL4-playground/gen_config.h>
#include <sel4/sel4.h>
#include <sel4platsupport/platsupport.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#include "bench.h"
#include "loader.h"
#include "mmap_override.h"

struct ip_packet *ring_buffer;
struct firewall_entry *firewall;
struct NAT_entry *nat;

void print_users() {
    struct NAT_entry *s;

    for (s = nat; s != NULL; s = s->hh.next) {
        printf("[%x] %u.%u.%u.%u  %x\n", s->in_port, IP_elems(s->out_ip), s->out_port);
    }
}

struct ip_packet *next_packet(struct ip_packet *p) {  // TODO use bounds
    size_t address = (size_t)p;
    address += p->total_length;
    address += G_UNIT;  // We go to the next 4K-bounded address
    return (struct ip_packet *)(address & ~G_LOW);
}

void setup() {
    firewall = NULL;
    size_t n_elems_firewall = sizeof(blacklist_ip) / sizeof(uint32_t);
    for (size_t i = 0; i < n_elems_firewall; i++) {
        struct firewall_entry *f = malloc(sizeof(struct firewall_entry));
        uint32_t ip = blacklist_ip[i];
        f->ip = ip;
        HASH_ADD_INT(firewall, ip, f);
    }

    nat = NULL;
    size_t n_elems_NAT = sizeof(translation_units) / sizeof(struct translation_NAT);
    for (size_t i = 0; i < n_elems_NAT; i++) {
        struct NAT_entry *n = malloc(sizeof(struct NAT_entry));
        uint32_t in_port = translation_units[i].in_port;
        n->in_port = in_port;
        n->out_ip = translation_units[i].out_ip;
        n->out_port = translation_units[i].out_port;
        HASH_ADD_INT(nat, in_port, n);
    }
}

void print_udp_packet(struct udp_packet *p) {
    printf("\t%x %x %x\n", p->source_port, p->dest_port, p->length);
}

void print_ip_packet(struct ip_packet *p, bool options, bool data) {
    printf("%p: %x %x %x %x %x %x %x %x %u.%u.%u.%u %u.%u.%u.%u\n",
           p,
           p->version_ihl,
           p->dscp_ecn,
           p->total_length,
           p->identification,
           p->flags_fragment_offset,
           p->ttl,
           p->protocol,
           p->header_checksum,
           IP_elems(p->source_address),
           IP_elems(p->dest_address));
    if (options) {
        for (size_t i = 0; i < 40; i++) {
            printf("%x", p->options[i]);
        }
        printf("\n");
    }
    if (data) {
        print_udp_packet((struct udp_packet *)&p->data);
    }
}

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

void ip_generate(struct ip_packet *p, size_t i) {  // TODO give tunable parameters
    p->version_ihl = (4 << 3) | 15;                // if IHL is less (min 5), we have to reduce the `options` field size
    p->dscp_ecn = 0;
    p->total_length = 64 + (i * 91 % 73);  // TODO make it variable // Same as with IHL, but `data` field
    p->identification = 0;
    p->flags_fragment_offset = 0;
    p->ttl = 0;
    p->protocol = 17;  // Corresponds to UDP, 6 does for TCP
    p->header_checksum = get_header_checksum(p);
    switch (i % 4) {
        case 0:
            p->source_address = IP(128, 0, 0, 1);
            break;
        case 1:
            p->source_address = IP(255, 255, 255, 255);
            break;
        case 2:
            p->source_address = IP(181, 91, 93, 83);
            break;
        default:
            p->source_address = IP(88, 13, 141, 131);
            break;
    }
    p->dest_address = IP(128, 19, 7, 31);
    for (size_t j = 0; j < 40; j++) {
        p->options[j] = 0;
    }

    // UDP packet generation
    struct udp_packet *udp_packet = (struct udp_packet *)&p->data;
    udp_packet->source_port = 0;
    udp_packet->dest_port = 10000 + (i % 9);
    udp_packet->length = p->total_length - 64;
    udp_packet->checksum = 0;
    for (size_t i = 0; i < udp_packet->length; i++) {
        udp_packet->data[i] = 0;
    }
}

bool valid_address(uint32_t ip) {
    struct firewall_entry *f;
    HASH_FIND_INT(firewall, &ip, f);
    return f == NULL;
}

bool process_firewall(struct ip_packet *p) {
    return (p->header_checksum == get_header_checksum(p) && valid_address(p->source_address));
}

void process_NAT(struct ip_packet *p) {
    struct NAT_entry *n;
    uint32_t in_port = ((struct udp_packet *)&p->data)->dest_port;
    HASH_FIND_INT(nat, &in_port, n);

    if (n != NULL) {
        p->dest_address = n->out_ip;
        ((struct udp_packet *)&p->data)->dest_port = n->out_port;
    }
    return;
}

void sink(struct ip_packet *p) {
    memset(p, 0, (size_t)next_packet(p) - (size_t)p);
}

int main(int argc, char *argv[]) {
    /* Setup serial output via seL4_Debug_PutChar */
    if (platsupport_serial_setup_bootinfo_failsafe()) {
        /* Error occured during setup => terminate */
        return 1;
    }

    // TODO process arguments
    ring_buffer = mmap(0, RING_BUFFER_SIZE, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, 0, 0);

    if (!ring_buffer) {
        printf("Allocation of ring buffer failed\n");
        seL4_TCB_Suspend(seL4_CapInitThreadTCB);
        return 1;
    }
    setup();

    struct ip_packet *end_buf = (struct ip_packet *)((size_t)ring_buffer + RING_BUFFER_SIZE);
    struct ip_packet *p = (((size_t)ring_buffer & G_LOW) == 0) ? ring_buffer : (struct ip_packet *)(((size_t)ring_buffer + G_UNIT) & ~G_LOW);
    struct ip_packet *first_packet = p;
    size_t i = 0;

    for (;;) {
        // TODO seccells instructions + change function calls by SDSwitch
        while (p < end_buf) {
            // SCReval
            ip_generate(p, i++);
            // SCGrant & SCTfer

            // SCRecv
            if (process_firewall(p)) {
                // SCProt
                // SCRecv
                process_NAT(p);
                // SCTfer
            }

            print_ip_packet(p, false, true);
            sink(ring_buffer);  // Consume...
            // SCInval
            p = next_packet(p);
        }
        p = first_packet;
    }

    seL4_TCB_Suspend(seL4_CapInitThreadTCB);
    return 0;
}
