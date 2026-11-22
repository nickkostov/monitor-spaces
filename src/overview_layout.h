#ifndef MONITOR_SPACES_OVERVIEW_LAYOUT_H
#define MONITOR_SPACES_OVERVIEW_LAYOUT_H

#include <stdbool.h>
#include <stddef.h>

typedef struct { int left, top, right, bottom; } MSOverviewRect;
typedef struct {
    MSOverviewRect area;
    int columns, rows, width, height, gap, scroll, max_scroll;
    size_t count;
} MSOverviewGrid;

enum { MS_EDGE_LEFT, MS_EDGE_TOP, MS_EDGE_RIGHT, MS_EDGE_BOTTOM };

#define MS_OVERVIEW_DURATION_MS 400u

double ms_overview_progress(double elapsed_ms);
MSOverviewRect ms_overview_interpolate(MSOverviewRect from, MSOverviewRect to, double progress);

MSOverviewGrid ms_overview_grid(MSOverviewRect area, size_t count, int scroll, int scale);
bool ms_overview_cell(const MSOverviewGrid *grid, size_t index, MSOverviewRect *rect);
MSOverviewRect ms_overview_fit(MSOverviewRect area, int width, int height);
MSOverviewRect ms_overview_cover(MSOverviewRect area, int width, int height);
MSOverviewRect ms_overview_project(MSOverviewRect area, MSOverviewRect monitor, MSOverviewRect window);
MSOverviewRect ms_overview_edge(MSOverviewRect monitor, unsigned edge, int thickness);
bool ms_overview_contains(MSOverviewRect rect, int x, int y);

#endif
