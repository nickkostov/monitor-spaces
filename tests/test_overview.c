#include "overview_layout.h"

#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>

static void animation_timing(void)
{
    assert(ms_overview_progress(-10.0) == 0.0);
    assert(ms_overview_progress(NAN) == 0.0);
    assert(ms_overview_progress(0.0) == 0.0);
    assert(ms_overview_progress(MS_OVERVIEW_DURATION_MS) == 1.0);
    assert(ms_overview_progress(INFINITY) == 1.0);
    assert(ms_overview_progress(200.0) == 0.5);
    MSOverviewRect from = {-4000, 100, 0, 2100}, to = {0, 200, 2000, 1200};
    int rates[] = {60, 75, 120, 144, 165, 240};
    for (unsigned r = 0; r < sizeof(rates) / sizeof(rates[0]); ++r) {
        double previous = -1.0;
        MSOverviewRect last = from;
        for (unsigned frame = 0; frame <= (unsigned)rates[r]; ++frame) {
            double phase = ms_overview_progress(frame * 1000.0 / rates[r]);
            assert(phase >= previous && phase <= 1.0);
            MSOverviewRect current = ms_overview_interpolate(from, to, phase);
            assert(current.left >= last.left && current.top >= last.top);
            assert(current.right >= last.right && current.bottom <= last.bottom);
            assert(current.right > current.left && current.bottom > current.top);
            previous = phase;
            last = current;
        }
        assert(last.left == to.left && last.top == to.top && last.right == to.right && last.bottom == to.bottom);
    }
    double first = ms_overview_progress(200.0), next = ms_overview_progress(200.25);
    assert(next > first); /* Preserve fractional QPC milliseconds at high refresh rates. */
    MSOverviewRect a = ms_overview_interpolate(from, to, first);
    MSOverviewRect b = ms_overview_interpolate(from, to, next);
    assert(b.left > a.left);
    a = ms_overview_interpolate(from, to, NAN);
    assert(a.left == from.left && a.bottom == from.bottom);
    a = ms_overview_interpolate((MSOverviewRect){INT_MIN, INT_MIN, INT_MAX, INT_MAX}, to, 0.5);
    assert(a.left < a.right && a.top < a.bottom);
    puts("PASS overview animation: fractional timing at 60-240 Hz, monotonic motion, exact endpoints, negative/large coordinates");
}

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
                assert(grid.columns >= 1 && grid.columns <= 4 && grid.rows >= 1);
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

static void floating_windows(void)
{
    MSOverviewRect area = {48, 180, 1232, 656};
    MSOverviewGrid grid = ms_overview_grid(area, 3, 0, 100);
    MSOverviewRect first, second, third;
    assert(grid.columns == 2 && grid.rows == 2 && grid.max_scroll == 0);
    assert(ms_overview_cell(&grid, 0, &first));
    assert(ms_overview_cell(&grid, 1, &second));
    assert(ms_overview_cell(&grid, 2, &third));
    assert(first.top == second.top && third.top > first.top);
    assert(third.left + third.right == area.left + area.right); /* Center incomplete rows. */
    assert(!overlap(first, third) && !overlap(second, third));
    grid = ms_overview_grid(area, 9, 0, 100);
    assert(grid.columns == 3 && grid.rows == 3 && grid.max_scroll == 0);
    puts("PASS floating windows: three-window arrangement centers the final row; nine windows fit without scrolling");
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

static void wallpaper_and_miniatures(void)
{
    MSOverviewRect monitor = {-1920, -1080, 0, 0};
    MSOverviewRect card = {100, 20, 292, 128};
    MSOverviewRect projected = ms_overview_project(card, monitor, monitor);
    assert(projected.left == card.left && projected.top == card.top &&
           projected.right == card.right && projected.bottom == card.bottom);
    projected = ms_overview_project(card, monitor, (MSOverviewRect){-960, -540, 0, 0});
    assert(projected.left == 196 && projected.top == 74 && projected.right == 292 && projected.bottom == 128);
    projected = ms_overview_project(card, monitor, (MSOverviewRect){-2200, -1400, -960, -540});
    assert(projected.left == 100 && projected.top == 20 && projected.right == 196 && projected.bottom == 74);
    projected = ms_overview_project(card, monitor, (MSOverviewRect){20, 20, 800, 600});
    assert(projected.right == projected.left && projected.bottom == projected.top);
    projected = ms_overview_project(card, (MSOverviewRect){0}, monitor);
    assert(projected.right == projected.left && projected.bottom == projected.top);
    MSOverviewRect area = {0, 0, 400, 300};
    MSOverviewRect cover = ms_overview_cover(area, 1920, 1080);
    assert(cover.left <= 0 && cover.top <= 0 && cover.right >= 400 && cover.bottom >= 300);
    assert(cover.bottom - cover.top == 300);
    cover = ms_overview_cover(area, 1080, 1920);
    assert(cover.left == 0 && cover.right == 400 && cover.top < 0 && cover.bottom > 300);
    cover = ms_overview_cover(monitor, 3840, 2160);
    assert(cover.left == monitor.left && cover.right == monitor.right &&
           cover.top == monitor.top && cover.bottom == monitor.bottom);
    cover = ms_overview_cover(area, 0, 1);
    assert(cover.right == cover.left && cover.bottom == cover.top);
    cover = ms_overview_cover(area, 1, INT_MAX);
    assert(cover.right == cover.left && cover.bottom == cover.top);
    puts("PASS wallpaper cover fitting and miniature projection: negative monitors, clipped/offscreen windows, invalid bounds");
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
    animation_timing();
    grids();
    floating_windows();
    aspect_ratio();
    wallpaper_and_miniatures();
    taskbar_edges();
    return 0;
}
