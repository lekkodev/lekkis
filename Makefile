CC = gcc
CFLAGS = -std=c11 -O2
LDFLAGS = -lpthread

# Source files and output
SOURCES = assembler.c benchmark.c linux_server.c main.c server.c vm.c
HEADERS = assembler.h counter.h linux_server.h server.h vm.h
OBJECTS = $(SOURCES:.c=.o)
TARGET = program

# Detect platform
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Linux)
    CFLAGS += -D_LINUX
    LDFLAGS += -luring
else ifeq ($(UNAME_S),Darwin)
    CFLAGS += -D_DARWIN
endif

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) -o $@ $(LDFLAGS)

%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(TARGET)

