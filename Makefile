CC     ?= clang
PKGS   := wayland-client
CFLAGS += -std=c11 -Wall -Wextra -O2 $(shell pkg-config --cflags $(PKGS))
LDLIBS += $(shell pkg-config --libs $(PKGS)) -lm

SRC = main.c v4l2cap.c win.c draw.c xdg-shell-protocol.c
OBJ = $(SRC:.c=.o)
BIN = dacam

WL_PROTO_DIR := $(shell pkg-config --variable=pkgdatadir wayland-protocols)

all: $(BIN)

xdg-shell-client-protocol.h:
	wayland-scanner client-header $(WL_PROTO_DIR)/stable/xdg-shell/xdg-shell.xml $@

xdg-shell-protocol.c: xdg-shell-client-protocol.h
	wayland-scanner private-code $(WL_PROTO_DIR)/stable/xdg-shell/xdg-shell.xml $@

$(BIN): $(OBJ)
	$(CC) -o $@ $(OBJ) $(LDLIBS)

%.o: %.c xdg-shell-client-protocol.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(BIN) $(OBJ) xdg-shell-client-protocol.h xdg-shell-protocol.c

.PHONY: all clean
