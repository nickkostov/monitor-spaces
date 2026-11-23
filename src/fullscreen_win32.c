#ifndef UNICODE
#define UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <shellapi.h>
#include <stdint.h>
#include <limits.h>

#include "fullscreen_win32.h"

static const wchar_t SETTINGS[] = L"Software\\MonitorSpaces";
static const wchar_t BAR_SAVED[] = L"OriginalTaskbarAutoHide.v1";
static const wchar_t MARK[] = L"MonitorSpaces.Fullscreen.v1.{52617889-437D-4910-B50A-70607E152BC2}";
static const wchar_t PRESERVE_SIZE[] = L"MonitorSpaces.PreserveSize.v1.{52617889-437D-4910-B50A-70607E152BC2}";
static const wchar_t NONRUDE[] = L"NonRudeHWND";
static const wchar_t OLD_NONRUDE[] = L"MonitorSpaces.Fullscreen.NonRude.v1.{52617889-437D-4910-B50A-70607E152BC2}";
/* All values live on the foreign HWND as encoded integers, never local pointers.
 * The final MARK commits the journal before any style/geometry changes. */
static const wchar_t *const FIELDS[] = {
    L"MonitorSpaces.Fullscreen.Style.v1.{52617889-437D-4910-B50A-70607E152BC2}",
    L"MonitorSpaces.Fullscreen.Flags.v1.{52617889-437D-4910-B50A-70607E152BC2}",
    L"MonitorSpaces.Fullscreen.Show.v1.{52617889-437D-4910-B50A-70607E152BC2}",
    L"MonitorSpaces.Fullscreen.MinX.v1.{52617889-437D-4910-B50A-70607E152BC2}",
    L"MonitorSpaces.Fullscreen.MinY.v1.{52617889-437D-4910-B50A-70607E152BC2}",
    L"MonitorSpaces.Fullscreen.MaxX.v1.{52617889-437D-4910-B50A-70607E152BC2}",
    L"MonitorSpaces.Fullscreen.MaxY.v1.{52617889-437D-4910-B50A-70607E152BC2}",
    L"MonitorSpaces.Fullscreen.Left.v1.{52617889-437D-4910-B50A-70607E152BC2}",
    L"MonitorSpaces.Fullscreen.Top.v1.{52617889-437D-4910-B50A-70607E152BC2}",
    L"MonitorSpaces.Fullscreen.Right.v1.{52617889-437D-4910-B50A-70607E152BC2}",
    L"MonitorSpaces.Fullscreen.Bottom.v1.{52617889-437D-4910-B50A-70607E152BC2}"
};
static bool g_enabled;
static bool (*g_eligible)(HWND);

void ms_fullscreen_configure(bool (*eligible)(HWND)) { g_eligible = eligible; }
bool ms_fullscreen_enabled(void) { return g_enabled; }
void ms_fullscreen_suspend(void) { g_enabled = false; }

static bool write_setting(const wchar_t *name, DWORD value)
{
    HKEY key;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, SETTINGS, 0, NULL, 0, KEY_SET_VALUE, NULL, &key, NULL) != ERROR_SUCCESS)
        return false;
    LSTATUS result = RegSetValueExW(key, name, 0, REG_DWORD, (BYTE *)&value, sizeof(value));
    RegCloseKey(key);
    return result == ERROR_SUCCESS;
}

bool ms_fullscreen_preferred(void)
{
    DWORD value = 1, size = sizeof(value);
    return RegGetValueW(HKEY_CURRENT_USER, SETTINGS, L"FullscreenEnabled", RRF_RT_REG_DWORD,
                        NULL, &value, &size) != ERROR_SUCCESS || value != 0;
}

void ms_fullscreen_save_preference(bool enabled)
{
    write_setting(L"FullscreenEnabled", enabled ? 1u : 0u);
}

static bool auto_hide(bool enabled)
{
    HWND bar = FindWindowW(L"Shell_TrayWnd", NULL);
    if (!bar)
        return false;
    APPBARDATA data = {.cbSize = sizeof(data), .hWnd = bar};
    UINT state = (UINT)SHAppBarMessage(ABM_GETSTATE, &data);
    if (((state & ABS_AUTOHIDE) != 0) == enabled)
        return true;
    /* GETSTATE no longer reports ALWAYSONTOP on Windows 7+, where it is always
     * enabled. Preserve that behavior while changing only auto-hide. */
    data.lParam = (state | ABS_ALWAYSONTOP) & ~ABS_AUTOHIDE;
    if (enabled) data.lParam |= ABS_AUTOHIDE;
    SHAppBarMessage(ABM_SETSTATE, &data); /* Return value is unconditional TRUE. */
    return (((UINT)SHAppBarMessage(ABM_GETSTATE, &data) & ABS_AUTOHIDE) != 0) == enabled;
}

bool ms_fullscreen_enable(void)
{
    if (g_enabled)
        return true;
    if (!FindWindowW(L"Shell_TrayWnd", NULL))
        return false;
    DWORD previous = 0, size = sizeof(previous);
    LSTATUS pending = RegGetValueW(HKEY_CURRENT_USER, SETTINGS, BAR_SAVED, RRF_RT_REG_DWORD,
                                   NULL, &previous, &size);
    if (pending != ERROR_FILE_NOT_FOUND && pending != ERROR_PATH_NOT_FOUND)
        return false; /* Never overwrite an outstanding recovery record. */
    APPBARDATA data = {.cbSize = sizeof(data)};
    previous = (SHAppBarMessage(ABM_GETSTATE, &data) & ABS_AUTOHIDE) != 0;
    if (!write_setting(BAR_SAVED, previous))
        return false;
    if (!auto_hide(true)) {
        ms_fullscreen_restore();
        return false;
    }
    g_enabled = true;
    ms_fullscreen_update();
    return true;
}

static void discard_journal(HWND window)
{
    /* Remove MARK first: without it the remaining fields are inert. */
    RemovePropW(window, MARK);
    if (GetPropW(window, MARK))
        return;
    for (size_t i = 0; i < sizeof(FIELDS) / sizeof(FIELDS[0]); ++i)
        RemovePropW(window, FIELDS[i]);
    RemovePropW(window, OLD_NONRUDE);
}

static bool save_window(HWND window)
{
    WINDOWPLACEMENT placement = {.length = sizeof(placement)};
    if (!GetWindowPlacement(window, &placement) || IsIconic(window))
        return false;
    DWORD values[] = {(DWORD)GetWindowLongPtrW(window, GWL_STYLE), placement.flags, placement.showCmd,
        (DWORD)placement.ptMinPosition.x, (DWORD)placement.ptMinPosition.y,
        (DWORD)placement.ptMaxPosition.x, (DWORD)placement.ptMaxPosition.y,
        (DWORD)placement.rcNormalPosition.left, (DWORD)placement.rcNormalPosition.top,
        (DWORD)placement.rcNormalPosition.right, (DWORD)placement.rcNormalPosition.bottom};
    for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
        if (!SetPropW(window, FIELDS[i], (HANDLE)((ULONG_PTR)values[i] + 1))) {
            discard_journal(window);
            return false;
        }
    }
    HANDLE nonrude = GetPropW(window, NONRUDE);
    if (nonrude && !SetPropW(window, OLD_NONRUDE, nonrude)) {
        discard_journal(window);
        return false;
    }
    if (!nonrude) RemovePropW(window, OLD_NONRUDE);
    if (SetPropW(window, MARK, (HANDLE)1))
        return true;
    discard_journal(window);
    return false;
}

static bool read_window(HWND window, DWORD *style, WINDOWPLACEMENT *placement)
{
    DWORD values[sizeof(FIELDS) / sizeof(FIELDS[0])];
    if (!GetPropW(window, MARK))
        return false;
    for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
        ULONG_PTR encoded = (ULONG_PTR)GetPropW(window, FIELDS[i]);
        if (!encoded || encoded - 1 > UINT32_MAX)
            return false;
        values[i] = (DWORD)(encoded - 1);
    }
    *style = values[0];
    *placement = (WINDOWPLACEMENT){.length = sizeof(*placement), .flags = values[1], .showCmd = values[2],
        .ptMinPosition = {(LONG)values[3], (LONG)values[4]}, .ptMaxPosition = {(LONG)values[5], (LONG)values[6]},
        .rcNormalPosition = {(LONG)values[7], (LONG)values[8], (LONG)values[9], (LONG)values[10]}};
    return values[2] >= SW_SHOWNORMAL && values[2] <= SW_SHOWMAXIMIZED;
}

unsigned ms_fullscreen_original_state(HWND window)
{
    WINDOWPLACEMENT placement;
    DWORD style;
    return read_window(window, &style, &placement) ? placement.showCmd : 0;
}

static bool set_style(HWND window, DWORD style)
{
    if ((DWORD)GetWindowLongPtrW(window, GWL_STYLE) == style)
        return true;
    SetLastError(ERROR_SUCCESS);
    return SetWindowLongPtrW(window, GWL_STYLE, (LONG_PTR)style) != 0 || GetLastError() == ERROR_SUCCESS;
}

static bool restore_nonrude(HWND window)
{
    HANDLE nonrude = GetPropW(window, OLD_NONRUDE);
    if (nonrude)
        return SetPropW(window, NONRUDE, nonrude) != FALSE;
    RemovePropW(window, NONRUDE);
    return GetPropW(window, NONRUDE) == NULL;
}

static bool restore_window(HWND window, HMONITOR keep_monitor)
{
    if (!IsWindow(window))
        return true;
    WINDOWPLACEMENT placement;
    DWORD original;
    if (!read_window(window, &original, &placement) || IsHungAppWindow(window))
        return false;
    if (keep_monitor) {
        MONITORINFO info = {.cbSize = sizeof(info)};
        if (!GetMonitorInfoW(keep_monitor, &info))
            return false;
        /* A full-screen app may have been moved to another physical monitor.
         * Restore its windowed size on the drop's monitor, fitting only when
         * necessary. WINDOWPLACEMENT uses workspace, rather than screen, coords. */
        int64_t width = (int64_t)placement.rcNormalPosition.right - placement.rcNormalPosition.left;
        int64_t height = (int64_t)placement.rcNormalPosition.bottom - placement.rcNormalPosition.top;
        int64_t available_width = (int64_t)info.rcWork.right - info.rcWork.left;
        int64_t available_height = (int64_t)info.rcWork.bottom - info.rcWork.top;
        if (width <= 0 || height <= 0 || available_width <= 0 || available_height <= 0 ||
            available_width > INT_MAX || available_height > INT_MAX)
            return false;
        if (width > available_width) width = available_width;
        if (height > available_height) height = available_height;
        int64_t left = placement.rcNormalPosition.left, top = placement.rcNormalPosition.top;
        /* Main windows' work-area offsets cancel when expressing these limits
         * in the placement's coordinate space, including top/left taskbars. */
        if (left < info.rcMonitor.left) left = info.rcMonitor.left;
        if (top < info.rcMonitor.top) top = info.rcMonitor.top;
        if (left + width > (int64_t)info.rcMonitor.left + available_width)
            left = (int64_t)info.rcMonitor.left + available_width - width;
        if (top + height > (int64_t)info.rcMonitor.top + available_height)
            top = (int64_t)info.rcMonitor.top + available_height - height;
        RECT fitted = {(LONG)left, (LONG)top, (LONG)(left + width), (LONG)(top + height)};
        if (!EqualRect(&fitted, &placement.rcNormalPosition))
            placement.ptMaxPosition = (POINT){-1, -1};
        placement.rcNormalPosition = fitted;
    }
    bool minimized = IsIconic(window) != FALSE, visible = IsWindowVisible(window) != FALSE;
    DWORD current = (DWORD)GetWindowLongPtrW(window, GWL_STYLE);
    DWORD frame = WS_CAPTION | WS_THICKFRAME;
    /* Restore only the frame bits we changed. Keep styles the app updated while
     * running; SetWindowPlacement below restores its original normal/max state. */
    if (!set_style(window, (original & frame) | (current & ~frame)))
        return false;
    if (minimized) {
        if (placement.showCmd == SW_SHOWMAXIMIZED) placement.flags |= WPF_RESTORETOMAXIMIZED;
        else placement.flags &= ~WPF_RESTORETOMAXIMIZED;
        placement.showCmd = SW_SHOWMINNOACTIVE;
    }
    if (!visible) placement.showCmd = SW_HIDE;
    if (!SetWindowPlacement(window, &placement) ||
        !SetWindowPos(window, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                      SWP_NOOWNERZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED))
        return !IsWindow(window);
    if (!restore_nonrude(window)) return false;
    discard_journal(window);
    return !GetPropW(window, MARK);
}

bool ms_fullscreen_preserve_size(HWND window)
{
    if (!IsWindow(window))
        return true;
    bool saved = GetPropW(window, MARK) != NULL;
    if (!g_enabled && !saved)
        return true;
    /* Set the exception before restoring: focus/restore callbacks and future
     * polling must not immediately turn this window full-screen again. */
    if (!SetPropW(window, PRESERVE_SIZE, (HANDLE)1))
        return false;
    if (!saved)
        return true;
    HWND foreground = GetForegroundWindow();
    bool restored = restore_window(window, MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST));
    if (IsWindow(foreground) && IsWindowVisible(foreground) && !IsIconic(foreground))
        SetForegroundWindow(foreground);
    return restored;
}

void ms_fullscreen_apply(HWND window)
{
    if (!g_enabled || !g_eligible || !IsWindow(window) || !IsWindowVisible(window) ||
        IsIconic(window) || IsHungAppWindow(window) || GetPropW(window, PRESERVE_SIZE))
        return;
    bool saved = GetPropW(window, MARK) != NULL;
    DWORD style = (DWORD)GetWindowLongPtrW(window, GWL_STYLE);
    if (!saved && (!(style & WS_THICKFRAME) || !(style & WS_CAPTION) ||
        (GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) ||
        GetWindow(window, GW_OWNER) || !g_eligible(window)))
        return;
    MONITORINFO info = {.cbSize = sizeof(info)};
    RECT current;
    if (!GetWindowRect(window, &current) || !GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &info))
        return;
    if (!saved && !save_window(window))
        return;
    if (IsIconic(window))
        return;
    /* Keep the real taskbar above the app when its native auto-hide edge fires.
     * No taskbar HWND is hidden or made non-topmost. Preserve the app's old value. */
    if (GetPropW(window, NONRUDE) != (HANDLE)1 && !SetPropW(window, NONRUDE, (HANDLE)1)) {
        if (!saved) discard_journal(window);
        return;
    }
    style = (DWORD)GetWindowLongPtrW(window, GWL_STYLE);
    DWORD fullscreen = style & ~(WS_CAPTION | WS_THICKFRAME | WS_MAXIMIZE);
    if (!set_style(window, fullscreen)) {
        if (!saved) {
            if (restore_nonrude(window)) discard_journal(window);
        } else {
            restore_window(window, NULL);
        }
        return;
    }
    if (style != fullscreen || !EqualRect(&current, &info.rcMonitor))
        SetWindowPos(window, NULL, info.rcMonitor.left, info.rcMonitor.top,
            info.rcMonitor.right - info.rcMonitor.left, info.rcMonitor.bottom - info.rcMonitor.top,
            SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

static BOOL CALLBACK apply_window(HWND window, LPARAM parameter)
{
    (void)parameter;
    ms_fullscreen_apply(window);
    return TRUE;
}

void ms_fullscreen_update(void)
{
    if (g_enabled) {
        auto_hide(true); /* Also handles an Explorer restart without replacing its journal. */
        EnumWindows(apply_window, 0);
    }
}

static BOOL CALLBACK recover_window(HWND window, LPARAM parameter)
{
    if (GetPropW(window, MARK) && !restore_window(window, NULL)) {
        ++*(size_t *)parameter;
    } else {
        /* A new full-screen session can manage every app again. Exceptions on
         * surviving HWNDs are also cleaned up during startup/crash recovery. */
        RemovePropW(window, PRESERVE_SIZE);
        if (GetPropW(window, PRESERVE_SIZE)) ++*(size_t *)parameter;
    }
    return TRUE;
}

size_t ms_fullscreen_restore(void)
{
    g_enabled = false; /* Recovery must not immediately full-screen apps again. */
    size_t errors = 0;
    HWND foreground = GetForegroundWindow();
    if (!EnumWindows(recover_window, (LPARAM)&errors))
        ++errors;
    DWORD previous = 0, size = sizeof(previous);
    LSTATUS read = RegGetValueW(HKEY_CURRENT_USER, SETTINGS, BAR_SAVED, RRF_RT_REG_DWORD,
                                NULL, &previous, &size);
    if (read == ERROR_SUCCESS) {
        if (previous > 1 || !auto_hide(previous != 0)) {
            ++errors;
        } else {
            HKEY key;
            if (RegOpenKeyExW(HKEY_CURRENT_USER, SETTINGS, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS) {
                ++errors;
            } else {
                LSTATUS removed = RegDeleteValueW(key, BAR_SAVED);
                RegCloseKey(key);
                if (removed != ERROR_SUCCESS && removed != ERROR_FILE_NOT_FOUND) ++errors;
            }
        }
    } else if (read != ERROR_FILE_NOT_FOUND && read != ERROR_PATH_NOT_FOUND) {
        ++errors;
    }
    if (IsWindow(foreground) && IsWindowVisible(foreground) && !IsIconic(foreground))
        SetForegroundWindow(foreground);
    return errors;
}
