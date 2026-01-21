CC      ?= gcc
CFLAGS  ?= -Wall -Wextra -O2
CFLAGS  += -Iinclude
LDFLAGS ?=

PREFIX  ?= /usr/local
BINDIR  ?= $(PREFIX)/bin
DESTDIR ?=

INSTALL ?= install
RM      ?= rm -f

SRC = \
    src/main.c \
    src/lnk/lnk_io.c \
    src/lnk/lnk_parse.c \
    src/lnk/lnk_target.c \
    src/platform/desktop.c \
    src/platform/error.c \
    src/resolve/cache_links.c \
    src/resolve/gvfs.c \
    src/resolve/mapping_file.c \
    src/resolve/mapping_table.c \
    src/resolve/mounts.c \
    src/resolve/unc.c \
    src/util/fs.c \
    src/util/str.c

OBJ = $(SRC:.c=.o)

BIN = open_lnk

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(OBJ) -o $@ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

install: $(BIN)
	$(INSTALL) -d "$(DESTDIR)$(BINDIR)"
	$(INSTALL) -m 0755 "$(BIN)" "$(DESTDIR)$(BINDIR)/$(BIN)"

uninstall:
	$(RM) "$(DESTDIR)$(BINDIR)/$(BIN)"

clean:
	$(RM) $(OBJ) $(BIN)

.PHONY: all install uninstall clean
