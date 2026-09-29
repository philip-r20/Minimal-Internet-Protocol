/*
 * ping_server.c - receive "PING:<msg>" via the local mipd, print it and
 * answer "PONG:<msg>" to the sender.
 *
 * Usage: ping_server [-h] <socket_lower>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <sys/socket.h>
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
	printf("Usage: %s [-h] <socket_lower>\n", prog);
}

/**
 * Parse arguments, connect to mipd, answer pings until mipd goes away.
 *
 * Global variables: none.
 * Returns EXIT_SUCCESS when mipd closes the socket, EXIT_FAILURE on errors.
 */
int main(int argc, char *argv[])
{
	int opt, fd;

	while ((opt = getopt(argc, argv, "h")) != -1) {
		usage(argv[0]);
		return opt == 'h' ? EXIT_SUCCESS : EXIT_FAILURE;
	}
	if (argc - optind != 1) {
		usage(argv[0]);
		return EXIT_FAILURE;
	}

	fd = unix_connect(argv[optind]);
	if (fd == -1)
		return EXIT_FAILURE;

	uint8_t buf[MAX_IPC_LEN + 1];
	while (1) {
		ssize_t n = recv(fd, buf, MAX_IPC_LEN, 0);
		if (n <= 0) {
			break;
		}
		buf[n] = '\0';
		uint8_t src = buf[0];
		char* text = (char*)buf + 1;
		
		printf("ping_server received from %d: %s\n", src, text);
		fflush(stdout);

		if (strncmp(text, "PING:", 5) == 0) {
			uint8_t reply[MAX_IPC_LEN + 1];
			reply[0] = src;
			memcpy(reply + 1, "PONG:", 5);
			memcpy(reply + 6, text + 5, strlen(text + 5) + 1);
			if (send(fd, reply, 1 + 5 + strlen(text + 5) + 1, 0) < 0) {
				perror("send");
			}
		}
	}
	close(fd);
	return EXIT_SUCCESS;
}
