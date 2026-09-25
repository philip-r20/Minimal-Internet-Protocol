CC      = gcc
CFLAGS  = -std=gnu11 -Wall -Wextra -g

COMMON  = mip.o link.o ipc.o
BINS    = mipd ping_client ping_server

all: $(BINS)

mipd: mipd.o $(COMMON)
	$(CC) $(CFLAGS) -o $@ $^

ping_client: ping_client.o ipc.o
	$(CC) $(CFLAGS) -o $@ $^

ping_server: ping_server.o ipc.o
	$(CC) $(CFLAGS) -o $@ $^

%.o: %.c common.h
	$(CC) $(CFLAGS) -c $<

clean:
	rm -f *.o $(BINS)

.PHONY: all clean
