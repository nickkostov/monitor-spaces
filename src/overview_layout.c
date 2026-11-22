#include "overview_layout.h"

#include <stdint.h>
#include <limits.h>

double ms_overview_progress(double elapsed_ms)
{
    if (!(elapsed_ms > 0.0))
        return 0.0;
    if (elapsed_ms >= MS_OVERVIEW_DURATION_MS)
        return 1.0;
    double time = elapsed_ms / MS_OVERVIEW_DURATION_MS;
    return time * time * (3.0 - 2.0 * time);
}

static int interpolate(int from, int to, double progress)
{
    double value = from + ((double)to - from) * progress;
    return (int)(value >= 0.0 ? value + 0.5 : value - 0.5);
}

MSOverviewRect ms_overview_interpolate(MSOverviewRect from, MSOverviewRect to, double progress)
{
    if (!(progress > 0.0))
        return from;
    if (progress >= 1.0)
        return to;
    return (MSOverviewRect){interpolate(from.left, to.left, progress),
        interpolate(from.top, to.top, progress), interpolate(from.right, to.right, progress),
        interpolate(from.bottom, to.bottom, progress)};
}

static int scaled(int value, int scale)
{
    return (int)((int64_t)value * scale / 100);
}

MSOverviewGrid ms_overview_grid(MSOverviewRect area, size_t count, int scroll, int scale)
{
    MSOverviewGrid grid = {.area = area, .count = count};
    int64_t width64 = (int64_t)area.right - area.left, height64 = (int64_t)area.bottom - area.top;
    if (width64 <= 0 || height64 <= 0 || width64 > INT_MAX || height64 > INT_MAX ||
        !count || count > INT_MAX || scale < 50 || scale > 800)
        return grid;
    int width = (int)width64, height = (int)height64;
    grid.gap = scaled(32, scale);
    grid.columns = (int)((width64 + grid.gap) / (scaled(300, scale) + grid.gap));
    if (grid.columns < 1)
        grid.columns = 1;
    int preferred = count == 1 ? 1 : count <= 4 ? 2 : count <= 9 ? 3 : 4;
    if (grid.columns > preferred)
        grid.columns = preferred;
    int total_rows = (int)((count - 1) / (size_t)grid.columns + 1);
    grid.rows = (int)((height64 + grid.gap) / (scaled(count <= 9 ? 120 : 220, scale) + grid.gap));
    if (grid.rows < 1)
        grid.rows = 1;
    if (grid.rows > total_rows)
        grid.rows = total_rows;
    grid.max_scroll = total_rows - grid.rows;
    grid.scroll = scroll < 0 ? 0 : scroll > grid.max_scroll ? grid.max_scroll : scroll;
    grid.width = (width - (grid.columns - 1) * grid.gap) / grid.columns;
    grid.height = (height - (grid.rows - 1) * grid.gap) / grid.rows;
    if (grid.width > scaled(1000, scale))
        grid.width = scaled(1000, scale);
    if (grid.height > scaled(700, scale))
        grid.height = scaled(700, scale);
    grid.area.left += (width - grid.columns * grid.width - (grid.columns - 1) * grid.gap) / 2;
    grid.area.top += (height - grid.rows * grid.height - (grid.rows - 1) * grid.gap) / 2;
    return grid;
}

bool ms_overview_cell(const MSOverviewGrid *grid, size_t index, MSOverviewRect *rect)
{
    if (!grid->columns || !grid->rows || index >= grid->count)
        return false;
    size_t first = (size_t)grid->scroll * (size_t)grid->columns;
    if (index < first || index - first >= (size_t)grid->columns * (size_t)grid->rows)
        return false;
    size_t slot = index - first;
    size_t row_start = index - index % (size_t)grid->columns;
    size_t row_count = grid->count - row_start;
    if (row_count > (size_t)grid->columns)
        row_count = (size_t)grid->columns;
    int inset = (grid->columns - (int)row_count) * (grid->width + grid->gap) / 2;
    int x = grid->area.left + inset + (int)(slot % (size_t)grid->columns) * (grid->width + grid->gap);
    int y = grid->area.top + (int)(slot / (size_t)grid->columns) * (grid->height + grid->gap);
    *rect = (MSOverviewRect){x, y, x + grid->width, y + grid->height};
    return true;
}

MSOverviewRect ms_overview_fit(MSOverviewRect area, int width, int height)
{
    int64_t width64 = (int64_t)area.right - area.left, height64 = (int64_t)area.bottom - area.top;
    if (width <= 0 || height <= 0 || width64 <= 0 || height64 <= 0 || width64 > INT_MAX || height64 > INT_MAX)
        return (MSOverviewRect){area.left, area.top, area.left, area.top};
    int available_width = (int)width64, available_height = (int)height64;
    int fit_width, fit_height;
    if ((int64_t)height * available_width > (int64_t)available_height * width) {
        fit_height = available_height;
        fit_width = (int)((int64_t)width * available_height / height);
    } else {
        fit_width = available_width;
        fit_height = (int)((int64_t)height * available_width / width);
    }
    int x = area.left + (available_width - fit_width) / 2;
    int y = area.top + (available_height - fit_height) / 2;
    return (MSOverviewRect){x, y, x + fit_width, y + fit_height};
}

MSOverviewRect ms_overview_cover(MSOverviewRect area, int width, int height)
{
    int64_t available_width = (int64_t)area.right - area.left;
    int64_t available_height = (int64_t)area.bottom - area.top;
    MSOverviewRect empty = {area.left, area.top, area.left, area.top};
    if (width <= 0 || height <= 0 || available_width <= 0 || available_height <= 0 ||
        available_width > INT_MAX || available_height > INT_MAX)
        return empty;
    int64_t fit_width = available_width, fit_height = available_height;
    if ((int64_t)height * available_width < available_height * width)
        fit_width = ((int64_t)width * available_height + height - 1) / height;
    else
        fit_height = ((int64_t)height * available_width + width - 1) / width;
    int64_t x = (int64_t)area.left + (available_width - fit_width) / 2;
    int64_t y = (int64_t)area.top + (available_height - fit_height) / 2;
    if (fit_width > INT_MAX || fit_height > INT_MAX || x < INT_MIN || y < INT_MIN ||
        x + fit_width > INT_MAX || y + fit_height > INT_MAX)
        return empty;
    return (MSOverviewRect){(int)x, (int)y, (int)(x + fit_width), (int)(y + fit_height)};
}

MSOverviewRect ms_overview_project(MSOverviewRect area, MSOverviewRect monitor, MSOverviewRect window)
{
    int64_t width = (int64_t)monitor.right - monitor.left, height = (int64_t)monitor.bottom - monitor.top;
    int64_t target_width = (int64_t)area.right - area.left, target_height = (int64_t)area.bottom - area.top;
    MSOverviewRect empty = {area.left, area.top, area.left, area.top};
    if (width <= 0 || height <= 0 || width > INT_MAX || height > INT_MAX ||
        target_width <= 0 || target_height <= 0 || target_width > INT_MAX || target_height > INT_MAX)
        return empty;
    if (window.left < monitor.left) window.left = monitor.left;
    if (window.top < monitor.top) window.top = monitor.top;
    if (window.right > monitor.right) window.right = monitor.right;
    if (window.bottom > monitor.bottom) window.bottom = monitor.bottom;
    if (window.right <= window.left || window.bottom <= window.top)
        return empty;
    return (MSOverviewRect){
        area.left + (int)(((int64_t)window.left - monitor.left) * target_width / width),
        area.top + (int)(((int64_t)window.top - monitor.top) * target_height / height),
        area.left + (int)(((int64_t)window.right - monitor.left) * target_width / width),
        area.top + (int)(((int64_t)window.bottom - monitor.top) * target_height / height)};
}

MSOverviewRect ms_overview_edge(MSOverviewRect monitor, unsigned edge, int thickness)
{
    int extent = edge == MS_EDGE_LEFT || edge == MS_EDGE_RIGHT ?
        monitor.right - monitor.left : monitor.bottom - monitor.top;
    if (extent <= 0 || edge > MS_EDGE_BOTTOM)
        return (MSOverviewRect){0};
    if (thickness < 1)
        thickness = 1;
    if (thickness > extent)
        thickness = extent;
    switch (edge) {
    case MS_EDGE_LEFT: monitor.right = monitor.left + thickness; break;
    case MS_EDGE_TOP: monitor.bottom = monitor.top + thickness; break;
    case MS_EDGE_RIGHT: monitor.left = monitor.right - thickness; break;
    case MS_EDGE_BOTTOM: monitor.top = monitor.bottom - thickness; break;
    }
    return monitor;
}

bool ms_overview_contains(MSOverviewRect rect, int x, int y)
{
    return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}
