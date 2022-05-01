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
#include <seccells/seccells.h>

#define SD_ENTRY(secdiv, func)                \
    do {                                      \
        asm volatile (                        \
            "jals %[sd], _" #func "_entry\n"  \
            "_" #func "_entry:\n"             \
            "entry\n"                         \
            :                                 \
            : [sd] "r"(secdiv));              \
    } while (0)

#define SD_EXIT(secdiv, func)                 \
    do {                                      \
        asm volatile (                        \
            "jals %[sd], _" #func "_exit\n"   \
            "_" #func "_exit:\n"              \
            "entry\n"                         \
            :                                 \
            : [sd] "r"(secdiv));              \
    } while (0)

/* SecDiv IDs get initialized in the code */
static unsigned int firewall_secdiv = 0;
static unsigned int nat_secdiv = 0;
static unsigned int generator_secdiv = 0;

#ifdef CONFIG_EVAL_TYPE_COMP
/* Additional buffers for passing the packet data between compartments via copies => get set up in the code */
static struct ip_packet *to_firewall = NULL;
static struct ip_packet *to_nat = NULL;
static struct ip_packet *to_generator = NULL;
#endif /* CONFIG_EVAL_TYPE_COMP */

#define SWITCH_SD (defined(CONFIG_EVAL_TYPE_COMP) || defined(CONFIG_EVAL_TYPE_SC_ZCOPY))
static const size_t packet_size = ROUND_UP_UNSAFE(sizeof(struct ip_packet), BIT(seL4_MinRangeBits));
#else
#define SWITCH_SD 0
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
#ifdef CONFIG_RISCV_SECCELL
    /* Create the firewall SecDiv */
    seL4_RISCV_RangeTable_AddSecDiv_t ret = seL4_RISCV_RangeTable_AddSecDiv(seL4_CapInitThreadVSpace);
    if (unlikely(ret.error != seL4_NoError)) {
        ZF_LOGF("Failed to create firewall SecDiv");
        return 1;
    }
    firewall_secdiv = ret.id;
    /* Create the NAT SecDiv */
    ret = seL4_RISCV_RangeTable_AddSecDiv(seL4_CapInitThreadVSpace);
    if (unlikely(ret.error != seL4_NoError)) {
        ZF_LOGF("Failed to create NAT SecDiv");
        return 1;
    }
    nat_secdiv = ret.id;
    /* Store the requesting SecDiv's ID */
    csrr_usid(generator_secdiv);

    /* Grant permissions to the firewall SecDiv */
    seL4_Error err = seL4_RISCV_RangeTable_GrantSecDivPermissions(seL4_CapInitThreadVSpace, (seL4_Word)firewall_secdiv,
                                                                  (seL4_Word)&process_firewall, RT_R | RT_W | RT_X);
    if (unlikely(err != seL4_NoError)) {
        ZF_LOGF("Failed to grant permissions to the firewall SecDiv");
        return 1;
    }
    /* Grant permissions to the NAT SecDiv */
    err = seL4_RISCV_RangeTable_GrantSecDivPermissions(seL4_CapInitThreadVSpace, (seL4_Word)nat_secdiv,
                                                       (seL4_Word)&process_NAT, RT_R | RT_W | RT_X);
    if (unlikely(err != seL4_NoError)) {
        ZF_LOGF("Failed to grant permissions to the NAT SecDiv");
        return 1;
    }

#ifdef CONFIG_EVAL_TYPE_COMP
    /* Initialize the buffers for copying packets between compartments */
    struct ip_packet **buffers[3] = {&to_firewall, &to_nat, &to_generator};
    for (size_t i = 0; i < 3; i++) {
        *buffers[i] = (struct ip_packet *)malloc(packet_size);
        if (buffers[i] == NULL) {
            printf("Allocation of copy buffer for data passing failed\n");
            return 1;
        }
    }
#endif /* CONFIG_EVAL_TYPE_COMP */
#endif /* CONFIG_RISCV_SECCELL */

    for (size_t i = 0; i < RING_BUFFER_ENTRIES; i++) {
        struct ip_packet *buff_entr = (struct ip_packet *)mmap(0, packet_size, PROT_READ | PROT_WRITE,
                                                               MAP_ANONYMOUS | MAP_PRIVATE, 0, 0);
        if (!buff_entr) {
            printf("Allocation of memory region for packet failed\n");
            return 1;
        }
        ring_buffer[i] = buff_entr;
#ifdef CONFIG_EVAL_TYPE_SC_ZCOPY
        /* By default, all possible packet slots pointed to by the ring buffer are invalidated */
        inval(ring_buffer[i]);
#endif /* CONFIG_EVAL_TYPE_SC_ZCOPY */
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

void ip_generate(struct ip_packet *p, size_t i, size_t data_size) {  // TODO give tunable parameters
    p->version_ihl = (4 << 3) | 15;  // if IHL is less (min 5), we have to reduce the `options` field size
    p->dscp_ecn = 0;
    p->total_length = 64 + data_size;  // TODO make it variable // Same as with IHL, but `data` field
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
        /* Just fill the packet with more or less random data */
        udp_packet->data[i] = i % 256;
    }
}

void sink(struct ip_packet *p) {
    // print_ip_packet(p, false, true);
    // memset(p, 0, packet_size);
    return;
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

    perf_counters_t counters[NPASSES] = { 0 };
    perf_counters_t before = { 0 };
    perf_counters_t after = { 0 };

#ifdef CONFIG_PRINT_CSV
    /* Print CSV header */
    printf("Data size");
    for (size_t pass = 0; pass < NPASSES; pass++) {
        printf(";Pass %zd", pass);
    }
    printf("\n");
#endif /* CONFIG_PRINT_CSV */

    for (size_t size = MIN_PACKET_SIZE; size <= MAX_PACKET_SIZE; size += MIN_PACKET_SIZE) {
        for (size_t pass = 0; pass < NPASSES; pass++) {
            for (size_t i = 0; i < RING_BUFFER_ENTRIES; i++) {
                RDCTR(before.instret, instret);
#ifdef CONFIG_EVAL_TYPE_SC_ZCOPY
                /* Revalidate the packet => has been invalidated before */
                reval(ring_buffer[i], RT_R | RT_W);
#endif /* CONFIG_EVAL_TYPE_SC_ZCOPY */
                ip_generate(ring_buffer[i], i, size);
#ifdef CONFIG_EVAL_TYPE_COMP
                /* Copy the packet into a heap buffer to pass it to the next compartment */
                memcpy((void *)to_firewall, (void *)ring_buffer[i], ring_buffer[i]->total_length);
                struct ip_packet *p = to_firewall;
#else
                /* Simply reference the packet based on the ring buffer */
                struct ip_packet *p = ring_buffer[i];
#endif /* CONFIG_EVAL_TYPE_COMP */

#if SWITCH_SD
#ifdef CONFIG_EVAL_TYPE_SC_ZCOPY
                /* Transfer permissions */
                tfer(p, firewall_secdiv, RT_R | RT_W);
#endif /* CONFIG_EVAL_TYPE_SC_ZCOPY */
                SD_ENTRY(firewall_secdiv, firewall);
#ifdef CONFIG_EVAL_TYPE_SC_ZCOPY
                /* Receive granted permissions */
                recv(p, generator_secdiv, RT_R | RT_W);
#endif /* CONFIG_EVAL_TYPE_SC_ZCOPY */
#endif /* SWITCH_SD */
                if (process_firewall(p)) {
#if SWITCH_SD
#if defined(CONFIG_EVAL_TYPE_COMP)
                    /* Copy the packet into a heap buffer to pass it to the next compartment */
                    memcpy((void *)to_nat, (void *)to_firewall, p->total_length);
                    p = to_nat;
#elif defined(CONFIG_EVAL_TYPE_SC_ZCOPY)
                    /* Transfer permissions */
                    tfer(p, nat_secdiv, RT_R | RT_W);
#endif /* CONFIG_EVAL_TYPE_COMP / CONFIG_EVAL_TYPE_SC_ZCOPY */
                    SD_ENTRY(nat_secdiv, nat);
#ifdef CONFIG_EVAL_TYPE_SC_ZCOPY
                    /* Receive granted permissions */
                    recv(p, firewall_secdiv, RT_R | RT_W);
#endif /* CONFIG_EVAL_TYPE_SC_ZCOPY */
#endif /* SWITCH_SD */
                    process_NAT(p);
                }
#if SWITCH_SD
#if defined(CONFIG_EVAL_TYPE_COMP)
                /* Copy the packet into a heap buffer to pass it to the next compartment */
                memcpy((void *)to_generator, (void *)to_nat, p->total_length);
                p = to_generator;
#elif defined(CONFIG_EVAL_TYPE_SC_ZCOPY)
                /* Transfer permissions */
                tfer(p, generator_secdiv, RT_R | RT_W);
#endif /* CONFIG_EVAL_TYPE_COMP / CONFIG_EVAL_TYPE_SC_ZCOPY */
                SD_ENTRY(generator_secdiv, generator);
#ifdef CONFIG_EVAL_TYPE_SC_ZCOPY
                /* Need to fetch previous SD's ID because we can either arrive here from the firewall or the NAT SecDiv */
                unsigned int prev_sd;
                csrr_urid(prev_sd);
                /* Receive granted permissions */
                recv(p, prev_sd, RT_R | RT_W);
#endif /* CONFIG_EVAL_TYPE_SC_ZCOPY */
#endif /* SWITCH_SD */

                sink(p);  // Consume...
#ifdef CONFIG_EVAL_TYPE_SC_ZCOPY
                inval(p);
#endif /* CONFIG_EVAL_TYPE_SC_ZCOPY */
                RDCTR(after.instret, instret);
                counters[pass].instret += after.instret - before.instret;
            }
        }
#ifdef CONFIG_PRINT_CSV
        printf("%zd", size);
        for (size_t pass = 0; pass < NPASSES; pass++) {
            printf(";%zd", counters[pass].instret);
        }
        printf("\n");
#else
        printf("Cumulative instruction counts for packet size %zd bytes:\n", size);
        for (size_t pass = 0; pass < NPASSES; pass++) {
            printf("    Pass %3zd: %12zd\n", pass, counters[pass].instret);
        }
#endif /* CONFIG_PRINT_CSV */
    }

    seL4_TCB_Suspend(seL4_CapInitThreadTCB);
    return 0;
}
