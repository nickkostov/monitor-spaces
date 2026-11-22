#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <wchar.h>

#include "carousel_win32.h"
#include "overview_layout.h"

static const wchar_t CAROUSEL_CLASS[] = L"MonitorSpaces.Carousel.v1";
enum { VISIBLE_CARDS = 4, TIMER_DRAG_PAGE = 1, TIMER_TASKBAR };
typedef struct { HTHUMBNAIL handle; SIZE size; } Preview;
typedef struct {
    HWND window, taskbar;
    MSHandle monitor;
    RECT bounds, revealed_strip;
    MSCarouselActions actions;
    MSCarouselItem *items;
    Preview *previews;
    size_t count, pressed;
    unsigned active, view, spaces, first, dpi, drop, taskbar_edge;
    int scroll[MS_MAX_WORKSPACES], hover_page, taskbar_size;
    HFONT font, heading;
    HDC buffer;
    HBITMAP bitmap;
    HGDIOBJ old_bitmap;
    SIZE buffer_size;
    POINT origin, pointer;
    bool dragging, busy, closing, taskbar_revealed;
    ULONGLONG taskbar_leave;
    wchar_t message[256];
} Carousel;
static Carousel g_carousel;

static int scaled(int value) { return MulDiv(value, (int)g_carousel.dpi, 96); }
static MSOverviewRect portable(RECT r) { return (MSOverviewRect){r.left, r.top, r.right, r.bottom}; }
static RECT native(MSOverviewRect r) { return (RECT){r.left, r.top, r.right, r.bottom}; }

static bool over_taskbar(POINT point)
{
    return g_carousel.taskbar_revealed && PtInRect(&g_carousel.revealed_strip, point);
}

static RECT card_rect(unsigned workspace)
{
    RECT client;
    GetClientRect(g_carousel.window, &client);
    int margin = scaled(40), gap = scaled(16);
    int width = (client.right - 2 * margin - (VISIBLE_CARDS - 1) * gap) / VISIBLE_CARDS;
    int left = margin + (int)(workspace - g_carousel.first - 1) * (width + gap);
    return (RECT){left, scaled(88), left + width, scaled(202)};
}

static unsigned card_at(POINT point)
{
    if (over_taskbar(point))
        return 0;
    for (unsigned workspace = g_carousel.first + 1;
         workspace <= g_carousel.spaces && workspace <= g_carousel.first + VISIBLE_CARDS; ++workspace) {
        RECT rect = card_rect(workspace);
        if (PtInRect(&rect, point))
            return workspace;
    }
    return 0;
}

static int workspace_count(unsigned workspace)
{
    int count = 0;
    for (size_t i = 0; i < g_carousel.count; ++i)
        if (g_carousel.items[i].workspace == workspace)
            ++count;
    return count;
}

static MSOverviewGrid grid(void)
{
    RECT client;
    GetClientRect(g_carousel.window, &client);
    MSOverviewRect area = {scaled(40), scaled(270), client.right - scaled(40), client.bottom - scaled(68)};
    return ms_overview_grid(area, (size_t)workspace_count(g_carousel.view),
        g_carousel.scroll[g_carousel.view - 1], MulDiv((int)g_carousel.dpi, 100, 96));
}

static bool tile_rect(size_t index, RECT *rect)
{
    if (g_carousel.items[index].workspace != g_carousel.view)
        return false;
    size_t ordinal = 0;
    for (size_t i = 0; i < index; ++i)
        if (g_carousel.items[i].workspace == g_carousel.view)
            ++ordinal;
    MSOverviewGrid layout = grid();
    MSOverviewRect cell;
    if (!ms_overview_cell(&layout, ordinal, &cell))
        return false;
    *rect = native(cell);
    return true;
}

static size_t tile_at(POINT point)
{
    if (over_taskbar(point))
        return SIZE_MAX;
    for (size_t i = 0; i < g_carousel.count; ++i) {
        RECT rect;
        if (tile_rect(i, &rect) && PtInRect(&rect, point))
            return i;
    }
    return SIZE_MAX;
}

static RECT ghost_rect(void)
{
    RECT client;
    GetClientRect(g_carousel.window, &client);
    int width = scaled(320), height = scaled(220);
    int left = g_carousel.pointer.x + scaled(14), top = g_carousel.pointer.y + scaled(14);
    if (left + width > client.right)
        left = client.right - width;
    if (top + height > client.bottom)
        top = client.bottom - height;
    if (left < 0)
        left = 0;
    if (top < 0)
        top = 0;
    return (RECT){left, top, left + width, top + height};
}

static RECT preview_area(RECT tile)
{
    return (RECT){tile.left + scaled(8), tile.top + scaled(40),
                  tile.right - scaled(8), tile.bottom - scaled(8)};
}

static void sync_previews(void)
{
    RECT ghost = ghost_rect();
    for (size_t i = 0; i < g_carousel.count; ++i) {
        Preview *preview = &g_carousel.previews[i];
        if (!preview->handle)
            continue;
        RECT tile = {0}, intersection;
        bool dragged = g_carousel.dragging && g_carousel.pressed == i;
        bool visible = dragged || tile_rect(i, &tile);
        if (dragged)
            tile = ghost;
        else if (visible && g_carousel.dragging && IntersectRect(&intersection, &tile, &ghost))
            visible = false; /* Keep other DWM thumbnails below the drag ghost. */
        RECT fitted = native(ms_overview_fit(portable(preview_area(tile)), preview->size.cx, preview->size.cy));
        DWM_THUMBNAIL_PROPERTIES properties = {
            .dwFlags = DWM_TNP_RECTDESTINATION | DWM_TNP_VISIBLE | DWM_TNP_OPACITY | DWM_TNP_SOURCECLIENTAREAONLY,
            .rcDestination = fitted, .opacity = dragged ? 235 : 255,
            .fVisible = visible && fitted.right > fitted.left && fitted.bottom > fitted.top,
            .fSourceClientAreaOnly = FALSE
        };
        DwmUpdateThumbnailProperties(preview->handle, &properties);
    }
}

static void free_snapshot(MSCarouselItem *items, Preview *previews, size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        if (items && items[i].icon)
            DestroyIcon(items[i].icon);
        if (previews && previews[i].handle)
            DwmUnregisterThumbnail(previews[i].handle);
    }
    free(previews);
    free(items);
}

static bool refresh(void)
{
    HWND window = g_carousel.window;
    bool previous_busy = g_carousel.busy;
    g_carousel.busy = true;
    MSCarouselItem *items = NULL;
    size_t count = 0;
    unsigned active = 0, spaces = 0;
    if (!g_carousel.actions.load(g_carousel.monitor, &items, &count, &active, &spaces)) {
        if (window == g_carousel.window)
            g_carousel.busy = previous_busy;
        return false;
    }
    Preview *previews = calloc(count ? count : 1, sizeof(*previews));
    if (window != g_carousel.window || !previews || (count && !items) ||
        spaces < MS_DEFAULT_WORKSPACES || spaces > MS_MAX_WORKSPACES || !active || active > spaces) {
        free_snapshot(items, previews, count);
        if (window == g_carousel.window)
            g_carousel.busy = previous_busy;
        return false;
    }
    ULONGLONG deadline = GetTickCount64() + 120;
    for (size_t i = 0; i < count; ++i) {
        DWORD_PTR icon = 0;
        if (GetTickCount64() < deadline)
            SendMessageTimeoutW((HWND)items[i].window, WM_GETICON, ICON_SMALL2, g_carousel.dpi,
                                SMTO_ABORTIFHUNG | SMTO_ERRORONEXIT, 30, &icon);
        if (window != g_carousel.window) {
            free_snapshot(items, previews, count);
            return false;
        }
        if (!icon)
            icon = (DWORD_PTR)GetClassLongPtrW((HWND)items[i].window, GCLP_HICONSM);
        if (!icon)
            icon = (DWORD_PTR)LoadIconW(NULL, IDI_APPLICATION);
        items[i].icon = CopyIcon((HICON)icon);
        /* Thumbnails never restore, resize, or move source application windows. */
        if (SUCCEEDED(DwmRegisterThumbnail(window, (HWND)items[i].window, &previews[i].handle))) {
            if (FAILED(DwmQueryThumbnailSourceSize(previews[i].handle, &previews[i].size)) ||
                previews[i].size.cx <= 0 || previews[i].size.cy <= 0) {
                DwmUnregisterThumbnail(previews[i].handle);
                previews[i].handle = NULL;
            }
        }
    }
    free_snapshot(g_carousel.items, g_carousel.previews, g_carousel.count);
    g_carousel.items = items;
    g_carousel.previews = previews;
    g_carousel.count = count;
    g_carousel.active = active;
    g_carousel.spaces = spaces;
    if (!g_carousel.view || g_carousel.view > spaces)
        g_carousel.view = active;
    unsigned maximum_first = spaces > VISIBLE_CARDS ? spaces - VISIBLE_CARDS : 0;
    if (g_carousel.first > maximum_first)
        g_carousel.first = maximum_first;
    g_carousel.busy = previous_busy;
    MSOverviewGrid layout = grid();
    g_carousel.scroll[g_carousel.view - 1] = layout.scroll;
    InvalidateRect(window, NULL, FALSE);
    return true;
}

static void fill(HDC dc, const RECT *rect, COLORREF color)
{
    HBRUSH brush = CreateSolidBrush(color);
    if (brush) {
        FillRect(dc, rect, brush);
        DeleteObject(brush);
    }
}

static void label(HDC dc, const wchar_t *text, RECT rect, COLORREF color, UINT flags)
{
    SetTextColor(dc, color);
    DrawTextW(dc, text, -1, &rect, flags | DT_NOPREFIX);
}

static RECT control_rect(int control)
{
    RECT client;
    GetClientRect(g_carousel.window, &client);
    int right = client.right - scaled(32);
    if (control == 2)
        return (RECT){right - scaled(32), scaled(20), right, scaled(52)};
    right -= scaled(44);
    if (control == 0)
        return (RECT){right - scaled(160), scaled(20), right, scaled(52)};
    right -= scaled(172);
    if (control < 0)
        right -= scaled(44);
    return (RECT){right - scaled(32), scaled(20), right, scaled(52)};
}

static bool can_page(int direction)
{
    return direction < 0 ? g_carousel.first > 0 : g_carousel.first + VISIBLE_CARDS < g_carousel.spaces;
}

static int page_at(POINT point)
{
    RECT previous = control_rect(-1), next = control_rect(1);
    if (over_taskbar(point))
        return 0;
    if (PtInRect(&previous, point) && can_page(-1))
        return -1;
    if (PtInRect(&next, point) && can_page(1))
        return 1;
    return 0;
}

static void page(int direction)
{
    unsigned last = g_carousel.spaces > VISIBLE_CARDS ? g_carousel.spaces - VISIBLE_CARDS : 0;
    if (direction < 0)
        g_carousel.first = g_carousel.first > VISIBLE_CARDS ? g_carousel.first - VISIBLE_CARDS : 0;
    else {
        g_carousel.first += VISIBLE_CARDS;
        if (g_carousel.first > last)
            g_carousel.first = last;
    }
    g_carousel.drop = 0;
    InvalidateRect(g_carousel.window, NULL, FALSE);
}

static void select_workspace(unsigned workspace)
{
    if (!workspace || workspace > g_carousel.spaces)
        return;
    g_carousel.view = workspace;
    if (workspace <= g_carousel.first)
        g_carousel.first = workspace - 1;
    else if (workspace > g_carousel.first + VISIBLE_CARDS)
        g_carousel.first = workspace - VISIBLE_CARDS;
    InvalidateRect(g_carousel.window, NULL, FALSE);
}

static void draw_control(HDC dc, int control, const wchar_t *caption, bool enabled)
{
    RECT rect = control_rect(control);
    fill(dc, &rect, enabled ? RGB(45, 66, 103) : RGB(33, 38, 48));
    label(dc, caption, rect, enabled ? RGB(233, 240, 255) : RGB(106, 119, 139),
          DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

static void draw_tile(HDC dc, size_t index, RECT rect, bool ghost)
{
    fill(dc, &rect, ghost ? RGB(69, 96, 146) : RGB(48, 57, 74));
    RECT inside = rect;
    InflateRect(&inside, -scaled(2), -scaled(2));
    fill(dc, &inside, RGB(27, 33, 44));
    int size = scaled(20);
    if (g_carousel.items[index].icon)
        DrawIconEx(dc, rect.left + scaled(10), rect.top + scaled(10), g_carousel.items[index].icon,
                   size, size, 0, NULL, DI_NORMAL);
    RECT title = {rect.left + scaled(40), rect.top, rect.right - scaled(8), rect.top + scaled(40)};
    label(dc, g_carousel.items[index].title, title, RGB(239, 242, 249),
          DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
    RECT body = preview_area(rect);
    fill(dc, &body, RGB(18, 23, 32));
    if (g_carousel.items[index].icon) {
        size = scaled(48);
        DrawIconEx(dc, (body.left + body.right - size) / 2, (body.top + body.bottom - size) / 2,
                   g_carousel.items[index].icon, size, size, 0, NULL, DI_NORMAL);
    }
}

static void paint(HDC dc, RECT client)
{
    fill(dc, &client, RGB(15, 20, 29));
    SetBkMode(dc, TRANSPARENT);
    HGDIOBJ previous = SelectObject(dc, g_carousel.heading);
    RECT controls = control_rect(-1);
    RECT title = {scaled(40), scaled(18), controls.left - scaled(16), scaled(50)};
    wchar_t text[96];
    unsigned last = g_carousel.first + VISIBLE_CARDS;
    if (last > g_carousel.spaces)
        last = g_carousel.spaces;
    swprintf(text, 96, L"Workspaces  %u-%u of %u", g_carousel.first + 1, last, g_carousel.spaces);
    label(dc, text, title, RGB(248, 249, 252), DT_SINGLELINE | DT_END_ELLIPSIS);
    SelectObject(dc, g_carousel.font);
    draw_control(dc, -1, L"<", can_page(-1));
    draw_control(dc, 1, L">", can_page(1));
    draw_control(dc, 0, L"+ Add workspace", g_carousel.spaces < MS_MAX_WORKSPACES);
    draw_control(dc, 2, L"X", true);
    RECT hint = {scaled(40), scaled(56), client.right - scaled(40), scaled(80)};
    label(dc, L"Select a workspace to browse  /  Drag an app preview to a workspace  /  Enter to switch  /  Esc to close",
          hint, RGB(162, 178, 203), DT_SINGLELINE | DT_END_ELLIPSIS);
    for (unsigned workspace = g_carousel.first + 1;
         workspace <= g_carousel.spaces && workspace <= g_carousel.first + VISIBLE_CARDS; ++workspace) {
        RECT card = card_rect(workspace);
        COLORREF border = g_carousel.dragging && g_carousel.drop == workspace ? RGB(145, 195, 255) :
                          workspace == g_carousel.view ? RGB(85, 145, 246) : RGB(53, 64, 83);
        fill(dc, &card, border);
        RECT inside = card;
        InflateRect(&inside, -scaled(2), -scaled(2));
        fill(dc, &inside, RGB(26, 34, 48));
        swprintf(text, 96, L"Workspace %u%ls", workspace, workspace == g_carousel.active ? L"  -  Active" : L"");
        RECT header = {card.left + scaled(16), card.top + scaled(12), card.right - scaled(12), card.top + scaled(38)};
        label(dc, text, header, RGB(230, 237, 249), DT_SINGLELINE | DT_END_ELLIPSIS);
        swprintf(text, 96, L"%d apps%ls", workspace_count(workspace), workspace == g_carousel.view ? L"  -  Browsing" : L"");
        header.top += scaled(29);
        header.bottom += scaled(29);
        label(dc, text, header, RGB(159, 178, 207), DT_SINGLELINE | DT_END_ELLIPSIS);
        int x = card.left + scaled(16);
        for (size_t i = 0; i < g_carousel.count && x + scaled(20) < card.right - scaled(12); ++i) {
            if (g_carousel.items[i].workspace == workspace && g_carousel.items[i].icon) {
                DrawIconEx(dc, x, card.bottom - scaled(32), g_carousel.items[i].icon,
                           scaled(20), scaled(20), 0, NULL, DI_NORMAL);
                x += scaled(28);
            }
        }
    }
    SelectObject(dc, g_carousel.heading);
    swprintf(text, 96, L"Apps in workspace %u", g_carousel.view);
    RECT heading = {scaled(40), scaled(226), client.right - scaled(40), scaled(260)};
    label(dc, text, heading, RGB(230, 237, 249), DT_SINGLELINE);
    SelectObject(dc, g_carousel.font);
    if (!workspace_count(g_carousel.view)) {
        RECT empty = {scaled(40), scaled(285), client.right - scaled(40), client.bottom - scaled(70)};
        label(dc, L"This workspace is empty. Drag apps here from another workspace.", empty,
              RGB(146, 166, 195), DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    for (size_t i = 0; i < g_carousel.count; ++i) {
        RECT tile;
        if (tile_rect(i, &tile))
            draw_tile(dc, i, tile, false);
    }
    RECT status = {scaled(40), client.bottom - scaled(48), client.right - scaled(40), client.bottom - scaled(24)};
    label(dc, g_carousel.message, status, RGB(157, 179, 211), DT_SINGLELINE | DT_END_ELLIPSIS);
    if (g_carousel.dragging && g_carousel.pressed < g_carousel.count)
        draw_tile(dc, g_carousel.pressed, ghost_rect(), true);
    SelectObject(dc, previous);
}

static void free_buffer(void)
{
    if (g_carousel.buffer && g_carousel.old_bitmap && g_carousel.old_bitmap != HGDI_ERROR)
        SelectObject(g_carousel.buffer, g_carousel.old_bitmap);
    if (g_carousel.bitmap)
        DeleteObject(g_carousel.bitmap);
    if (g_carousel.buffer)
        DeleteDC(g_carousel.buffer);
    g_carousel.buffer = NULL;
    g_carousel.bitmap = NULL;
    g_carousel.old_bitmap = NULL;
}

static bool ensure_buffer(HDC dc, int width, int height)
{
    if (g_carousel.buffer && g_carousel.buffer_size.cx == width && g_carousel.buffer_size.cy == height)
        return true;
    free_buffer();
    g_carousel.buffer = CreateCompatibleDC(dc);
    g_carousel.bitmap = CreateCompatibleBitmap(dc, width, height);
    if (g_carousel.buffer && g_carousel.bitmap) {
        g_carousel.old_bitmap = SelectObject(g_carousel.buffer, g_carousel.bitmap);
        if (g_carousel.old_bitmap && g_carousel.old_bitmap != HGDI_ERROR) {
            g_carousel.buffer_size = (SIZE){width, height};
            return true;
        }
    }
    free_buffer();
    return false;
}

/* The overview covers the taskbar. A temporary cutout in our own window reveals
 * it; shell visibility, auto-hide settings, and taskbar window positions stay intact. */
static BOOL CALLBACK find_taskbar_window(HWND window, LPARAM parameter)
{
    (void)parameter;
    wchar_t name[64];
    if (!GetClassNameW(window, name, 64) ||
        (wcscmp(name, L"Shell_TrayWnd") && wcscmp(name, L"Shell_SecondaryTrayWnd")) ||
        MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST) != (HMONITOR)g_carousel.monitor)
        return TRUE;
    RECT rect;
    if (!GetWindowRect(window, &rect) || rect.right <= rect.left || rect.bottom <= rect.top)
        return TRUE;
    g_carousel.taskbar = window;
    if (rect.right - rect.left >= rect.bottom - rect.top) {
        g_carousel.taskbar_edge = (int64_t)rect.top + rect.bottom < (int64_t)g_carousel.bounds.top + g_carousel.bounds.bottom ?
            MS_EDGE_TOP : MS_EDGE_BOTTOM;
        g_carousel.taskbar_size = rect.bottom - rect.top;
    } else {
        g_carousel.taskbar_edge = (int64_t)rect.left + rect.right < (int64_t)g_carousel.bounds.left + g_carousel.bounds.right ?
            MS_EDGE_LEFT : MS_EDGE_RIGHT;
        g_carousel.taskbar_size = rect.right - rect.left;
    }
    if (g_carousel.taskbar_size < scaled(32))
        g_carousel.taskbar_size = scaled(32);
    int extent = g_carousel.taskbar_edge == MS_EDGE_LEFT || g_carousel.taskbar_edge == MS_EDGE_RIGHT ?
        g_carousel.bounds.right - g_carousel.bounds.left : g_carousel.bounds.bottom - g_carousel.bounds.top;
    if (g_carousel.taskbar_size > extent / 3)
        g_carousel.taskbar_size = extent / 3;
    return FALSE;
}

static void reveal_taskbar(bool reveal)
{
    if (reveal == g_carousel.taskbar_revealed)
        return;
    if (!reveal) {
        if (SetWindowRgn(g_carousel.window, NULL, TRUE)) {
            g_carousel.taskbar_revealed = false;
            SetWindowPos(g_carousel.window, HWND_TOPMOST, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }
        return;
    }
    RECT client;
    GetClientRect(g_carousel.window, &client);
    MSOverviewRect strip = ms_overview_edge(portable(client), g_carousel.taskbar_edge, g_carousel.taskbar_size);
    RECT visible = client;
    switch (g_carousel.taskbar_edge) {
    case MS_EDGE_LEFT: visible.left = strip.right; break;
    case MS_EDGE_TOP: visible.top = strip.bottom; break;
    case MS_EDGE_RIGHT: visible.right = strip.left; break;
    case MS_EDGE_BOTTOM: visible.bottom = strip.top; break;
    }
    HRGN region = CreateRectRgn(visible.left, visible.top, visible.right, visible.bottom);
    if (!region)
        return;
    if (SetWindowRgn(g_carousel.window, region, TRUE)) {
        g_carousel.taskbar_revealed = true;
        g_carousel.revealed_strip = native(strip);
    } else {
        DeleteObject(region); /* Successful SetWindowRgn transfers ownership. */
    }
}

static void update_taskbar(void)
{
    if (!IsWindow(g_carousel.taskbar)) {
        g_carousel.taskbar = NULL;
        /* Auto-hide bars can sit offscreen, where MonitorFromWindow could pick
         * an adjacent display. Ask the shell for the exact monitor/edge first. */
        for (unsigned edge = MS_EDGE_LEFT; edge <= MS_EDGE_BOTTOM; ++edge) {
            APPBARDATA data = {.cbSize = sizeof(data), .uEdge = edge, .rc = g_carousel.bounds};
            HWND bar = (HWND)SHAppBarMessage(ABM_GETAUTOHIDEBAREX, &data);
            if (IsWindow(bar)) {
                g_carousel.taskbar = bar;
                g_carousel.taskbar_edge = edge;
                RECT rect;
                g_carousel.taskbar_size = GetWindowRect(bar, &rect) ?
                    (edge == MS_EDGE_LEFT || edge == MS_EDGE_RIGHT ? rect.right - rect.left : rect.bottom - rect.top) : 0;
                if (g_carousel.taskbar_size < scaled(48))
                    g_carousel.taskbar_size = scaled(48);
                break;
            }
        }
        if (!g_carousel.taskbar)
            EnumWindows(find_taskbar_window, 0);
        if (g_carousel.taskbar && !g_carousel.taskbar_revealed)
            SetWindowPos(g_carousel.window, HWND_TOPMOST, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
    if (!g_carousel.taskbar) {
        reveal_taskbar(false);
        return;
    }
    POINT point;
    if (!GetCursorPos(&point))
        return;
    ULONGLONG now = GetTickCount64();
    MSOverviewRect bounds = portable(g_carousel.bounds);
    MSOverviewRect edge = ms_overview_edge(bounds, g_carousel.taskbar_edge, scaled(3));
    MSOverviewRect stay = ms_overview_edge(bounds, g_carousel.taskbar_edge, g_carousel.taskbar_size + scaled(20));
    if (ms_overview_contains(edge, point.x, point.y)) {
        reveal_taskbar(true);
        g_carousel.taskbar_leave = now;
    } else if (g_carousel.taskbar_revealed) {
        if (ms_overview_contains(stay, point.x, point.y))
            g_carousel.taskbar_leave = now;
        else if (now - g_carousel.taskbar_leave >= 250)
            reveal_taskbar(false);
    }
}

void ms_carousel_close(void)
{
    if (g_carousel.window && !g_carousel.closing) {
        g_carousel.closing = true;
        DestroyWindow(g_carousel.window);
    }
}

bool ms_carousel_visible(void) { return g_carousel.window != NULL; }

static void cancel_drag(void)
{
    KillTimer(g_carousel.window, TIMER_DRAG_PAGE);
    g_carousel.hover_page = 0;
    g_carousel.pressed = SIZE_MAX;
    g_carousel.dragging = false;
    g_carousel.drop = 0;
    if (GetCapture() == g_carousel.window)
        ReleaseCapture();
    InvalidateRect(g_carousel.window, NULL, FALSE);
}

static void activate_workspace(void)
{
    MSHandle monitor = g_carousel.monitor;
    unsigned workspace = g_carousel.view;
    void (*activate)(MSHandle, unsigned) = g_carousel.actions.activate;
    ms_carousel_close();
    activate(monitor, workspace);
}

static LRESULT CALLBACK carousel_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    switch (message) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(window, &ps);
        if (window == g_carousel.window && g_carousel.font && g_carousel.heading && g_carousel.view) {
            RECT client;
            GetClientRect(window, &client);
            if (ensure_buffer(dc, client.right, client.bottom)) {
                paint(g_carousel.buffer, client);
                BitBlt(dc, ps.rcPaint.left, ps.rcPaint.top, ps.rcPaint.right - ps.rcPaint.left,
                       ps.rcPaint.bottom - ps.rcPaint.top, g_carousel.buffer, ps.rcPaint.left, ps.rcPaint.top, SRCCOPY);
            } else {
                paint(dc, client);
            }
            sync_previews();
        }
        EndPaint(window, &ps);
        return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_LBUTTONDBLCLK: {
        POINT point = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
        unsigned workspace = card_at(point);
        if (workspace) {
            g_carousel.view = workspace;
            activate_workspace();
        }
        return 0;
    }
    case WM_LBUTTONDOWN: {
        POINT point = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
        if (over_taskbar(point))
            return 0;
        RECT add = control_rect(0), close = control_rect(2);
        if (PtInRect(&close, point)) {
            ms_carousel_close();
            return 0;
        }
        if (PtInRect(&add, point)) {
            if (g_carousel.spaces < MS_MAX_WORKSPACES) {
                g_carousel.busy = true;
                unsigned added = g_carousel.actions.add(g_carousel.monitor, g_carousel.message, 256);
                if (window == g_carousel.window) {
                    bool updated = refresh();
                    if (updated && added)
                        g_carousel.first = g_carousel.spaces > VISIBLE_CARDS ? g_carousel.spaces - VISIBLE_CARDS : 0;
                    if (!updated)
                        wcscpy(g_carousel.message, L"Could not refresh workspaces. Press F5 to retry.");
                    g_carousel.busy = false;
                    InvalidateRect(window, NULL, FALSE);
                }
            }
            return 0;
        }
        int direction = page_at(point);
        if (direction) {
            page(direction);
            return 0;
        }
        unsigned workspace = card_at(point);
        if (workspace) {
            select_workspace(workspace);
            return 0;
        }
        g_carousel.pressed = tile_at(point);
        g_carousel.origin = point;
        if (g_carousel.pressed != SIZE_MAX) {
            SetFocus(window);
            SetCapture(window);
        }
        return 0;
    }
    case WM_MOUSEMOVE:
        if (g_carousel.pressed < g_carousel.count && (wparam & MK_LBUTTON)) {
            POINT point = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            if (abs(point.x - g_carousel.origin.x) >= GetSystemMetrics(SM_CXDRAG) ||
                abs(point.y - g_carousel.origin.y) >= GetSystemMetrics(SM_CYDRAG))
                g_carousel.dragging = true;
            g_carousel.pointer = point;
            g_carousel.drop = card_at(point);
            int direction = g_carousel.dragging ? page_at(point) : 0;
            if (direction != g_carousel.hover_page) {
                KillTimer(window, TIMER_DRAG_PAGE);
                g_carousel.hover_page = direction;
                if (direction)
                    SetTimer(window, TIMER_DRAG_PAGE, 500, NULL);
            }
            if (g_carousel.dragging)
                SetCursor(LoadCursorW(NULL, IDC_SIZEALL));
            InvalidateRect(window, NULL, FALSE);
        }
        return 0;
    case WM_LBUTTONUP: {
        POINT point = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
        unsigned destination = card_at(point);
        size_t pressed = g_carousel.pressed;
        bool move = g_carousel.dragging && destination && pressed < g_carousel.count;
        bool focus = !g_carousel.dragging && pressed < g_carousel.count && tile_at(point) == pressed;
        MSCarouselItem item = {0};
        if (move || focus)
            item = g_carousel.items[pressed];
        cancel_drag();
        if (focus) {
            MSHandle monitor = g_carousel.monitor;
            void (*activate)(MSHandle, MSHandle, DWORD) = g_carousel.actions.focus;
            ms_carousel_close();
            activate(monitor, item.window, item.process);
        } else if (move) {
            g_carousel.busy = true;
            g_carousel.actions.move(g_carousel.monitor, item.window, item.process, destination, g_carousel.message, 256);
            if (window == g_carousel.window) {
                if (!refresh())
                    wcscpy(g_carousel.message, L"Could not refresh apps. Press F5 to retry.");
                g_carousel.busy = false;
                SetForegroundWindow(window);
                SetFocus(window);
                InvalidateRect(window, NULL, FALSE);
            }
        }
        return 0;
    }
    case WM_CAPTURECHANGED:
        KillTimer(window, TIMER_DRAG_PAGE);
        g_carousel.hover_page = 0;
        g_carousel.pressed = SIZE_MAX;
        g_carousel.dragging = false;
        g_carousel.drop = 0;
        InvalidateRect(window, NULL, FALSE);
        return 0;
    case WM_TIMER:
        if (wparam == TIMER_TASKBAR)
            update_taskbar();
        else if (wparam == TIMER_DRAG_PAGE && g_carousel.dragging && g_carousel.hover_page) {
            if (can_page(g_carousel.hover_page))
                page(g_carousel.hover_page);
            else {
                KillTimer(window, TIMER_DRAG_PAGE);
                g_carousel.hover_page = 0;
            }
        }
        return 0;
    case WM_CANCELMODE: cancel_drag(); return 0;
    case WM_MOUSEWHEEL:
        if (g_carousel.view) {
            g_carousel.scroll[g_carousel.view - 1] += GET_WHEEL_DELTA_WPARAM(wparam) > 0 ? -1 : 1;
            MSOverviewGrid layout = grid();
            g_carousel.scroll[g_carousel.view - 1] = layout.scroll;
            InvalidateRect(window, NULL, FALSE);
        }
        return 0;
    case WM_KEYDOWN:
        if (wparam == VK_ESCAPE)
            ms_carousel_close();
        else if (wparam == VK_RETURN && !g_carousel.dragging)
            activate_workspace();
        else if ((wparam == VK_LEFT || wparam == VK_RIGHT) && !g_carousel.dragging) {
            unsigned next = g_carousel.view;
            if (wparam == VK_LEFT)
                next = next == 1 ? g_carousel.spaces : next - 1;
            else
                next = next == g_carousel.spaces ? 1 : next + 1;
            select_workspace(next);
        } else if (wparam == VK_F5 && !g_carousel.dragging) {
            cancel_drag();
            if (!refresh())
                wcscpy(g_carousel.message, L"Could not refresh apps. Press F5 to retry.");
        }
        return 0;
    case WM_ACTIVATE:
        if (LOWORD(wparam) == WA_INACTIVE && !g_carousel.busy && window == g_carousel.window)
            ms_carousel_close();
        return 0;
    case WM_CLOSE:
    case WM_DISPLAYCHANGE:
    case WM_DPICHANGED:
        if (window == g_carousel.window)
            ms_carousel_close();
        return 0;
    case WM_DESTROY:
        KillTimer(window, TIMER_TASKBAR);
        cancel_drag();
        free_snapshot(g_carousel.items, g_carousel.previews, g_carousel.count);
        free_buffer();
        if (g_carousel.font)
            DeleteObject(g_carousel.font);
        if (g_carousel.heading)
            DeleteObject(g_carousel.heading);
        g_carousel = (Carousel){0};
        return 0;
    default: return DefWindowProcW(window, message, wparam, lparam);
    }
}

bool ms_carousel_open(HINSTANCE instance, MSHandle monitor, const MSCarouselActions *actions)
{
    ms_carousel_close();
    MONITORINFO info = {.cbSize = sizeof(info)};
    if (!GetMonitorInfoW((HMONITOR)monitor, &info))
        return false;
    WNDCLASSEXW cls = {.cbSize = sizeof(cls), .style = CS_DBLCLKS, .lpfnWndProc = carousel_proc,
        .hInstance = instance, .hCursor = LoadCursorW(NULL, IDC_ARROW), .lpszClassName = CAROUSEL_CLASS};
    if (!RegisterClassExW(&cls) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return false;
    g_carousel.monitor = monitor;
    g_carousel.bounds = info.rcMonitor;
    g_carousel.actions = *actions;
    g_carousel.pressed = SIZE_MAX;
    g_carousel.dpi = 96;
    g_carousel.window = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, CAROUSEL_CLASS,
        L"Monitor Spaces overview", WS_POPUP, info.rcMonitor.left, info.rcMonitor.top,
        info.rcMonitor.right - info.rcMonitor.left, info.rcMonitor.bottom - info.rcMonitor.top,
        NULL, NULL, instance, NULL);
    if (!g_carousel.window)
        return false;
    g_carousel.dpi = GetDpiForWindow(g_carousel.window);
    if (!g_carousel.dpi)
        g_carousel.dpi = 96;
    g_carousel.font = CreateFontW(-scaled(13), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    g_carousel.heading = CreateFontW(-scaled(21), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    wcscpy(g_carousel.message, L"Drag previews to a workspace above. Scroll for more apps. Move to the taskbar edge to reveal it. F5 refreshes.");
    if (!g_carousel.font || !g_carousel.heading || !refresh()) {
        ms_carousel_close();
        return false;
    }
    select_workspace(g_carousel.active);
    if (!SetTimer(g_carousel.window, TIMER_TASKBAR, 50, NULL)) {
        ms_carousel_close();
        return false;
    }
    ShowWindow(g_carousel.window, SW_SHOW);
    SetForegroundWindow(g_carousel.window);
    SetFocus(g_carousel.window);
    return true;
}
