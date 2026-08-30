CC      := gcc
CFLAGS  := -O2 -Wall -Wextra -std=gnu11
LDFLAGS := -lm

SRCS    := userfs.c myshell.c
TARGET  := aura

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(SRCS) userfs.h
	$(CC) $(CFLAGS) $(SRCS) -o $(TARGET) $(LDFLAGS)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET) disk1 *.o
