#include "overview_layout.h"

#include <stdint.h>
#include <limits.h>

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
    grid.gap = scaled(20, scale);
    grid.columns = (int)((width64 + grid.gap) / (scaled(260, scale) + grid.gap));
    if (grid.columns < 1)
        grid.columns = 1;
    if (grid.columns > 5)
        grid.columns = 5;
    if ((size_t)grid.columns > count)
        grid.columns = (int)count;
    int total_rows = (int)((count - 1) / (size_t)grid.columns + 1);
    grid.rows = (int)((height64 + grid.gap) / (scaled(185, scale) + grid.gap));
    if (grid.rows < 1)
        grid.rows = 1;
    if (grid.rows > total_rows)
        grid.rows = total_rows;
    grid.max_scroll = total_rows - grid.rows;
    grid.scroll = scroll < 0 ? 0 : scroll > grid.max_scroll ? grid.max_scroll : scroll;
    grid.width = (width - (grid.columns - 1) * grid.gap) / grid.columns;
    grid.height = (height - (grid.rows - 1) * grid.gap) / grid.rows;
    if (grid.width > scaled(480, scale))
        grid.width = scaled(480, scale);
    if (grid.height > scaled(300, scale))
        grid.height = scaled(300, scale);
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
    int x = grid->area.left + (int)(slot % (size_t)grid->columns) * (grid->width + grid->gap);
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
