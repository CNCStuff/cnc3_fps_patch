ZIG ?= zig
TARGET := x86-windows-gnu

SOURCES := \
	src/common.c \
	src/game_layout.c \
	src/game_patches.c \
	src/frame_pacer.c \
	src/memory_patch.c \
	src/config.c \
	src/log.c \
	src/runtime.c \
	src/proxy_dinput8.c \
	src/dllmain.c

HEADERS := $(wildcard src/*.h)

COMMON_CFLAGS := \
	-target $(TARGET) \
	-std=c11 \
	-Wall \
	-Wextra \
	-Werror \
	-Isrc

COMMON_LDFLAGS := \
	-target $(TARGET) \
	-shared \
	-Wl,--subsystem,windows \
	-Wl,--major-subsystem-version,6 \
	-Wl,--minor-subsystem-version,1 \
	-Wl,--dynamicbase \
	-Wl,--nxcompat

MODE ?= release
OBJDIR := zig-out/obj/$(MODE)
BINDIR := zig-out/bin
LIBDIR := zig-out/lib
OBJECTS := $(patsubst src/%.c,$(OBJDIR)/%.obj,$(SOURCES))

ifeq ($(MODE),debug)
  CFLAGS := $(COMMON_CFLAGS) -O0 -g
  OUTPUT := $(BINDIR)/dinput8-debug.dll
else
  CFLAGS := $(COMMON_CFLAGS) -O2 -DNDEBUG
  OUTPUT := $(BINDIR)/dinput8.dll
endif

.PHONY: all release debug build package clean

all: release

release:
	$(MAKE) MODE=release build

debug:
	$(MAKE) MODE=debug build

build: $(OUTPUT)

$(OUTPUT): $(OBJECTS) src/dinput8.def | $(BINDIR) $(LIBDIR)
	$(ZIG) cc $(COMMON_LDFLAGS) \
		-Wl,/implib:$(LIBDIR)/dinput8-$(MODE).lib \
		-Wl,/pdb:$(LIBDIR)/dinput8-$(MODE).pdb \
		$(OBJECTS) src/dinput8.def -lkernel32 -lwinmm -o $@

$(OBJDIR)/%.obj: src/%.c $(HEADERS) | $(OBJDIR)
	$(ZIG) cc $(CFLAGS) -c $< -o $@

$(OBJDIR) $(BINDIR) $(LIBDIR):
	mkdir -p $@

package: release
	rm -rf zig-out/package
	mkdir -p zig-out/package
	cp zig-out/bin/dinput8.dll zig-out/package/dinput8.dll
	cp fps_patch.ini.example zig-out/package/fps_patch.ini
	cp README.md zig-out/package/README.md

clean:
	rm -rf zig-out
