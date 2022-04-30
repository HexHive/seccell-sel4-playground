#include <assert.h>
#include <seL4-playground/gen_config.h>
#include <sel4/sel4.h>
#include <sel4platsupport/platsupport.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#include "bench.h"
#include "firewall.h"
#include "loader.h"
#include "mmap_override.h"
#include "nat.h"

#ifdef CONFIG_RISCV_SECCELL
static const size_t packet_size = ROUND_UP_UNSAFE(sizeof(struct ip_packet), BIT(seL4_MinRangeBits));
#else
static const size_t packet_size = ROUND_UP_UNSAFE(sizeof(struct ip_packet), BIT(seL4_PageBits));
#endif /* CONFIG_RISCV_SECCELL */

struct ip_packet *ring_buffer[RING_BUFFER_ENTRIES];
struct firewall_entry *firewall;
struct NAT_entry *nat;

void print_users() {
    struct NAT_entry *s;

    for (s = nat; s != NULL; s = s->hh.next) {
        printf("[%x] %u.%u.%u.%u  %x\n", s->in_port, IP_elems(s->out_ip), s->out_port);
    }
}

int setup() {
    for (size_t i = 0; i < RING_BUFFER_ENTRIES; i++) {
        struct ip_packet *buff_entr = (struct ip_packet *)mmap(0, packet_size, PROT_READ | PROT_WRITE,
                                                               MAP_ANONYMOUS | MAP_PRIVATE, 0, 0);
        if (!buff_entr) {
            printf("Allocation of memory region for packet failed\n");
            return 1;
        }
        ring_buffer[i] = buff_entr;
    }

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

    return 0;
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

void sink(struct ip_packet *p) {
    memset(p, 0, packet_size);
}

int main(int argc, char *argv[]) {
    /* Setup serial output via seL4_Debug_PutChar */
    if (platsupport_serial_setup_bootinfo_failsafe()) {
        /* Error occured during setup => terminate */
        return 1;
    }

    int err = setup();
    if (err != 0) {
        seL4_TCB_Suspend(seL4_CapInitThreadTCB);
        return 1;
    }

    for (;;) {
        // TODO seccells instructions + change function calls by SDSwitch
        for (size_t i = 0; i < RING_BUFFER_ENTRIES; i++) {
            struct ip_packet *p = ring_buffer[i];
            // SCReval
            ip_generate(p, i);
            // SCGrant & SCTfer

            // SCRecv
            if (process_firewall(p)) {
                // SCProt
                // SCRecv
                process_NAT(p);
                // SCTfer
            }

            print_ip_packet(p, false, true);
            sink(p);  // Consume...
            // SCInval
        }
    }

    seL4_TCB_Suspend(seL4_CapInitThreadTCB);
    return 0;
}
