#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <dwmapi.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "slide.h"
#include "transition_win32.h"
#include "fullscreen_win32.h"

static const wchar_t OVERLAY_CLASS[] = L"MonitorSpaces.Slide.v1";
enum { WM_SLIDE_FRAME = WM_APP + 100 };

typedef struct {
    HDC dc;
    HBITMAP bitmap;
    HGDIOBJ previous;
} Surface;

static struct {
    HWND overlay;
    RECT area;
    Surface outgoing, incoming, frame;
    LARGE_INTEGER started, frequency;
    unsigned generation;
    int width, height, direction;
} g_slide;

typedef struct {
    HWND window;
    BOOL previous;
} WindowAnimation;

static struct {
    WindowAnimation *items;
    size_t count, capacity;
    bool active;
} g_changes;

static void finish_window_changes(void)
{
    /* One composition barrier for the whole workspace, not one per window. */
    if (g_changes.count)
        DwmFlush();
    for (size_t i = 0; i < g_changes.count; ++i) {
        WindowAnimation *item = &g_changes.items[i];
        if (IsWindow(item->window))
            DwmSetWindowAttribute(item->window, DWMWA_TRANSITIONS_FORCEDISABLED,
                                  &item->previous, sizeof(item->previous));
    }
    free(g_changes.items);
    g_changes.items = NULL;
    g_changes.count = g_changes.capacity = 0;
    g_changes.active = false;
}

static void free_surface(Surface *surface)
{
    if (surface->dc && surface->previous && surface->previous != HGDI_ERROR)
        SelectObject(surface->dc, surface->previous);
    if (surface->bitmap)
        DeleteObject(surface->bitmap);
    if (surface->dc)
        DeleteDC(surface->dc);
    *surface = (Surface){0};
}

static void cancel_overlay(void)
{
    HWND overlay = g_slide.overlay;
    g_slide.overlay = NULL;
    if (overlay)
        DestroyWindow(overlay);
    free_surface(&g_slide.outgoing);
    free_surface(&g_slide.incoming);
    free_surface(&g_slide.frame);
}

void ms_transition_cancel(void)
{
    finish_window_changes();
    cancel_overlay();
}

static bool present(void)
{
    POINT destination = {g_slide.area.left, g_slide.area.top}, source = {0};
    SIZE size = {g_slide.width, g_slide.height};
    /* Submit one complete opaque frame. Do not mix this presentation path with
     * SetLayeredWindowAttributes or window-DC painting. */
    return UpdateLayeredWindow(g_slide.overlay, NULL, &destination, &size,
        g_slide.frame.dc, &source, 0, NULL, ULW_OPAQUE) != FALSE;
}

static bool queue_frame(void)
{
    return PostMessageW(g_slide.overlay, WM_SLIDE_FRAME, g_slide.generation, 0) != FALSE;
}

static LRESULT CALLBACK overlay_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    switch (message) {
    case WM_SLIDE_FRAME: {
        /* Destroyed/reused HWNDs and interrupted slides can leave stale messages. */
        if (window != g_slide.overlay || wparam != g_slide.generation)
            return 0;
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        double progress = (double)(now.QuadPart - g_slide.started.QuadPart) /
                          (double)g_slide.frequency.QuadPart;
        unsigned elapsed = progress >= MS_SLIDE_DURATION_MS / 1000.0 ?
            MS_SLIDE_DURATION_MS : (unsigned)(progress * 1000.0);
        MSSlideFrame frame = ms_slide_frame(g_slide.width, elapsed, g_slide.direction);
        if (!BitBlt(g_slide.frame.dc, frame.outgoing, 0, g_slide.width, g_slide.height,
                    g_slide.outgoing.dc, 0, 0, SRCCOPY) ||
            !BitBlt(g_slide.frame.dc, frame.incoming, 0, g_slide.width, g_slide.height,
                    g_slide.incoming.dc, 0, 0, SRCCOPY)) {
            ms_transition_cancel();
            return 0;
        }
        if (!present() || FAILED(DwmFlush())) {
            ms_transition_cancel();
            return 0;
        }
        /* Flush paces this message chain to composition. Input messages can run
         * between frames; there is no low-priority/coarse WM_TIMER animation. */
        if (window != g_slide.overlay || wparam != g_slide.generation)
            return 0;
        if (elapsed == MS_SLIDE_DURATION_MS || !queue_frame())
            ms_transition_cancel();
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(window, &ps); /* UpdateLayeredWindow retains the last frame. */
        EndPaint(window, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_NCHITTEST:
        return HTTRANSPARENT;
    case WM_DISPLAYCHANGE:
    case WM_DPICHANGED:
    case WM_CLOSE:
        if (window == g_slide.overlay)
            ms_transition_cancel();
        return 0;
    default:
        return DefWindowProcW(window, message, wparam, lparam);
    }
}

static bool create_surface(Surface *surface, HDC screen)
{
    surface->dc = CreateCompatibleDC(screen);
    surface->bitmap = CreateCompatibleBitmap(screen, g_slide.width, g_slide.height);
    if (!surface->dc || !surface->bitmap)
        return false;
    surface->previous = SelectObject(surface->dc, surface->bitmap);
    return surface->previous && surface->previous != HGDI_ERROR;
}

static bool capture(Surface *surface)
{
    HDC screen = GetDC(NULL);
    if (!screen)
        return false;
    BOOL copied = BitBlt(surface->dc, 0, 0, g_slide.width, g_slide.height,
        screen, g_slide.area.left, g_slide.area.top, SRCCOPY | CAPTUREBLT);
    ReleaseDC(NULL, screen);
    return copied != FALSE;
}

static bool capture_exclusion_supported(void)
{
    /* Older Windows accepts WDA_EXCLUDEFROMCAPTURE but captures a black rectangle.
     * Build 19041 introduced real exclusion. The executable declares Windows 10
     * compatibility, so VerifyVersionInfo can check the actual build. */
    OSVERSIONINFOEXW version = {.dwOSVersionInfoSize = sizeof(version), .dwBuildNumber = 19041};
    DWORDLONG mask = VerSetConditionMask(0, VER_BUILDNUMBER, VER_GREATER_EQUAL);
    return VerifyVersionInfoW(&version, VER_BUILDNUMBER, mask) != FALSE;
}

void ms_transition_begin(HMONITOR monitor, int direction)
{
    finish_window_changes();
    g_changes.active = true;
    BOOL animate = FALSE;
    if (!SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animate, 0) || !animate ||
        GetSystemMetrics(SM_REMOTESESSION) || !capture_exclusion_supported() ||
        !QueryPerformanceFrequency(&g_slide.frequency) || g_slide.frequency.QuadPart <= 0) {
        cancel_overlay();
        return;
    }
    MONITORINFO info = {.cbSize = sizeof(info)};
    if (!GetMonitorInfoW(monitor, &info)) {
        cancel_overlay();
        return;
    }
    if (ms_fullscreen_enabled())
        info.rcWork = info.rcMonitor;
    ++g_slide.generation;
    if (g_slide.overlay && EqualRect(&g_slide.area, &info.rcWork)) {
        /* Keep the overlay on screen. Continue from exactly the pixels already
         * presented, rather than exposing the live desktop between shortcuts. */
        g_slide.direction = direction;
        if (!BitBlt(g_slide.outgoing.dc, 0, 0, g_slide.width, g_slide.height,
                    g_slide.frame.dc, 0, 0, SRCCOPY))
            cancel_overlay();
        return;
    }
    cancel_overlay();
    g_slide.area = info.rcWork; /* Full monitor in full-screen mode; otherwise work area. */
    g_slide.width = info.rcWork.right - info.rcWork.left;
    g_slide.height = info.rcWork.bottom - info.rcWork.top;
    g_slide.direction = direction;
    /* Three full-size bitmaps: cap temporary memory, falling back on huge displays. */
    if (g_slide.width <= 0 || g_slide.height <= 0 ||
        (uint64_t)g_slide.width * (uint64_t)g_slide.height > 16000000u)
        return;
    HDC screen = GetDC(NULL);
    if (!screen)
        return;
    bool allocated = create_surface(&g_slide.outgoing, screen) &&
                     create_surface(&g_slide.incoming, screen) &&
                     create_surface(&g_slide.frame, screen);
    ReleaseDC(NULL, screen);
    if (!allocated || !capture(&g_slide.outgoing)) {
        cancel_overlay();
        return;
    }
    if (!BitBlt(g_slide.frame.dc, 0, 0, g_slide.width, g_slide.height,
                g_slide.outgoing.dc, 0, 0, SRCCOPY)) {
        cancel_overlay();
        return;
    }
    HINSTANCE instance = GetModuleHandleW(NULL);
    WNDCLASSEXW cls = {.cbSize = sizeof(cls), .lpfnWndProc = overlay_proc,
                     .hInstance = instance, .lpszClassName = OVERLAY_CLASS};
    if (!RegisterClassExW(&cls) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        cancel_overlay();
        return;
    }
    g_slide.overlay = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE |
        WS_EX_LAYERED | WS_EX_TRANSPARENT, OVERLAY_CLASS, L"", WS_POPUP,
        info.rcWork.left, info.rcWork.top, g_slide.width, g_slide.height,
        NULL, NULL, instance, NULL);
    if (!g_slide.overlay || !SetWindowDisplayAffinity(g_slide.overlay, WDA_EXCLUDEFROMCAPTURE) ||
        !present()) {
        cancel_overlay();
        return;
    }
    BOOL disabled = TRUE;
    DwmSetWindowAttribute(g_slide.overlay, DWMWA_TRANSITIONS_FORCEDISABLED, &disabled, sizeof(disabled));
    ShowWindow(g_slide.overlay, SW_SHOWNOACTIVATE);
    DwmFlush(); /* Cover outgoing apps before changing their state. */
}

void ms_transition_commit(void)
{
    finish_window_changes();
    if (!g_slide.overlay)
        return;
    /* The overlay is excluded from this capture, leaving the restored desktop.
     * Snapshot pixels stay in process memory and are released at the end. */
    DwmFlush();
    if (!capture(&g_slide.incoming)) {
        ms_transition_cancel();
        return;
    }
    QueryPerformanceCounter(&g_slide.started);
    if (!queue_frame())
        ms_transition_cancel();
}

void ms_show_without_animation(HWND window, int command)
{
    /* Suppress only this window's shell transition; never change global Windows
     * animation preferences. Older DWM versions may not support querying this
     * write-oriented attribute, in which case restore its default FALSE value. */
    BOOL previous = FALSE, disabled = TRUE;
    for (size_t i = 0; i < g_changes.count; ++i) {
        if (g_changes.items[i].window == window) {
            ShowWindow(window, command);
            return; /* Preserve the original setting if this window is reused. */
        }
    }
    if (FAILED(DwmGetWindowAttribute(window, DWMWA_TRANSITIONS_FORCEDISABLED, &previous, sizeof(previous))))
        previous = FALSE;
    bool batched = g_changes.active;
    if (batched && g_changes.count == g_changes.capacity) {
        size_t capacity = g_changes.capacity ? g_changes.capacity * 2 : 16;
        WindowAnimation *items = capacity > g_changes.capacity && capacity <= SIZE_MAX / sizeof(*items) ?
            realloc(g_changes.items, capacity * sizeof(*items)) : NULL;
        if (items) {
            g_changes.items = items;
            g_changes.capacity = capacity;
        } else {
            batched = false; /* Retain synchronous cleanup on allocation failure. */
        }
    }
    bool changed = SUCCEEDED(DwmSetWindowAttribute(window, DWMWA_TRANSITIONS_FORCEDISABLED,
                                                   &disabled, sizeof(disabled)));
    if (changed && batched)
        g_changes.items[g_changes.count++] = (WindowAnimation){window, previous};
    ShowWindow(window, command);
    if (changed && !batched) {
        DwmFlush();
        DwmSetWindowAttribute(window, DWMWA_TRANSITIONS_FORCEDISABLED, &previous, sizeof(previous));
    }
}
