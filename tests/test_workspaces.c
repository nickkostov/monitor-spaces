#include "workspaces.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

enum { LEFT = 11, RIGHT = 22, WINDOW_LIMIT = 16 };

typedef struct {
    MSWindow windows[WINDOW_LIMIT];
    size_t count;
    MSHandle fail_tag, fail_hide, fail_show, fail_clear;
    MSHandle owner, popup;
    size_t mutations;
    bool require_all_tagged;
    bool keep_visible; /* Minimization preserves the Win32 WS_VISIBLE style. */
} Desktop;

static MSWindow *find(Desktop *desktop, MSHandle id)
{
    for (size_t i = 0; i < desktop->count; ++i)
        if (desktop->windows[i].id == id)
            return &desktop->windows[i];
    assert(!"unknown simulated window");
    return NULL;
}

static MSHandle open_window(Desktop *desktop, MSHandle monitor, bool visible, bool eligible)
{
    assert(desktop->count < WINDOW_LIMIT);
    MSHandle id = 100 + desktop->count;
    desktop->windows[desktop->count++] = (MSWindow){
        .id = id, .monitor = monitor, .visible = visible, .eligible = eligible
    };
    return id;
}

static bool tag(void *context, MSHandle id, unsigned workspace)
{
    Desktop *desktop = context;
    ++desktop->mutations;
    if (desktop->fail_tag == id)
        return false;
    find(desktop, id)->tag = workspace;
    return true;
}

static bool clear(void *context, MSHandle id)
{
    Desktop *desktop = context;
    ++desktop->mutations;
    if (desktop->fail_clear == id)
        return false;
    find(desktop, id)->tag = 0;
    return true;
}

static bool hide(void *context, MSHandle id)
{
    Desktop *desktop = context;
    ++desktop->mutations;
    MSWindow *window = find(desktop, id);
    /* Enforce the recovery invariant at the platform boundary. */
    assert(ms_valid_tag(window->tag));
    if (desktop->require_all_tagged)
        for (size_t i = 0; i < desktop->count; ++i)
            assert(ms_valid_tag(desktop->windows[i].tag));
    if (desktop->fail_hide == id)
        return false;
    window->visible = desktop->keep_visible;
    if (desktop->owner == id)
        find(desktop, desktop->popup)->visible = false;
    return true;
}

static bool show(void *context, MSHandle id)
{
    Desktop *desktop = context;
    ++desktop->mutations;
    if (desktop->fail_show == id)
        return false;
    find(desktop, id)->visible = true;
    return true;
}

static MSOperations operations(Desktop *desktop)
{
    return (MSOperations){desktop, tag, clear, hide, show};
}

static MSState start(void)
{
    MSState state = {0};
    MSHandle monitors[] = {LEFT, RIGHT};
    assert(ms_set_monitors(&state, monitors, 2));
    assert(ms_active(&state, LEFT) == 1 && ms_active(&state, RIGHT) == 1);
    return state;
}

static MSResult switch_to(MSState *state, Desktop *desktop, MSHandle monitor, unsigned destination)
{
    MSWindow snapshot[WINDOW_LIMIT];
    memcpy(snapshot, desktop->windows, desktop->count * sizeof(*snapshot));
    MSOperations ops = operations(desktop);
    MSResult result;
    assert(ms_switch(state, monitor, destination, snapshot, desktop->count, &ops, &result));
    return result;
}

static MSResult restore(MSState *state, Desktop *desktop)
{
    MSWindow snapshot[WINDOW_LIMIT];
    memcpy(snapshot, desktop->windows, desktop->count * sizeof(*snapshot));
    MSOperations ops = operations(desktop);
    MSResult result;
    ms_restore(state, snapshot, desktop->count, &ops, &result);
    return result;
}

static void independent_monitors_and_new_windows(void)
{
    Desktop desktop = {0};
    MSState state = start();
    MSHandle a = open_window(&desktop, LEFT, true, true);
    MSHandle b = open_window(&desktop, RIGHT, true, true);
    MSResult result = switch_to(&state, &desktop, LEFT, 2);
    assert(result.hidden == 1 && !result.errors);
    assert(!find(&desktop, a)->visible && find(&desktop, a)->tag == 1);
    assert(find(&desktop, b)->visible && !find(&desktop, b)->tag);
    assert(ms_active(&state, LEFT) == 2 && ms_active(&state, RIGHT) == 1);

    MSHandle c = open_window(&desktop, LEFT, true, true);
    result = switch_to(&state, &desktop, RIGHT, 4);
    assert(result.hidden == 1 && !result.errors);
    assert(find(&desktop, c)->visible && !find(&desktop, b)->visible);
    assert(ms_active(&state, LEFT) == 2 && ms_active(&state, RIGHT) == 4);

    result = switch_to(&state, &desktop, LEFT, 1);
    assert(result.hidden == 1 && result.shown == 1 && !result.errors);
    assert(find(&desktop, a)->visible && !find(&desktop, a)->tag);
    assert(!find(&desktop, c)->visible && find(&desktop, c)->tag == 2);
    assert(!find(&desktop, b)->visible && ms_active(&state, RIGHT) == 4);

    switch_to(&state, &desktop, LEFT, 2);
    assert(!find(&desktop, a)->visible && find(&desktop, c)->visible);
    switch_to(&state, &desktop, RIGHT, 1);
    assert(find(&desktop, b)->visible && find(&desktop, c)->visible);
    puts("PASS independent displays, workspaces 1/2/4, and windows opened in workspace 2");
}

static void recovery_after_interruption(void)
{
    Desktop desktop = {0};
    MSState state = start();
    MSHandle a = open_window(&desktop, LEFT, true, true);
    MSHandle b = open_window(&desktop, RIGHT, true, true);
    MSHandle tray = open_window(&desktop, LEFT, false, true);
    switch_to(&state, &desktop, LEFT, 3);
    switch_to(&state, &desktop, RIGHT, 2);
    /* Simulate process memory loss; only foreign window properties survive. */
    memset(&state, 0, sizeof(state));
    /* Also recover a window tagged just before a crash, but not yet hidden. */
    MSHandle pending = open_window(&desktop, RIGHT, true, true);
    find(&desktop, pending)->tag = 4;
    /* Recovery cannot depend on the old monitor handles or current eligibility. */
    find(&desktop, a)->monitor = 999;
    find(&desktop, b)->eligible = false;
    MSResult result = restore(&state, &desktop);
    assert(result.shown == 3 && !result.errors);
    assert(find(&desktop, a)->visible && find(&desktop, b)->visible);
    assert(find(&desktop, pending)->visible && !find(&desktop, pending)->tag);
    assert(!find(&desktop, tray)->visible && !find(&desktop, tray)->tag);
    state = start();
    assert(ms_active(&state, LEFT) == 1 && ms_active(&state, RIGHT) == 1);
    puts("PASS interrupted run recovery, removed displays, changed eligibility, naturally hidden windows");
}

static void all_four_workspaces(void)
{
    Desktop desktop = {0};
    MSState state = start();
    MSHandle apps[MS_WORKSPACE_COUNT];
    apps[0] = open_window(&desktop, LEFT, true, true);
    for (unsigned workspace = 2; workspace <= MS_WORKSPACE_COUNT; ++workspace) {
        switch_to(&state, &desktop, LEFT, workspace);
        apps[workspace - 1] = open_window(&desktop, LEFT, true, true);
    }
    for (unsigned workspace = 1; workspace <= MS_WORKSPACE_COUNT; ++workspace) {
        MSResult result = switch_to(&state, &desktop, LEFT, workspace);
        assert(!result.errors && ms_active(&state, LEFT) == workspace);
        for (unsigned i = 0; i < MS_WORKSPACE_COUNT; ++i)
            assert(find(&desktop, apps[i])->visible == (i + 1 == workspace));
        assert(ms_active(&state, RIGHT) == 1);
    }
    MSResult result = restore(&state, &desktop);
    assert(result.shown == 3 && !result.errors);
    for (unsigned i = 0; i < MS_WORKSPACE_COUNT; ++i)
        assert(find(&desktop, apps[i])->visible && !find(&desktop, apps[i])->tag);
    puts("PASS all four workspaces keep their own newly opened windows");
}

static void exclusions_and_noops(void)
{
    Desktop desktop = {0};
    MSState state = start();
    MSHandle panel = open_window(&desktop, LEFT, true, false);
    MSHandle taskbar = open_window(&desktop, LEFT, true, false);
    MSHandle hidden = open_window(&desktop, LEFT, false, true);
    MSHandle app = open_window(&desktop, LEFT, true, true);
    switch_to(&state, &desktop, LEFT, 1);
    assert(desktop.mutations == 0);
    MSWindow snapshot[WINDOW_LIMIT];
    memcpy(snapshot, desktop.windows, desktop.count * sizeof(*snapshot));
    MSOperations ops = operations(&desktop);
    MSResult result;
    assert(!ms_switch(&state, LEFT, 0, snapshot, desktop.count, &ops, &result));
    assert(!ms_switch(&state, LEFT, 5, snapshot, desktop.count, &ops, &result));
    assert(!ms_switch(&state, 999, 2, snapshot, desktop.count, &ops, &result));
    assert(desktop.mutations == 0 && ms_active(&state, LEFT) == 1);
    result = switch_to(&state, &desktop, LEFT, 2);
    assert(result.hidden == 1 && !result.errors);
    assert(find(&desktop, panel)->visible && !find(&desktop, panel)->tag);
    assert(find(&desktop, taskbar)->visible && !find(&desktop, taskbar)->tag);
    assert(!find(&desktop, hidden)->visible && !find(&desktop, hidden)->tag);
    assert(!find(&desktop, app)->visible);
    puts("PASS exclusions, repeat selection, and invalid shortcuts/monitors");
}

static void platform_failures_remain_recoverable(void)
{
    Desktop desktop = {0};
    MSState state = start();
    MSHandle protected = open_window(&desktop, LEFT, true, true);
    MSHandle stubborn = open_window(&desktop, LEFT, true, true);
    MSHandle app = open_window(&desktop, LEFT, true, true);
    desktop.fail_tag = protected;
    desktop.fail_hide = stubborn;
    MSResult result = switch_to(&state, &desktop, LEFT, 2);
    assert(result.errors == 2 && result.hidden == 1);
    assert(find(&desktop, protected)->visible && !find(&desktop, protected)->tag);
    assert(find(&desktop, stubborn)->visible && find(&desktop, stubborn)->tag == 1);
    assert(!find(&desktop, app)->visible && find(&desktop, app)->tag == 1);

    desktop.fail_show = app;
    result = restore(&state, &desktop);
    assert(result.errors == 1 && !find(&desktop, app)->visible);
    assert(find(&desktop, app)->tag == 1);
    assert(ms_active(&state, LEFT) == 1 && ms_active(&state, RIGHT) == 1);
    desktop.fail_show = 0;
    desktop.fail_clear = app;
    result = restore(&state, &desktop);
    assert(result.errors == 1 && find(&desktop, app)->visible && find(&desktop, app)->tag == 1);
    desktop.fail_clear = 0;
    result = restore(&state, &desktop);
    assert(!result.errors && !find(&desktop, app)->tag);
    result = restore(&state, &desktop);
    assert(!result.errors && result.shown == 0);
    puts("PASS tag/hide/show/clear failures, retry recovery, and idempotent restore");
}

static void owner_side_effects_use_a_complete_snapshot(void)
{
    Desktop desktop = {0};
    MSState state = start();
    desktop.owner = open_window(&desktop, LEFT, true, true);
    desktop.popup = open_window(&desktop, LEFT, true, true);
    desktop.require_all_tagged = true;
    MSResult result = switch_to(&state, &desktop, LEFT, 2);
    assert(result.hidden == 2 && !result.errors);
    assert(find(&desktop, desktop.owner)->tag == 1 && find(&desktop, desktop.popup)->tag == 1);
    result = restore(&state, &desktop);
    assert(result.shown == 2 && !result.errors);
    puts("PASS all recovery tags written before an owner can hide its popup");
}

static void moved_windows_and_monitor_reset(void)
{
    Desktop desktop = {0};
    MSState state = start();
    MSHandle app = open_window(&desktop, LEFT, true, true);
    switch_to(&state, &desktop, LEFT, 2);
    find(&desktop, app)->monitor = RIGHT;
    switch_to(&state, &desktop, RIGHT, 2);
    assert(!find(&desktop, app)->visible);
    switch_to(&state, &desktop, RIGHT, 1);
    assert(find(&desktop, app)->visible && !find(&desktop, app)->tag);
    switch_to(&state, &desktop, RIGHT, 3);
    restore(&state, &desktop);
    MSHandle replacement[] = {33};
    assert(ms_set_monitors(&state, replacement, 1));
    assert(ms_active(&state, 33) == 1 && ms_active(&state, LEFT) == 0);
    assert(find(&desktop, app)->visible && !find(&desktop, app)->tag);
    puts("PASS moved windows and restore before replacing the monitor configuration");
}

static void monitor_validation(void)
{
    MSState state = start();
    MSHandle duplicate[] = {LEFT, LEFT};
    MSHandle zero[] = {0};
    MSHandle many[MS_MAX_MONITORS + 1];
    for (size_t i = 0; i < MS_MAX_MONITORS + 1; ++i)
        many[i] = i + 1;
    assert(!ms_set_monitors(&state, duplicate, 2));
    assert(!ms_set_monitors(&state, zero, 1));
    assert(!ms_set_monitors(&state, NULL, 0));
    assert(!ms_set_monitors(&state, many, MS_MAX_MONITORS + 1));
    assert(state.count == 2 && ms_active(&state, RIGHT) == 1);
    assert(ms_set_monitors(&state, many, MS_MAX_MONITORS));
    assert(state.count == MS_MAX_MONITORS && ms_active(&state, MS_MAX_MONITORS) == 1);
    assert(!ms_valid_tag(0) && !ms_valid_tag(5) && ms_valid_tag(4));
    puts("PASS monitor validation and supported monitor limit");
}

static void taskbar_windows_keep_membership(void)
{
    Desktop desktop = {.keep_visible = true};
    MSState state = start();
    MSHandle apps[MS_WORKSPACE_COUNT];
    MSHandle other = open_window(&desktop, RIGHT, true, true);
    apps[0] = open_window(&desktop, LEFT, true, true);
    for (unsigned workspace = 2; workspace <= MS_WORKSPACE_COUNT; ++workspace) {
        MSResult result = switch_to(&state, &desktop, LEFT, workspace);
        assert(result.hidden == 1 && !result.errors);
        apps[workspace - 1] = open_window(&desktop, LEFT, true, true);
    }
    /* All remain visible to Windows/taskbar, with independent membership. */
    for (unsigned i = 0; i < MS_WORKSPACE_COUNT; ++i) {
        assert(find(&desktop, apps[i])->visible);
        assert(find(&desktop, apps[i])->tag == (i == 3 ? 0 : i + 1));
    }
    /* Selecting an inactive app targets its workspace and its monitor, even
     * when all other inactive apps also have WS_VISIBLE. */
    MSWindow *selected = find(&desktop, apps[1]);
    MSResult result = switch_to(&state, &desktop, selected->monitor, selected->tag);
    assert(result.hidden == 1 && result.shown == 1 && !result.errors);
    assert(!selected->tag && ms_active(&state, LEFT) == 2);
    assert(find(&desktop, apps[0])->tag == 1);
    assert(find(&desktop, apps[2])->tag == 3);
    assert(find(&desktop, apps[3])->tag == 4);
    assert(ms_active(&state, RIGHT) == 1 && !find(&desktop, other)->tag);
    /* Process memory loss must still recover every inactive membership. */
    memset(&state, 0, sizeof(state));
    result = restore(&state, &desktop);
    assert(result.shown == 3 && !result.errors);
    for (unsigned i = 0; i < MS_WORKSPACE_COUNT; ++i)
        assert(find(&desktop, apps[i])->visible && !find(&desktop, apps[i])->tag);
    puts("PASS taskbar-visible inactive windows retain membership across all workspaces and recovery");
}

static void relative_navigation_wraps_per_monitor(void)
{
    Desktop desktop = {0};
    MSState state = start();
    MSHandle first = open_window(&desktop, LEFT, true, true);
    MSHandle other = open_window(&desktop, RIGHT, true, true);
    /* Left from workspace 1 wraps to 4; a new window belongs to workspace 4. */
    switch_to(&state, &desktop, LEFT, ms_adjacent(&state, LEFT, -1));
    assert(ms_active(&state, LEFT) == 4 && !find(&desktop, first)->visible);
    MSHandle fourth = open_window(&desktop, LEFT, true, true);
    switch_to(&state, &desktop, LEFT, ms_adjacent(&state, LEFT, 1));
    assert(ms_active(&state, LEFT) == 1 && find(&desktop, first)->visible);
    assert(!find(&desktop, fourth)->visible && find(&desktop, fourth)->tag == 4);
    /* Exercise every intermediate workspace in both directions. */
    for (unsigned expected = 2; expected <= 4; ++expected) {
        switch_to(&state, &desktop, LEFT, ms_adjacent(&state, LEFT, 1));
        assert(ms_active(&state, LEFT) == expected);
        assert(find(&desktop, fourth)->visible == (expected == 4));
    }
    for (unsigned expected = 3; expected >= 1; --expected) {
        switch_to(&state, &desktop, LEFT, ms_adjacent(&state, LEFT, -1));
        assert(ms_active(&state, LEFT) == expected);
    }
    assert(ms_active(&state, RIGHT) == 1 && find(&desktop, other)->visible);
    switch_to(&state, &desktop, RIGHT, ms_adjacent(&state, RIGHT, -1));
    assert(ms_active(&state, RIGHT) == 4 && ms_active(&state, LEFT) == 1);
    assert(find(&desktop, first)->visible && !find(&desktop, other)->visible);
    assert(ms_adjacent(&state, 999, 1) == 0);
    assert(ms_adjacent(&state, LEFT, 0) == 0);
    assert(ms_adjacent(&state, LEFT, 2) == 0);
    puts("PASS relative navigation wraps 1/4, restores windows, and targets each monitor independently");
}

int main(void)
{
    independent_monitors_and_new_windows();
    all_four_workspaces();
    recovery_after_interruption();
    exclusions_and_noops();
    platform_failures_remain_recoverable();
    owner_side_effects_use_a_complete_snapshot();
    moved_windows_and_monitor_reset();
    monitor_validation();
    taskbar_windows_keep_membership();
    relative_navigation_wraps_per_monitor();
    puts("All workspace simulations passed.");
    return 0;
}
