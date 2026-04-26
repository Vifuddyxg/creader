CC      = gcc
TARGET  = creader
SRCDIR  = src
INCDIR  = include
BUILDDIR = build
PREFIX ?= /usr/local
APPDIR ?= $(HOME)/.local/share/applications

SRCS = $(wildcard $(SRCDIR)/*.c)
OBJS = $(patsubst $(SRCDIR)/%.c,$(BUILDDIR)/%.o,$(SRCS))

# ---- Dependencies ---------------------------------------------------------
SDL2_CFLAGS  := $(shell pkg-config --cflags sdl2 2>/dev/null || echo "-I/usr/include/SDL2 -D_REENTRANT")
SDL2_LIBS    := $(shell pkg-config --libs   sdl2 2>/dev/null || echo "-lSDL2")

SDL2TTF_CFLAGS := $(shell pkg-config --cflags SDL2_ttf 2>/dev/null || echo "")
SDL2TTF_LIBS   := $(shell pkg-config --libs   SDL2_ttf 2>/dev/null || echo "-lSDL2_ttf")

# MuPDF: try pkg-config first, then try common locations
MUPDF_PC     := $(shell pkg-config --exists mupdf 2>/dev/null && echo "yes" || echo "no")
ifeq ($(MUPDF_PC),yes)
MUPDF_CFLAGS := $(shell pkg-config --cflags mupdf)
MUPDF_LIBS   := $(shell pkg-config --libs   mupdf)
else
# Fallback: detect include path and use typical link flags
MUPDF_INC    := $(firstword $(wildcard /usr/include/mupdf \
                                        /usr/local/include/mupdf \
                                        /usr/include))
MUPDF_LIB    := $(firstword $(wildcard /usr/lib64/libmupdf.so \
                                        /usr/lib64/libmupdf.a \
                                        /usr/lib/libmupdf.so \
                                        /usr/lib/libmupdf.a \
                                        /usr/local/lib/libmupdf.so))
MUPDF_CFLAGS := -I$(MUPDF_INC)
MUPDF_LIBS   := -lmupdf -lm
# Some distros bundle third-party libs separately
ifneq ($(wildcard /usr/lib64/libmupdf-third.a /usr/lib/libmupdf-third.a),)
MUPDF_LIBS   += -lmupdf-third
endif
endif

# ---- Flags ----------------------------------------------------------------
CFLAGS  = -std=c11 -Wall -Wextra -O2 -I$(INCDIR) \
          $(SDL2_CFLAGS) $(SDL2TTF_CFLAGS) $(MUPDF_CFLAGS)

LDFLAGS = $(SDL2_LIBS) $(SDL2TTF_LIBS) $(MUPDF_LIBS) -lm

# ---- Rules ----------------------------------------------------------------
.PHONY: all clean install install-desktop install-menu uninstall-desktop

all: $(BUILDDIR) $(TARGET)

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

$(TARGET): $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)
	@echo "Build successful: ./$(TARGET)"

$(BUILDDIR)/%.o: $(SRCDIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILDDIR) $(TARGET)

install: $(TARGET)
	install -Dm755 $(TARGET) $(DESTDIR)$(PREFIX)/bin/$(TARGET)

install-desktop: install install-menu

install-menu:
	mkdir -p "$(APPDIR)"
	printf '%s\n' \
		'[Desktop Entry]' \
		'Type=Application' \
		'Name=creader' \
		'Comment=PDF and comic reader' \
		'Exec=$(PREFIX)/bin/$(TARGET) %f' \
		'Terminal=false' \
		'Categories=Office;Viewer;Graphics;' \
		'MimeType=application/pdf;application/epub+zip;application/vnd.comicbook+zip;application/vnd.comicbook-rar;' \
		'StartupNotify=true' \
		> "$(APPDIR)/$(TARGET).desktop"
	update-desktop-database "$(APPDIR)" 2>/dev/null || true
	xdg-mime default "$(TARGET).desktop" application/pdf 2>/dev/null || true
	@echo "Desktop launcher installed: $(APPDIR)/$(TARGET).desktop"

uninstall-desktop:
	rm -f "$(APPDIR)/$(TARGET).desktop"
	update-desktop-database "$(APPDIR)" 2>/dev/null || true
	@echo "Desktop launcher removed."

# ---- Dependency generation ------------------------------------------------
-include $(OBJS:.o=.d)
$(BUILDDIR)/%.d: $(SRCDIR)/%.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -MM -MT $(BUILDDIR)/$*.o $< > $@
