ZIG ?= zig
TARGET := x86-windows-gnu

SOURCES := \
	src/kw_common.c \
	src/game_1_02.c \
	src/memory_patch.c \
	src/config.c \
	src/log.c \
	src/runtime.c \
	src/proxy_dinput8.c \
	src/dllmain.c

COMMON_CFLAGS := \
	-target $(TARGET) \
	-std=c11 \
	-ffreestanding \
	-fno-builtin \
	-fno-sanitize=undefined \
	-fno-stack-protector \
	-Wall \
	-Wextra \
	-Werror \
	-Isrc

COMMON_LDFLAGS := \
	-target $(TARGET) \
	-shared \
	-nostdlib \
	-Wl,--entry,DllMainCRTStartup \
	-Wl,--subsystem,windows \
	-Wl,--major-subsystem-version,5 \
	-Wl,--minor-subsystem-version,1 \
	-Wl,--dynamicbase \
	-Wl,--nxcompat

MODE ?= release
OBJDIR := zig-out/obj/$(MODE)
BINDIR := zig-out/bin
LIBDIR := zig-out/lib
OBJECTS := $(patsubst src/%.c,$(OBJDIR)/%.obj,$(SOURCES))

ifeq ($(MODE),debug)
  CFLAGS := $(COMMON_CFLAGS) -O0 -g -DKW_DEBUG=1
  OUTPUT := $(BINDIR)/dinput8-debug.dll
else
  CFLAGS := $(COMMON_CFLAGS) -O2 -DNDEBUG
  OUTPUT := $(BINDIR)/dinput8.dll
endif

.PHONY: all release debug build verify package clean

all: release

release:
	$(MAKE) MODE=release build

debug:
	$(MAKE) MODE=debug build

build: $(OUTPUT)

verify: release
	@file zig-out/bin/dinput8.dll | grep -q 'PE32 executable (DLL).*Intel 80386'
	@objdump -p zig-out/bin/dinput8.dll | grep -q 'DLL Name: KERNEL32.dll'
	@objdump -p zig-out/bin/dinput8.dll | grep -q 'DLL Name: WINMM.DLL'
	@! objdump -p zig-out/bin/dinput8.dll | grep -Eq 'api-ms-win-crt|MSVCR|VCRUNTIME|ucrtbase'
	@for symbol in DirectInput8Create DllCanUnloadNow DllGetClassObject DllRegisterServer DllUnregisterServer; do \
		objdump -p zig-out/bin/dinput8.dll | grep -q " $$symbol$$" || exit 1; \
	done
	@echo 'Verified PE32/i386 DLL, proxy exports, and CRT-free imports.'

$(OUTPUT): $(OBJECTS) src/dinput8.def | $(BINDIR) $(LIBDIR)
	$(ZIG) cc $(COMMON_LDFLAGS) \
		-Wl,/implib:$(LIBDIR)/dinput8-$(MODE).lib \
		-Wl,/pdb:$(LIBDIR)/dinput8-$(MODE).pdb \
		$(OBJECTS) src/dinput8.def -lkernel32 -lwinmm -o $@

$(OBJDIR)/%.obj: src/%.c | $(OBJDIR)
	$(ZIG) cc $(CFLAGS) -c $< -o $@

$(OBJDIR) $(BINDIR) $(LIBDIR):
	mkdir -p $@

package: verify
	rm -rf zig-out/package
	mkdir -p zig-out/package
	cp zig-out/bin/dinput8.dll zig-out/package/dinput8.dll
	cp kw_fps_patch.ini.example zig-out/package/kw_fps_patch.ini
	cp README.md zig-out/package/README.md
	mkdir -p zig-out/package/docs
	cp docs/implementation.md zig-out/package/docs/implementation.md
	cp docs/testing.md zig-out/package/docs/testing.md

clean:
	rm -rf zig-out
