CC     := gcc
CFLAGS := -O2 -Wall -Wextra -std=gnu11

TARGETS := femtoshell picoshell omarcat omarcp omarecho omarmv omarpwd

.PHONY: all clean

all: $(TARGETS)

femtoshell: femtoshell.c
	$(CC) $(CFLAGS) $< -o $@

picoshell: picoshell.c
	$(CC) $(CFLAGS) $< -o $@

omarcat: omarcat.c
	$(CC) $(CFLAGS) $< -o $@

omarcp: omarcp.c
	$(CC) $(CFLAGS) $< -o $@

omarecho: omarecho.c
	$(CC) $(CFLAGS) $< -o $@

omarmv: omarmv.c
	$(CC) $(CFLAGS) $< -o $@

omarpwd: omarpwd.c
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -f $(TARGETS) *.o
