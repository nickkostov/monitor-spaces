#include "overview_layout.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>

static bool inside(MSOverviewRect outer, MSOverviewRect inner)
{
    return inner.left >= outer.left && inner.right <= outer.right &&
           inner.top >= outer.top && inner.bottom <= outer.bottom &&
           inner.right > inner.left && inner.bottom > inner.top;
}

static bool overlap(MSOverviewRect a, MSOverviewRect b)
{
    return a.left < b.right && a.right > b.left && a.top < b.bottom && a.bottom > b.top;
}

static void grids(void)
{
    MSOverviewRect areas[] = {{40, 270, 1880, 1012}, {-1880, -900, -40, -100},
        {80, 540, 3760, 2024}, {0, 0, 1, 1}, {30, 20, 630, 360}};
    size_t counts[] = {1, 3, 10, 37, 200};
    int scales[] = {100, 150, 200};
    for (unsigned a = 0; a < sizeof(areas) / sizeof(areas[0]); ++a)
        for (unsigned c = 0; c < sizeof(counts) / sizeof(counts[0]); ++c)
            for (unsigned s = 0; s < sizeof(scales) / sizeof(scales[0]); ++s) {
                MSOverviewGrid grid = ms_overview_grid(areas[a], counts[c], INT_MAX, scales[s]);
                assert(grid.columns >= 1 && grid.columns <= 5 && grid.rows >= 1);
                assert(grid.scroll == grid.max_scroll);
                bool last_visible = false;
                for (size_t i = 0; i < counts[c]; ++i) {
                    MSOverviewRect cell;
                    if (!ms_overview_cell(&grid, i, &cell))
                        continue;
                    assert(inside(areas[a], cell));
                    if (i + 1 == counts[c])
                        last_visible = true;
                    for (size_t j = i + 1; j < counts[c]; ++j) {
                        MSOverviewRect other;
                        if (ms_overview_cell(&grid, j, &other))
                            assert(!overlap(cell, other));
                    }
                }
                assert(last_visible); /* Scrolling can always reach the last app. */
                grid = ms_overview_grid(areas[a], counts[c], -50, scales[s]);
                assert(grid.scroll == 0);
                MSOverviewRect cell;
                assert(ms_overview_cell(&grid, 0, &cell));
                assert(!ms_overview_cell(&grid, counts[c], &cell));
            }
    MSOverviewGrid empty = ms_overview_grid(areas[0], 0, 0, 100);
    MSOverviewRect cell;
    assert(!ms_overview_cell(&empty, 0, &cell));
    empty = ms_overview_grid((MSOverviewRect){0}, 5, 0, 100);
    assert(!ms_overview_cell(&empty, 0, &cell));
    empty = ms_overview_grid((MSOverviewRect){INT_MIN, 0, INT_MAX, 100}, 5, 0, 100);
    assert(!ms_overview_cell(&empty, 0, &cell));
    puts("PASS overview grids: mixed DPI, negative coordinates, small displays, scrolling, and non-overlapping hit regions");
}

static void aspect_ratio(void)
{
    MSOverviewRect body = {10, 20, 410, 320};
    MSOverviewRect wide = ms_overview_fit(body, 1920, 1080);
    assert(wide.right - wide.left == 400 && wide.bottom - wide.top == 225);
    MSOverviewRect tall = ms_overview_fit(body, 1080, 1920);
    assert(tall.bottom - tall.top == 300 && tall.right - tall.left == 168);
    assert(inside(body, wide) && inside(body, tall));
    assert(inside(body, ms_overview_fit(body, INT_MAX, INT_MAX)));
    MSOverviewRect invalid = ms_overview_fit(body, 0, 40);
    assert(invalid.left == invalid.right && invalid.top == invalid.bottom);
    puts("PASS preview fitting preserves landscape/portrait aspect ratios and rejects invalid sources");
}

static void taskbar_edges(void)
{
    MSOverviewRect monitor = {-1920, -1080, 0, 0};
    for (unsigned edge = MS_EDGE_LEFT; edge <= MS_EDGE_BOTTOM; ++edge) {
        MSOverviewRect strip = ms_overview_edge(monitor, edge, 48);
        assert(inside(monitor, strip));
        if (edge == MS_EDGE_LEFT || edge == MS_EDGE_RIGHT)
            assert(strip.right - strip.left == 48);
        else
            assert(strip.bottom - strip.top == 48);
        assert(!ms_overview_contains(strip, -960, -540));
        assert(ms_overview_contains(strip, strip.left, strip.top));
        assert(!ms_overview_contains(strip, strip.right, strip.bottom));
        MSOverviewRect clamped = ms_overview_edge(monitor, edge, INT_MAX);
        assert(clamped.left == monitor.left && clamped.right == monitor.right);
        assert(clamped.top == monitor.top && clamped.bottom == monitor.bottom);
    }
    puts("PASS taskbar reveal strips for every screen edge, negative coordinates, clipping, and hit testing");
}

int main(void)
{
    grids();
    aspect_ratio();
    taskbar_edges();
    return 0;
}
