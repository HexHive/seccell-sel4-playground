#include "nat.h"

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
