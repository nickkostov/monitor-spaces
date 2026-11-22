WINCC ?= x86_64-w64-mingw32-gcc
WINDRES ?= x86_64-w64-mingw32-windres
HOSTCC ?= cc
PYTHON ?= python3

WARNINGS = -Wall -Wextra -Wpedantic -Werror
WIN_CFLAGS = -std=c11 -O2 $(WARNINGS) -ffunction-sections -fdata-sections -fstack-protector-strong
WIN_LDFLAGS = -municode -mwindows -static -s -Wl,--gc-sections,--dynamicbase,--nxcompat,--high-entropy-va,--no-insert-timestamp
SOURCES = src/win32_main.c src/workspaces.c src/navigation.c src/slide.c src/transition_win32.c src/carousel_win32.c src/overview_layout.c src/wallpaper_win32.c src/app_icons_win32.c src/fullscreen_win32.c

.PHONY: all windows test test-sanitize inspect package clean

all: windows

windows: build/MonitorSpaces.exe

build:
	mkdir -p build

build/resources.o: resources/monitor-spaces.rc resources/monitor-spaces.manifest | build
	$(WINDRES) --preprocessor="$(WINCC)" --preprocessor-arg=-E --preprocessor-arg=-xc --preprocessor-arg=-DRC_INVOKED -I resources -i $< -O coff -o $@

build/MonitorSpaces.exe: $(SOURCES) src/workspaces.h src/navigation.h src/slide.h src/transition_win32.h src/carousel_win32.h src/overview_layout.h src/wallpaper_win32.h src/app_icons_win32.h src/fullscreen_win32.h build/resources.o Makefile
	$(WINCC) $(WIN_CFLAGS) $(SOURCES) build/resources.o -o $@ $(WIN_LDFLAGS) -luser32 -lgdi32 -ldwmapi -lshell32 -ladvapi32 -lole32 -luuid -lwindowscodecs

build/test_workspaces: src/workspaces.c src/workspaces.h tests/test_workspaces.c | build
	$(HOSTCC) -std=c11 -O2 $(WARNINGS) -Isrc src/workspaces.c tests/test_workspaces.c -o $@

build/test_navigation: src/navigation.c src/navigation.h tests/test_navigation.c | build
	$(HOSTCC) -std=c11 -O2 $(WARNINGS) -Isrc src/navigation.c tests/test_navigation.c -o $@

build/test_slide: src/slide.c src/slide.h tests/test_slide.c | build
	$(HOSTCC) -std=c11 -O2 $(WARNINGS) -Isrc src/slide.c tests/test_slide.c -o $@

build/test_overview: src/overview_layout.c src/overview_layout.h tests/test_overview.c | build
	$(HOSTCC) -std=c11 -O2 $(WARNINGS) -Isrc src/overview_layout.c tests/test_overview.c -o $@

build/test_fullscreen: src/fullscreen_win32.c src/fullscreen_win32.h tests/test_fullscreen.c tests/win32_stubs/windows.h tests/win32_stubs/shellapi.h | build
	$(HOSTCC) -std=c11 -O2 $(WARNINGS) -Itests/win32_stubs -Isrc src/fullscreen_win32.c tests/test_fullscreen.c -o $@

test: build/test_workspaces build/test_navigation build/test_slide build/test_overview build/test_fullscreen
	./build/test_workspaces
	./build/test_navigation
	./build/test_slide
	./build/test_overview
	./build/test_fullscreen

build/test_workspaces_sanitize: src/workspaces.c src/workspaces.h tests/test_workspaces.c | build
	$(HOSTCC) -std=c11 -O1 -g $(WARNINGS) -fsanitize=address,undefined -fno-omit-frame-pointer -Isrc src/workspaces.c tests/test_workspaces.c -o $@

build/test_navigation_sanitize: src/navigation.c src/navigation.h tests/test_navigation.c | build
	$(HOSTCC) -std=c11 -O1 -g $(WARNINGS) -fsanitize=address,undefined -fno-omit-frame-pointer -Isrc src/navigation.c tests/test_navigation.c -o $@

build/test_slide_sanitize: src/slide.c src/slide.h tests/test_slide.c | build
	$(HOSTCC) -std=c11 -O1 -g $(WARNINGS) -fsanitize=address,undefined -fno-omit-frame-pointer -Isrc src/slide.c tests/test_slide.c -o $@

build/test_overview_sanitize: src/overview_layout.c src/overview_layout.h tests/test_overview.c | build
	$(HOSTCC) -std=c11 -O1 -g $(WARNINGS) -fsanitize=address,undefined -fno-omit-frame-pointer -Isrc src/overview_layout.c tests/test_overview.c -o $@

build/test_fullscreen_sanitize: src/fullscreen_win32.c src/fullscreen_win32.h tests/test_fullscreen.c tests/win32_stubs/windows.h tests/win32_stubs/shellapi.h | build
	$(HOSTCC) -std=c11 -O1 -g $(WARNINGS) -fsanitize=address,undefined -fno-omit-frame-pointer -Itests/win32_stubs -Isrc src/fullscreen_win32.c tests/test_fullscreen.c -o $@

test-sanitize: build/test_workspaces_sanitize build/test_navigation_sanitize build/test_slide_sanitize build/test_overview_sanitize build/test_fullscreen_sanitize
	./build/test_workspaces_sanitize
	./build/test_navigation_sanitize
	./build/test_slide_sanitize
	./build/test_overview_sanitize
	./build/test_fullscreen_sanitize

inspect: windows
	$(PYTHON) tools/inspect_pe.py build/MonitorSpaces.exe

package: test inspect
	$(PYTHON) tools/package.py

clean:
	rm -rf build dist
