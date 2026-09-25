#ifndef COMMON_H
#define COMMON_H

/*
 * Shared definitions for mipd, ping_server and ping_client.
 *
 * Files:
 *   mip.c   - MIP header / MIP-ARP packing and the MIP-ARP cache   (mostly TODO)
 *   link.c  - raw Ethernet socket + interface discovery            (done)
 *   ipc.c   - UNIX domain socket helpers                           (done)
 *   mipd.c  - the daemon: event loop (done) + protocol logic       (TODO)
 *   ping_client.c / ping_server.c                                  (TODO)
 */

#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>
#include <net/if.h>          /* IFNAMSIZ */
#include <net/ethernet.h>    /* ETH_ALEN, struct ether_header */

/* ---------- Constants from the assignment / MIP spec ---------- */

#define ETH_P_MIP        0x88B5 /* EtherType identifying MIP frames */
#define MIP_BROADCAST    0xFF   /* MIP broadcast address (spec sec. 3) */
#define MIP_HDR_LEN      4      /* 8+8+4+9+3 bits = 32 bits */

#define MIP_SDU_ARP      0x01   /* SDU type: MIP-ARP (spec appendix A) */
#define MIP_SDU_PING     0x02   /* SDU type: Ping    (spec appendix A) */

#define MIP_ARP_REQUEST  0x00   /* MIP-ARP "Type" bit: request  */
#define MIP_ARP_RESPONSE 0x01   /* MIP-ARP "Type" bit: response */
#define MIP_ARP_LEN      4      /* MIP-ARP message is exactly 32 bits */

/* TTL used for datagrams we originate. No forwarding exists yet, so 1 is
 * enough; the spec recommends 1 for broadcasts anyway. The field is 4 bits,
 * so the maximum legal value is 15. */
#define MIP_TTL_DEFAULT  1

/* The SDU length field is 9 bits counting 32-bit words: max 511 words. */
#define MIP_MAX_SDU_LEN  (511 * 4)

/* Largest Ethernet frame we build/receive (header + payload, no FCS). */
#define MAX_FRAME_LEN    1514

/* Largest message on the UNIX socket: 1 address byte + SDU. */
#define MAX_IPC_LEN      (1 + MIP_MAX_SDU_LEN)

#define MAX_IFS          8      /* max number of Ethernet interfaces we track */

/* ---------- Data structures ---------- */

/* Unpacked (host-friendly) MIP header. Use mip_hdr_pack()/mip_hdr_unpack()
 * to convert to/from the 4-byte wire format. */
struct mip_hdr {
	uint8_t  dst;      /* destination MIP address */
	uint8_t  src;      /* source MIP address */
	uint8_t  ttl;      /* 4 bits on the wire */
	uint16_t sdu_len;  /* 9 bits on the wire, in 32-bit WORDS (not bytes!) */
	uint8_t  sdu_type; /* 3 bits on the wire */
};

/* One local Ethernet interface. */
struct iface {
	int     ifindex;             /* kernel interface index (for sendto) */
	uint8_t mac[ETH_ALEN];       /* our MAC address on this interface */
	char    name[IFNAMSIZ];      /* e.g. "A-eth0" */
};

/* One MIP-ARP cache entry. The cache is simply an array indexed by MIP
 * address (only 256 possible addresses), so no searching is needed. */
struct arp_entry {
	int     valid;               /* 0 = empty slot */
	uint8_t mac[ETH_ALEN];       /* neighbour's MAC address */
	int     ifindex;             /* local interface the neighbour is reached on */
};

/* ---------- mip.c ---------- */
void mip_hdr_pack(const struct mip_hdr *hdr, uint8_t *buf);
void mip_hdr_unpack(const uint8_t *buf, struct mip_hdr *hdr);
void mip_arp_pack(uint8_t type, uint8_t addr, uint8_t *buf);
void mip_arp_unpack(const uint8_t *buf, uint8_t *type, uint8_t *addr);

void arp_cache_init(void);
void arp_cache_insert(uint8_t mip, const uint8_t *mac, int ifindex);
const struct arp_entry *arp_cache_lookup(uint8_t mip);
void arp_cache_print(void);

/* ---------- link.c ---------- */
int     raw_socket_create(void);
int     get_interfaces(struct iface *ifs, int max);
int     send_frame(int sock, const struct iface *ifc, const uint8_t *dst_mac,
		   const uint8_t *payload, size_t len);
ssize_t recv_frame(int sock, uint8_t *buf, size_t len, int *ifindex);
void    print_mac(const uint8_t *mac);

/* ---------- ipc.c ---------- */
int unix_listen(const char *path);
int unix_connect(const char *path);

#endif /* COMMON_H */
