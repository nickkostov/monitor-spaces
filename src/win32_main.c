#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0A00

#include <windows.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <stdint.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

#include "workspaces.h"
#include "navigation.h"
#include "slide.h"
#include "transition_win32.h"
#include "carousel_win32.h"

/* Store an integer, never a pointer into this process. The property stays on
 * the foreign HWND if this utility is killed. Keep this key stable in updates. */
static const wchar_t TAG_KEY[] = L"MonitorSpaces.HiddenWorkspace.v1.{52617889-437D-4910-B50A-70607E152BC2}";
/* Numeric original state: SW_SHOWNORMAL, SW_SHOWMINIMIZED, SW_SHOWMAXIMIZED.
 * An absent property identifies windows hidden by earlier versions. */
static const wchar_t STATE_KEY[] = L"MonitorSpaces.OriginalState.v1.{52617889-437D-4910-B50A-70607E152BC2}";
static const wchar_t PANEL_CLASS[] = L"MonitorSpaces.Panel.v1";
static const wchar_t MUTEX_NAME[] = L"Local\\MonitorSpaces.Instance.v1";
static const wchar_t COUNTS_KEY[] = L"Software\\MonitorSpaces\\WorkspaceCounts";

enum { ID_RESTORE = 100, ID_EXIT, ID_SHOW_PANEL,
       TIMER_TOPOLOGY = 1, TIMER_INDICATOR, TRAY_ID = 1,
       WM_RECOVER = WM_APP + 1, WM_NAVIGATE, WM_NAVIGATION_ERROR,
       WM_TRAY, WM_SHOW_PANEL, WM_EXIT, DIRECT_WORKSPACES = 4 };

typedef struct {
    HMONITOR handle;
    MONITORINFOEXW info;
} Display;

typedef struct {
    Display items[MS_MAX_MONITORS];
    size_t count;
    bool failed;
} Displays;

typedef struct {
    MSWindow *items;
    size_t count;
    size_t capacity;
    bool failed;
} Windows;

static MSState g_state;
static Displays g_displays;
static HWND g_panel, g_intro, g_list, g_status, g_restore, g_exit;
static HFONT g_font;
static UINT g_dpi = 96;
static unsigned g_registered;
static HANDLE g_keyboard_thread, g_keyboard_ready;
static DWORD g_keyboard_thread_id, g_keyboard_error;
static MSNavigation g_navigation;
static const ULONG_PTR MENU_MASK_MARKER = 0x4d535041;
static bool g_topology_dirty, g_recovery_needed, g_busy;
static bool g_tray_enabled, g_tray_added, g_tray_version4;
static UINT g_taskbar_created;
static HICON g_tray_icons[MS_MAX_WORKSPACES + 1];
static int g_tray_icon_size;
static unsigned g_tray_workspace = UINT_MAX;
static MSHandle g_tray_monitor;
static wchar_t g_tray_tip[128];
static wchar_t g_message[512] = L"Ready. Each display starts on workspace 1.";

static void show_panel(void)
{
    ShowWindow(g_panel, IsIconic(g_panel) ? SW_RESTORE : SW_SHOW);
    SetForegroundWindow(g_panel);
}

static void destroy_tray_icons(void)
{
    for (unsigned i = 0; i <= MS_MAX_WORKSPACES; ++i) {
        if (g_tray_icons[i])
            DestroyIcon(g_tray_icons[i]);
        g_tray_icons[i] = NULL;
    }
    g_tray_icon_size = 0;
}

static HICON number_icon(unsigned workspace, int size)
{
    BITMAPINFO bitmap = {0};
    bitmap.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmap.bmiHeader.biWidth = size;
    bitmap.bmiHeader.biHeight = -size;
    bitmap.bmiHeader.biPlanes = 1;
    bitmap.bmiHeader.biBitCount = 32;
    bitmap.bmiHeader.biCompression = BI_RGB;
    DWORD *pixels = NULL;
    HDC dc = CreateCompatibleDC(NULL);
    HBITMAP color = CreateDIBSection(NULL, &bitmap, DIB_RGB_COLORS, (void **)&pixels, NULL, 0);
    /* At most 128 x 128; a monochrome mask uses word-aligned rows. */
    BYTE mask_bits[2048] = {0};
    HBITMAP mask = CreateBitmap(size, size, 1, 1, mask_bits);
    HFONT font = CreateFontW(-MulDiv(size, workspace >= 10 ? 5 : 7, 8), 0, 0, 0, FW_BOLD,
        FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    HICON icon = NULL;
    if (dc && color && pixels && mask && font) {
        for (int i = 0; i < size * size; ++i)
            pixels[i] = 0xff165dbe;
        HGDIOBJ old_bitmap = SelectObject(dc, color);
        HGDIOBJ old_font = SelectObject(dc, font);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(255, 255, 255));
        wchar_t digit[12] = L"?";
        if (workspace)
            swprintf(digit, 12, L"%u", workspace);
        RECT rect = {0, 0, size, size};
        int drawn = DrawTextW(dc, digit, -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        GdiFlush();
        /* GDI text rendering does not preserve the DIB's alpha channel. */
        for (int i = 0; i < size * size; ++i)
            pixels[i] |= 0xff000000;
        SelectObject(dc, old_font);
        SelectObject(dc, old_bitmap);
        if (drawn) {
            ICONINFO info = {.fIcon = TRUE, .hbmMask = mask, .hbmColor = color};
            icon = CreateIconIndirect(&info);
        }
    }
    if (font)
        DeleteObject(font);
    if (mask)
        DeleteObject(mask);
    if (color)
        DeleteObject(color);
    if (dc)
        DeleteDC(dc);
    return icon;
}

static bool update_tray(void)
{
    if (!g_tray_enabled || !g_panel)
        return false;
    POINT cursor;
    HMONITOR monitor = GetCursorPos(&cursor) ? MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST) : NULL;
    unsigned workspace = ms_active(&g_state, (MSHandle)monitor);
    wchar_t tip[128] = L"Monitor Spaces - display unavailable";
    for (size_t i = 0; i < g_displays.count; ++i) {
        if (g_displays.items[i].handle == monitor) {
            swprintf(tip, sizeof(tip) / sizeof(tip[0]),
                L"Monitor Spaces - Display %zu (%ls): workspace %u of %u%ls", i + 1,
                g_displays.items[i].info.szDevice, workspace, ms_workspace_count(&g_state, (MSHandle)monitor),
                g_recovery_needed ? L" - recovery needed" : L"");
            break;
        }
    }
    HWND taskbar = FindWindowW(L"Shell_TrayWnd", NULL);
    UINT dpi = taskbar ? GetDpiForWindow(taskbar) : 96;
    int size = GetSystemMetricsForDpi(SM_CXSMICON, dpi ? dpi : 96);
    if (size < 16)
        size = 16;
    if (size > 128)
        size = 128;
    bool resized = size != g_tray_icon_size;
    if (resized) {
        destroy_tray_icons();
        g_tray_icon_size = size;
    }
    if (!g_tray_icons[workspace])
        g_tray_icons[workspace] = number_icon(workspace, size);
    if (!g_tray_icons[workspace])
        return false;
    if (g_tray_added && !resized && g_tray_workspace == workspace &&
        g_tray_monitor == (MSHandle)monitor && wcscmp(g_tray_tip, tip) == 0)
        return true;

    NOTIFYICONDATAW data = {.cbSize = sizeof(data), .hWnd = g_panel, .uID = TRAY_ID,
        .uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP,
        .uCallbackMessage = WM_TRAY, .hIcon = g_tray_icons[workspace]};
    wcscpy(data.szTip, tip);
    if (!Shell_NotifyIconW(g_tray_added ? NIM_MODIFY : NIM_ADD, &data)) {
        g_tray_added = false;
        return false;
    }
    if (!g_tray_added) {
        data.uVersion = NOTIFYICON_VERSION_4;
        g_tray_version4 = Shell_NotifyIconW(NIM_SETVERSION, &data) != FALSE;
    }
    g_tray_added = true;
    g_tray_workspace = workspace;
    g_tray_monitor = (MSHandle)monitor;
    wcscpy(g_tray_tip, tip);
    return true;
}

static void remove_tray(void)
{
    g_tray_enabled = false;
    NOTIFYICONDATAW data = {.cbSize = sizeof(data), .hWnd = g_panel, .uID = TRAY_ID};
    if (g_tray_added)
        Shell_NotifyIconW(NIM_DELETE, &data);
    g_tray_added = false;
    destroy_tray_icons();
}

static void tray_menu(POINT point)
{
    HMENU menu = CreatePopupMenu();
    if (!menu)
        return;
    AppendMenuW(menu, MF_STRING, ID_SHOW_PANEL, L"&Open panel");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, ID_RESTORE, L"&Restore all windows");
    AppendMenuW(menu, MF_STRING, ID_EXIT, L"Restore and e&xit");
    SetMenuDefaultItem(menu, ID_SHOW_PANEL, FALSE);
    SetForegroundWindow(g_panel);
    UINT command = (UINT)TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
                                       point.x, point.y, 0, g_panel, NULL);
    DestroyMenu(menu);
    PostMessageW(g_panel, WM_NULL, 0, 0);
    if (command)
        PostMessageW(g_panel, WM_COMMAND, command, 0);
    else {
        NOTIFYICONDATAW data = {.cbSize = sizeof(data), .hWnd = g_panel, .uID = TRAY_ID};
        Shell_NotifyIconW(NIM_SETFOCUS, &data);
    }
}

static LRESULT CALLBACK keyboard_proc(int code, WPARAM message, LPARAM parameter)
{
    if (code == HC_ACTION && (message == WM_KEYDOWN || message == WM_KEYUP ||
                             message == WM_SYSKEYDOWN || message == WM_SYSKEYUP)) {
        const KBDLLHOOKSTRUCT *key = (const KBDLLHOOKSTRUCT *)parameter;
        if (!((key->flags & LLKHF_INJECTED) && key->dwExtraInfo == MENU_MASK_MARKER)) {
            MSNavigation previous = g_navigation;
            bool down = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
            MSNavigationAction action = ms_navigation_key(&g_navigation, key->vkCode, down);
            if (action.direction || action.overview) {
                /* A swallowed arrow must not leave the shell seeing Win alone.
                 * A brief Ctrl press masks its Start-menu action. */
                bool masked = true;
                if (action.mask_menu) {
                    INPUT mask[2] = {0};
                    mask[0].type = mask[1].type = INPUT_KEYBOARD;
                    mask[0].ki.wVk = mask[1].ki.wVk = VK_CONTROL;
                    mask[0].ki.dwExtraInfo = mask[1].ki.dwExtraInfo = MENU_MASK_MARKER;
                    mask[1].ki.dwFlags = KEYEVENTF_KEYUP;
                    UINT sent = SendInput(2, mask, sizeof(INPUT));
                    masked = sent == 2;
                    if (sent == 1)
                        SendInput(1, &mask[1], sizeof(INPUT));
                }
                if (!masked || !PostMessageW(g_panel, WM_NAVIGATE, key->vkCode, 0)) {
                    DWORD error = GetLastError();
                    g_navigation = previous;
                    /* Keep tracking this unconsumed key so autorepeat cannot
                     * become a fresh shortcut halfway through a keypress. */
                    if (key->vkCode < MS_KEY_COUNT)
                        g_navigation.down[key->vkCode] = down;
                    PostMessageW(g_panel, WM_NAVIGATION_ERROR, error, 0);
                    return CallNextHookEx(NULL, code, message, parameter);
                }
            }
            if (action.swallow)
                return 1;
        }
    }
    return CallNextHookEx(NULL, code, message, parameter);
}

static DWORD WINAPI keyboard_thread(void *parameter)
{
    MSG message;
    /* Create the queue before announcing readiness so shutdown can post WM_QUIT. */
    PeekMessageW(&message, NULL, WM_USER, WM_USER, PM_NOREMOVE);
    /* Seed side-specific modifiers only. Generic Ctrl/Shift/Alt aliases would
     * otherwise stay latched after a side-specific physical key-up event. */
    const unsigned held_keys[] = {VK_LWIN, VK_RWIN, VK_LSHIFT, VK_RSHIFT,
        VK_LCONTROL, VK_RCONTROL, VK_LMENU, VK_RMENU, VK_LEFT, VK_RIGHT, VK_UP};
    for (size_t i = 0; i < sizeof(held_keys) / sizeof(held_keys[0]); ++i)
        g_navigation.down[held_keys[i]] = (GetAsyncKeyState((int)held_keys[i]) & 0x8000) != 0;
    HHOOK hook = SetWindowsHookExW(WH_KEYBOARD_LL, keyboard_proc, (HINSTANCE)parameter, 0);
    g_keyboard_error = hook ? ERROR_SUCCESS : GetLastError();
    SetEvent(g_keyboard_ready);
    if (!hook)
        return 1;
    /* The hook only handles input and posts commands. Never switch windows on
     * this thread: a slow foreign window must not stall the keyboard hook. */
    while (GetMessageW(&message, NULL, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    UnhookWindowsHookEx(hook);
    return 0;
}

static void stop_keyboard(void)
{
    if (g_keyboard_thread) {
        PostThreadMessageW(g_keyboard_thread_id, WM_QUIT, 0, 0);
        WaitForSingleObject(g_keyboard_thread, INFINITE);
        CloseHandle(g_keyboard_thread);
        g_keyboard_thread = NULL;
    }
}

static bool start_keyboard(HINSTANCE instance)
{
    g_keyboard_ready = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!g_keyboard_ready)
        return false;
    g_keyboard_thread = CreateThread(NULL, 0, keyboard_thread, instance, 0, &g_keyboard_thread_id);
    if (!g_keyboard_thread) {
        DWORD error = GetLastError();
        CloseHandle(g_keyboard_ready);
        g_keyboard_ready = NULL;
        SetLastError(error);
        return false;
    }
    WaitForSingleObject(g_keyboard_ready, INFINITE);
    CloseHandle(g_keyboard_ready);
    g_keyboard_ready = NULL;
    if (g_keyboard_error) {
        stop_keyboard();
        SetLastError(g_keyboard_error);
        return false;
    }
    return true;
}

static unsigned window_tag(HWND window)
{
    ULONG_PTR tag = (ULONG_PTR)GetPropW(window, TAG_KEY);
    return tag <= MS_MAX_WORKSPACES ? (unsigned)tag : 0;
}

static bool shell_class(const wchar_t *name)
{
    static const wchar_t *const names[] = {
        L"Progman", L"WorkerW", L"Shell_TrayWnd", L"Shell_SecondaryTrayWnd",
        L"NotifyIconOverflowWindow", L"TaskListThumbnailWnd", L"TaskSwitcherWnd",
        L"MultitaskingViewFrame", L"DV2ControlHost", L"ImmersiveLauncher",
        L"ApplicationManager_ImmersiveShellWindow", L"ForegroundStaging",
        L"IME", L"MSCTFIME UI"
    };
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
        if (wcscmp(name, names[i]) == 0)
            return true;
    return false;
}

static bool eligible(HWND window)
{
    DWORD process = 0;
    wchar_t name[256];
    RECT rect;
    DWORD cloaked = 0;
    if (!IsWindow(window) || !IsWindowVisible(window) ||
        window == GetDesktopWindow() || window == GetShellWindow())
        return false;
    if (!GetWindowThreadProcessId(window, &process) || process == GetCurrentProcessId())
        return false;
    LONG_PTR style = GetWindowLongPtrW(window, GWL_STYLE);
    LONG_PTR exstyle = GetWindowLongPtrW(window, GWL_EXSTYLE);
    if ((style & WS_CHILD) || (exstyle & WS_EX_NOACTIVATE))
        return false;
    /* Include owned dialogs and tool windows; hiding only their owner can leave
     * app UI behind. Exclude shell windows by class rather than by process. */
    if (!GetClassNameW(window, name, (int)(sizeof(name) / sizeof(name[0]))) || shell_class(name))
        return false;
    if (SUCCEEDED(DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked)
        return false;
    return GetWindowRect(window, &rect) && rect.right > rect.left && rect.bottom > rect.top;
}

static BOOL CALLBACK collect_window(HWND window, LPARAM parameter)
{
    Windows *windows = (Windows *)parameter;
    unsigned tag = window_tag(window);
    bool visible = IsWindowVisible(window) != FALSE;
    bool can_manage = visible && eligible(window);
    if (!tag && !can_manage)
        return TRUE;
    if (windows->count == windows->capacity) {
        size_t capacity = windows->capacity ? windows->capacity * 2 : 128;
        if (capacity < windows->capacity || capacity > SIZE_MAX / sizeof(MSWindow)) {
            windows->failed = true;
            return FALSE;
        }
        MSWindow *items = realloc(windows->items, capacity * sizeof(*items));
        if (!items) {
            windows->failed = true;
            return FALSE;
        }
        windows->items = items;
        windows->capacity = capacity;
    }
    windows->items[windows->count++] = (MSWindow){
        .id = (MSHandle)window,
        .monitor = (MSHandle)MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST),
        .tag = tag, .visible = visible, .eligible = can_manage
    };
    return TRUE;
}

static bool snapshot_windows(Windows *windows)
{
    *windows = (Windows){0};
    if (!EnumWindows(collect_window, (LPARAM)windows) || windows->failed) {
        free(windows->items);
        *windows = (Windows){0};
        return false;
    }
    return true;
}

static bool set_tag(void *context, MSHandle handle, unsigned workspace)
{
    (void)context;
    HWND window = (HWND)handle;
    /* Save every owner's and popup's state before minimizing any window. Never
     * overwrite a saved state when retrying a failed minimize/tag removal. */
    bool tagged = ms_valid_tag(window_tag(window));
    if (!IsWindow(window) || (!tagged && !eligible(window)))
        return false;
    if (!tagged) {
        unsigned state = IsIconic(window) ? SW_SHOWMINIMIZED :
                         IsZoomed(window) ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL;
        if (!SetPropW(window, STATE_KEY, (HANDLE)(ULONG_PTR)state))
            return false;
    }
    /* UIPI may deny property writes. Never minimize without a recovery tag. */
    if (SetPropW(window, TAG_KEY, (HANDLE)(ULONG_PTR)workspace))
        return true;
    if (!tagged)
        RemovePropW(window, STATE_KEY);
    return false;
}

static bool clear_tag(void *context, MSHandle handle)
{
    (void)context;
    HWND window = (HWND)handle;
    if (!IsWindow(window))
        return true;
    RemovePropW(window, TAG_KEY);
    if (GetPropW(window, TAG_KEY) != NULL)
        return false;
    RemovePropW(window, STATE_KEY);
    return true;
}

static bool hide_window(void *context, MSHandle handle)
{
    (void)context;
    HWND window = (HWND)handle;
    if (!IsWindow(window))
        return true;
    if (!ms_valid_tag(window_tag(window)))
        return false;
    /* WS_VISIBLE stays set, preserving real taskbar buttons and Alt+Tab entries.
     * ShowWindow's return value is previous visibility, not success. */
    if (!IsIconic(window))
        ms_show_without_animation(window, SW_SHOWMINNOACTIVE);
    return !IsWindow(window) || IsIconic(window);
}

static bool show_window(void *context, MSHandle handle)
{
    (void)context;
    HWND window = (HWND)handle;
    if (!IsWindow(window))
        return true;
    if (!ms_valid_tag(window_tag(window)))
        return false;
    unsigned state = (unsigned)(ULONG_PTR)GetPropW(window, STATE_KEY);
    if (state < SW_SHOWNORMAL || state > SW_SHOWMAXIMIZED) {
        /* Recover legacy releases' hidden windows without changing min/max. */
        BOOL changed = SetWindowPos(window, NULL, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER |
            SWP_NOACTIVATE | SWP_NOSENDCHANGING | SWP_SHOWWINDOW);
        return !IsWindow(window) || (changed && IsWindowVisible(window));
    }
    if (state == SW_SHOWMINIMIZED) {
        /* Leave originally minimized windows alone. A taskbar/Alt+Tab selection
         * may already have restored one; do not minimize the selected app again. */
        if (!IsWindowVisible(window))
            ms_show_without_animation(window, IsIconic(window) ? SW_SHOWMINNOACTIVE : SW_SHOWNA);
    } else if (IsIconic(window) || !IsWindowVisible(window)) {
        ms_show_without_animation(window, state == SW_SHOWMAXIMIZED ? SW_SHOWMAXIMIZED : SW_SHOWNOACTIVATE);
    }
    return !IsWindow(window) || (IsWindowVisible(window) &&
        (state == SW_SHOWMINIMIZED || !IsIconic(window)));
}

static const MSOperations OPERATIONS = {NULL, set_tag, clear_tag, hide_window, show_window};

static BOOL CALLBACK collect_display(HMONITOR monitor, HDC dc, LPRECT rect, LPARAM parameter)
{
    (void)dc;
    (void)rect;
    Displays *displays = (Displays *)parameter;
    if (displays->count == MS_MAX_MONITORS) {
        displays->failed = true;
        return FALSE;
    }
    Display *display = &displays->items[displays->count];
    *display = (Display){.handle = monitor};
    display->info.cbSize = sizeof(display->info);
    if (!GetMonitorInfoW(monitor, (MONITORINFO *)&display->info)) {
        displays->failed = true;
        return FALSE;
    }
    ++displays->count;
    return TRUE;
}

static int display_order(const void *left, const void *right)
{
    return wcscmp(((const Display *)left)->info.szDevice,
                  ((const Display *)right)->info.szDevice);
}

static bool snapshot_displays(Displays *displays)
{
    *displays = (Displays){0};
    if (!EnumDisplayMonitors(NULL, NULL, collect_display, (LPARAM)displays) ||
        displays->failed || !displays->count)
        return false;
    qsort(displays->items, displays->count, sizeof(Display), display_order);
    return true;
}

static bool same_displays(const Displays *left, const Displays *right)
{
    if (left->count != right->count)
        return false;
    for (size_t i = 0; i < left->count; ++i) {
        const Display *a = &left->items[i], *b = &right->items[i];
        if (a->handle != b->handle || wcscmp(a->info.szDevice, b->info.szDevice) ||
            !EqualRect(&a->info.rcMonitor, &b->info.rcMonitor) ||
            !EqualRect(&a->info.rcWork, &b->info.rcWork) || a->info.dwFlags != b->info.dwFlags)
            return false;
    }
    return true;
}

static void update_panel(void)
{
    if (!g_panel)
        return;
    update_tray();
    if (!IsWindowVisible(g_panel))
        return;
    POINT cursor;
    HMONITOR pointed = GetCursorPos(&cursor) ? MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST) : NULL;
    wchar_t text[12288] = L"";
    size_t used = 0;
    for (size_t i = 0; i < g_displays.count; ++i) {
        Display *display = &g_displays.items[i];
        int written = swprintf(text + used, sizeof(text) / sizeof(text[0]) - used,
            L"%lsDisplay %zu: %ls%ls\r\n    Workspace %u of %u   |   %ld x %ld\r\n\r\n",
            display->handle == pointed ? L"> " : L"  ", i + 1,
            display->info.szDevice, (display->info.dwFlags & MONITORINFOF_PRIMARY) ? L" (primary)" : L"",
            ms_active(&g_state, (MSHandle)display->handle),
            ms_workspace_count(&g_state, (MSHandle)display->handle),
            display->info.rcMonitor.right - display->info.rcMonitor.left,
            display->info.rcMonitor.bottom - display->info.rcMonitor.top);
        if (written < 0)
            break;
        used += (size_t)written;
    }
    wchar_t previous[12288];
    GetWindowTextW(g_list, previous, (int)(sizeof(previous) / sizeof(previous[0])));
    if (wcscmp(previous, text))
        SetWindowTextW(g_list, text);
    SetWindowTextW(g_status, g_message);
}

static MSResult recover_windows(void)
{
    ms_carousel_close();
    ms_transition_cancel();
    Windows windows;
    MSResult result = {0};
    if (!snapshot_windows(&windows)) {
        result.errors = 1;
    } else {
        ms_restore(&g_state, windows.items, windows.count, &OPERATIONS, &result);
        free(windows.items);
    }
    g_recovery_needed = result.errors != 0;
    if (result.errors)
        swprintf(g_message, sizeof(g_message) / sizeof(g_message[0]),
            L"Recovery incomplete (%zu errors). Tags were retained. Retry Ctrl+Alt+0; "
            L"use the same privilege level as the previous run.", result.errors);
    else
        swprintf(g_message, sizeof(g_message) / sizeof(g_message[0]),
            L"Restored %zu tagged windows. All displays are on workspace 1.", result.shown);
    return result;
}

static void display_settings_id(const Display *display, wchar_t id[128])
{
    for (DWORD index = 0; ; ++index) {
        DISPLAY_DEVICEW device = {.cb = sizeof(device)};
        if (!EnumDisplayDevicesW(display->info.szDevice, index, &device, EDD_GET_DEVICE_INTERFACE_NAME))
            break;
        if ((device.StateFlags & DISPLAY_DEVICE_ACTIVE) && device.DeviceID[0]) {
            wcscpy(id, device.DeviceID);
            return;
        }
    }
    wcscpy(id, display->info.szDevice);
}

static void load_workspace_counts(void)
{
    for (size_t i = 0; i < g_displays.count; ++i) {
        wchar_t id[128];
        display_settings_id(&g_displays.items[i], id);
        DWORD count = 0, bytes = sizeof(count);
        if (RegGetValueW(HKEY_CURRENT_USER, COUNTS_KEY, id, RRF_RT_REG_DWORD, NULL, &count, &bytes) == ERROR_SUCCESS)
            ms_set_workspace_count(&g_state, (MSHandle)g_displays.items[i].handle, count);
    }
}

static bool save_workspace_count(MSHandle monitor, unsigned count)
{
    for (size_t i = 0; i < g_displays.count; ++i) {
        if ((MSHandle)g_displays.items[i].handle != monitor)
            continue;
        wchar_t id[128];
        display_settings_id(&g_displays.items[i], id);
        HKEY key;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, COUNTS_KEY, 0, NULL, REG_OPTION_NON_VOLATILE,
                            KEY_SET_VALUE, NULL, &key, NULL) != ERROR_SUCCESS)
            return false;
        DWORD value = count;
        LSTATUS status = RegSetValueExW(key, id, 0, REG_DWORD, (const BYTE *)&value, sizeof(value));
        RegCloseKey(key);
        return status == ERROR_SUCCESS;
    }
    return false;
}

static bool sync_displays(void)
{
    Displays displays;
    if (!snapshot_displays(&displays)) {
        /* Even if enumeration fails during disconnect, recover by HWND first. */
        recover_windows();
        g_topology_dirty = true;
        wcscpy(g_message, L"Display enumeration failed (limit: 64). Windows were offered recovery. Switching is paused.");
        return false;
    }
    if (g_topology_dirty || !same_displays(&g_displays, &displays)) {
        MSResult result = recover_windows();
        MSHandle ids[MS_MAX_MONITORS];
        for (size_t i = 0; i < displays.count; ++i)
            ids[i] = (MSHandle)displays.items[i].handle;
        if (!ms_set_monitors(&g_state, ids, displays.count))
            return false;
        g_displays = displays;
        load_workspace_counts();
        g_topology_dirty = false;
        if (!result.errors)
            wcscpy(g_message, L"Display configuration updated. Windows restored; all workspaces reset to 1.");
    }
    return !g_recovery_needed;
}

static void switch_on_monitor(HMONITOR target, unsigned workspace, int navigation)
{
    if (ms_active(&g_state, (MSHandle)target) == workspace) {
        swprintf(g_message, sizeof(g_message) / sizeof(g_message[0]),
            L"The display under the pointer is already on workspace %u.", workspace);
        return;
    }
    Windows windows;
    if (!snapshot_windows(&windows)) {
        wcscpy(g_message, L"Could not enumerate windows. No windows changed.");
        return;
    }
    MSResult result;
    HWND foreground = GetForegroundWindow();
    int direction = ms_slide_direction(ms_active(&g_state, (MSHandle)target), workspace, navigation);
    ms_transition_begin(target, direction);
    bool valid = ms_switch(&g_state, (MSHandle)target, workspace,
                           windows.items, windows.count, &OPERATIONS, &result);
    free(windows.items);
    /* Maximizing a restored app can activate it. Preserve a selected app or
     * focus on another monitor, provided it was not just minimized by us. */
    if (IsWindow(foreground) && IsWindowVisible(foreground) && !IsIconic(foreground))
        SetForegroundWindow(foreground);
    if (!valid) {
        ms_transition_cancel();
        g_topology_dirty = true;
        wcscpy(g_message, L"The target display changed. Retry after display recovery.");
        return;
    }
    ms_transition_commit();
    swprintf(g_message, sizeof(g_message) / sizeof(g_message[0]),
        L"Workspace %u: minimized %zu, restored %zu. %zu errors.%ls",
        workspace, result.hidden, result.shown, result.errors,
        result.errors ? L" Some apps could not be managed; Ctrl+Alt+0 retries recovery." : L"");
}

static void switch_at_pointer(unsigned workspace, int direction)
{
    ms_carousel_close();
    if (!sync_displays())
        return;
    POINT cursor;
    if (!GetCursorPos(&cursor)) {
        wcscpy(g_message, L"Could not read the pointer position. No windows changed.");
        return;
    }
    HMONITOR target = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
    if (direction)
        workspace = ms_adjacent(&g_state, (MSHandle)target, direction);
    switch_on_monitor(target, workspace, direction);
}

static MSHandle window_group(MSHandle handle)
{
    HWND root = GetAncestor((HWND)handle, GA_ROOTOWNER);
    return root ? (MSHandle)root : handle;
}

static bool carousel_load(MSHandle monitor, MSCarouselItem **items, size_t *count, unsigned *active, unsigned *spaces)
{
    *items = NULL;
    *count = 0;
    *active = ms_active(&g_state, monitor);
    *spaces = ms_workspace_count(&g_state, monitor);
    if (!*active)
        return false;
    Windows windows;
    if (!snapshot_windows(&windows))
        return false;
    MSCarouselItem *result = windows.count ? calloc(windows.count, sizeof(*result)) : NULL;
    if (windows.count && !result) {
        free(windows.items);
        return false;
    }
    for (size_t i = 0; i < windows.count; ++i) {
        MSWindow *window = &windows.items[i];
        if (window->monitor != monitor)
            continue;
        DWORD process = 0;
        if (!GetWindowThreadProcessId((HWND)window->id, &process))
            continue;
        MSHandle group = window_group(window->id);
        size_t index = 0;
        while (index < *count && window_group(result[index].window) != group)
            ++index;
        if (index < *count && window->id != group)
            continue;
        if (index == *count)
            ++*count;
        result[index].window = window->id;
        result[index].workspace = ms_valid_tag(window->tag) ? window->tag : *active;
        result[index].process = process;
        if (!GetWindowTextW((HWND)window->id, result[index].title, 256))
            wcscpy(result[index].title, L"Untitled window");
    }
    free(windows.items);
    *items = result;
    return true;
}

static bool carousel_move(MSHandle monitor, MSHandle handle, DWORD process, unsigned destination,
                          wchar_t *message, size_t capacity)
{
    DWORD current_process = 0;
    GetWindowThreadProcessId((HWND)handle, &current_process);
    if (g_busy || !IsWindow((HWND)handle) || current_process != process) {
        swprintf(message, capacity, L"That app is no longer available. Press F5 to refresh.");
        return false;
    }
    g_busy = true;
    if (!sync_displays() || !ms_carousel_visible()) {
        g_busy = false;
        return false; /* Display recovery closes the carousel. */
    }
    Windows windows;
    if (!snapshot_windows(&windows)) {
        swprintf(message, capacity, L"Could not read the app windows. Try again.");
        g_busy = false;
        return false;
    }
    MSHandle group = window_group(handle);
    size_t count = 0;
    bool found = false;
    for (size_t i = 0; i < windows.count; ++i) {
        MSWindow window = windows.items[i];
        if (window.monitor == monitor && window_group(window.id) == group) {
            windows.items[count++] = window;
            if (window.id == handle)
                found = true;
        }
    }
    MSResult result = {0};
    bool moved = found && ms_move(&g_state, monitor, destination, windows.items, count, &OPERATIONS, &result);
    free(windows.items);
    if (!moved)
        swprintf(message, capacity, L"The app closed or changed displays. Press F5 to refresh.");
    else if (result.errors)
        swprintf(message, capacity, L"Move incomplete (%zu errors). Ctrl+Alt+0 can restore managed windows.", result.errors);
    else
        swprintf(message, capacity, L"App moved to workspace %u.", destination);
    g_busy = false;
    update_panel();
    return moved && !result.errors;
}

static void carousel_activate(MSHandle monitor, unsigned workspace)
{
    if (g_busy)
        return;
    g_busy = true;
    if (sync_displays())
        switch_on_monitor((HMONITOR)monitor, workspace, 0);
    update_panel();
    g_busy = false;
}

static unsigned carousel_add(MSHandle monitor, wchar_t *message, size_t capacity)
{
    if (g_busy)
        return 0;
    g_busy = true;
    if (!sync_displays() || !ms_carousel_visible()) {
        g_busy = false;
        return 0;
    }
    unsigned count = ms_add_workspace(&g_state, monitor);
    if (!count)
        swprintf(message, capacity, L"This monitor supports up to %u workspaces.", MS_MAX_WORKSPACES);
    else if (!save_workspace_count(monitor, count))
        swprintf(message, capacity, L"Workspace %u added for this run; the count could not be saved.", count);
    else
        swprintf(message, capacity, L"Workspace %u added. Your current workspace has not changed.", count);
    update_panel();
    g_busy = false;
    return count;
}

static void carousel_focus(MSHandle monitor, MSHandle handle, DWORD process)
{
    if (g_busy)
        return;
    g_busy = true;
    DWORD current_process = 0;
    HWND window = (HWND)handle;
    if (sync_displays() && IsWindow(window) &&
        GetWindowThreadProcessId(window, &current_process) && current_process == process &&
        (MSHandle)MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST) == monitor) {
        unsigned workspace = window_tag(window);
        if (!workspace)
            workspace = ms_active(&g_state, monitor);
        switch_on_monitor((HMONITOR)monitor, workspace, 0);
        ms_transition_cancel(); /* Open the selected app immediately, without a frozen preview. */
        /* Selecting a minimized preview explicitly opens it, including windows
         * the user had minimized before entering the workspace utility. */
        if (IsIconic(window))
            ms_show_without_animation(window, SW_RESTORE);
        HWND popup = GetLastActivePopup(window);
        if (popup && IsWindowVisible(popup)) {
            window = popup;
            if (IsIconic(window))
                ms_show_without_animation(window, SW_RESTORE);
        }
        SetForegroundWindow(window);
    }
    update_panel();
    g_busy = false;
}

static void toggle_carousel(void)
{
    if (ms_carousel_visible()) {
        ms_carousel_close();
        return;
    }
    if (!sync_displays())
        return;
    ms_transition_cancel();
    POINT cursor;
    if (!GetCursorPos(&cursor))
        return;
    MSHandle monitor = (MSHandle)MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
    const MSCarouselActions actions = {carousel_load, carousel_move, carousel_activate, carousel_add, carousel_focus};
    if (!ms_carousel_open(GetModuleHandleW(NULL), monitor, &actions)) {
        wcscpy(g_message, L"Could not open the workspace overview.");
        MessageBoxW(g_panel, g_message, L"Monitor Spaces", MB_OK | MB_ICONERROR);
    }
}

static void follow_foreground(void)
{
    /* Taskbar and Alt+Tab can restore an inactive app. Follow its membership,
     * using the app's monitor rather than the pointer's. Poll current focus so
     * queued activation events from our own minimize/restore calls are ignored. */
    HWND window = GetForegroundWindow();
    unsigned workspace = window_tag(window);
    if (!ms_valid_tag(workspace) || IsIconic(window) || !IsWindowVisible(window))
        return;
    if (!sync_displays())
        return;
    workspace = window_tag(window); /* Display recovery may have cleared it. */
    if (!ms_valid_tag(workspace) || window != GetForegroundWindow())
        return;
    HMONITOR monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
    if (ms_active(&g_state, (MSHandle)monitor) != workspace) {
        ms_carousel_close();
        switch_on_monitor(monitor, workspace, 0);
        update_panel();
    }
}

static int px(int value)
{
    return MulDiv(value, (int)g_dpi, 96);
}

static void layout_panel(void)
{
    RECT rect;
    GetClientRect(g_panel, &rect);
    int width = rect.right, height = rect.bottom;
    int margin = px(20), available = width - 2 * margin;
    int list_height = height - px(244);
    if (list_height < px(40))
        list_height = px(40);
    MoveWindow(g_intro, margin, margin, available, px(82), TRUE);
    MoveWindow(g_list, margin, px(110), available, list_height, TRUE);
    MoveWindow(g_status, margin, height - px(120), available, px(62), TRUE);
    MoveWindow(g_restore, margin, height - px(48), px(190), px(30), TRUE);
    MoveWindow(g_exit, width - margin - px(190), height - px(48), px(190), px(30), TRUE);
}

static void set_panel_font(void)
{
    HFONT font = CreateFontW(-px(14), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    if (!font)
        return;
    HWND controls[] = {g_intro, g_list, g_status, g_restore, g_exit};
    for (size_t i = 0; i < sizeof(controls) / sizeof(controls[0]); ++i)
        SendMessageW(controls[i], WM_SETFONT, (WPARAM)font, TRUE);
    if (g_font)
        DeleteObject(g_font);
    g_font = font;
}

static LRESULT CALLBACK panel_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (g_taskbar_created && message == g_taskbar_created) {
        /* Explorer owns the notification area and loses its icons on restart. */
        g_tray_added = false;
        update_tray();
        return 0;
    }
    switch (message) {
    case WM_SHOW_PANEL:
        show_panel();
        update_panel();
        return 0;
    case WM_TRAY: {
        UINT event = g_tray_version4 ? LOWORD(lparam) : (UINT)lparam;
        UINT id = g_tray_version4 ? HIWORD(lparam) : (UINT)wparam;
        if (id != TRAY_ID)
            return 0;
        if (event == NIN_SELECT || event == NIN_KEYSELECT ||
            (!g_tray_version4 && event == WM_LBUTTONUP)) {
            show_panel();
            update_panel();
        } else if (event == WM_CONTEXTMENU || (!g_tray_version4 && event == WM_RBUTTONUP)) {
            POINT point = {(short)LOWORD(wparam), (short)HIWORD(wparam)};
            if (!g_tray_version4 || (point.x == -1 && point.y == -1)) {
                if (!GetCursorPos(&point))
                    return 0;
            }
            tray_menu(point);
        }
        return 0;
    }
    case WM_HOTKEY:
    case WM_NAVIGATE:
    case WM_RECOVER:
    case WM_COMMAND:
        if (g_busy)
            return 0;
        if (message == WM_COMMAND && LOWORD(wparam) == ID_EXIT) {
            PostMessageW(window, WM_EXIT, 0, 0);
            return 0;
        }
        if (message == WM_COMMAND && LOWORD(wparam) == ID_SHOW_PANEL) {
            show_panel();
            update_panel();
            return 0;
        }
        g_busy = true;
        if (message == WM_RECOVER || (message == WM_COMMAND && LOWORD(wparam) == ID_RESTORE) ||
            (message == WM_HOTKEY && wparam == 0))
            recover_windows();
        else if (message == WM_HOTKEY && wparam >= 1 && wparam <= DIRECT_WORKSPACES)
            switch_at_pointer((unsigned)wparam, 0);
        else if (message == WM_NAVIGATE && (wparam == VK_LEFT || wparam == VK_RIGHT))
            switch_at_pointer(0, wparam == VK_LEFT ? -1 : 1);
        else if (message == WM_NAVIGATE && wparam == VK_UP)
            toggle_carousel();
        update_panel();
        g_busy = false;
        return 0;
    case WM_NAVIGATION_ERROR:
        swprintf(g_message, sizeof(g_message) / sizeof(g_message[0]),
            L"Could not intercept Win+Arrow (Windows error %lu). Use Ctrl+Alt+1-4 for workspace switching.", (DWORD)wparam);
        update_panel();
        return 0;
    case WM_DISPLAYCHANGE:
        ms_carousel_close();
        ms_transition_cancel();
        g_topology_dirty = true;
        return 0;
    case WM_TIMER:
        if (wparam == TIMER_INDICATOR && !g_busy) {
            g_busy = true;
            follow_foreground();
            update_tray();
            g_busy = false;
            return 0;
        }
        if (wparam == TIMER_TOPOLOGY && !g_busy) {
            g_busy = true;
            sync_displays();
            update_panel();
            g_busy = false;
        }
        return 0;
    case WM_DPICHANGED: {
        g_dpi = HIWORD(wparam);
        RECT *suggested = (RECT *)lparam;
        set_panel_font();
        SetWindowPos(window, NULL, suggested->left, suggested->top,
                     suggested->right - suggested->left, suggested->bottom - suggested->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        layout_panel();
        return 0;
    }
    case WM_SIZE:
        if (wparam == SIZE_MINIMIZED && g_tray_added) {
            ShowWindow(window, SW_HIDE);
            return 0;
        }
        if (g_intro)
            layout_panel();
        return 0;
    case WM_GETMINMAXINFO: {
        MINMAXINFO *info = (MINMAXINFO *)lparam;
        info->ptMinTrackSize.x = px(500);
        info->ptMinTrackSize.y = px(380);
        return 0;
    }
    case WM_QUERYENDSESSION:
        recover_windows();
        return TRUE;
    case WM_ENDSESSION:
        if (wparam)
            DestroyWindow(window);
        return 0;
    case WM_CLOSE:
        if (g_tray_added) {
            ShowWindow(window, SW_HIDE);
            return 0;
        }
        /* Keep a way out when the notification area is unavailable. */
        PostMessageW(window, WM_EXIT, 0, 0);
        return 0;
    case WM_EXIT:
        if (g_busy) {
            PostMessageW(window, WM_EXIT, 0, 0);
            return 0;
        }
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        KillTimer(window, TIMER_TOPOLOGY);
        KillTimer(window, TIMER_INDICATOR);
        remove_tray();
        stop_keyboard();
        for (unsigned i = 0; i <= DIRECT_WORKSPACES; ++i)
            if (g_registered & (1u << i))
                UnregisterHotKey(window, (int)i);
        recover_windows();
        g_panel = NULL;
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(window, message, wparam, lparam);
    }
}

static bool create_panel(HINSTANCE instance)
{
    g_taskbar_created = RegisterWindowMessageW(L"TaskbarCreated");
    if (!g_taskbar_created)
        return false;
    WNDCLASSEXW cls = {.cbSize = sizeof(cls), .lpfnWndProc = panel_proc,
        .hInstance = instance, .hIcon = LoadIconW(NULL, IDI_APPLICATION),
        .hCursor = LoadCursorW(NULL, IDC_ARROW), .hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1),
        .lpszClassName = PANEL_CLASS};
    if (!RegisterClassExW(&cls))
        return false;
    g_panel = CreateWindowExW(WS_EX_CONTROLPARENT | WS_EX_TOOLWINDOW, PANEL_CLASS, L"Monitor Spaces",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 620, 500,
        NULL, NULL, instance, NULL);
    if (!g_panel)
        return false;
    g_dpi = GetDpiForWindow(g_panel);
    if (!g_dpi)
        g_dpi = 96;
    g_intro = CreateWindowExW(0, L"STATIC",
        L"Point at a display; Win+Left/Right switches, Win+Up organizes apps.\r\n"
        L"Ctrl+Alt+1-4 jumps directly to a workspace.\r\n"
        L"Ctrl+Alt+0 restores all managed windows on every display.\r\n"
        L"Close to keep running in the tray. Use Restore and exit to quit.",
        WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, g_panel, NULL, instance, NULL);
    g_list = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
        0, 0, 0, 0, g_panel, NULL, instance, NULL);
    g_status = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE,
        0, 0, 0, 0, g_panel, NULL, instance, NULL);
    g_restore = CreateWindowExW(0, L"BUTTON", L"&Restore all (Ctrl+Alt+0)",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        0, 0, 0, 0, g_panel, (HMENU)(INT_PTR)ID_RESTORE, instance, NULL);
    g_exit = CreateWindowExW(0, L"BUTTON", L"Restore and e&xit",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        0, 0, 0, 0, g_panel, (HMENU)(INT_PTR)ID_EXIT, instance, NULL);
    if (!g_intro || !g_list || !g_status || !g_restore || !g_exit)
        return false;
    set_panel_font();
    RECT rect = {0, 0, px(590), px(450)};
    AdjustWindowRectExForDpi(&rect, WS_OVERLAPPEDWINDOW, FALSE,
                             WS_EX_CONTROLPARENT | WS_EX_TOOLWINDOW, g_dpi);
    SetWindowPos(g_panel, NULL, 0, 0, rect.right - rect.left, rect.bottom - rect.top,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    layout_panel();
    return true;
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, PWSTR command_line, int show)
{
    (void)previous;
    (void)command_line;
    (void)show;
    int argc = 0;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv)
        return 1;
    bool restore_only = argc == 2 && wcscmp(argv[1], L"--restore") == 0;
    bool open_panel = argc == 2 && wcscmp(argv[1], L"--panel") == 0;
    bool valid = argc == 1 || restore_only || open_panel;
    LocalFree(argv);
    if (!valid) {
        MessageBoxW(NULL, L"Usage: MonitorSpaces.exe [--panel | --restore]\n\n"
                    L"Default: run in the background with a numbered tray icon.\n"
                    L"--panel also opens the control panel.\n"
                    L"--restore restores tagged windows and exits, or asks the running instance to restore.",
                    L"Monitor Spaces", MB_OK | MB_ICONINFORMATION);
        return 1;
    }

    HANDLE mutex = CreateMutexW(NULL, FALSE, MUTEX_NAME);
    DWORD mutex_error = GetLastError();
    if (!mutex) {
        MessageBoxW(NULL, L"Could not acquire the single-instance guard. Another copy may be running at a different privilege level.",
                    L"Monitor Spaces", MB_OK | MB_ICONERROR);
        return 1;
    }
    if (mutex_error == ERROR_ALREADY_EXISTS) {
        HWND panel = FindWindowW(PANEL_CLASS, NULL);
        if (panel) {
            if (restore_only) {
                if (!PostMessageW(panel, WM_RECOVER, 0, 0)) {
                    MessageBoxW(NULL, L"Could not request recovery. Run at the same privilege level as the running copy.",
                                L"Monitor Spaces", MB_OK | MB_ICONERROR);
                    CloseHandle(mutex);
                    return 1;
                }
            } else {
                if (!PostMessageW(panel, WM_SHOW_PANEL, 0, 0)) {
                    MessageBoxW(NULL, L"Could not open the panel. Run at the same privilege level as the running copy.",
                                L"Monitor Spaces", MB_OK | MB_ICONERROR);
                    CloseHandle(mutex);
                    return 1;
                }
            }
        } else {
            MessageBoxW(NULL, L"Monitor Spaces is already starting or recovering windows. Try again shortly.",
                        L"Monitor Spaces", MB_OK | MB_ICONINFORMATION);
        }
        CloseHandle(mutex);
        return panel ? 0 : 1;
    }

    MSResult recovered = recover_windows();
    if (restore_only) {
        if (recovered.errors)
            MessageBoxW(NULL, g_message, L"Monitor Spaces recovery", MB_OK | MB_ICONWARNING);
        CloseHandle(mutex);
        return recovered.errors ? 1 : 0;
    }
    /* No foreign windows are hidden until recovery, display discovery, panel
     * creation, shortcut registration, and keyboard-hook setup have completed. */
    g_topology_dirty = true;
    bool displays_ok = sync_displays();
    if (!displays_ok && !g_displays.count) {
        MessageBoxW(NULL, g_message, L"Monitor Spaces", MB_OK | MB_ICONERROR);
        CloseHandle(mutex);
        return 1;
    }
    if (!create_panel(instance)) {
        if (g_panel)
            DestroyWindow(g_panel);
        MessageBoxW(NULL, L"Could not create the control panel. No workspace switch was performed.",
                    L"Monitor Spaces", MB_OK | MB_ICONERROR);
        if (g_font)
            DeleteObject(g_font);
        CloseHandle(mutex);
        return 1;
    }
    for (unsigned i = 0; i <= DIRECT_WORKSPACES; ++i) {
        if (!RegisterHotKey(g_panel, (int)i, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, '0' + i)) {
            DWORD error = GetLastError();
            wchar_t message[256];
            swprintf(message, sizeof(message) / sizeof(message[0]),
                L"Could not register Ctrl+Alt+%u (Windows error %lu).\n\n"
                L"Close the app using this shortcut and try again. Monitor Spaces will exit.", i, error);
            MessageBoxW(g_panel, message, L"Monitor Spaces shortcut unavailable", MB_OK | MB_ICONERROR);
            DestroyWindow(g_panel);
            DeleteObject(g_font);
            CloseHandle(mutex);
            return 1;
        }
        g_registered |= 1u << i;
    }
    if (!start_keyboard(instance)) {
        DWORD error = GetLastError();
        wchar_t message[256];
        swprintf(message, sizeof(message) / sizeof(message[0]),
            L"Could not enable Win+Left/Right/Up (Windows error %lu). Monitor Spaces will exit.", error);
        MessageBoxW(g_panel, message, L"Monitor Spaces keyboard shortcut unavailable", MB_OK | MB_ICONERROR);
        DestroyWindow(g_panel);
        DeleteObject(g_font);
        CloseHandle(mutex);
        return 1;
    }
    if (!SetTimer(g_panel, TIMER_TOPOLOGY, 1000, NULL) ||
        !SetTimer(g_panel, TIMER_INDICATOR, 250, NULL)) {
        MessageBoxW(g_panel, L"Could not start display monitoring. Monitor Spaces will exit.",
                    L"Monitor Spaces", MB_OK | MB_ICONERROR);
        DestroyWindow(g_panel);
        DeleteObject(g_font);
        CloseHandle(mutex);
        return 1;
    }
    g_tray_enabled = true;
    bool tray_ok = update_tray();
    if (!tray_ok)
        wcscpy(g_message, L"The notification icon is unavailable. The panel stays open; the icon will be retried.");
    if (open_panel || !tray_ok || g_recovery_needed)
        show_panel();
    update_panel();
    MSG message;
    int status;
    while ((status = GetMessageW(&message, NULL, 0, 0)) > 0) {
        if (!g_panel || !IsDialogMessageW(g_panel, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    if (g_panel)
        DestroyWindow(g_panel);
    if (g_font)
        DeleteObject(g_font);
    if (g_recovery_needed)
        MessageBoxW(NULL, g_message, L"Monitor Spaces recovery incomplete", MB_OK | MB_ICONWARNING);
    CloseHandle(mutex);
    return status == -1 || g_recovery_needed ? 1 : 0;
}
