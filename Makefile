# BACKEND=wayland (default) or BACKEND=x11
BACKEND ?= wayland

CC      ?= cc
BIN      = dacam
PREFIX  ?= /usr
BINDIR  := $(PREFIX)/bin
ICONDIR := $(PREFIX)/share/icons/hicolor/256x256/apps
APPDIR  := $(PREFIX)/share/applications

CFLAGS  += -std=c11 -Wall -Wextra -O2
LDLIBS  += -lm

SRC_COMMON = main.c src/v4l2cap.c src/draw.c

ifeq ($(BACKEND),x11)
  SRC_BACKEND  = src/win_x11.c
  PKGS         = x11 xext
else
  BACKEND      = wayland
  SRC_BACKEND  = src/win.c src/xdg-shell-protocol.c
  PKGS         = wayland-client
  WL_PROTO_DIR := $(shell pkg-config --variable=pkgdatadir wayland-protocols 2>/dev/null)
endif

CFLAGS  += $(shell pkg-config --cflags $(PKGS) 2>/dev/null)
LDLIBS  += $(shell pkg-config --libs   $(PKGS) 2>/dev/null)

SRC = $(SRC_COMMON) $(SRC_BACKEND)
OBJ = $(SRC:.c=.o)

all: $(BIN)

# config.h is generated from config.def.h on first build and never
# touched again — edit config.h directly, config.def.h stays the
# tracked template. (suckless convention)
config.h:
	cp config.def.h $@

$(OBJ): config.h

$(BIN): $(OBJ)
	$(CC) -o $@ $(OBJ) $(LDLIBS)
	@echo "built $(BIN) [$(BACKEND)]"

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Wayland protocol glue — only needed when BACKEND=wayland, but harmless
# to declare unconditionally; make only runs them when the files are missing.
src/xdg-shell-client-protocol.h:
	wayland-scanner client-header \
	    $(WL_PROTO_DIR)/stable/xdg-shell/xdg-shell.xml $@

src/xdg-shell-protocol.c: src/xdg-shell-client-protocol.h
	wayland-scanner private-code \
	    $(WL_PROTO_DIR)/stable/xdg-shell/xdg-shell.xml $@

ifeq ($(BACKEND),wayland)
# Make wayland objects depend on the generated header
win.o xdg-shell-protocol.o main.o draw.o v4l2cap.o: src/xdg-shell-client-protocol.h
endif

install: $(BIN)
	install -Dm755 $(BIN)          $(DESTDIR)$(BINDIR)/$(BIN)
	install -Dm644 app/logo.png        $(DESTDIR)$(ICONDIR)/dacam.png
	install -Dm644 app/dacam.desktop   $(DESTDIR)$(APPDIR)/dacam.desktop

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(BIN)
	rm -f $(DESTDIR)$(ICONDIR)/dacam.png
	rm -f $(DESTDIR)$(APPDIR)/dacam.desktop

clean:
	rm -f $(BIN) $(OBJ) xdg-shell-client-protocol.h xdg-shell-protocol.c

# distclean also removes your generated config.h — use with care
distclean: clean
	rm -f config.h

.PHONY: all install uninstall clean distclean
