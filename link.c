/*
 * link.c - Ethernet (link layer) access through an AF_PACKET raw socket.
 *
 * This file is complete: it is plumbing, not protocol logic. Read it
 * anyway - you must be able to explain it in a spot check.
 */

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <ifaddrs.h>
#include <arpa/inet.h>          /* htons */
#include <sys/socket.h>
#include <linux/if_packet.h>    /* struct sockaddr_ll */
#include "common.h"

/**
 * Create the raw socket used for all MIP traffic.
 * The protocol argument ETH_P_MIP makes the kernel deliver only frames
 * with EtherType 0x88B5 to us, on all interfaces.
 *
 * Global variables: none.
 * Returns the socket fd, or -1 on error (errno set; needs root/mininet).
 */
int raw_socket_create(void)
{
	int sock = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_MIP));

	if (sock == -1)
		perror("socket(AF_PACKET)");
	return sock;
}

/**
 * Find all local Ethernet interfaces except loopback.
 * ifs: output array, filled with index, MAC and name of each interface.
 * max: capacity of ifs.
 *
 * Uses getifaddrs(): the AF_PACKET entries carry a sockaddr_ll holding the
 * interface index and hardware (MAC) address.
 *
 * Global variables: none.
 * Returns the number of interfaces found, or -1 on error.
 */
int get_interfaces(struct iface *ifs, int max)
{
	struct ifaddrs *list, *ifa;
	int n = 0;

	if (getifaddrs(&list) == -1) {
		perror("getifaddrs");
		return -1;
	}

	for (ifa = list; ifa && n < max; ifa = ifa->ifa_next) {
		struct sockaddr_ll *sll;

		if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_PACKET)
			continue;
		if (strcmp(ifa->ifa_name, "lo") == 0)
			continue;

		sll = (struct sockaddr_ll *)ifa->ifa_addr;
		ifs[n].ifindex = sll->sll_ifindex;
		memcpy(ifs[n].mac, sll->sll_addr, ETH_ALEN);
		strncpy(ifs[n].name, ifa->ifa_name, IFNAMSIZ - 1);
		ifs[n].name[IFNAMSIZ - 1] = '\0';
		n++;
	}

	freeifaddrs(list);
	return n;
}

/**
 * Send one Ethernet frame carrying a MIP PDU.
 * sock:    raw socket from raw_socket_create().
 * ifc:     interface to send on (source MAC is taken from here).
 * dst_mac: destination MAC (ff:ff:ff:ff:ff:ff for broadcast).
 * payload: the MIP PDU (MIP header + SDU).
 * len:     length of payload in bytes.
 *
 * Builds the 14-byte Ethernet header in front of the payload.
 *
 * Global variables: none.
 * Returns bytes sent, or -1 on error (also if the frame would be too big).
 */
int send_frame(int sock, const struct iface *ifc, const uint8_t *dst_mac,
	       const uint8_t *payload, size_t len)
{
	uint8_t frame[MAX_FRAME_LEN];
	struct ether_header *eth = (struct ether_header *)frame;
	struct sockaddr_ll addr;
	ssize_t rc;

	if (sizeof(*eth) + len > sizeof(frame)) {
		fprintf(stderr, "send_frame: payload too large (%zu)\n", len);
		return -1;
	}

	memcpy(eth->ether_dhost, dst_mac, ETH_ALEN);
	memcpy(eth->ether_shost, ifc->mac, ETH_ALEN);
	eth->ether_type = htons(ETH_P_MIP);
	memcpy(frame + sizeof(*eth), payload, len);

	/* sockaddr_ll tells the kernel which interface to transmit on. */
	memset(&addr, 0, sizeof(addr));
	addr.sll_family   = AF_PACKET;
	addr.sll_ifindex  = ifc->ifindex;
	addr.sll_halen    = ETH_ALEN;
	memcpy(addr.sll_addr, dst_mac, ETH_ALEN);

	rc = sendto(sock, frame, sizeof(*eth) + len, 0,
		    (struct sockaddr *)&addr, sizeof(addr));
	if (rc == -1)
		perror("sendto");
	return (int)rc;
}

/**
 * Receive one Ethernet frame from the raw socket.
 * sock:    raw socket.
 * buf:     output buffer for the whole frame (Ethernet header included).
 * len:     size of buf.
 * ifindex: output, index of the interface the frame arrived on.
 *
 * Frames we sent ourselves are also seen by packet sockets
 * (PACKET_OUTGOING); those are dropped here and reported as 0 bytes.
 *
 * Global variables: none.
 * Returns frame length, 0 for an ignored frame, or -1 on error.
 */
ssize_t recv_frame(int sock, uint8_t *buf, size_t len, int *ifindex)
{
	struct sockaddr_ll addr;
	socklen_t alen = sizeof(addr);
	ssize_t rc;

	rc = recvfrom(sock, buf, len, 0, (struct sockaddr *)&addr, &alen);
	if (rc == -1) {
		perror("recvfrom");
		return -1;
	}
	if (addr.sll_pkttype == PACKET_OUTGOING)
		return 0;

	*ifindex = addr.sll_ifindex;
	return rc;
}

/**
 * Print a MAC address as aa:bb:cc:dd:ee:ff (no newline).
 * mac: ETH_ALEN bytes.
 *
 * Global variables: none.
 * Returns nothing.
 */
void print_mac(const uint8_t *mac)
{
	printf("%02x:%02x:%02x:%02x:%02x:%02x",
	       mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}
