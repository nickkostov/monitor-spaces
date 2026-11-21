WINCC ?= x86_64-w64-mingw32-gcc
WINDRES ?= x86_64-w64-mingw32-windres
HOSTCC ?= cc
PYTHON ?= python3

WARNINGS = -Wall -Wextra -Wpedantic -Werror
WIN_CFLAGS = -std=c11 -O2 $(WARNINGS) -ffunction-sections -fdata-sections -fstack-protector-strong
WIN_LDFLAGS = -municode -mwindows -static -s -Wl,--gc-sections,--dynamicbase,--nxcompat,--high-entropy-va,--no-insert-timestamp
SOURCES = src/win32_main.c src/workspaces.c src/navigation.c

.PHONY: all windows test test-sanitize inspect package clean

all: windows

windows: build/MonitorSpaces.exe

build:
	mkdir -p build

build/resources.o: resources/monitor-spaces.rc resources/monitor-spaces.manifest | build
	$(WINDRES) --preprocessor="$(WINCC)" --preprocessor-arg=-E --preprocessor-arg=-xc --preprocessor-arg=-DRC_INVOKED -I resources -i $< -O coff -o $@

build/MonitorSpaces.exe: $(SOURCES) src/workspaces.h src/navigation.h build/resources.o Makefile
	$(WINCC) $(WIN_CFLAGS) $(SOURCES) build/resources.o -o $@ $(WIN_LDFLAGS) -luser32 -lgdi32 -ldwmapi -lshell32

build/test_workspaces: src/workspaces.c src/workspaces.h tests/test_workspaces.c | build
	$(HOSTCC) -std=c11 -O2 $(WARNINGS) -Isrc src/workspaces.c tests/test_workspaces.c -o $@

build/test_navigation: src/navigation.c src/navigation.h tests/test_navigation.c | build
	$(HOSTCC) -std=c11 -O2 $(WARNINGS) -Isrc src/navigation.c tests/test_navigation.c -o $@

test: build/test_workspaces build/test_navigation
	./build/test_workspaces
	./build/test_navigation

build/test_workspaces_sanitize: src/workspaces.c src/workspaces.h tests/test_workspaces.c | build
	$(HOSTCC) -std=c11 -O1 -g $(WARNINGS) -fsanitize=address,undefined -fno-omit-frame-pointer -Isrc src/workspaces.c tests/test_workspaces.c -o $@

build/test_navigation_sanitize: src/navigation.c src/navigation.h tests/test_navigation.c | build
	$(HOSTCC) -std=c11 -O1 -g $(WARNINGS) -fsanitize=address,undefined -fno-omit-frame-pointer -Isrc src/navigation.c tests/test_navigation.c -o $@

test-sanitize: build/test_workspaces_sanitize build/test_navigation_sanitize
	./build/test_workspaces_sanitize
	./build/test_navigation_sanitize

inspect: windows
	$(PYTHON) tools/inspect_pe.py build/MonitorSpaces.exe

package: test inspect
	$(PYTHON) tools/package.py

clean:
	rm -rf build dist
