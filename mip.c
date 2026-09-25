/*
 * mip.c - MIP header and MIP-ARP message encoding, plus the MIP-ARP cache.
 *
 * Everything marked TODO is yours to write. The function signatures and
 * comments describe exactly what each function must do.
 */

#include <stdio.h>
#include <string.h>
#include "common.h"
#include <arpa/inet.h>

/* The MIP-ARP cache: one slot per possible MIP address (0..255).
 * Indexing by address makes lookup O(1) and needs no search code. */
static struct arp_entry arp_cache[256];

/**
 * Pack a MIP header into its 4-byte wire format.
 * hdr: pointer to the unpacked header to encode.
 * buf: output buffer, must be at least MIP_HDR_LEN (4) bytes.
 *
 * Wire layout (most significant bit first, network byte order):
 *   | dst 8 | src 8 | ttl 4 | sdu_len 9 | sdu_type 3 |
 *
 * Global variables: none.
 * Returns nothing. Fields wider than their wire width are truncated.
 */
void mip_hdr_pack(const struct mip_hdr *hdr, uint8_t *buf)
{
	uint32_t word;

	word = (uint32_t)hdr->dst << 24;
	word |= (uint32_t)hdr->src << 16;
	word |= ((uint32_t)hdr->ttl & 0xF) << 12;
	word |= ((uint32_t)hdr->sdu_len & 0x1FF) << 3;
	word |= (uint32_t)hdr->sdu_type & 0x7;

	word = htonl(word);
	memcpy(buf, &word, MIP_HDR_LEN);
}

/**
 * Unpack a 4-byte wire-format MIP header.
 * buf: input buffer holding at least MIP_HDR_LEN bytes.
 * hdr: output struct filled with the decoded fields.
 *
 * Global variables: none.
 * Returns nothing. Validating the fields is the caller's job.
 */
void mip_hdr_unpack(const uint8_t *buf, struct mip_hdr *hdr)
{
	uint32_t word;

	memcpy(&word, buf, MIP_HDR_LEN);
	word = ntohl(word);

	hdr->dst = word >> 24;
	hdr->src = word >> 16;
	hdr->ttl = (word >> 12) & 0xF;
	hdr->sdu_len = (word >> 3) & 0x1FF;
	hdr->sdu_type = word & 0x7;
}

/**
 * Encode a MIP-ARP message (spec section 6.1).
 * type: MIP_ARP_REQUEST or MIP_ARP_RESPONSE (1 bit on the wire).
 * addr: MIP address being looked up (request) or that matched (response).
 * buf:  output buffer, at least MIP_ARP_LEN (4) bytes.
 *
 * Layout: | type 1 | addr 8 | 23 zero bits |
 * Note: addr is NOT byte-aligned - it straddles the first two bytes.
 *
 * Global variables: none.
 * Returns nothing.
 */
void mip_arp_pack(uint8_t type, uint8_t addr, uint8_t *buf)
{
	/* TODO(you): same technique as mip_hdr_pack(). */
	(void)type;
	(void)addr;
	memset(buf, 0, MIP_ARP_LEN);
}

/**
 * Decode a MIP-ARP message.
 * buf:  input buffer, at least MIP_ARP_LEN bytes.
 * type: output, set to MIP_ARP_REQUEST or MIP_ARP_RESPONSE.
 * addr: output, the MIP address carried in the message.
 *
 * Global variables: none.
 * Returns nothing.
 */
void mip_arp_unpack(const uint8_t *buf, uint8_t *type, uint8_t *addr)
{
	/* TODO(you) */
	(void)buf;
	*type = 0;
	*addr = 0;
}

/**
 * Empty the MIP-ARP cache.
 *
 * Global variables: arp_cache (all entries cleared).
 * Returns nothing.
 */
void arp_cache_init(void)
{
	memset(arp_cache, 0, sizeof(arp_cache));
}

/**
 * Add or overwrite the cache entry for a MIP address.
 * mip:     the neighbour's MIP address.
 * mac:     the neighbour's MAC address (ETH_ALEN bytes).
 * ifindex: local interface on which the neighbour was heard.
 *
 * Global variables: arp_cache (entry for mip is written).
 * Returns nothing. Overwriting an existing entry is intended: the newest
 * information wins.
 */
void arp_cache_insert(uint8_t mip, const uint8_t *mac, int ifindex)
{
	/* TODO(you): fill arp_cache[mip] and mark it valid. */
	(void)mip;
	(void)mac;
	(void)ifindex;
}

/**
 * Look up a MIP address in the cache.
 * mip: address to look up.
 *
 * Global variables: arp_cache (read only).
 * Returns a pointer to the entry, or NULL if there is no valid entry.
 */
const struct arp_entry *arp_cache_lookup(uint8_t mip)
{
	/* TODO(you) */
	(void)mip;
	return NULL;
}

/**
 * Print every valid cache entry as "MIP -> MAC (ifindex)".
 * Used in debug mode, as required by the assignment.
 *
 * Global variables: arp_cache (read only).
 * Returns nothing.
 */
void arp_cache_print(void)
{
	/* TODO(you): loop over all 256 slots, print the valid ones.
	 * print_mac() from link.c prints a MAC address for you. */
	printf("[ARP cache] (not implemented yet)\n");
}
