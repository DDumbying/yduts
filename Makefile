CC      = gcc
CFLAGS  = -std=c11 -O2 -Wall -Wextra -I/usr/local/include
LDFLAGS = -L/usr/local/lib -lraylib -lm -lpthread -ldl

TARGET  = yduts
SRC     = yduts.c

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(TARGET)
