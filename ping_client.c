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

	uint8_t buf[MAX_IPC_LEN + 1];
	size_t len = 1 + 5 + strlen(message) + 1;
	if (len > MAX_IPC_LEN) {
		fprintf(stderr, "ping_client: message too long\n");
		close(fd);
		return EXIT_FAILURE;
	}
	buf[0] = dst;
	memcpy(buf + 1, "PING:", 5);
	memcpy(buf + 6, message, strlen(message) + 1);

	struct timespec start, end;
	struct timeval tv = { .tv_sec = 1 };
	if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
		perror("setsockopt");
		close(fd);
		return EXIT_FAILURE;
	}
	clock_gettime(CLOCK_MONOTONIC, &start);
	if (send(fd, buf, len, 0) < 0) {
		perror("send");
		close(fd);
		return EXIT_FAILURE;
	}

	uint8_t reply[MAX_IPC_LEN + 1];
	ssize_t n = recv(fd, reply, MAX_IPC_LEN, 0);
	clock_gettime(CLOCK_MONOTONIC, &end);
	if (n < 0) {
		printf("timeout\n");
		close(fd);
		return EXIT_FAILURE;
	}
	if (n == 0) {
		fprintf(stderr, "ping_client: mipd closed the connection\n");
		close(fd);
		return EXIT_FAILURE;
	}
	reply[n] = '\0';

	uint8_t src = reply[0];
	char* text = (char*)reply + 1;
	if (strncmp(text, "PONG:", 5) == 0 && strcmp(text + 5, message) - 5) == 0) {
		printf("reply received from %d: %s\n", src, text);
		printf("elapsed time in ms: %.3f\n", elapsed_ms(&start, &end));
	} else {
		fprintf(stderr, "ping_client: unexpected reply: %s\n", text);
		close(fd);
		return EXIT_FAILURE;
	}
	close(fd);
	return EXIT_SUCCESS;
}
