# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Hugh Frater
#
# magview — Marine Magnetics Explorer acquisition and display
#
#   make            build ./magview
#   make test       build and run the unit tests (ASan + UBSan)
#   make debug      sanitised build of the application itself
#   make run        build and launch against the simulator
#   make help-doc   regenerate docs/HELP.md from the built-in manual
#   make windows        cross-compile magview.exe with mingw-w64
#   make windows-dist   exe + SDL2.dll + docs, zipped, in dist/
#   make appimage       portable Linux x86_64 AppImage in dist/
#   make release        windows-dist + appimage + dist/SHA256SUMS.txt
#   make clean
#
# Dependencies: SDL2 only. Nuklear is vendored in third_party/.
#   Debian/Ubuntu   sudo apt install libsdl2-dev
#   Arch            sudo pacman -S sdl2
#   macOS           brew install sdl2
#
# For the Windows cross-build see the "Windows cross-build" section below.

CC      ?= cc
UNAME   := $(shell uname -s)

SRC_DIR  = src
TEST_DIR = tests
OBJ_DIR  = build

WARN    = -Wall -Wextra -Wshadow -Wpointer-arith -Wstrict-prototypes \
          -Wno-unused-parameter

CFLAGS  = -std=c99 -O2 $(WARN) -MMD -MP -I$(SRC_DIR) -Ithird_party
LDLIBS  = -lm

# Nuklear's single-header implementation trips these in code we do not own.
NK_CFLAGS = -Wno-unused-function -Wno-sign-compare -Wno-implicit-fallthrough

ifeq ($(UNAME),Darwin)
    CFLAGS += -D_DARWIN_C_SOURCE
else
    CFLAGS += -D_DEFAULT_SOURCE
endif

SDL_CFLAGS := $(shell sdl2-config --cflags 2>/dev/null)
SDL_LIBS   := $(shell sdl2-config --libs 2>/dev/null)

# ---------------------------------------------------------------------
# Sources
# ---------------------------------------------------------------------

# Portable core plus the app: no SDL, and the unit tests link against these.
CORE = mv_geo mv_proto mv_log mv_config mv_sim mv_app mv_help

ifeq ($(OS),Windows_NT)
    PLAT = plat_win32
else
    PLAT = plat_posix
endif

UI   = mv_theme mv_plot mv_chart mv_ui mv_main

OBJS = $(addprefix $(OBJ_DIR)/,$(addsuffix .o,$(CORE) $(PLAT) $(UI)))

TARGET = magview

TESTS = test_proto test_geo test_log test_config

.PHONY: all clean distclean test debug run help-doc windows windows-dist appimage release

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $@ $(SDL_LIBS) $(LDLIBS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) $(SDL_CFLAGS) $(NK_CFLAGS) -c $< -o $@

$(OBJ_DIR):
	@mkdir -p $(OBJ_DIR)

-include $(OBJS:.o=.d)

# ---------------------------------------------------------------------
# Tests — always sanitised. A test that only passes without ASan has not
# passed.
# ---------------------------------------------------------------------

TEST_CFLAGS = -std=c99 -O1 -g $(WARN) -I$(SRC_DIR) \
              -fsanitize=address,undefined -fno-omit-frame-pointer
ifeq ($(UNAME),Darwin)
    TEST_CFLAGS += -D_DARWIN_C_SOURCE
else
    TEST_CFLAGS += -D_DEFAULT_SOURCE
endif

# The core the tests exercise, plus the POSIX platform for mv_log's file I/O.
TEST_SRCS = $(SRC_DIR)/mv_geo.c $(SRC_DIR)/mv_proto.c $(SRC_DIR)/mv_log.c \
            $(SRC_DIR)/mv_config.c $(SRC_DIR)/plat_posix.c

test:
	@mkdir -p $(OBJ_DIR)
	@fail=0; \
	for t in $(TESTS); do \
	    $(CC) $(TEST_CFLAGS) $(TEST_DIR)/$$t.c $(TEST_SRCS) $(LDLIBS) \
	        -o $(OBJ_DIR)/$$t || exit 1; \
	    $(OBJ_DIR)/$$t || fail=1; \
	done; \
	if [ $$fail -ne 0 ]; then echo "TESTS FAILED"; exit 1; fi; \
	echo "all tests passed"

debug: CFLAGS += -g -O0 -fsanitize=address,undefined
debug: LDLIBS += -fsanitize=address,undefined
debug: clean $(TARGET)

run: $(TARGET)
	./$(TARGET) --sim

# The manual lives in src/mv_help.c and is rendered two ways: the Help page
# draws it, and this writes it out. docs/HELP.md is generated — do not edit it
# by hand.
help-doc: $(TARGET)
	./$(TARGET) --help-doc > docs/HELP.md
	@echo "docs/HELP.md regenerated"

# ---------------------------------------------------------------------
# Windows cross-build (mingw-w64)
#
#   make windows        build magview.exe
#   make windows-dist   exe + SDL2.dll + docs, zipped, in dist/
#
# Needs the mingw toolchain and the SDL2 mingw development SDK:
#
#   sudo pacman -S --needed mingw-w64-gcc          (Arch; apt: mingw-w64)
#   tools/win/get-sdl2.sh                          (downloads and unpacks)
#
# Arch has no mingw pkg-config and no mingw SDL2 package, so the SDK is
# pointed at by path rather than discovered. Override for another location:
#
#   make windows SDL2_MINGW=/path/to/SDL2-2.x.y/x86_64-w64-mingw32
#
# Windows objects go in their own directory: sharing build/ with the native
# build links host .o files into the .exe and fails in confusing ways.
# ---------------------------------------------------------------------

SDL2_VER   ?= 2.32.10
SDL2_MINGW ?= tools/win/SDL2-$(SDL2_VER)/x86_64-w64-mingw32

WIN_CC     = x86_64-w64-mingw32-gcc
WIN_RC     = x86_64-w64-mingw32-windres
WIN_OBJ    = $(OBJ_DIR)/win
WIN_TARGET = magview.exe
WIN_RES    = $(WIN_OBJ)/magview_res.o
WIN_DIST   = dist/magview-$(MV_VERSION)-win64

# The version in the file's Properties tab is read from the same header the
# About dialog reads, so the two cannot disagree. Leading zeros are stripped:
# the resource compiler reads 08 as a malformed octal constant.
MV_VERSION := $(shell sed -n 's/.*MAGVIEW_VERSION "\([^"]*\)".*/\1/p' \
                      $(SRC_DIR)/mv_version.h)
MV_VER_A   := $(shell echo $(MV_VERSION) | cut -d. -f1 | sed 's/^0*//')
MV_VER_B   := $(shell echo $(MV_VERSION) | cut -d. -f2 | sed 's/^0*//')
MV_VER_C   := $(shell echo $(MV_VERSION) | cut -d. -f3 | sed 's/^0*//')

WIN_OBJS = $(addprefix $(WIN_OBJ)/,\
             $(addsuffix .o,$(CORE) plat_win32 $(UI)))

# _USE_MATH_DEFINES: mingw's math.h hides M_PI in strict C99, which glibc
# does not, so mv_sim stops compiling at the first use.
# __USE_MINGW_ANSI_STDIO: use mingw's own printf rather than the MSVCRT one,
# which has no %zu.
# Both SDL include paths: the program says <SDL2/SDL.h>, and the vendored
# nuklear_sdl_renderer.h says <SDL.h>, which is what sdl2-config's -I gives it
# on the native build.
WIN_CFLAGS = -std=c99 -O2 $(WARN) -MMD -MP -I$(SRC_DIR) -Ithird_party \
             -D_USE_MATH_DEFINES -D__USE_MINGW_ANSI_STDIO=1 \
             -I$(SDL2_MINGW)/include -I$(SDL2_MINGW)/include/SDL2

# -mwindows: no console window behind the application. --help and --help-doc
# reattach to the parent console themselves. -static-libgcc so the only DLL to
# ship is SDL2's. ws2_32 for the UDP GPS source, shell32 for the Documents
# folder lookup, setupapi/uuid for SDL2's own device enumeration.
WIN_LDFLAGS = -mwindows -static-libgcc -L$(SDL2_MINGW)/lib
WIN_LIBS    = -lmingw32 -lSDL2main -lSDL2 \
              -lws2_32 -lshell32 -lsetupapi -luuid -lm

windows: $(WIN_TARGET)

$(WIN_TARGET): $(WIN_OBJS) $(WIN_RES)
	@test -f $(SDL2_MINGW)/lib/libSDL2.a || { \
	    echo "No SDL2 mingw SDK at $(SDL2_MINGW)"; \
	    echo "Run tools/win/get-sdl2.sh, or set SDL2_MINGW=<dir>"; \
	    exit 1; }
	$(WIN_CC) $(WIN_OBJS) $(WIN_RES) -o $@ $(WIN_LDFLAGS) $(WIN_LIBS)
	@echo "built $@ — ship it with $(SDL2_MINGW)/bin/SDL2.dll"

$(WIN_OBJ)/%.o: $(SRC_DIR)/%.c | $(WIN_OBJ)
	$(WIN_CC) $(WIN_CFLAGS) $(NK_CFLAGS) -c $< -o $@

$(WIN_RES): tools/win/magview.rc tools/win/magview.ico \
            tools/win/magview.manifest $(SRC_DIR)/mv_version.h | $(WIN_OBJ)
	$(WIN_RC) -I tools/win -I $(SRC_DIR) \
	    -DMV_VER_A=$(MV_VER_A) -DMV_VER_B=$(MV_VER_B) -DMV_VER_C=$(MV_VER_C) \
	    -o $@ tools/win/magview.rc

$(WIN_OBJ):
	@mkdir -p $(WIN_OBJ)

-include $(WIN_OBJS:.o=.d)

windows-dist: $(WIN_TARGET) help-doc
	@rm -rf $(WIN_DIST)
	@mkdir -p $(WIN_DIST)
	cp $(WIN_TARGET) $(WIN_DIST)/
	cp $(SDL2_MINGW)/bin/SDL2.dll $(WIN_DIST)/
	cp LICENSE $(WIN_DIST)/LICENSE.txt
	cp docs/HELP.md $(WIN_DIST)/
	cp README.md $(WIN_DIST)/README.md
	cd dist && zip -qr $(notdir $(WIN_DIST)).zip $(notdir $(WIN_DIST))
	@echo "packaged dist/$(notdir $(WIN_DIST)).zip"

# ---------------------------------------------------------------------
# Portable Linux binary (AppImage)
#
# A native binary built on a rolling-release machine binds glibc symbol
# versions no stable distribution has, so it will not start elsewhere. The
# script builds against an older glibc inside bubblewrap — no root, no
# container runtime — and wraps the result with SDL2 in an AppImage. See
# tools/linux/make-appimage.sh.
# ---------------------------------------------------------------------

appimage:
	tools/linux/make-appimage.sh

# ---------------------------------------------------------------------
# Release: both platform artifacts plus a checksum manifest.
# ---------------------------------------------------------------------

release: windows-dist appimage
	cd dist && sha256sum *.zip *.AppImage > SHA256SUMS.txt 2>/dev/null || \
	           sha256sum *.zip > SHA256SUMS.txt
	@echo "release artifacts in dist/:"
	@ls -1 dist

# clean deliberately leaves dist/ alone: the AppImage build runs `make clean`
# inside its build box, and a release assembles the Windows zip and the
# AppImage into the same dist/ — wiping it mid-build would lose the first
# artifact. Use distclean to remove the packaged output as well.
clean:
	rm -rf $(OBJ_DIR) $(TARGET) magview.exe

distclean: clean
	rm -rf dist
