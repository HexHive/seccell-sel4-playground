#ifndef FIREWALL_H
#define FIREWALL_H

#include "loader.h"

extern struct firewall_entry *firewall;

uint16_t get_header_checksum(struct ip_packet *p);
bool process_firewall(struct ip_packet *p);

#endif /* FIREWALL_H */
