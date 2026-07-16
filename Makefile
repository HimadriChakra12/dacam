CC     ?= clang
PKGS   := wayland-client
CFLAGS += -std=c11 -Wall -Wextra -O2 $(shell pkg-config --cflags $(PKGS))
LDLIBS += $(shell pkg-config --libs $(PKGS)) -lm

SRC = main.c v4l2cap.c win.c draw.c xdg-shell-protocol.c
OBJ = $(SRC:.c=.o)
BIN = dacam

WL_PROTO_DIR := $(shell pkg-config --variable=pkgdatadir wayland-protocols)

PREFIX  ?= /usr
BINDIR  := $(PREFIX)/bin
ICONDIR := $(PREFIX)/share/icons/hicolor/256x256/apps
APPDIR  := $(PREFIX)/share/applications

all: $(BIN)

xdg-shell-client-protocol.h:
	wayland-scanner client-header $(WL_PROTO_DIR)/stable/xdg-shell/xdg-shell.xml $@

xdg-shell-protocol.c: xdg-shell-client-protocol.h
	wayland-scanner private-code $(WL_PROTO_DIR)/stable/xdg-shell/xdg-shell.xml $@

$(BIN): $(OBJ)
	$(CC) -o $@ $(OBJ) $(LDLIBS)

%.o: %.c xdg-shell-client-protocol.h
	$(CC) $(CFLAGS) -c $< -o $@

install: $(BIN)
	install -Dm755 $(BIN)           $(DESTDIR)$(BINDIR)/$(BIN)
	install -Dm644 logo.png         $(DESTDIR)$(ICONDIR)/dacam.png
	install -Dm644 dacam.desktop  $(DESTDIR)$(APPDIR)/dacam.desktop

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(BIN)
	rm -f $(DESTDIR)$(ICONDIR)/dacam.png
	rm -f $(DESTDIR)$(APPDIR)/dacam.desktop

clean:
	rm -f $(BIN) $(OBJ) xdg-shell-client-protocol.h xdg-shell-protocol.c

.PHONY: all install uninstall clean
