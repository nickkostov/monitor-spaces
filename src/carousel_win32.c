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
#include "wallpaper_win32.h"
#include "app_icons_win32.h"

static const wchar_t CAROUSEL_CLASS[] = L"MonitorSpaces.Carousel.v1";
enum { VISIBLE_CARDS = 4, TIMER_DRAG_PAGE = 1, TIMER_TASKBAR, TIMER_WORKSPACE_HOLD, HOLD_MS = 600 };
enum { WM_OVERVIEW_FRAME = WM_APP + 120 };
typedef struct {
    HTHUMBNAIL handle, miniature;
    HICON caption_icon, miniature_icon;
    SIZE size;
    RECT original;
    RECT tile, target, start, mini_target;
    DWM_THUMBNAIL_PROPERTIES last, mini_last;
    bool minimized, last_valid, mini_valid, visible;
} Preview;
typedef struct {
    HWND window, taskbar;
    MSHandle monitor;
    RECT bounds, revealed_strip;
    MSCarouselActions actions;
    MSCarouselItem *items;
    Preview *previews;
    size_t count, pressed, hovered;
    unsigned active, view, spaces, first, dpi, drop, taskbar_edge, hover_space;
    unsigned hold_space, close_space, pressed_close;
    ULONGLONG hold_started;
    int scroll[MS_MAX_WORKSPACES], hover_page, taskbar_size;
    HFONT font, heading;
    HDC buffer;
    HBITMAP bitmap;
    HGDIOBJ old_bitmap;
    SIZE buffer_size;
    HDC wallpaper_dc, backdrop_dc;
    HBITMAP wallpaper, backdrop;
    HGDIOBJ wallpaper_old, backdrop_old;
    POINT origin, pointer;
    bool dragging, busy, closing, taskbar_revealed, hover_add;
    ULONGLONG taskbar_leave;
    LARGE_INTEGER started, frequency;
    unsigned generation;
    double progress;
    bool layout_valid;
    unsigned layout_view, layout_first;
    int layout_scroll;
    wchar_t message[256];
} Carousel;
static Carousel g_carousel;
static unsigned g_generation;

static int scaled(int value) { return MulDiv(value, (int)g_carousel.dpi, 96); }
static MSOverviewRect portable(RECT r) { return (MSOverviewRect){r.left, r.top, r.right, r.bottom}; }
static RECT native(MSOverviewRect r) { return (RECT){r.left, r.top, r.right, r.bottom}; }

static bool over_taskbar(POINT point)
{
    return g_carousel.taskbar_revealed && PtInRect(&g_carousel.revealed_strip, point);
}

static int strip_height(void)
{
    RECT client;
    GetClientRect(g_carousel.window, &client);
    int height = scaled(148);
    return height < client.bottom / 3 ? height : client.bottom / 3;
}

static RECT card_rect(unsigned workspace)
{
    RECT client;
    GetClientRect(g_carousel.window, &client);
    int gap = scaled(24), width = scaled(172);
    int available = (client.right - scaled(224) - (VISIBLE_CARDS - 1) * gap) / VISIBLE_CARDS;
    if (width > available) width = available;
    if (width < 1) width = 1;
    MSOverviewRect fitted = ms_overview_fit((MSOverviewRect){0, 0, width, strip_height() - scaled(42)},
        g_carousel.bounds.right - g_carousel.bounds.left, g_carousel.bounds.bottom - g_carousel.bounds.top);
    int height = fitted.bottom - fitted.top;
    int left = (client.right - (VISIBLE_CARDS * width + (VISIBLE_CARDS - 1) * gap)) / 2 +
               (int)(workspace - g_carousel.first - 1) * (width + gap);
    int top = (strip_height() - height - scaled(24)) / 2;
    return (RECT){left, top, left + width, top + height + scaled(24)};
}

static RECT card_body(unsigned workspace)
{
    RECT rect = card_rect(workspace);
    rect.bottom -= scaled(24);
    return rect;
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

/* Sit just outside the upper-right edge so DWM thumbnails cannot cover it.
 * The button fits in the existing gap between desktop previews. */
static RECT workspace_close_rect(unsigned workspace)
{
    RECT rect = card_body(workspace);
    rect.left = rect.right + scaled(2);
    rect.top -= scaled(2);
    rect.right = rect.left + scaled(20);
    rect.bottom = rect.top + scaled(20);
    return rect;
}

static unsigned workspace_close_at(POINT point)
{
    unsigned workspace = g_carousel.close_space;
    if (over_taskbar(point) || g_carousel.spaces <= MS_MIN_WORKSPACES ||
        !workspace || workspace > g_carousel.spaces ||
        workspace <= g_carousel.first || workspace > g_carousel.first + VISIBLE_CARDS)
        return 0;
    RECT rect = workspace_close_rect(workspace);
    return PtInRect(&rect, point) ? workspace : 0;
}

static void cancel_hold(void)
{
    KillTimer(g_carousel.window, TIMER_WORKSPACE_HOLD);
    g_carousel.hold_space = 0;
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
    MSOverviewRect area = {scaled(48), strip_height() + scaled(32),
        client.right - scaled(48), client.bottom - scaled(64)};
    return ms_overview_grid(area, (size_t)workspace_count(g_carousel.view),
        g_carousel.scroll[g_carousel.view - 1], MulDiv((int)g_carousel.dpi, 100, 96));
}

static RECT preview_area(size_t index, RECT tile)
{
    tile.bottom -= scaled(42);
    SIZE size = g_carousel.previews[index].size;
    if (size.cx <= 0 || size.cy <= 0) {
        size.cx = 16;
        size.cy = 10;
    }
    return native(ms_overview_fit(portable(tile), size.cx, size.cy));
}

static void update_layout(void)
{
    if (g_carousel.layout_valid && g_carousel.layout_view == g_carousel.view &&
        g_carousel.layout_first == g_carousel.first &&
        g_carousel.layout_scroll == g_carousel.scroll[g_carousel.view - 1])
        return;
    MSOverviewGrid layout = grid();
    RECT client;
    GetClientRect(g_carousel.window, &client);
    size_t ordinal = 0;
    for (size_t i = 0; i < g_carousel.count; ++i) {
        Preview *preview = &g_carousel.previews[i];
        MSOverviewRect cell = {0};
        preview->visible = false;
        if (g_carousel.items[i].workspace == g_carousel.view)
            preview->visible = ms_overview_cell(&layout, ordinal++, &cell);
        preview->tile = native(cell);
        preview->target = preview_area(i, preview->tile);
        preview->start = preview->original;
        OffsetRect(&preview->start, -g_carousel.bounds.left, -g_carousel.bounds.top);
        if (preview->minimized || !IntersectRect(&preview->start, &preview->start, &client)) {
            preview->start = preview->target;
            InflateRect(&preview->start, -(preview->start.right - preview->start.left) / 10,
                        -(preview->start.bottom - preview->start.top) / 10);
        }
        unsigned workspace = g_carousel.items[i].workspace;
        preview->mini_target = (RECT){0};
        if (workspace > g_carousel.first && workspace <= g_carousel.first + VISIBLE_CARDS) {
            RECT card = card_body(workspace);
            InflateRect(&card, -scaled(2), -scaled(2));
            preview->mini_target = native(ms_overview_project(portable(card), portable(g_carousel.bounds),
                                                             portable(preview->original)));
        }
    }
    g_carousel.layout_view = g_carousel.view;
    g_carousel.layout_first = g_carousel.first;
    g_carousel.layout_scroll = g_carousel.scroll[g_carousel.view - 1];
    g_carousel.layout_valid = true;
}

static bool tile_rect(size_t index, RECT *rect)
{
    update_layout();
    *rect = g_carousel.previews[index].tile;
    return g_carousel.previews[index].visible;
}

static RECT preview_destination(size_t index, RECT tile)
{
    if (g_carousel.dragging && g_carousel.pressed == index)
        return preview_area(index, tile);
    update_layout();
    Preview *preview = &g_carousel.previews[index];
    /* Icon fallbacks stay still; only DWM surfaces move during the entrance. */
    if (g_carousel.progress >= 1.0 || !preview->handle)
        return preview->target;
    return native(ms_overview_interpolate(portable(preview->start), portable(preview->target), g_carousel.progress));
}

static void finish_entrance(void)
{
    if (g_carousel.progress < 1.0) {
        g_carousel.progress = 1.0;
        InvalidateRect(g_carousel.window, NULL, FALSE);
    }
}

static size_t tile_at(POINT point)
{
    if (over_taskbar(point))
        return SIZE_MAX;
    for (size_t i = 0; i < g_carousel.count; ++i) {
        RECT rect;
        if (tile_rect(i, &rect)) {
            rect = preview_destination(i, rect);
            rect.bottom += scaled(36);
            if (PtInRect(&rect, point))
                return i;
        }
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

static RECT miniature_rect(size_t index)
{
    update_layout();
    return g_carousel.previews[index].mini_target;
}

static void update_preview(HTHUMBNAIL handle, const DWM_THUMBNAIL_PROPERTIES *properties,
                           DWM_THUMBNAIL_PROPERTIES *last, bool *valid)
{
    if (!handle || (*valid && last->fVisible == properties->fVisible &&
        (!properties->fVisible || (EqualRect(&last->rcDestination, &properties->rcDestination) &&
                                   last->opacity == properties->opacity))))
        return;
    if (SUCCEEDED(DwmUpdateThumbnailProperties(handle, properties))) {
        *last = *properties;
        *valid = true;
    }
}

static void sync_previews(void)
{
    RECT ghost = ghost_rect();
    for (size_t i = 0; i < g_carousel.count; ++i) {
        Preview *preview = &g_carousel.previews[i];
        RECT tile = {0}, intersection;
        bool dragged = g_carousel.dragging && g_carousel.pressed == i;
        bool visible = dragged || tile_rect(i, &tile);
        if (dragged)
            tile = ghost;
        else if (visible && g_carousel.dragging && IntersectRect(&intersection, &tile, &ghost))
            visible = false; /* Keep other DWM thumbnails below the drag ghost. */
        RECT fitted = preview_destination(i, tile);
        DWM_THUMBNAIL_PROPERTIES properties = {
            .dwFlags = DWM_TNP_RECTDESTINATION | DWM_TNP_VISIBLE | DWM_TNP_OPACITY | DWM_TNP_SOURCECLIENTAREAONLY,
            .rcDestination = fitted, .opacity = dragged ? 235 : 255,
            .fVisible = visible && fitted.right > fitted.left && fitted.bottom > fitted.top,
            .fSourceClientAreaOnly = FALSE
        };
        update_preview(preview->handle, &properties, &preview->last, &preview->last_valid);
        RECT miniature = miniature_rect(i);
        properties.rcDestination = miniature;
        properties.opacity = 255;
        properties.fVisible = miniature.right > miniature.left && miniature.bottom > miniature.top &&
            !(g_carousel.dragging && IntersectRect(&intersection, &miniature, &ghost));
        update_preview(preview->miniature, &properties, &preview->mini_last, &preview->mini_valid);
    }
}

static void free_snapshot(MSCarouselItem *items, Preview *previews, size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        if (items && items[i].icon)
            DestroyIcon(items[i].icon);
        if (previews && previews[i].handle)
            DwmUnregisterThumbnail(previews[i].handle);
        if (previews && previews[i].miniature)
            DwmUnregisterThumbnail(previews[i].miniature);
        if (previews && previews[i].caption_icon)
            DestroyIcon(previews[i].caption_icon);
        if (previews && previews[i].miniature_icon)
            DestroyIcon(previews[i].miniature_icon);
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
        spaces < MS_MIN_WORKSPACES || spaces > MS_MAX_WORKSPACES || !active || active > spaces) {
        free_snapshot(items, previews, count);
        if (window == g_carousel.window)
            g_carousel.busy = previous_busy;
        return false;
    }
    MSIconLoader loader;
    ms_icon_loader_open(&loader);
    if (window != g_carousel.window) {
        ms_icon_loader_close(&loader);
        free_snapshot(items, previews, count);
        return false;
    }
    ULONGLONG deadline = GetTickCount64() + 120;
    unsigned dpi = g_carousel.dpi;
    for (size_t i = 0; i < count; ++i) {
        MSAppIcons icons = ms_app_icons_load(&loader, (HWND)items[i].window, items[i].process, dpi, deadline);
        items[i].icon = icons.large;
        previews[i].caption_icon = icons.caption;
        previews[i].miniature_icon = icons.miniature;
        if (window != g_carousel.window) {
            ms_icon_loader_close(&loader);
            free_snapshot(items, previews, count);
            return false;
        }
        HWND source = (HWND)items[i].window;
        previews[i].minimized = IsIconic(source) != FALSE;
        GetWindowRect(source, &previews[i].original);
        if (previews[i].minimized) {
            WINDOWPLACEMENT placement = {.length = sizeof(placement)};
            MONITORINFO info = {.cbSize = sizeof(info)};
            if (GetWindowPlacement(source, &placement) && GetMonitorInfoW((HMONITOR)g_carousel.monitor, &info)) {
                previews[i].original = placement.rcNormalPosition;
                OffsetRect(&previews[i].original, info.rcWork.left - info.rcMonitor.left,
                           info.rcWork.top - info.rcMonitor.top);
                if (placement.flags & WPF_RESTORETOMAXIMIZED)
                    previews[i].original = info.rcWork;
            }
        }
        /* Thumbnails never restore, resize, or move source application windows. */
        if (SUCCEEDED(DwmRegisterThumbnail(window, (HWND)items[i].window, &previews[i].handle))) {
            if (FAILED(DwmQueryThumbnailSourceSize(previews[i].handle, &previews[i].size)) ||
                previews[i].size.cx <= 0 || previews[i].size.cy <= 0) {
                DwmUnregisterThumbnail(previews[i].handle);
                previews[i].handle = NULL;
            }
        }
        DwmRegisterThumbnail(window, source, &previews[i].miniature);
        if (previews[i].miniature == previews[i].handle)
            previews[i].miniature = NULL;
    }
    ms_icon_loader_close(&loader);
    if (window != g_carousel.window) {
        free_snapshot(items, previews, count);
        return false;
    }
    free_snapshot(g_carousel.items, g_carousel.previews, g_carousel.count);
    g_carousel.items = items;
    g_carousel.previews = previews;
    g_carousel.count = count;
    g_carousel.layout_valid = false;
    g_carousel.hovered = SIZE_MAX;
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
    SetDCBrushColor(dc, color);
    FillRect(dc, rect, GetStockObject(DC_BRUSH));
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
    if (control == 2)
        return (RECT){client.right - scaled(52), client.bottom - scaled(48),
                      client.right - scaled(20), client.bottom - scaled(16)};
    int top = (strip_height() - scaled(36)) / 2;
    if (control == 0)
        return (RECT){client.right - scaled(64), top, client.right - scaled(28), top + scaled(36)};
    RECT first = card_rect(g_carousel.first + 1), last = card_rect(g_carousel.first + VISIBLE_CARDS);
    int left = control < 0 ? first.left - scaled(44) : last.right + scaled(12);
    return (RECT){left, top, left + scaled(32), top + scaled(36)};
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
    cancel_hold();
    g_carousel.close_space = 0;
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
    cancel_hold();
    g_carousel.close_space = 0;
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
    if (!enabled)
        return;
    if (control == 0 && g_carousel.hover_add) {
        HBRUSH brush = CreateSolidBrush(RGB(58, 123, 206));
        HGDIOBJ old_brush = SelectObject(dc, brush), old_pen = SelectObject(dc, GetStockObject(NULL_PEN));
        RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, scaled(10), scaled(10));
        SelectObject(dc, old_pen);
        SelectObject(dc, old_brush);
        DeleteObject(brush);
    }
    HGDIOBJ previous = SelectObject(dc, g_carousel.heading);
    label(dc, caption, rect, RGB(245, 248, 252),
          DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    SelectObject(dc, previous);
}

static void draw_tile(HDC dc, size_t index, RECT rect, bool ghost)
{
    RECT body = preview_destination(index, rect), shadow = body;
    InflateRect(&shadow, scaled(4), scaled(4));
    OffsetRect(&shadow, 0, scaled(4));
    fill(dc, &shadow, RGB(23, 28, 35));
    RECT border = body;
    InflateRect(&border, scaled(2), scaled(2));
    fill(dc, &border, ghost || index == g_carousel.hovered ? RGB(122, 188, 255) : RGB(95, 107, 122));
    fill(dc, &body, RGB(18, 23, 32));
    if (g_carousel.items[index].icon) {
        int size = scaled(48);
        DrawIconEx(dc, (body.left + body.right - size) / 2, (body.top + body.bottom - size) / 2,
                   g_carousel.items[index].icon, size, size, 0, NULL, DI_NORMAL);
    }
    SIZE text_size = {0};
    GetTextExtentPoint32W(dc, g_carousel.items[index].title, (int)wcslen(g_carousel.items[index].title), &text_size);
    int caption_width = text_size.cx + scaled(48);
    if (caption_width > rect.right - rect.left)
        caption_width = rect.right - rect.left;
    int left = (body.left + body.right - caption_width) / 2;
    RECT caption = {left, body.bottom + scaled(10), left + caption_width, body.bottom + scaled(36)};
    HBRUSH brush = CreateSolidBrush(RGB(32, 39, 49));
    HGDIOBJ old_brush = SelectObject(dc, brush), old_pen = SelectObject(dc, GetStockObject(NULL_PEN));
    RoundRect(dc, caption.left, caption.top, caption.right, caption.bottom, scaled(12), scaled(12));
    SelectObject(dc, old_pen);
    SelectObject(dc, old_brush);
    DeleteObject(brush);
    if (g_carousel.previews[index].caption_icon)
        DrawIconEx(dc, caption.left + scaled(6), caption.top + scaled(3), g_carousel.previews[index].caption_icon,
                   scaled(20), scaled(20), 0, NULL, DI_NORMAL);
    caption.left += scaled(32);
    caption.right -= scaled(8);
    label(dc, g_carousel.items[index].title, caption, RGB(251, 252, 255),
          DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
}

static void paint(HDC dc, RECT client)
{
    if (g_carousel.backdrop_dc)
        BitBlt(dc, 0, 0, client.right, client.bottom, g_carousel.backdrop_dc, 0, 0, SRCCOPY);
    else
        fill(dc, &client, RGB(48, 66, 83));
    SetBkMode(dc, TRANSPARENT);
    HGDIOBJ previous = SelectObject(dc, g_carousel.font);
    wchar_t text[96];
    draw_control(dc, -1, L"\x2039", can_page(-1));
    draw_control(dc, 1, L"\x203a", can_page(1));
    draw_control(dc, 0, L"+", g_carousel.spaces < MS_MAX_WORKSPACES);
    draw_control(dc, 2, L"\x00d7", true);
    for (unsigned workspace = g_carousel.first + 1;
         workspace <= g_carousel.spaces && workspace <= g_carousel.first + VISIBLE_CARDS; ++workspace) {
        RECT card = card_body(workspace);
        COLORREF border = g_carousel.dragging && g_carousel.drop == workspace ? RGB(145, 195, 255) :
                          workspace == g_carousel.view ? RGB(96, 174, 255) :
                          workspace == g_carousel.hover_space ? RGB(207, 221, 239) : RGB(113, 126, 143);
        RECT outline = card;
        InflateRect(&outline, scaled(2), scaled(2));
        fill(dc, &outline, border);
        if (g_carousel.wallpaper_dc) {
            SetStretchBltMode(dc, HALFTONE);
            SetBrushOrgEx(dc, 0, 0, NULL);
            StretchBlt(dc, card.left, card.top, card.right - card.left, card.bottom - card.top,
                       g_carousel.wallpaper_dc, 0, 0, client.right, client.bottom, SRCCOPY);
        } else {
            fill(dc, &card, RGB(65, 88, 108));
        }
        for (size_t i = g_carousel.count; i > 0; --i) {
            if (g_carousel.items[i - 1].workspace == workspace) {
                RECT miniature = miniature_rect(i - 1);
                fill(dc, &miniature, RGB(203, 213, 224));
                if (!g_carousel.previews[i - 1].miniature && g_carousel.previews[i - 1].miniature_icon &&
                    miniature.right - miniature.left >= scaled(16) && miniature.bottom - miniature.top >= scaled(16))
                    DrawIconEx(dc, miniature.left, miniature.top, g_carousel.previews[i - 1].miniature_icon,
                               scaled(16), scaled(16), 0, NULL, DI_NORMAL);
            }
        }
        RECT caption = card_rect(workspace);
        caption.top = card.bottom + scaled(5);
        swprintf(text, 96, L"Desktop %u%ls", workspace, workspace == g_carousel.active ? L"  \x2022" : L"");
        label(dc, text, caption, RGB(251, 252, 255), DT_CENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        if (g_carousel.close_space == workspace && g_carousel.spaces > MS_MIN_WORKSPACES) {
            RECT button = workspace_close_rect(workspace);
            fill(dc, &button, workspace_close_at(g_carousel.pointer) == workspace ?
                 RGB(183, 57, 65) : RGB(88, 51, 61));
            label(dc, L"\x00d7", button, RGB(255, 255, 255), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
    }
    if (!workspace_count(g_carousel.view)) {
        RECT empty = {scaled(40), strip_height(), client.right - scaled(40), client.bottom - scaled(70)};
        label(dc, L"An empty desktop. Drag a window onto its preview in the strip above.", empty,
              RGB(243, 246, 251), DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    for (size_t i = 0; i < g_carousel.count; ++i) {
        RECT tile;
        if (!(g_carousel.dragging && g_carousel.pressed == i) &&
            (g_carousel.progress >= 1.0 || !g_carousel.previews[i].handle) && tile_rect(i, &tile))
            draw_tile(dc, i, tile, false);
    }
    RECT status = {scaled(32), client.bottom - scaled(38), client.right - scaled(72), client.bottom - scaled(16)};
    label(dc, g_carousel.message, status, RGB(232, 238, 246), DT_SINGLELINE | DT_END_ELLIPSIS);
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

static void free_backdrop(void)
{
    if (g_carousel.wallpaper_dc && g_carousel.wallpaper_old && g_carousel.wallpaper_old != HGDI_ERROR)
        SelectObject(g_carousel.wallpaper_dc, g_carousel.wallpaper_old);
    if (g_carousel.backdrop_dc && g_carousel.backdrop_old && g_carousel.backdrop_old != HGDI_ERROR)
        SelectObject(g_carousel.backdrop_dc, g_carousel.backdrop_old);
    if (g_carousel.wallpaper_dc) DeleteDC(g_carousel.wallpaper_dc);
    if (g_carousel.backdrop_dc) DeleteDC(g_carousel.backdrop_dc);
    if (g_carousel.wallpaper) DeleteObject(g_carousel.wallpaper);
    if (g_carousel.backdrop) DeleteObject(g_carousel.backdrop);
    g_carousel.wallpaper_dc = g_carousel.backdrop_dc = NULL;
    g_carousel.wallpaper = g_carousel.backdrop = NULL;
    g_carousel.wallpaper_old = g_carousel.backdrop_old = NULL;
}

static void prepare_backdrop(void)
{
    HWND window = g_carousel.window;
    HBITMAP wallpaper = ms_wallpaper_load((HMONITOR)g_carousel.monitor);
    /* Shell COM calls can dispatch display/focus messages while loading. */
    if (window != g_carousel.window) {
        if (wallpaper) DeleteObject(wallpaper);
        return;
    }
    g_carousel.wallpaper = wallpaper;
    if (!g_carousel.wallpaper)
        return;
    g_carousel.wallpaper_dc = CreateCompatibleDC(NULL);
    g_carousel.backdrop_dc = CreateCompatibleDC(NULL);
    RECT client;
    GetClientRect(g_carousel.window, &client);
    BITMAPINFO format = {.bmiHeader = {.biSize = sizeof(BITMAPINFOHEADER),
        .biWidth = client.right, .biHeight = -client.bottom, .biPlanes = 1, .biBitCount = 32, .biCompression = BI_RGB}};
    uint32_t *pixels = NULL;
    g_carousel.backdrop = CreateDIBSection(NULL, &format, DIB_RGB_COLORS, (void **)&pixels, NULL, 0);
    if (!g_carousel.wallpaper_dc || !g_carousel.backdrop_dc || !g_carousel.backdrop) {
        free_backdrop();
        return;
    }
    g_carousel.wallpaper_old = SelectObject(g_carousel.wallpaper_dc, g_carousel.wallpaper);
    g_carousel.backdrop_old = SelectObject(g_carousel.backdrop_dc, g_carousel.backdrop);
    if (!g_carousel.wallpaper_old || g_carousel.wallpaper_old == HGDI_ERROR ||
        !g_carousel.backdrop_old || g_carousel.backdrop_old == HGDI_ERROR) {
        free_backdrop();
        return;
    }
    BitBlt(g_carousel.backdrop_dc, 0, 0, client.right, client.bottom, g_carousel.wallpaper_dc, 0, 0, SRCCOPY);
    /* A small cached wallpaper image gives the spaces strip a frosted appearance.
     * Decode, blur and tint once per opening, never during mouse movement. */
    HDC small_dc = CreateCompatibleDC(g_carousel.wallpaper_dc);
    int small_width = client.right / 32 + 1, small_height = client.bottom / 32 + 1;
    HBITMAP small = CreateCompatibleBitmap(g_carousel.wallpaper_dc, small_width, small_height);
    if (small_dc && small) {
        HGDIOBJ previous = SelectObject(small_dc, small);
        SetStretchBltMode(small_dc, HALFTONE);
        SetBrushOrgEx(small_dc, 0, 0, NULL);
        StretchBlt(small_dc, 0, 0, small_width, small_height, g_carousel.wallpaper_dc,
                   0, 0, client.right, client.bottom, SRCCOPY);
        int saved = SaveDC(g_carousel.backdrop_dc);
        IntersectClipRect(g_carousel.backdrop_dc, 0, 0, client.right, strip_height());
        SetStretchBltMode(g_carousel.backdrop_dc, HALFTONE);
        SetBrushOrgEx(g_carousel.backdrop_dc, 0, 0, NULL);
        StretchBlt(g_carousel.backdrop_dc, 0, 0, client.right, client.bottom,
                   small_dc, 0, 0, small_width, small_height, SRCCOPY);
        RestoreDC(g_carousel.backdrop_dc, saved);
        SelectObject(small_dc, previous);
    }
    if (small_dc) DeleteDC(small_dc);
    if (small) DeleteObject(small);
    GdiFlush(); /* Finish GDI writes before editing DIB pixels directly. */
    int strip = strip_height();
    for (int y = 0; y < client.bottom; ++y)
        for (int x = 0; x < client.right; ++x) {
            size_t index = (size_t)y * (size_t)client.right + (size_t)x;
            uint32_t pixel = pixels[index];
            unsigned red = (pixel >> 16) & 255u, green = (pixel >> 8) & 255u, blue = pixel & 255u;
            if (y < strip) {
                red = (red * 76 + 185 * 24) / 100;
                green = (green * 76 + 201 * 24) / 100;
                blue = (blue * 76 + 219 * 24) / 100;
            } else {
                unsigned shade = y < strip + scaled(12) ? 68u + (unsigned)((y - strip) * 14 / scaled(12)) : 82u;
                red = red * shade / 100;
                green = green * shade / 100;
                blue = blue * shade / 100;
            }
            pixels[index] = red << 16 | green << 8 | blue;
        }
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
    cancel_hold();
    g_carousel.pressed_close = 0;
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
                int saved = SaveDC(g_carousel.buffer);
                if (saved)
                    IntersectClipRect(g_carousel.buffer, ps.rcPaint.left, ps.rcPaint.top, ps.rcPaint.right, ps.rcPaint.bottom);
                paint(g_carousel.buffer, client);
                if (saved)
                    RestoreDC(g_carousel.buffer, saved);
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
    case WM_OVERVIEW_FRAME: {
        if (window != g_carousel.window || wparam != g_carousel.generation || g_carousel.progress >= 1.0)
            return 0;
        /* Posted frame messages otherwise precede hardware input in GetMessage.
         * Drain a bounded amount of input before the next compositor update. */
        MSG input;
        for (unsigned i = 0; i < 32 && PeekMessageW(&input, NULL, 0, 0, PM_REMOVE | PM_QS_INPUT); ++i) {
            if (input.message == WM_QUIT) {
                PostQuitMessage((int)input.wParam);
                finish_entrance();
                return 0;
            }
            TranslateMessage(&input);
            DispatchMessageW(&input);
            if (window != g_carousel.window || wparam != g_carousel.generation || g_carousel.progress >= 1.0)
                return 0;
        }
        if (window != g_carousel.window || wparam != g_carousel.generation || g_carousel.progress >= 1.0)
            return 0; /* PeekMessage can dispatch sent close/display messages. */
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        double elapsed = (double)(now.QuadPart - g_carousel.started.QuadPart) * 1000.0 /
                         (double)g_carousel.frequency.QuadPart;
        g_carousel.progress = ms_overview_progress(elapsed);
        /* DWM owns all moving pixels. Do not repaint full-screen GDI wallpaper,
         * borders or captions alongside independently presented thumbnails. */
        sync_previews();
        bool paced = SUCCEEDED(DwmFlush());
        if (window == g_carousel.window && wparam == g_carousel.generation) {
            if (g_carousel.progress >= 1.0) {
                InvalidateRect(window, NULL, FALSE); /* Paint settled borders/captions once. */
            } else if (!paced || !PostMessageW(window, WM_OVERVIEW_FRAME, g_carousel.generation, 0)) {
                finish_entrance();
            }
        }
        return 0;
    }
    case WM_LBUTTONDBLCLK: {
        POINT point = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
        if (workspace_close_at(point))
            return SendMessageW(window, WM_LBUTTONDOWN, wparam, lparam);
        cancel_drag();
        unsigned workspace = card_at(point);
        if (workspace) {
            g_carousel.view = workspace;
            activate_workspace();
        }
        return 0;
    }
    case WM_LBUTTONDOWN: {
        POINT point = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
        size_t hit = tile_at(point);
        finish_entrance();
        if (over_taskbar(point))
            return 0;
        cancel_drag();
        g_carousel.pointer = point;
        unsigned closing = workspace_close_at(point);
        if (closing) {
            g_carousel.pressed_close = closing;
            SetCapture(window);
            return 0;
        }
        g_carousel.close_space = 0;
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
            if (g_carousel.spaces > MS_MIN_WORKSPACES) {
                g_carousel.origin = point;
                g_carousel.hold_space = workspace;
                g_carousel.hold_started = GetTickCount64();
                SetCapture(window);
                if (!SetTimer(window, TIMER_WORKSPACE_HOLD, HOLD_MS, NULL))
                    cancel_hold();
            }
            return 0;
        }
        if (hit == SIZE_MAX && point.y > strip_height()) {
            activate_workspace();
            return 0;
        }
        g_carousel.pressed = hit;
        g_carousel.origin = point;
        if (g_carousel.pressed != SIZE_MAX) {
            SetFocus(window);
            SetCapture(window);
        }
        return 0;
    }
    case WM_MOUSEMOVE:
    {
        POINT point = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
        RECT previous_ghost = ghost_rect();
        bool was_dragging = g_carousel.dragging;
        unsigned previous_drop = g_carousel.drop;
        g_carousel.pointer = point;
        if (g_carousel.hold_space && (!(wparam & MK_LBUTTON) ||
            abs(point.x - g_carousel.origin.x) >= GetSystemMetrics(SM_CXDRAG) ||
            abs(point.y - g_carousel.origin.y) >= GetSystemMetrics(SM_CYDRAG) ||
            card_at(point) != g_carousel.hold_space))
            cancel_hold();
        if (g_carousel.close_space) {
            RECT button = workspace_close_rect(g_carousel.close_space);
            InvalidateRect(window, &button, FALSE);
        }
        RECT add = control_rect(0);
        bool hover_add = !over_taskbar(point) && PtInRect(&add, point) && g_carousel.spaces < MS_MAX_WORKSPACES;
        if (hover_add != g_carousel.hover_add) {
            g_carousel.hover_add = hover_add;
            InvalidateRect(window, NULL, FALSE);
        }
        TRACKMOUSEEVENT tracking = {.cbSize = sizeof(tracking), .dwFlags = TME_LEAVE, .hwndTrack = window};
        TrackMouseEvent(&tracking);
        if (g_carousel.pressed < g_carousel.count && (wparam & MK_LBUTTON)) {
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
            if (was_dragging) {
                /* Move only the ghost's old/new pixels, plus changed targets.
                 * WM_PAINT coalesces high-rate mouse events into one update. */
                RECT next_ghost = ghost_rect();
                InflateRect(&previous_ghost, scaled(8), scaled(12));
                InflateRect(&next_ghost, scaled(8), scaled(12));
                InvalidateRect(window, &previous_ghost, FALSE);
                InvalidateRect(window, &next_ghost, FALSE);
                if (previous_drop != g_carousel.drop) {
                    RECT strip;
                    GetClientRect(window, &strip);
                    strip.bottom = strip_height();
                    InvalidateRect(window, &strip, FALSE);
                }
            } else {
                InvalidateRect(window, NULL, FALSE); /* Remove the source tile once. */
            }
        } else {
            size_t hovered = tile_at(point);
            unsigned space = card_at(point);
            if (hovered != g_carousel.hovered || space != g_carousel.hover_space) {
                g_carousel.hovered = hovered;
                g_carousel.hover_space = space;
                InvalidateRect(window, NULL, FALSE);
            }
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        g_carousel.hovered = SIZE_MAX;
        g_carousel.hover_space = 0;
        g_carousel.hover_add = false;
        InvalidateRect(window, NULL, FALSE);
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(lparam) == HTCLIENT) {
            SetCursor(LoadCursorW(NULL, g_carousel.dragging ? IDC_SIZEALL :
                g_carousel.hovered < g_carousel.count || g_carousel.hover_space || g_carousel.hover_add ||
                workspace_close_at(g_carousel.pointer) ? IDC_HAND : IDC_ARROW));
            return TRUE;
        }
        break;
    case WM_LBUTTONUP: {
        POINT point = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
        unsigned closing = g_carousel.pressed_close;
        if (closing) {
            bool remove = workspace_close_at(point) == closing;
            cancel_drag();
            if (remove) {
                unsigned old_view = g_carousel.view;
                g_carousel.busy = true;
                bool removed = g_carousel.actions.remove(g_carousel.monitor, closing, g_carousel.message, 256);
                if (window != g_carousel.window)
                    return 0;
                g_carousel.close_space = 0;
                if (removed) {
                    g_carousel.view = old_view >= closing && old_view > 1 ? old_view - 1 : old_view;
                    for (unsigned i = closing; i < g_carousel.spaces; ++i)
                        g_carousel.scroll[i - 1] = g_carousel.scroll[i];
                    g_carousel.scroll[g_carousel.spaces - 1] = 0;
                }
                if (!refresh()) {
                    /* Never leave stale numbered controls active after a close. */
                    ms_carousel_close();
                    return 0;
                }
                select_workspace(g_carousel.view);
                g_carousel.busy = false;
                SetForegroundWindow(window);
                SetFocus(window);
                InvalidateRect(window, NULL, FALSE);
            }
            return 0;
        }
        unsigned destination = card_at(point);
        size_t pressed = g_carousel.pressed;
        RECT add = control_rect(0);
        bool create = g_carousel.dragging && !over_taskbar(point) && PtInRect(&add, point) &&
            g_carousel.spaces < MS_MAX_WORKSPACES && pressed < g_carousel.count;
        bool move = g_carousel.dragging && (destination || create) && pressed < g_carousel.count;
        bool focus = !g_carousel.dragging && pressed < g_carousel.count &&
            (tile_at(point) == pressed ||
             (abs(point.x - g_carousel.origin.x) < GetSystemMetrics(SM_CXDRAG) &&
              abs(point.y - g_carousel.origin.y) < GetSystemMetrics(SM_CYDRAG)));
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
            if (create)
                destination = g_carousel.actions.add(g_carousel.monitor, g_carousel.message, 256);
            if (window == g_carousel.window && destination)
                g_carousel.actions.move(g_carousel.monitor, item.window, item.process, destination, g_carousel.message, 256);
            if (window == g_carousel.window) {
                if (!refresh())
                    wcscpy(g_carousel.message, L"Could not refresh apps. Press F5 to retry.");
                if (create && destination)
                    g_carousel.first = g_carousel.spaces > VISIBLE_CARDS ? g_carousel.spaces - VISIBLE_CARDS : 0;
                g_carousel.busy = false;
                SetForegroundWindow(window);
                SetFocus(window);
                InvalidateRect(window, NULL, FALSE);
            }
        }
        return 0;
    }
    case WM_CAPTURECHANGED:
        cancel_hold();
        g_carousel.pressed_close = 0;
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
        else if (wparam == TIMER_WORKSPACE_HOLD && g_carousel.hold_space &&
                 GetTickCount64() - g_carousel.hold_started >= HOLD_MS) {
            POINT point;
            int button = GetSystemMetrics(SM_SWAPBUTTON) ? VK_RBUTTON : VK_LBUTTON;
            bool held = GetCapture() == window && (GetAsyncKeyState(button) & 0x8000) &&
                GetCursorPos(&point) && ScreenToClient(window, &point) &&
                card_at(point) == g_carousel.hold_space &&
                abs(point.x - g_carousel.origin.x) < GetSystemMetrics(SM_CXDRAG) &&
                abs(point.y - g_carousel.origin.y) < GetSystemMetrics(SM_CYDRAG);
            if (held) {
                g_carousel.close_space = g_carousel.hold_space;
                wcscpy(g_carousel.message, L"Release, then click \x00d7 to close this desktop and keep its apps on a neighbor.");
            }
            cancel_hold();
            InvalidateRect(window, NULL, FALSE);
        }
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
        finish_entrance();
        if (g_carousel.hold_space)
            cancel_drag();
        if (g_carousel.view) {
            g_carousel.scroll[g_carousel.view - 1] += GET_WHEEL_DELTA_WPARAM(wparam) > 0 ? -1 : 1;
            MSOverviewGrid layout = grid();
            g_carousel.scroll[g_carousel.view - 1] = layout.scroll;
            InvalidateRect(window, NULL, FALSE);
        }
        return 0;
    case WM_KEYDOWN:
        finish_entrance();
        if (g_carousel.hold_space || g_carousel.pressed_close)
            cancel_drag();
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
        free_backdrop();
        if (g_carousel.font)
            DeleteObject(g_carousel.font);
        if (g_carousel.heading)
            DeleteObject(g_carousel.heading);
        g_carousel = (Carousel){0};
        return 0;
    default: return DefWindowProcW(window, message, wparam, lparam);
    }
    return DefWindowProcW(window, message, wparam, lparam);
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
    g_carousel.hovered = SIZE_MAX;
    g_carousel.progress = 1.0;
    g_carousel.generation = ++g_generation;
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
    g_carousel.busy = true;
    HWND window = g_carousel.window;
    prepare_backdrop();
    if (window != g_carousel.window)
        return false;
    wcscpy(g_carousel.message, L"Drag windows to a desktop or +. Hold a desktop to reveal \x00d7. Enter switches. Esc returns.");
    if (!g_carousel.font || !g_carousel.heading || !refresh()) {
        ms_carousel_close();
        return false;
    }
    g_carousel.busy = false;
    select_workspace(g_carousel.active);
    if (!SetTimer(g_carousel.window, TIMER_TASKBAR, 50, NULL)) {
        ms_carousel_close();
        return false;
    }
    BOOL animate = FALSE, composition = FALSE;
    bool entrance = !GetSystemMetrics(SM_REMOTESESSION) &&
        SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animate, 0) && animate &&
        SUCCEEDED(DwmIsCompositionEnabled(&composition)) && composition &&
        QueryPerformanceFrequency(&g_carousel.frequency) && g_carousel.frequency.QuadPart > 0;
    if (entrance) {
        update_layout();
        bool moving = false;
        for (size_t i = 0; i < g_carousel.count; ++i)
            if (g_carousel.previews[i].visible && g_carousel.previews[i].handle)
                moving = true;
        entrance = moving;
    }
    if (entrance)
        g_carousel.progress = 0.0;
    SetPropW(g_carousel.window, L"NonRudeHWND", (HANDLE)1);
    ShowWindow(g_carousel.window, SW_SHOW);
    SetForegroundWindow(g_carousel.window);
    SetFocus(g_carousel.window);
    if (window != g_carousel.window)
        return true;
    /* Allocate buffers, compute layout and present initial thumbnails before
     * starting the animation clock, so startup work cannot consume its frames. */
    RedrawWindow(window, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOERASE);
    if (window != g_carousel.window)
        return true;
    if (entrance) {
        if (SUCCEEDED(DwmFlush())) {
            QueryPerformanceCounter(&g_carousel.started);
            if (!PostMessageW(window, WM_OVERVIEW_FRAME, g_carousel.generation, 0))
                finish_entrance();
        } else {
            finish_entrance();
        }
    }
    return true;
}
