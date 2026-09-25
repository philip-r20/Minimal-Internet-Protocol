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

	/* TODO(you): loop forever:
	 *  1. n = recv(fd, buf, MAX_IPC_LEN, 0); n <= 0 -> mipd gone, break.
	 *  2. src = buf[0]; text = buf + 1. Make sure it is '\0'-terminated
	 *     (buf[n] = '\0' if you left room for it).
	 *  3. Print it (and fflush(stdout) - it runs in an xterm).
	 *  4. If it starts with "PING:", reply with
	 *     reply[0] = src, then "PONG:" + (text + 5) + '\0', one send().
	 */

	close(fd);
	return EXIT_SUCCESS;
}
