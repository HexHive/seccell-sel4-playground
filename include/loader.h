#ifndef LOADER_H
#define LOADER_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <utils/uthash.h>

#include "mmap_override.h"

struct ip_packet {
    uint8_t version_ihl;
    uint8_t dscp_ecn;
    uint16_t total_length;
    uint16_t identification;
    uint16_t flags_fragment_offset;
    uint8_t ttl;
    uint8_t protocol;
    uint16_t header_checksum;
    uint32_t source_address;
    uint32_t dest_address;
    uint8_t options[40];
    uint8_t data[65536];
};

struct udp_packet {
    uint16_t source_port;
    uint16_t dest_port;
    uint16_t length;
    uint16_t checksum;  // Can be all zeroes in IPv4
    uint8_t data[65472];
};

// 64 KiB
// #define RING_BUFFER_SIZE 65536
// 1MiB
#define RING_BUFFER_SIZE 1048576

// TODO variable-sized attributes not part of the packet, calculate on-the-fly

struct translation_NAT {
    uint16_t in_port;
    uint32_t out_ip;
    uint32_t out_port;
};

#define IP(a, b, c, d) ((uint32_t)(((a) << 24) | ((b) << 16) | ((c) << 8) | (d)))
#define IP_elems(ip) ((ip) >> 24) & 0xff, ((ip) >> 16) & 0xff, ((ip) >> 8) & 0xff, (ip) & 0xff

// G stands for granularity
#define G_LOW 0xfff
#define G_UNIT 0x1000

struct firewall_entry {
    uint32_t ip;
    UT_hash_handle hh;
};

static uint32_t blacklist_ip[] = {
    IP(0, 0, 0, 0),
    IP(192, 168, 1, 10),
    IP(255, 255, 255, 255)};

static struct translation_NAT translation_units[] = {
    {10000, IP(192, 184, 121, 48), 17},
    {10001, IP(192, 83, 128, 40), 17},
    {10002, IP(192, 18, 92, 123), 17},
    {10003, IP(192, 47, 99, 39), 17},
    {10004, IP(192, 82, 91, 3), 17},
    {10005, IP(192, 31, 91, 9), 17},
    {10006, IP(192, 73, 1, 4), 17},
    {10007, IP(192, 83, 23, 9), 17},
    {10008, IP(192, 83, 91, 73), 17}};

struct NAT_entry {
    uint16_t in_port;  // id
    uint32_t out_ip;
    uint32_t out_port;
    UT_hash_handle hh;
};

#endif /* LOADER_H */
