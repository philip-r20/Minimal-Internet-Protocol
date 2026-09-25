/*
 * ping_client.c - send "PING:<message>" to a MIP host, wait up to 1 s
 * for "PONG:<message>", print the RTT or "timeout".
 *
 * Usage: ping_client [-h] <socket_lower> <message> <destination_host>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <getopt.h>
#include <sys/socket.h>
#include <sys/time.h>
#include "common.h"

/**
 * Print usage.
 * prog: program name.
 *
 * Global variables: none.
 * Returns nothing.
 */
static void usage(const char *prog)
{
	printf("Usage: %s [-h] <socket_lower> <message> <destination_host>\n",
	       prog);
}

/**
 * Milliseconds between two timestamps.
 * a: start time. b: end time.
 *
 * Global variables: none.
 * Returns b - a in milliseconds (fractional).
 */
static double elapsed_ms(const struct timespec *a, const struct timespec *b)
{
	return (b->tv_sec - a->tv_sec) * 1e3 +
	       (b->tv_nsec - a->tv_nsec) / 1e6;
}

/**
 * Parse arguments, send the ping, wait for the reply.
 *
 * Global variables: none.
 * Returns EXIT_SUCCESS on reply or timeout, EXIT_FAILURE on errors.
 */
int main(int argc, char *argv[])
{
	int opt, fd, dst;
	const char *sock_path, *message;

	while ((opt = getopt(argc, argv, "h")) != -1) {
		usage(argv[0]);
		return opt == 'h' ? EXIT_SUCCESS : EXIT_FAILURE;
	}
	if (argc - optind != 3) {
		usage(argv[0]);
		return EXIT_FAILURE;
	}
	sock_path = argv[optind];
	message   = argv[optind + 1];
	dst       = atoi(argv[optind + 2]);
	if (dst < 0 || dst > 254) {
		fprintf(stderr, "destination must be 0-254\n");
		return EXIT_FAILURE;
	}

	fd = unix_connect(sock_path);
	if (fd == -1)
		return EXIT_FAILURE;

	/* TODO(you):
	 *  1. Build the IPC message: buf[0] = dst, then "PING:" + message,
	 *     including the terminating '\0' (so the receiver can treat the
	 *     zero-padded SDU as a C string). Check it fits in MAX_IPC_LEN.
	 *  2. Set a 1 second receive timeout:
	 *       struct timeval tv = { .tv_sec = 1 };
	 *       setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
	 *  3. clock_gettime(CLOCK_MONOTONIC, &start); send(fd, buf, len, 0);
	 *  4. recv(): -1 with errno EAGAIN/EWOULDBLOCK -> print "timeout".
	 *     Otherwise reply[0] is the sender, reply + 1 is the SDU.
	 *     If it is "PONG:" + message -> clock_gettime(&end), print
	 *     elapsed_ms(). Unknown messages: ignore and keep waiting
	 *     (bonus: shrink the timeout by the time already spent).
	 */
	(void)message;
	(void)elapsed_ms;

	close(fd);
	return EXIT_SUCCESS;
}
