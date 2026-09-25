/*
 * ipc.c - UNIX domain socket helpers (the MIP <-> application interface).
 *
 * SOCK_SEQPACKET is used because it is connection-oriented (we notice
 * when the app exits) AND keeps message boundaries, so one send() on one
 * side is exactly one recv() on the other. That fits the assignment's
 * "one message = [MIP address][SDU]" format without extra framing.
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include "common.h"

/**
 * Fill a sockaddr_un with a path.
 * addr: output address struct.
 * path: filesystem path of the socket.
 *
 * Global variables: none.
 * Returns 0, or -1 if the path is too long for sun_path.
 */
static int make_addr(struct sockaddr_un *addr, const char *path)
{
	memset(addr, 0, sizeof(*addr));
	addr->sun_family = AF_UNIX;
	if (strlen(path) >= sizeof(addr->sun_path)) {
		fprintf(stderr, "socket path too long: %s\n", path);
		return -1;
	}
	strcpy(addr->sun_path, path);
	return 0;
}

/**
 * Create a listening UNIX socket at path (used by mipd).
 * path: filesystem path; any stale file there is removed first.
 *
 * Global variables: none.
 * Returns the listening fd, or -1 on error.
 */
int unix_listen(const char *path)
{
	struct sockaddr_un addr;
	int fd;

	if (make_addr(&addr, path) == -1)
		return -1;

	fd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
	if (fd == -1) {
		perror("socket(AF_UNIX)");
		return -1;
	}

	unlink(path); /* leftover from a previous run would make bind fail */
	if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) == -1 ||
	    listen(fd, 1) == -1) {
		perror("bind/listen");
		close(fd);
		return -1;
	}
	return fd;
}

/**
 * Connect to the daemon's UNIX socket (used by the ping apps).
 * path: filesystem path the daemon bound.
 *
 * Global variables: none.
 * Returns the connected fd, or -1 on error (e.g. daemon not running).
 */
int unix_connect(const char *path)
{
	struct sockaddr_un addr;
	int fd;

	if (make_addr(&addr, path) == -1)
		return -1;

	fd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
	if (fd == -1) {
		perror("socket(AF_UNIX)");
		return -1;
	}
	if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
		perror("connect");
		close(fd);
		return -1;
	}
	return fd;
}
