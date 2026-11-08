#include "net.h"
#include "pci.h"
#include "heap.h"
#include "string.h"
#include "printf.h"
#include "pit.h"
#include "rtc.h"
#include "bearssl.h"
#include "tls_trust.h"

#define NET_IP(a, b, c, d) \
    (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(c) << 8) | (uint32_t)(d))

#define ETH_ALEN 6
#define ETH_HEADER_LEN 14
#define NET_FRAME_MAX 1600
#define NET_BUF_SIZE 2048
#define RX_DESC_COUNT 32
#define TX_DESC_COUNT 8

#define E1000_VENDOR 0x8086
#define E1000_DEVICE 0x100E

#define E1000_CTRL 0x0000
#define E1000_STATUS 0x0008
#define E1000_ICR 0x00C0
#define E1000_IMS 0x00D0
#define E1000_IMC 0x00D8
#define E1000_RCTL 0x0100
#define E1000_TCTL 0x0400
#define E1000_RDBAL 0x2800
#define E1000_RDBAH 0x2804
#define E1000_RDLEN 0x2808
#define E1000_RDH 0x2810
#define E1000_RDT 0x2818
#define E1000_TDBAL 0x3800
#define E1000_TDBAH 0x3804
#define E1000_TDLEN 0x3808
#define E1000_TDH 0x3810
#define E1000_TDT 0x3818
#define E1000_MTA 0x5200
#define E1000_RAL0 0x5400
#define E1000_RAH0 0x5404

#define E1000_CTRL_RST (1u << 26)
#define E1000_CTRL_SLU (1u << 6)
#define E1000_RCTL_EN (1u << 1)
#define E1000_RCTL_UPE (1u << 3)
#define E1000_RCTL_MPE (1u << 4)
#define E1000_RCTL_BAM (1u << 15)
#define E1000_RCTL_SECRC (1u << 26)
#define E1000_TCTL_EN (1u << 1)
#define E1000_TCTL_PSP (1u << 3)
#define E1000_TCTL_CT_SHIFT 4
#define E1000_TCTL_COLD_SHIFT 12
#define E1000_TXD_CMD_EOP 0x01
#define E1000_TXD_CMD_IFCS 0x02
#define E1000_TXD_CMD_RS 0x08
#define E1000_TXD_STAT_DD 0x01
#define E1000_RXD_STAT_DD 0x01
#define E1000_RXD_STAT_EOP 0x02

#define ETH_TYPE_IP 0x0800
#define ETH_TYPE_ARP 0x0806

#define IP_PROTO_ICMP 1
#define IP_PROTO_TCP 6
#define IP_PROTO_UDP 17

#define ARP_CACHE_SIZE 8
#define DNS_LOCAL_PORT 53001
#define TCP_RX_MAX 65536
#define TLS_IO_TIMEOUT_MS 15000

typedef struct {
    uint64_t addr;
    uint16_t length;
    uint16_t checksum;
    uint8_t status;
    uint8_t errors;
    uint16_t special;
} PACKED e1000_rx_desc_t;

typedef struct {
    uint64_t addr;
    uint16_t length;
    uint8_t cso;
    uint8_t cmd;
    uint8_t status;
    uint8_t css;
    uint16_t special;
} PACKED e1000_tx_desc_t;

typedef struct {
    uint32_t ip;
    uint8_t mac[ETH_ALEN];
    bool valid;
} arp_entry_t;

typedef struct {
    bool active;
    bool established;
    bool fin_recv;
    bool reset;
    uint32_t remote_ip;
    uint16_t remote_port;
    uint16_t local_port;
    uint32_t send_seq;
    uint32_t recv_ack;
} tcp_state_t;

static volatile uint32_t *e1000_mmio;
static volatile e1000_rx_desc_t *e1000_rx_desc;
static volatile e1000_tx_desc_t *e1000_tx_desc;
static uint8_t *e1000_rx_buffers;
static uint8_t *e1000_tx_buffers;
static int e1000_rx_cur;
static int e1000_tx_cur;

static bool net_up;
static bool net_in_poll;
static uint8_t net_mac[ETH_ALEN] = { 0x52, 0x54, 0x00, 0x12, 0x34, 0x56 };
static uint32_t net_ip = NET_IP(10, 0, 2, 15);
static uint32_t net_mask = NET_IP(255, 255, 255, 0);
static uint32_t net_gateway = NET_IP(10, 0, 2, 2);
static uint32_t net_dns = NET_IP(10, 0, 2, 3);
static char net_error[128];
static uint8_t net_tx_frame[NET_FRAME_MAX];

static arp_entry_t arp_cache[ARP_CACHE_SIZE];
static bool arp_waiting;
static uint32_t arp_wait_ip;
static uint8_t arp_wait_mac[ETH_ALEN];

static uint8_t dns_rx[512];
static int dns_rx_len;
static bool dns_done;
static uint16_t dns_id;

static tcp_state_t tcp;
static uint8_t tcp_rx[TCP_RX_MAX];
static int tcp_rx_len;
static int tcp_rx_pos;
static uint16_t tcp_port_counter;

static br_ssl_client_context tls_client;
static br_x509_minimal_context tls_x509;
static br_sslio_context tls_io;
static unsigned char tls_iobuf[BR_SSL_BUFSIZE_BIDI];
static char tls_rx[TCP_RX_MAX];

static void net_poll(void);

static uint16_t read_be16(const uint8_t *p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint32_t read_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static void write_be16(uint8_t *p, uint16_t value) {
    p[0] = (uint8_t)(value >> 8);
    p[1] = (uint8_t)value;
}

static void write_be32(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)(value >> 24);
    p[1] = (uint8_t)(value >> 16);
    p[2] = (uint8_t)(value >> 8);
    p[3] = (uint8_t)value;
}

static void net_set_error(const char *text) {
    kstrncpy(net_error, text, sizeof(net_error) - 1);
    net_error[sizeof(net_error) - 1] = 0;
}

static uint32_t net_checksum_add(const void *data, int len, uint32_t sum) {
    const uint8_t *p = (const uint8_t *)data;
    while (len > 1) {
        sum += (uint32_t)(((uint16_t)p[0] << 8) | p[1]);
        p += 2;
        len -= 2;
    }
    if (len) {
        sum += (uint32_t)((uint16_t)p[0] << 8);
    }
    return sum;
}

static uint16_t net_checksum_finish(uint32_t sum) {
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    return (uint16_t)(~sum & 0xFFFF);
}

static uint16_t net_checksum(const void *data, int len) {
    return net_checksum_finish(net_checksum_add(data, len, 0));
}

static uint32_t e1000_read(uint32_t reg) {
    return e1000_mmio[reg / 4];
}

static void e1000_write(uint32_t reg, uint32_t value) {
    e1000_mmio[reg / 4] = value;
}

static bool e1000_init(void) {
    pci_device_t pci;
    if (!pci_find_device(E1000_VENDOR, E1000_DEVICE, &pci)) {
        net_set_error("No Intel e1000 network adapter found");
        return false;
    }
    uint32_t bar = pci.bar0;
    if (!bar || bar == 0xFFFFFFFFu || (bar & 1)) {
        net_set_error("e1000 memory BAR is unavailable");
        return false;
    }
    e1000_mmio = (volatile uint32_t *)(uintptr_t)(bar & ~0xFu);
    pci_enable_device(&pci);

    e1000_write(E1000_CTRL, E1000_CTRL_RST);
    for (uint32_t i = 0; i < 1000000; i++) {
        if (!(e1000_read(E1000_CTRL) & E1000_CTRL_RST)) {
            break;
        }
    }
    pit_sleep_ms(10);
    e1000_write(E1000_CTRL, E1000_CTRL_SLU);
    e1000_write(E1000_IMS, 0);
    e1000_write(E1000_ICR, 0xFFFFFFFFu);

    uint32_t ral = (uint32_t)net_mac[0] |
                   ((uint32_t)net_mac[1] << 8) |
                   ((uint32_t)net_mac[2] << 16) |
                   ((uint32_t)net_mac[3] << 24);
    uint32_t rah = (uint32_t)net_mac[4] |
                   ((uint32_t)net_mac[5] << 8) |
                   (1u << 31);
    e1000_write(E1000_RAL0, ral);
    e1000_write(E1000_RAH0, rah);
    for (int i = 0; i < 128; i++) {
        e1000_write(E1000_MTA + (uint32_t)i * 4, 0);
    }

    e1000_rx_desc = (volatile e1000_rx_desc_t *)kzalloc(
        sizeof(e1000_rx_desc_t) * RX_DESC_COUNT);
    e1000_tx_desc = (volatile e1000_tx_desc_t *)kzalloc(
        sizeof(e1000_tx_desc_t) * TX_DESC_COUNT);
    e1000_rx_buffers = (uint8_t *)kzalloc(NET_BUF_SIZE * RX_DESC_COUNT);
    e1000_tx_buffers = (uint8_t *)kzalloc(NET_BUF_SIZE * TX_DESC_COUNT);
    if (!e1000_rx_desc || !e1000_tx_desc || !e1000_rx_buffers || !e1000_tx_buffers) {
        net_set_error("Not enough memory for the network adapter");
        return false;
    }
    for (int i = 0; i < RX_DESC_COUNT; i++) {
        e1000_rx_desc[i].addr = (uint64_t)(uintptr_t)(e1000_rx_buffers + i * NET_BUF_SIZE);
        e1000_rx_desc[i].status = 0;
    }
    for (int i = 0; i < TX_DESC_COUNT; i++) {
        e1000_tx_desc[i].addr = (uint64_t)(uintptr_t)(e1000_tx_buffers + i * NET_BUF_SIZE);
        e1000_tx_desc[i].status = E1000_TXD_STAT_DD;
    }

    e1000_write(E1000_RDBAL, (uint32_t)(uintptr_t)e1000_rx_desc);
    e1000_write(E1000_RDBAH, 0);
    e1000_write(E1000_RDLEN, sizeof(e1000_rx_desc_t) * RX_DESC_COUNT);
    e1000_write(E1000_RDH, 0);
    e1000_write(E1000_RDT, RX_DESC_COUNT - 1);
    e1000_write(E1000_RCTL, E1000_RCTL_EN | E1000_RCTL_UPE | E1000_RCTL_MPE |
                            E1000_RCTL_BAM | E1000_RCTL_SECRC);

    e1000_write(E1000_TDBAL, (uint32_t)(uintptr_t)e1000_tx_desc);
    e1000_write(E1000_TDBAH, 0);
    e1000_write(E1000_TDLEN, sizeof(e1000_tx_desc_t) * TX_DESC_COUNT);
    e1000_write(E1000_TDH, 0);
    e1000_write(E1000_TDT, 0);
    e1000_write(E1000_TCTL, E1000_TCTL_EN | E1000_TCTL_PSP |
                            (0x10u << E1000_TCTL_CT_SHIFT) |
                            (0x40u << E1000_TCTL_COLD_SHIFT));

    e1000_rx_cur = 0;
    e1000_tx_cur = 0;
    uint32_t link_start = pit_ticks();
    while (!(e1000_read(E1000_STATUS) & 0x02u) &&
           (uint32_t)(pit_ticks() - link_start) < 100u) {
        pit_sleep_ms(10);
    }
    net_up = true;
    kprintf("[net] e1000 at 0x%08x, link %s, ip 10.0.2.15 gw 10.0.2.2\n",
            (uint32_t)(uintptr_t)e1000_mmio,
            (e1000_read(E1000_STATUS) & 0x02u) ? "up" : "down");
    return true;
}

static int e1000_receive(uint8_t *out, int max) {
    if (!net_up) {
        return 0;
    }
    for (;;) {
        volatile e1000_rx_desc_t *desc = &e1000_rx_desc[e1000_rx_cur];
        if (!(desc->status & E1000_RXD_STAT_DD)) {
            return 0;
        }
        int length = (int)desc->length;
        int copied = 0;
        if ((desc->status & E1000_RXD_STAT_EOP) && !(desc->errors) &&
            length > 0) {
            copied = MIN(length, max);
            kmemcpy(out, e1000_rx_buffers + e1000_rx_cur * NET_BUF_SIZE, (size_t)copied);
        }
        desc->status = 0;
        desc->length = 0;
        e1000_write(E1000_RDT, e1000_rx_cur);
        e1000_rx_cur = (e1000_rx_cur + 1) % RX_DESC_COUNT;
        if (copied > 0) {
            return copied;
        }
    }
}

static bool e1000_send(const uint8_t *data, int len) {
    if (!net_up || len <= 0 || len > NET_BUF_SIZE) {
        return false;
    }
    volatile e1000_tx_desc_t *desc = &e1000_tx_desc[e1000_tx_cur];
    uint32_t start = pit_ticks();
    while (!(desc->status & E1000_TXD_STAT_DD) &&
           (uint32_t)(pit_ticks() - start) < 200u) {
    }
    if (!(desc->status & E1000_TXD_STAT_DD)) {
        return false;
    }
    kmemcpy(e1000_tx_buffers + e1000_tx_cur * NET_BUF_SIZE, data, (size_t)len);
    desc->length = (uint16_t)len;
    desc->cso = 0;
    desc->cmd = E1000_TXD_CMD_EOP | E1000_TXD_CMD_IFCS | E1000_TXD_CMD_RS;
    desc->status = 0;
    desc->special = 0;
    __asm__ volatile("" ::: "memory");
    e1000_write(E1000_TDT, (uint32_t)((e1000_tx_cur + 1) % TX_DESC_COUNT));

    start = pit_ticks();
    while (!(desc->status & E1000_TXD_STAT_DD) &&
           (uint32_t)(pit_ticks() - start) < 200u) {
    }
    bool ok = (desc->status & E1000_TXD_STAT_DD) != 0;
    e1000_tx_cur = (e1000_tx_cur + 1) % TX_DESC_COUNT;
    return ok;
}

static bool net_send_eth(const uint8_t *dst, uint16_t type,
                         const uint8_t *payload, int len) {
    if (len < 0 || len + ETH_HEADER_LEN > NET_FRAME_MAX) {
        return false;
    }
    int total = len + ETH_HEADER_LEN;
    kmemcpy(net_tx_frame, dst, ETH_ALEN);
    kmemcpy(net_tx_frame + ETH_ALEN, net_mac, ETH_ALEN);
    write_be16(net_tx_frame + 12, type);
    kmemcpy(net_tx_frame + ETH_HEADER_LEN, payload, (size_t)len);
    while (total < 60) {
        net_tx_frame[total++] = 0;
    }
    return e1000_send(net_tx_frame, total);
}

static void arp_cache_add(uint32_t ip, const uint8_t *mac) {
    int slot = -1;
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].valid && arp_cache[i].ip == ip) {
            slot = i;
            break;
        }
        if (!arp_cache[i].valid && slot < 0) {
            slot = i;
        }
    }
    if (slot < 0) {
        slot = 0;
    }
    arp_cache[slot].ip = ip;
    kmemcpy(arp_cache[slot].mac, mac, ETH_ALEN);
    arp_cache[slot].valid = true;
}

static bool arp_cache_lookup(uint32_t ip, uint8_t *mac) {
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].valid && arp_cache[i].ip == ip) {
            kmemcpy(mac, arp_cache[i].mac, ETH_ALEN);
            return true;
        }
    }
    return false;
}

static uint32_t net_next_hop(uint32_t ip) {
    if ((ip & net_mask) == (net_ip & net_mask)) {
        return ip;
    }
    return net_gateway;
}

static void arp_send_request(uint32_t target) {
    uint8_t packet[28];
    uint8_t broadcast[ETH_ALEN] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    write_be16(packet + 0, 1);
    write_be16(packet + 2, ETH_TYPE_IP);
    packet[4] = ETH_ALEN;
    packet[5] = 4;
    write_be16(packet + 6, 1);
    kmemcpy(packet + 8, net_mac, ETH_ALEN);
    write_be32(packet + 14, net_ip);
    kmemset(packet + 18, 0, ETH_ALEN);
    write_be32(packet + 24, target);
    net_send_eth(broadcast, ETH_TYPE_ARP, packet, sizeof(packet));
}

static void arp_send_reply(uint32_t target_ip, const uint8_t *target_mac) {
    uint8_t packet[28];
    write_be16(packet + 0, 1);
    write_be16(packet + 2, ETH_TYPE_IP);
    packet[4] = ETH_ALEN;
    packet[5] = 4;
    write_be16(packet + 6, 2);
    kmemcpy(packet + 8, net_mac, ETH_ALEN);
    write_be32(packet + 14, net_ip);
    kmemcpy(packet + 18, target_mac, ETH_ALEN);
    write_be32(packet + 24, target_ip);
    net_send_eth(target_mac, ETH_TYPE_ARP, packet, sizeof(packet));
}

static bool arp_resolve(uint32_t ip, uint8_t *mac, uint32_t timeout_ms) {
    if (arp_cache_lookup(ip, mac)) {
        return true;
    }
    if (net_in_poll) {
        return false;
    }
    arp_waiting = true;
    arp_wait_ip = ip;
    uint32_t start = pit_ticks();
    uint32_t timeout = timeout_ms / 10u;
    if (!timeout) {
        timeout = 1;
    }
    while ((uint32_t)(pit_ticks() - start) < timeout) {
        arp_send_request(ip);
        uint32_t retry = pit_ticks();
        while ((uint32_t)(pit_ticks() - retry) < 10u) {
            net_poll();
            if (!arp_waiting) {
                kmemcpy(mac, arp_wait_mac, ETH_ALEN);
                return true;
            }
        }
    }
    arp_waiting = false;
    return false;
}

static void net_handle_arp(const uint8_t *packet, int len) {
    if (len < 28) {
        return;
    }
    uint16_t oper = read_be16(packet + 6);
    uint32_t sender_ip = read_be32(packet + 14);
    uint32_t target_ip = read_be32(packet + 24);
    arp_cache_add(sender_ip, packet + 8);
    if (arp_waiting && sender_ip == arp_wait_ip) {
        kmemcpy(arp_wait_mac, packet + 8, ETH_ALEN);
        arp_waiting = false;
    }
    if (oper == 1 && target_ip == net_ip) {
        arp_send_reply(sender_ip, packet + 8);
    }
}

static bool net_send_ipv4(uint32_t dst, uint8_t protocol,
                          const uint8_t *payload, int len) {
    if (!net_up || len < 0 || len + 20 > NET_FRAME_MAX) {
        return false;
    }
    uint8_t packet[NET_FRAME_MAX];
    kmemset(packet, 0, 20);
    packet[0] = 0x45;
    write_be16(packet + 2, (uint16_t)(20 + len));
    write_be16(packet + 4, (uint16_t)(pit_ticks() & 0xFFFF));
    write_be16(packet + 6, 0x4000);
    packet[8] = 64;
    packet[9] = protocol;
    write_be32(packet + 12, net_ip);
    write_be32(packet + 16, dst);
    write_be16(packet + 10, net_checksum(packet, 20));
    kmemcpy(packet + 20, payload, (size_t)len);

    uint32_t next_hop = net_next_hop(dst);
    uint8_t mac[ETH_ALEN];
    if (dst == 0xFFFFFFFFu) {
        kmemset(mac, 0xFF, ETH_ALEN);
    } else if (!arp_resolve(next_hop, mac, 1000)) {
        net_set_error("Could not resolve the gateway MAC address");
        return false;
    }
    return net_send_eth(mac, ETH_TYPE_IP, packet, 20 + len);
}

static bool net_send_udp(uint32_t dst, uint16_t source_port, uint16_t dest_port,
                         const uint8_t *payload, int len) {
    if (len < 0 || len + 8 > NET_FRAME_MAX - 20) {
        return false;
    }
    uint8_t packet[NET_FRAME_MAX];
    write_be16(packet + 0, source_port);
    write_be16(packet + 2, dest_port);
    write_be16(packet + 4, (uint16_t)(8 + len));
    write_be16(packet + 6, 0);
    kmemcpy(packet + 8, payload, (size_t)len);

    uint8_t pseudo[12];
    write_be32(pseudo, net_ip);
    write_be32(pseudo + 4, dst);
    pseudo[8] = 0;
    pseudo[9] = IP_PROTO_UDP;
    write_be16(pseudo + 10, (uint16_t)(8 + len));
    uint32_t sum = net_checksum_add(pseudo, sizeof(pseudo), 0);
    sum = net_checksum_add(packet, 8 + len, sum);
    uint16_t checksum = net_checksum_finish(sum);
    if (!checksum) {
        checksum = 0xFFFF;
    }
    write_be16(packet + 6, checksum);
    return net_send_ipv4(dst, IP_PROTO_UDP, packet, 8 + len);
}

static bool dns_skip_name(const uint8_t *packet, int len, int *offset) {
    int pos = *offset;
    for (;;) {
        if (pos >= len) {
            return false;
        }
        uint8_t size = packet[pos];
        if (size == 0) {
            pos++;
            break;
        }
        if ((size & 0xC0) == 0xC0) {
            if (pos + 1 >= len) {
                return false;
            }
            pos += 2;
            break;
        }
        if (size > 63 || pos + 1 + size > len) {
            return false;
        }
        pos += 1 + size;
    }
    *offset = pos;
    return true;
}

static bool net_dns_resolve(const char *host, uint32_t *ip_out) {
    uint32_t direct = 0;
    int parts = 0;
    const char *cursor = host;
    bool numeric = true;
    for (;;) {
        int value = 0;
        int digits = 0;
        while (*cursor >= '0' && *cursor <= '9') {
            value = value * 10 + (*cursor - '0');
            cursor++;
            digits++;
        }
        if (!digits || value > 255) {
            numeric = false;
            break;
        }
        direct = (direct << 8) | (uint32_t)value;
        parts++;
        if (*cursor == '.') {
            cursor++;
            if (parts == 4) {
                numeric = false;
                break;
            }
            continue;
        }
        if (*cursor == 0 && parts == 4) {
            break;
        }
        numeric = false;
        break;
    }
    if (numeric && parts == 4) {
        *ip_out = direct;
        return true;
    }

    uint8_t query[512];
    kmemset(query, 0, sizeof(query));
    dns_id = (uint16_t)(pit_ticks() * 31u + 0x4D2u);
    write_be16(query + 0, dns_id);
    write_be16(query + 2, 0x0100);
    write_be16(query + 4, 1);
    int len = 12;
    size_t host_len = kstrlen(host);
    if (host_len && host[host_len - 1] == '.') {
        host_len--;
    }
    if (host_len == 0 || host_len > 253) {
        net_set_error("Invalid DNS host name");
        return false;
    }
    for (size_t i = 0; i < host_len;) {
        size_t label = 0;
        while (i + label < host_len && host[i + label] != '.') {
            label++;
        }
        if (label == 0 || label > 63 || len + 1 + (int)label >= (int)sizeof(query)) {
            net_set_error("Invalid DNS host name");
            return false;
        }
        query[len++] = (uint8_t)label;
        kmemcpy(query + len, host + i, label);
        len += (int)label;
        i += label;
        if (i < host_len && host[i] == '.') {
            i++;
        }
    }
    query[len++] = 0;
    write_be16(query + len, 1);
    len += 2;
    write_be16(query + len, 1);
    len += 2;

    dns_done = false;
    dns_rx_len = 0;
    uint32_t start = pit_ticks();
    while ((uint32_t)(pit_ticks() - start) < 400u) {
        if (!net_send_udp(net_dns, DNS_LOCAL_PORT, 53, query, len)) {
            net_set_error("Could not send the DNS query");
            return false;
        }
        uint32_t retry = pit_ticks();
        while ((uint32_t)(pit_ticks() - retry) < 100u) {
            net_poll();
            if (dns_done) {
                goto got_answer;
            }
        }
    }
    net_set_error("DNS lookup timed out");
    return false;

got_answer:
    if (dns_rx_len < 12) {
        net_set_error("DNS response was too short");
        return false;
    }
    if (read_be16(dns_rx) != dns_id) {
        net_set_error("DNS response did not match the query");
        return false;
    }
    int offset = 12;
    uint16_t questions = read_be16(dns_rx + 4);
    uint16_t answers = read_be16(dns_rx + 6);
    for (uint16_t i = 0; i < questions; i++) {
        if (!dns_skip_name(dns_rx, dns_rx_len, &offset) || offset + 4 > dns_rx_len) {
            return false;
        }
        offset += 4;
    }
    for (uint16_t i = 0; i < answers; i++) {
        if (!dns_skip_name(dns_rx, dns_rx_len, &offset) || offset + 10 > dns_rx_len) {
            return false;
        }
        uint16_t type = read_be16(dns_rx + offset);
        uint16_t klass = read_be16(dns_rx + offset + 2);
        uint16_t rdlength = read_be16(dns_rx + offset + 8);
        offset += 10;
        if (offset + rdlength > dns_rx_len) {
            return false;
        }
        if (type == 1 && klass == 1 && rdlength == 4) {
            *ip_out = read_be32(dns_rx + offset);
            kprintf("[net] DNS %s -> %u.%u.%u.%u\n", host,
                    (*ip_out >> 24) & 0xFF, (*ip_out >> 16) & 0xFF,
                    (*ip_out >> 8) & 0xFF, *ip_out & 0xFF);
            return true;
        }
        offset += rdlength;
    }
    net_set_error("DNS response did not contain an IPv4 address");
    return false;
}

static void net_handle_dns(const uint8_t *packet, int len, uint16_t source_port,
                           uint16_t dest_port) {
    if (source_port != 53 || dest_port != DNS_LOCAL_PORT || dns_done) {
        return;
    }
    int copy = MIN(len, (int)sizeof(dns_rx));
    kmemcpy(dns_rx, packet, (size_t)copy);
    dns_rx_len = copy;
    dns_done = true;
}

static uint16_t tcp_checksum(uint32_t source, uint32_t dest,
                             const uint8_t *packet, int len) {
    uint8_t pseudo[12];
    write_be32(pseudo, source);
    write_be32(pseudo + 4, dest);
    pseudo[8] = 0;
    pseudo[9] = IP_PROTO_TCP;
    write_be16(pseudo + 10, (uint16_t)len);
    uint32_t sum = net_checksum_add(pseudo, sizeof(pseudo), 0);
    sum = net_checksum_add(packet, len, sum);
    return net_checksum_finish(sum);
}

static bool tcp_send(uint8_t flags, const uint8_t *payload, int len) {
    if (!tcp.active || len < 0 || len + 20 > NET_FRAME_MAX - 20) {
        return false;
    }
    uint8_t packet[NET_FRAME_MAX];
    kmemset(packet, 0, 20);
    write_be16(packet + 0, tcp.local_port);
    write_be16(packet + 2, tcp.remote_port);
    write_be32(packet + 4, tcp.send_seq);
    write_be32(packet + 8, tcp.recv_ack);
    packet[12] = 5 << 4;
    packet[13] = flags;
    write_be16(packet + 14, 64240);
    if (payload && len) {
        kmemcpy(packet + 20, payload, (size_t)len);
    }
    uint16_t sum = tcp_checksum(net_ip, tcp.remote_ip, packet, 20 + len);
    write_be16(packet + 16, sum);
    if (!net_send_ipv4(tcp.remote_ip, IP_PROTO_TCP, packet, 20 + len)) {
        return false;
    }
    tcp.send_seq += (uint32_t)len;
    if (flags & 0x02) {
        tcp.send_seq++;
    }
    if (flags & 0x01) {
        tcp.send_seq++;
    }
    return true;
}

static bool tcp_connect(uint32_t remote_ip, uint16_t remote_port) {
    kmemset(&tcp, 0, sizeof(tcp));
    tcp_rx_len = 0;
    tcp_rx_pos = 0;
    tcp.active = true;
    tcp.remote_ip = remote_ip;
    tcp.remote_port = remote_port;
    tcp.local_port = (uint16_t)(40000u + (tcp_port_counter++ % 2000u));
    tcp.send_seq = 0x13572468u + pit_ticks() * 7919u;
    tcp.recv_ack = 0;

    uint8_t mac[ETH_ALEN];
    if (!arp_resolve(net_next_hop(remote_ip), mac, 1000)) {
        net_set_error("Could not resolve the gateway MAC address");
        tcp.active = false;
        return false;
    }

    for (int attempt = 0; attempt < 3; attempt++) {
        if (!tcp_send(0x02, NULLPTR, 0)) {
            net_set_error("Could not send the TCP SYN packet");
            tcp.active = false;
            return false;
        }
        uint32_t start = pit_ticks();
        while ((uint32_t)(pit_ticks() - start) < 100u) {
            net_poll();
            if (tcp.established || tcp.reset) {
                break;
            }
        }
        if (tcp.established) {
            kprintf("[net] TCP connected to %u.%u.%u.%u:%u\n",
                    (remote_ip >> 24) & 0xFF, (remote_ip >> 16) & 0xFF,
                    (remote_ip >> 8) & 0xFF, remote_ip & 0xFF, remote_port);
            return true;
        }
        if (tcp.reset) {
            net_set_error("The remote server reset the TCP connection");
            tcp.active = false;
            return false;
        }
    }
    net_set_error("TCP connection timed out");
    tcp.active = false;
    return false;
}

static void tcp_rx_compact(void) {
    if (tcp_rx_pos <= 0) {
        return;
    }
    if (tcp_rx_pos >= tcp_rx_len) {
        tcp_rx_pos = 0;
        tcp_rx_len = 0;
        return;
    }
    int remain = tcp_rx_len - tcp_rx_pos;
    kmemmove(tcp_rx, tcp_rx + tcp_rx_pos, (size_t)remain);
    tcp_rx_pos = 0;
    tcp_rx_len = remain;
}

static int tcp_read_stream(uint8_t *out, int max, uint32_t timeout_ms) {
    if (!tcp.active) {
        net_set_error("The TCP connection is not open");
        return -1;
    }
    uint32_t start = pit_ticks();
    for (;;) {
        if (tcp_rx_pos < tcp_rx_len) {
            int avail = tcp_rx_len - tcp_rx_pos;
            int copy = MIN(avail, max);
            kmemcpy(out, tcp_rx + tcp_rx_pos, (size_t)copy);
            tcp_rx_pos += copy;
            if (tcp_rx_pos >= tcp_rx_len) {
                tcp_rx_pos = 0;
                tcp_rx_len = 0;
            } else if (tcp_rx_pos > 4096) {
                tcp_rx_compact();
            }
            return copy;
        }
        if (tcp.reset) {
            net_set_error("The remote server reset the connection");
            return -1;
        }
        if (tcp.fin_recv) {
            return 0;
        }
        if ((uint32_t)(pit_ticks() - start) >= timeout_ms) {
            net_set_error("The connection timed out");
            return -1;
        }
        net_poll();
        pit_sleep_ms(1);
    }
}

static int tcp_write_stream(const uint8_t *data, int len) {
    int total = len;
    while (len > 0) {
        int chunk = len > 1400 ? 1400 : len;
        if (!tcp_send(0x18, data, chunk)) {
            net_set_error("Could not send data over TCP");
            return -1;
        }
        data += chunk;
        len -= chunk;
    }
    return total;
}

static void net_handle_tcp(const uint8_t *packet, int len) {
    if (len < 20 || !tcp.active) {
        return;
    }
    uint16_t source_port = read_be16(packet + 0);
    uint16_t dest_port = read_be16(packet + 2);
    if (source_port != tcp.remote_port || dest_port != tcp.local_port) {
        return;
    }
    int header_len = (packet[12] >> 4) * 4;
    if (header_len < 20 || header_len > len) {
        return;
    }
    uint32_t seq = read_be32(packet + 4);
    uint8_t flags = packet[13];
    const uint8_t *payload = packet + header_len;
    int payload_len = len - header_len;
    uint32_t payload_end = seq + (uint32_t)payload_len;

    if (flags & 0x04) {
        tcp.reset = true;
        return;
    }
    if ((flags & 0x12) == 0x12 && !tcp.established) {
        tcp.recv_ack = seq + 1;
        tcp.established = true;
        tcp_send(0x10, NULLPTR, 0);
        return;
    }
    if (!tcp.established) {
        return;
    }
    if (payload_len > 0) {
        if ((int)sizeof(tcp_rx) - 1 - tcp_rx_len < payload_len) {
            tcp_rx_compact();
        }
        if (seq == tcp.recv_ack) {
            int space = (int)sizeof(tcp_rx) - 1 - tcp_rx_len;
            int copy = MIN(space, payload_len);
            if (copy > 0) {
                kmemcpy(tcp_rx + tcp_rx_len, payload, (size_t)copy);
                tcp_rx_len += copy;
            }
            tcp.recv_ack += (uint32_t)payload_len;
            tcp_send(0x10, NULLPTR, 0);
        } else if (seq < tcp.recv_ack && payload_end > tcp.recv_ack) {
            uint32_t overlap = tcp.recv_ack - seq;
            int copy = (int)(payload_len - (int)overlap);
            int space = (int)sizeof(tcp_rx) - 1 - tcp_rx_len;
            if (copy > space) {
                copy = space;
            }
            if (copy > 0) {
                kmemcpy(tcp_rx + tcp_rx_len, payload + overlap, (size_t)copy);
                tcp_rx_len += copy;
            }
            tcp.recv_ack = payload_end;
            tcp_send(0x10, NULLPTR, 0);
        } else {
            tcp_send(0x10, NULLPTR, 0);
        }
    }
    if (flags & 0x01) {
        uint32_t fin_at = seq + (uint32_t)payload_len;
        if (fin_at == tcp.recv_ack) {
            tcp.recv_ack++;
            tcp.fin_recv = true;
            tcp_send(0x10, NULLPTR, 0);
        }
    }
}

static void net_handle_ipv4(const uint8_t *packet, int len) {
    if (len < 20 || (packet[0] >> 4) != 4) {
        return;
    }
    int header_len = (packet[0] & 0x0F) * 4;
    if (header_len < 20 || header_len > len) {
        return;
    }
    int total = read_be16(packet + 2);
    if (total < header_len || total > len) {
        return;
    }
    if (net_checksum(packet, header_len) != 0) {
        return;
    }
    uint32_t dest = read_be32(packet + 16);
    if (dest != net_ip && dest != 0xFFFFFFFFu) {
        return;
    }
    if (packet[9] == IP_PROTO_UDP && total >= header_len + 8) {
        uint16_t source_port = read_be16(packet + header_len);
        uint16_t dest_port = read_be16(packet + header_len + 2);
        uint16_t udp_len = read_be16(packet + header_len + 4);
        if (udp_len >= 8 && header_len + udp_len <= total) {
            net_handle_dns(packet + header_len + 8, udp_len - 8, source_port, dest_port);
        }
    } else if (packet[9] == IP_PROTO_TCP) {
        net_handle_tcp(packet + header_len, total - header_len);
    }
}

static void net_poll(void) {
    if (!net_up || net_in_poll) {
        return;
    }
    net_in_poll = true;
    uint8_t frame[NET_FRAME_MAX];
    for (;;) {
        int len = e1000_receive(frame, sizeof(frame));
        if (len < ETH_HEADER_LEN) {
            break;
        }
        uint16_t type = read_be16(frame + 12);
        if (type == ETH_TYPE_ARP) {
            net_handle_arp(frame + ETH_HEADER_LEN, len - ETH_HEADER_LEN);
        } else if (type == ETH_TYPE_IP) {
            net_handle_ipv4(frame + ETH_HEADER_LEN, len - ETH_HEADER_LEN);
        }
    }
    net_in_poll = false;
}

static bool http_ci_prefix(const char *text, const char *prefix) {
    while (*prefix) {
        char a = *text++;
        char b = *prefix++;
        if (a >= 'A' && a <= 'Z') {
            a = (char)(a + ('a' - 'A'));
        }
        if (b >= 'A' && b <= 'Z') {
            b = (char)(b + ('a' - 'A'));
        }
        if (!a || a != b) {
            return false;
        }
    }
    return true;
}

static int http_parse_status(const char *response) {
    if (!http_ci_prefix(response, "HTTP/")) {
        return 0;
    }
    const char *space = kstrchr(response, ' ');
    if (!space) {
        return 0;
    }
    return katoi(space + 1);
}

static const char *http_header(const char *headers, const char *name,
                               char *value, size_t value_size) {
    const char *line = headers;
    size_t name_len = kstrlen(name);
    while (line && *line) {
        const char *end = kstrstr(line, "\r\n");
        if (!end) {
            end = line + kstrlen(line);
        }
        if ((size_t)(end - line) >= name_len &&
            http_ci_prefix(line, name) && line[name_len] == ':') {
            const char *v = line + name_len + 1;
            while (*v == ' ' || *v == '\t') {
                v++;
            }
            size_t n = 0;
            while (v + n < end && n + 1 < value_size) {
                value[n] = v[n];
                n++;
            }
            value[n] = 0;
            while (n && (value[n - 1] == ' ' || value[n - 1] == '\t')) {
                value[--n] = 0;
            }
            return value;
        }
        line = *end ? end + 2 : end;
    }
    return NULLPTR;
}

static int http_decode_chunked(const char *body, int len, char *out, uint32_t max_out) {
    if (!max_out) {
        return -1;
    }
    const char *p = body;
    const char *end = body + len;
    uint32_t total = 0;
    uint32_t capacity = max_out - 1;
    while (p < end && total < capacity) {
        uint32_t size = 0;
        bool digits = false;
        while (p < end && *p != '\r' && *p != '\n') {
            uint32_t digit;
            if (*p >= '0' && *p <= '9') {
                digit = (uint32_t)(*p - '0');
            } else if (*p >= 'a' && *p <= 'f') {
                digit = (uint32_t)(*p - 'a' + 10);
            } else if (*p >= 'A' && *p <= 'F') {
                digit = (uint32_t)(*p - 'A' + 10);
            } else {
                break;
            }
            size = size * 16u + digit;
            digits = true;
            p++;
        }
        while (p < end && *p != '\n') {
            p++;
        }
        if (p < end) {
            p++;
        }
        if (!digits || size == 0) {
            break;
        }
        int available = (int)(end - p);
        int take = MIN((int)size, available);
        int space = (int)(capacity - total);
        if (take > space) {
            take = space;
        }
        if (take > 0) {
            kmemcpy(out + total, p, (size_t)take);
            total += (uint32_t)take;
        }
        p += size;
        if (p < end && *p == '\r') {
            p++;
        }
        if (p < end && *p == '\n') {
            p++;
        }
    }
    out[total] = 0;
    return (int)total;
}

static int http_extract(const char *response, int response_len,
                        char *out, uint32_t max_out,
                        char *location, size_t location_size,
                        int *status_out) {
    const char *header_end = kstrstr(response, "\r\n\r\n");
    if (!header_end) {
        net_set_error("The server returned an incomplete HTTP response");
        return -1;
    }
    int header_len = (int)(header_end - response) + 4;
    if (header_len > response_len) {
        net_set_error("The HTTP response was malformed");
        return -1;
    }
    int status = http_parse_status(response);
    if (status_out) {
        *status_out = status;
    }
    if (location && location_size) {
        location[0] = 0;
        http_header(response, "Location", location, location_size);
    }
    const char *body = response + header_len;
    int body_len = response_len - header_len;
    char value[128];
    bool chunked = false;
    int content_length = -1;
    if (http_header(response, "Transfer-Encoding", value, sizeof(value)) &&
        http_ci_prefix(value, "chunked")) {
        chunked = true;
    }
    if (http_header(response, "Content-Length", value, sizeof(value))) {
        content_length = katoi(value);
    }
    if (chunked) {
        return http_decode_chunked(body, body_len, out, max_out);
    }
    if (!max_out) {
        return -1;
    }
    int copy = body_len;
    if (content_length >= 0 && content_length < copy) {
        copy = content_length;
    }
    if (copy < 0) {
        copy = 0;
    }
    if ((uint32_t)copy >= max_out) {
        copy = (int)(max_out - 1);
    }
    kmemcpy(out, body, (size_t)copy);
    out[copy] = 0;
    return copy;
}

static bool http_complete(const char *response, int len) {
    const char *header_end = kstrstr(response, "\r\n\r\n");
    if (!header_end) {
        return false;
    }
    int body_len = len - (int)(header_end - response) - 4;
    char value[128];
    if (http_header(response, "Transfer-Encoding", value, sizeof(value)) &&
        http_ci_prefix(value, "chunked")) {
        const char *chunk = header_end + 4;
        if (body_len >= 5 && kstrncmp(chunk, "0\r\n\r\n", 5) == 0) {
            return true;
        }
        return kstrstr(chunk, "\r\n0\r\n\r\n") != NULLPTR;
    }
    if (http_header(response, "Content-Length", value, sizeof(value))) {
        int expected = katoi(value);
        return expected >= 0 && body_len >= expected;
    }
    return false;
}

static uint64_t tls_rdtsc(void) {
    uint32_t lo;
    uint32_t hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

static bool tls_cpu_has_rdrand(void) {
    uint32_t eax;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;
    __asm__ volatile("cpuid"
                     : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                     : "a"(1u));
    UNUSED(eax);
    UNUSED(ebx);
    UNUSED(edx);
    return (ecx & (1u << 30)) != 0;
}

static bool tls_rdrand32(uint32_t *value) {
    uint32_t result;
    unsigned char ok;
    __asm__ volatile("rdrand %0\n\tsetc %1" : "=r"(result), "=qm"(ok));
    if (ok) {
        *value = result;
    }
    return ok != 0;
}

static void tls_fill_entropy(uint8_t *out, size_t len) {
    uint64_t tsc = tls_rdtsc();
    uint32_t state = (uint32_t)tsc ^ (uint32_t)(tsc >> 32) ^
                     pit_ticks() ^ rtc_unix_time() ^ 0xA5A5A5A5u;
    bool have_rdrand = tls_cpu_has_rdrand();
    for (size_t i = 0; i < len; i += 4) {
        uint32_t value;
        if (!have_rdrand || !tls_rdrand32(&value)) {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            value = state ^ pit_ticks() ^ (uint32_t)tsc;
        }
        for (size_t j = 0; j < 4 && i + j < len; j++) {
            out[i + j] = (uint8_t)(value >> (j * 8));
        }
        tsc = tsc * 6364136223846793005ULL + 1u;
    }
}

static int tls_read_cb(void *ctx, unsigned char *data, size_t len) {
    UNUSED(ctx);
    return tcp_read_stream(data, (int)len, TLS_IO_TIMEOUT_MS);
}

static int tls_write_cb(void *ctx, const unsigned char *data, size_t len) {
    UNUSED(ctx);
    return tcp_write_stream(data, (int)len);
}

static void tls_set_error(unsigned err) {
    if (net_error[0]) {
        return;
    }
    if (err >= BR_ERR_RECV_FATAL_ALERT && err < BR_ERR_SEND_FATAL_ALERT) {
        unsigned alert = err - BR_ERR_RECV_FATAL_ALERT;
        switch (alert) {
        case 40:
            net_set_error("The server rejected the TLS handshake");
            break;
        case 70:
        case 71:
            net_set_error("The server does not support TLS 1.2");
            break;
        case 112:
            net_set_error("The server did not recognize this host name");
            break;
        case 113:
        case 116:
            net_set_error("The server rejected the TLS certificate exchange");
            break;
        default: {
            char message[96];
            snprintf(message, sizeof(message),
                     "The server sent a TLS alert (%u)", alert);
            net_set_error(message);
            break;
        }
        }
        return;
    }
    switch (err) {
    case BR_ERR_OK:
        return;
    case BR_ERR_BAD_PARAM:
        net_set_error("TLS parameter error");
        break;
    case BR_ERR_BAD_STATE:
        net_set_error("TLS internal state error");
        break;
    case BR_ERR_UNSUPPORTED_VERSION:
    case BR_ERR_BAD_VERSION:
        net_set_error("The server does not support TLS 1.2");
        break;
    case BR_ERR_BAD_LENGTH:
    case BR_ERR_TOO_LARGE:
        net_set_error("The server sent a malformed TLS record");
        break;
    case BR_ERR_BAD_MAC:
        net_set_error("TLS record authentication failed");
        break;
    case BR_ERR_NO_RANDOM:
        net_set_error("No entropy is available for TLS");
        break;
    case BR_ERR_UNKNOWN_TYPE:
    case BR_ERR_UNEXPECTED:
        net_set_error("The server sent an unexpected TLS message");
        break;
    case BR_ERR_BAD_CIPHER_SUITE:
        net_set_error("No shared TLS cipher suite");
        break;
    case BR_ERR_BAD_HANDSHAKE:
        net_set_error("The TLS handshake failed");
        break;
    case BR_ERR_X509_NOT_TRUSTED:
        net_set_error("The server certificate is not trusted");
        break;
    case BR_ERR_X509_BAD_SERVER_NAME:
        net_set_error("The certificate does not match the server name");
        break;
    case BR_ERR_X509_EXPIRED:
        net_set_error("The server certificate is expired or not yet valid");
        break;
    case BR_ERR_X509_INVALID_VALUE:
    case BR_ERR_X509_TRUNCATED:
        net_set_error("The server certificate is invalid");
        break;
    case BR_ERR_IO:
        net_set_error("The TLS connection was interrupted");
        break;
    default:
        net_set_error("TLS connection failed");
        break;
    }
}

int net_https_get(const char *host, uint16_t port, const char *path,
                  char *out, uint32_t max_out,
                  char *location, size_t location_size,
                  int *status_out) {
    if (!net_up) {
        net_set_error("No network adapter is available");
        return -1;
    }
    if (!out || !max_out) {
        net_set_error("The HTTPS response buffer is invalid");
        return -1;
    }
    if (status_out) {
        *status_out = 0;
    }
    if (location && location_size) {
        location[0] = 0;
    }
    net_error[0] = 0;
    uint32_t ip;
    if (!net_dns_resolve(host, &ip)) {
        return -1;
    }
    if (!tcp_connect(ip, port)) {
        return -1;
    }
    tcp_rx_len = 0;
    tcp_rx_pos = 0;

    br_ssl_client_init_full(&tls_client, &tls_x509,
                            bordify_trust_anchors,
                            bordify_trust_anchor_count);
    br_ssl_engine_set_buffer(&tls_client.eng, tls_iobuf,
                             sizeof(tls_iobuf), 1);
    uint32_t now = rtc_unix_time();
    if (now < 946684800u) {
        now = 1767225600u;
    }
    br_x509_minimal_set_time(&tls_x509, now / 86400u + 719528u,
                             (now % 86400u) / 60u);
    uint8_t entropy[32];
    tls_fill_entropy(entropy, sizeof(entropy));
    br_ssl_engine_inject_entropy(&tls_client.eng, entropy, sizeof(entropy));
    if (!br_ssl_client_reset(&tls_client, host, 0)) {
        tls_set_error(br_ssl_engine_last_error(&tls_client.eng));
        tcp.active = false;
        return -1;
    }
    br_sslio_init(&tls_io, &tls_client.eng, tls_read_cb, NULLPTR,
                  tls_write_cb, NULLPTR);

    char host_header[192];
    if (port == 443) {
        kstrncpy(host_header, host, sizeof(host_header) - 1);
        host_header[sizeof(host_header) - 1] = 0;
    } else {
        snprintf(host_header, sizeof(host_header), "%s:%u", host, port);
    }
    char request[640];
    int request_len = snprintf(request, sizeof(request),
                               "GET %s HTTP/1.0\r\n"
                               "Host: %s\r\n"
                               "User-Agent: Bordify/1.0\r\n"
                               "Accept: text/html,text/plain\r\n"
                               "Accept-Encoding: identity\r\n"
                               "Connection: close\r\n\r\n",
                               path, host_header);
    if (request_len <= 0 || request_len >= (int)sizeof(request)) {
        net_set_error("The request URL was too long");
        tcp.active = false;
        return -1;
    }
    if (br_sslio_write_all(&tls_io, request, (size_t)request_len) < 0 ||
        br_sslio_flush(&tls_io) < 0) {
        tls_set_error(br_ssl_engine_last_error(&tls_client.eng));
        tcp.active = false;
        return -1;
    }

    int total = 0;
    while (total < TCP_RX_MAX - 1) {
        int n = br_sslio_read(&tls_io, tls_rx + total,
                              (size_t)(TCP_RX_MAX - 1 - total));
        if (n < 0) {
            unsigned err = br_ssl_engine_last_error(&tls_client.eng);
            tls_rx[total] = 0;
            if (!total || !http_complete(tls_rx, total)) {
                tls_set_error(err);
                tcp.active = false;
                return -1;
            }
            break;
        }
        if (n == 0) {
            break;
        }
        total += n;
        tls_rx[total] = 0;
        if (http_complete(tls_rx, total)) {
            break;
        }
    }
    tls_rx[total] = 0;
    if (total <= 0) {
        tls_set_error(br_ssl_engine_last_error(&tls_client.eng));
        tcp.active = false;
        return -1;
    }
    if (total >= TCP_RX_MAX - 1) {
        net_set_error("The HTTPS response exceeds the 64 KiB page limit");
        tcp.active = false;
        return -1;
    }
    int status = 0;
    int result = http_extract(tls_rx, total, out, max_out,
                              location, location_size, &status);
    kprintf("[net] HTTPS %d, %d response bytes\n", status, total);
    if (status_out) {
        *status_out = status;
    }
    br_sslio_close(&tls_io);
    tcp.active = false;
    return result;
}

void net_init(void) {
    net_up = false;
    net_error[0] = 0;
    kprintf("[net] probing Intel e1000\n");
    if (!e1000_init()) {
        kprintf("[net] %s\n", net_error);
        return;
    }
}

bool net_ready(void) {
    return net_up;
}

const char *net_status(void) {
    return net_up ? "e1000 ready at 10.0.2.15" : "no network adapter";
}

const char *net_last_error(void) {
    return net_error[0] ? net_error : "Network request failed";
}

int net_http_get(const char *host, uint16_t port, const char *path,
                 char *out, uint32_t max_out,
                 char *location, size_t location_size,
                 int *status_out) {
    if (!net_up) {
        net_set_error("No network adapter is available");
        return -1;
    }
    if (status_out) {
        *status_out = 0;
    }
    if (location && location_size) {
        location[0] = 0;
    }
    net_error[0] = 0;
    uint32_t ip;
    if (!net_dns_resolve(host, &ip)) {
        return -1;
    }
    if (!tcp_connect(ip, port)) {
        return -1;
    }
    tcp_rx_len = 0;
    tcp_rx_pos = 0;

    char host_header[192];
    if (port == 80) {
        kstrncpy(host_header, host, sizeof(host_header) - 1);
        host_header[sizeof(host_header) - 1] = 0;
    } else {
        snprintf(host_header, sizeof(host_header), "%s:%u", host, port);
    }
    char request[640];
    int request_len = snprintf(request, sizeof(request),
                               "GET %s HTTP/1.0\r\n"
                               "Host: %s\r\n"
                               "User-Agent: Bordify/1.0\r\n"
                               "Accept: text/html,text/plain\r\n"
                               "Accept-Encoding: identity\r\n"
                               "Connection: close\r\n\r\n",
                               path, host_header);
    if (request_len <= 0 || request_len >= (int)sizeof(request)) {
        net_set_error("The request URL was too long");
        tcp.active = false;
        return -1;
    }
    if (!tcp_send(0x18, (const uint8_t *)request, request_len)) {
        net_set_error("Could not send the HTTP request");
        tcp.active = false;
        return -1;
    }

    tcp_rx_len = 0;
    uint32_t start = pit_ticks();
    while ((uint32_t)(pit_ticks() - start) < 800u) {
        net_poll();
        if (tcp.reset) {
            net_set_error("The remote server closed the connection unexpectedly");
            tcp.active = false;
            return -1;
        }
        if (tcp_rx_len > 0) {
            tcp_rx[tcp_rx_len] = 0;
            if (tcp.fin_recv || http_complete((const char *)tcp_rx, tcp_rx_len)) {
                break;
            }
        }
        if (tcp.fin_recv) {
            break;
        }
        pit_sleep_ms(1);
    }
    if (!tcp.fin_recv) {
        tcp_send(0x11, NULLPTR, 0);
    }
    tcp.active = false;
    if (tcp_rx_len <= 0) {
        net_set_error("The HTTP request timed out");
        return -1;
    }
    tcp_rx[tcp_rx_len] = 0;
    int status = 0;
    int result = http_extract((const char *)tcp_rx, tcp_rx_len, out, max_out,
                              location, location_size, &status);
    kprintf("[net] HTTP %d, %d response bytes\n", status, tcp_rx_len);
    if (status_out) {
        *status_out = status;
    }
    return result;
}
