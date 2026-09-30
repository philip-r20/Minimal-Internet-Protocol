/*
 * mipd.c - the MIP daemon.
 *
 * Usage: mipd [-h] [-d] <socket_upper> <MIP address>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include "common.h"

#define MAX_EVENTS 8 /* max events handled per epoll_wait() call */ 

static int     debug; /* 1 if -d was given: log every packet */
static uint8_t my_mip; /* this host's MIP address */

static struct iface ifs[MAX_IFS]; /* local Ethernet interfaces */
static int     n_ifs; /* number of valid entries in ifs */

static int     raw_fd = -1; /* AF_PACKET socket for MIP frames */
static int     listen_fd = -1; /* UNIX listening socket */
static int     app_fd = -1; /* connected application, -1 if none */

/** 
 * A ping waiting for a MIP-ARP response before it can be sent. 
 * One slot is enough: only one app is connected and it sends one ping at a time. 
 */
static struct {
	int     active;                /* 1 while waiting for ARP */
	uint8_t dst;                   /* MIP address we are resolving */
	uint8_t sdu[MIP_MAX_SDU_LEN];  /* the (already padded) SDU */
	size_t  sdu_len;               /* SDU length in BYTES, multiple of 4 */
} pending;

/* Ethernet broadcast address, used for MIP-ARP requests */
static const uint8_t BCAST_MAC[ETH_ALEN] = {0xff,0xff,0xff,0xff,0xff,0xff}; 
/**
 * Print usage.
 * prog: program name (argv[0]).
 *
 * Global variables: none.
 * Returns nothing.
 */
static void usage(const char *prog)
{
	printf("Usage: %s [-h] [-d] <socket_upper> <MIP address>\n"
	       "  -h  print this help and exit\n"
	       "  -d  debug mode: log every packet and the MIP-ARP cache\n"
	       "  socket_upper  path of the UNIX socket for upper layers\n"
	       "  MIP address   this host's MIP address (0-254)\n", prog);
}

/**
 * Find a local interface by kernel index.
 * ifindex: kernel interface index to look for.
 *
 * Global variables: ifs, n_ifs (read only).
 * Returns a pointer into ifs, or NULL if unknown.
 */
static struct iface *find_iface(int ifindex)
{
	for (int i = 0; i < n_ifs; i++)
		if (ifs[i].ifindex == ifindex)
			return &ifs[i];
	return NULL;
}

/**
 * Build a MIP PDU (header + SDU) and send it in one Ethernet frame.
 * ifc: interface to send on.
 * dst_mac: destination MAC address.
 * hdr: MIP header to use.
 * sdu: SDU bytes.
 * sdu_len: SDU length in bytes (multiple of 4).
 *
 * In debug mode, logs MAC and MIP addresses and the ARP cache.
 *
 * Global variables: raw_fd, debug.
 * Returns 0 on success, -1 on error.
 */
static int send_pdu(struct iface *ifc, const uint8_t *dst_mac,
		    const struct mip_hdr *hdr, const uint8_t *sdu,
		    size_t sdu_len)
{
	if (sdu_len > MIP_MAX_SDU_LEN) {
		return -1;
	}
	uint8_t pdu[MIP_HDR_LEN + MIP_MAX_SDU_LEN];
	mip_hdr_pack(hdr, pdu);
	memcpy(pdu + MIP_HDR_LEN, sdu, sdu_len);

	if (send_frame(raw_fd, ifc, dst_mac, pdu, MIP_HDR_LEN + sdu_len) < 0) {
		return -1;
	}
	if (debug) {
		printf("[mipd] sent PDU src MAC ");
		print_mac(ifc->mac);
		printf(" dst MAC ");
		print_mac(dst_mac);
		printf("\n");
		printf("[mipd] src MIP %d ", hdr->src);
		printf("dst MIP %d\n", hdr->dst);
		arp_cache_print();
	}
	return 0;
}


/**
 * Broadcast a MIP-ARP request for a MIP address on ALL interfaces.
 * target: MIP address whose MAC we want.
 *
 * Global variables: ifs, n_ifs, my_mip (via send_pdu: raw_fd, debug).
 * Returns nothing.
 */
static void send_arp_request(uint8_t target)
{
	uint8_t sdu[MIP_ARP_LEN];
	struct mip_hdr hdr;

	mip_arp_pack(MIP_ARP_REQUEST, target, sdu);

	hdr.dst = MIP_BROADCAST;
	hdr.src = my_mip;
	hdr.ttl = MIP_TTL_DEFAULT;
	hdr.sdu_len = 1;
	hdr.sdu_type = MIP_SDU_ARP;

	for (int i = 0; i < n_ifs; i++) {
		send_pdu(&ifs[i], BCAST_MAC, &hdr, sdu, MIP_ARP_LEN);
	}
}

/**
 * Handle one message from the application. msg is [dst MIP][SDU]. 
 * The SDU is padded to a multiple of 4. If dst is in the ARP cache, it's sent at once,
 * otherwise it's stored in pending and an ARP request is broadcast.
 *
 * msg: the received bytes.
 * len: number of bytes received.
 *
 * Global variables: my_mip, pending (written if the destination's MAC is unknown).
 * Returns nothing. Too-large messages are dropped with an error message.
 */
static void handle_app_msg(const uint8_t *msg, size_t len)
{
	if (len - 1 > MIP_MAX_SDU_LEN) {
		fprintf(stderr, "mipd: message too large\n");
		return;
	}
	uint8_t dst = msg[0];
	size_t payload_len = len - 1;
	size_t sdu_len = ((payload_len + 3) / 4) * 4;
	uint8_t sdu[MIP_MAX_SDU_LEN];

	memset(sdu, 0, sizeof(sdu));
	memcpy(sdu, msg + 1, payload_len);

	const struct arp_entry *entry = arp_cache_lookup(dst);
	if (entry != NULL) {
		struct mip_hdr hdr;

		hdr.dst = dst;
		hdr.src = my_mip;
		hdr.ttl = MIP_TTL_DEFAULT;
		hdr.sdu_len = sdu_len / 4;
		hdr.sdu_type = MIP_SDU_PING;

		struct iface *ifc = find_iface(entry->ifindex);
		if (ifc == NULL) {
			fprintf(stderr, "mipd: no interface for ifindex %d\n", entry->ifindex);
			return;
		}
		if (send_pdu(ifc, entry->mac, &hdr, sdu, sdu_len) < 0) {
			fprintf(stderr, "mipd: send_pdu failed\n");
			return;
		} 
	} else {
		pending.active = 1;
		pending.dst = dst;
		memcpy(pending.sdu, sdu, sdu_len);
		pending.sdu_len = sdu_len;
		send_arp_request(dst);
	}
}

/**
 * Handle one received MIP PDU. Validates the header, then:
 * - ARP requests for our address: learn the sender and send an ARP response.
 * - ARP response: learn the sender and send the pending ping if it was waiting for that address.
 * - Ping: pass [src MIP][SDU] to the connected application, if any.
 *
 * src_mac: source MAC of the Ethernet frame.
 * ifindex: interface it arrived on.
 * pdu:     MIP PDU (starts with the MIP header).
 * len:     PDU length in bytes.
 *
 * Global variables: my_mip, app_fd, pending, debug (and the ARP cache).
 * Returns nothing. Incorrectly formated or foreign PDUs are silently dropped.
 */
static void handle_pdu(const uint8_t *src_mac, int ifindex,
		       const uint8_t *pdu, size_t len)
{
	if (len < MIP_HDR_LEN) {
		return;
	}
	struct mip_hdr hdr;
	mip_hdr_unpack(pdu, &hdr);
	if (hdr.sdu_len * 4 > len - MIP_HDR_LEN) {
		return;
	}
	if (hdr.dst != MIP_BROADCAST && hdr.dst != my_mip) {
		return;
	}
	if (debug) {
		printf("[mipd] received PDU\n");
		printf("[mipd] src MAC: ");
		print_mac(src_mac);
		printf("\n");
		printf("[mipd] ifindex: %d\n", ifindex);
		printf("[mipd] MIP src: %d\n", hdr.src);
		printf("[mipd] MIP dst: %d\n", hdr.dst);
		arp_cache_print();
	}

	const uint8_t *sdu = pdu + MIP_HDR_LEN;
	size_t sdu_bytes = hdr.sdu_len * 4;

	if (hdr.sdu_type == MIP_SDU_PING) {
		if (app_fd == -1) {
			return;
		}
		uint8_t out[MAX_IPC_LEN];
		out[0] = hdr.src;
		memcpy(out + 1, sdu, sdu_bytes);
		if (send(app_fd, out, sdu_bytes + 1, 0) < 0) {
			perror("send");
		}
	} else if (hdr.sdu_type == MIP_SDU_ARP) {
		if (sdu_bytes < MIP_ARP_LEN) {
			return;
		}
		uint8_t type, addr;
		mip_arp_unpack(sdu, &type, &addr);

		if (type == MIP_ARP_REQUEST && addr == my_mip) {
			arp_cache_insert(hdr.src, src_mac, ifindex);
			uint8_t buf[MIP_ARP_LEN];
			mip_arp_pack(MIP_ARP_RESPONSE, my_mip, buf);

			struct iface *ifc = find_iface(ifindex);
			if (ifc == NULL) {
				return;
			}

			struct mip_hdr out_hdr;
			out_hdr.dst = hdr.src;
			out_hdr.src = my_mip;
			out_hdr.ttl = MIP_TTL_DEFAULT;
			out_hdr.sdu_len = 1;
			out_hdr.sdu_type = MIP_SDU_ARP;

			send_pdu(ifc, src_mac, &out_hdr, buf, MIP_ARP_LEN);
		} else if (type == MIP_ARP_RESPONSE) {
			arp_cache_insert(hdr.src, src_mac, ifindex);
			if (pending.active && pending.dst == hdr.src) {
				struct iface *ifc = find_iface(ifindex);
				if (ifc == NULL) {
					return;
				}
				
				struct mip_hdr out_hdr;
				out_hdr.dst = hdr.src;
				out_hdr.src = my_mip;
				out_hdr.ttl = MIP_TTL_DEFAULT;
				out_hdr.sdu_len = pending.sdu_len / 4;
				out_hdr.sdu_type = MIP_SDU_PING;

				send_pdu(ifc, src_mac, &out_hdr, pending.sdu, pending.sdu_len);
				pending.active = 0;
			}
		}

	}
}

/**
 * Read one frame from the raw socket and pass the MIP part on.
 *
 * Global variables: raw_fd.
 * Returns nothing.
 */
static void on_raw_readable(void)
{
	uint8_t frame[MAX_FRAME_LEN];
	struct ether_header *eth = (struct ether_header *)frame;
	int ifindex;
	ssize_t n = recv_frame(raw_fd, frame, sizeof(frame), &ifindex);

	if (n <= (ssize_t)sizeof(*eth))
		return; 

	handle_pdu(eth->ether_shost, ifindex,
		   frame + sizeof(*eth), (size_t)n - sizeof(*eth));
}

/**
 * Read one message from the connected application.
 * Detects a disconnect (recv returns 0) and forgets the app.
 *
 * epfd: epoll instance; the app's fd is removed from it on disconnect.
 *
 * Global variables: app_fd (closed and set to -1 on disconnect).
 * Returns nothing.
 */
static void on_app_readable(int epfd)
{
	uint8_t buf[MAX_IPC_LEN];
	ssize_t n = recv(app_fd, buf, sizeof(buf), 0);

	if (n <= 0) {
		if (debug)
			printf("[mipd] application disconnected\n");
		epoll_ctl(epfd, EPOLL_CTL_DEL, app_fd, NULL);
		close(app_fd);
		app_fd = -1;
		return;
	}
	handle_app_msg(buf, (size_t)n);
}

/**
 * Accept a new application. Only one app is supported at a time, so an
 * existing connection is replaced.
 * epfd: epoll instance to register the new fd with.
 *
 * Global variables: listen_fd, app_fd.
 * Returns nothing.
 */
static void on_new_app(int epfd)
{
	struct epoll_event ev = { .events = EPOLLIN };
	int fd = accept(listen_fd, NULL, NULL);

	if (fd == -1) {
		perror("accept");
		return;
	}
	if (app_fd != -1) {
		epoll_ctl(epfd, EPOLL_CTL_DEL, app_fd, NULL);
		close(app_fd);
	}
	app_fd = fd;
	ev.data.fd = fd;
	epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev);
	if (debug)
		printf("[mipd] application connected\n");
}

/**
 * Parse arguments, open sockets, then run the event loop forever.
 *
 * Global variables: all of the above are initialised here.
 * Returns EXIT_FAILURE on setup errors; otherwise never returns.
 */
int main(int argc, char *argv[])
{
	struct epoll_event ev, events[MAX_EVENTS];
	int opt, epfd, addr;

	while ((opt = getopt(argc, argv, "hd")) != -1) {
		switch (opt) {
		case 'd': debug = 1; break;
		case 'h': usage(argv[0]); return EXIT_SUCCESS;
		default:  usage(argv[0]); return EXIT_FAILURE;
		}
	}
	if (argc - optind != 2) {
		usage(argv[0]);
		return EXIT_FAILURE;
	}
	addr = atoi(argv[optind + 1]);
	if (addr < 0 || addr > 254) {
		fprintf(stderr, "MIP address must be 0-254\n");
		return EXIT_FAILURE;
	}
	my_mip = (uint8_t)addr;

	arp_cache_init();

	n_ifs = get_interfaces(ifs, MAX_IFS);
	if (n_ifs <= 0) {
		fprintf(stderr, "no usable interfaces\n");
		return EXIT_FAILURE;
	}
	if (debug) {
		for (int i = 0; i < n_ifs; i++) {
			printf("[mipd] iface %s idx %d mac ", ifs[i].name,
			       ifs[i].ifindex);
			print_mac(ifs[i].mac);
			printf("\n");
		}
	}

	raw_fd = raw_socket_create();
	listen_fd = unix_listen(argv[optind]);
	if (raw_fd == -1 || listen_fd == -1)
		return EXIT_FAILURE;

	epfd = epoll_create1(0);
	if (epfd == -1) {
		perror("epoll_create1");
		return EXIT_FAILURE;
	}
	ev.events = EPOLLIN;
	ev.data.fd = raw_fd;
	epoll_ctl(epfd, EPOLL_CTL_ADD, raw_fd, &ev);
	ev.data.fd = listen_fd;
	epoll_ctl(epfd, EPOLL_CTL_ADD, listen_fd, &ev);

	printf("[mipd] MIP address %u, socket %s\n", my_mip, argv[optind]);
	fflush(stdout);

	/* Event loop: block until a socket is readable, dispatch on it. */
	for (;;) {
		int n = epoll_wait(epfd, events, MAX_EVENTS, -1);

		if (n == -1) {
			perror("epoll_wait");
			return EXIT_FAILURE;
		}
		for (int i = 0; i < n; i++) {
			int fd = events[i].data.fd;

			if (fd == raw_fd)
				on_raw_readable();
			else if (fd == listen_fd)
				on_new_app(epfd);
			else if (fd == app_fd)
				on_app_readable(epfd);
		}
		fflush(stdout); 
	}
}
