#ifndef NAT_H
#define NAT_H

#include "loader.h"

extern struct NAT_entry *nat;

void process_NAT(struct ip_packet *p);

#endif /* NAT_H */
