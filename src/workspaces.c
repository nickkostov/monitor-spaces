#include "workspaces.h"

bool ms_valid_tag(unsigned tag)
{
    /* Recovery accepts any supported tag, even before monitor counts load. */
    return tag >= 1 && tag <= MS_MAX_WORKSPACES;
}

bool ms_set_monitors(MSState *state, const MSHandle *ids, size_t count)
{
    if (!ids || count == 0 || count > MS_MAX_MONITORS)
        return false;
    for (size_t i = 0; i < count; ++i) {
        if (!ids[i])
            return false;
        for (size_t j = 0; j < i; ++j)
            if (ids[i] == ids[j])
                return false;
    }
    state->count = count;
    for (size_t i = 0; i < count; ++i)
        state->monitors[i] = (MSMonitor){ids[i], 1, MS_DEFAULT_WORKSPACES};
    return true;
}

unsigned ms_active(const MSState *state, MSHandle monitor)
{
    for (size_t i = 0; i < state->count; ++i)
        if (state->monitors[i].id == monitor)
            return state->monitors[i].active;
    return 0;
}

unsigned ms_workspace_count(const MSState *state, MSHandle monitor)
{
    for (size_t i = 0; i < state->count; ++i)
        if (state->monitors[i].id == monitor)
            return state->monitors[i].workspaces;
    return 0;
}

bool ms_set_workspace_count(MSState *state, MSHandle monitor, unsigned count)
{
    if (count < MS_DEFAULT_WORKSPACES || count > MS_MAX_WORKSPACES)
        return false;
    for (size_t i = 0; i < state->count; ++i) {
        if (state->monitors[i].id == monitor) {
            /* Expansion only: shrinking could strand inactive windows. */
            if (count < state->monitors[i].workspaces)
                return false;
            state->monitors[i].workspaces = count;
            return true;
        }
    }
    return false;
}

unsigned ms_add_workspace(MSState *state, MSHandle monitor)
{
    unsigned count = ms_workspace_count(state, monitor);
    return count && count < MS_MAX_WORKSPACES && ms_set_workspace_count(state, monitor, count + 1) ?
        count + 1 : 0;
}

unsigned ms_adjacent(const MSState *state, MSHandle monitor, int direction)
{
    unsigned active = ms_active(state, monitor);
    unsigned count = ms_workspace_count(state, monitor);
    if (!ms_valid_tag(active) || (direction != -1 && direction != 1))
        return 0;
    if (direction < 0)
        return active == 1 ? count : active - 1;
    return active == count ? 1 : active + 1;
}

static void reveal(MSWindow *window, const MSOperations *ops, MSResult *result)
{
    /* Keep the tag if revealing fails, so a later recovery can retry. */
    if (!ops->show(ops->context, window->id)) {
        ++result->errors;
        return;
    }
    ++result->shown;
    if (ops->clear_tag(ops->context, window->id))
        window->tag = 0;
    else
        ++result->errors;
}

bool ms_switch(MSState *state, MSHandle monitor, unsigned destination,
               MSWindow *windows, size_t count, const MSOperations *ops,
               MSResult *result)
{
    *result = (MSResult){0};
    unsigned source = ms_active(state, monitor);
    if (!source || !ms_valid_tag(destination) || destination > ms_workspace_count(state, monitor))
        return false;
    if (source == destination)
        return true;

    /* Minimized inactive windows are still WS_VISIBLE and remain in the taskbar.
     * Their existing membership must survive switches through other workspaces. */
    /* First tag ALL departing windows, before hiding/minimizing any owner. */
    for (size_t i = 0; i < count; ++i) {
        MSWindow *window = &windows[i];
        window->prepared = false;
        if (window->monitor != monitor || !window->visible || !window->eligible)
            continue;
        if (ms_valid_tag(window->tag) && window->tag != source)
            continue;
        if (!ops->set_tag(ops->context, window->id, source)) {
            ++result->errors;
            continue;
        }
        window->tag = source;
        window->prepared = true;
    }

    for (size_t i = 0; i < count; ++i) {
        MSWindow *window = &windows[i];
        if (!window->prepared)
            continue;
        if (ops->hide(ops->context, window->id))
            ++result->hidden;
        else
            ++result->errors;
    }

    for (size_t i = 0; i < state->count; ++i)
        if (state->monitors[i].id == monitor)
            state->monitors[i].active = destination;

    /* Tagged windows remain recoverable even if no longer eligible. */
    /* EnumWindows gives Z order: reveal from the bottom, owners before popups. */
    for (size_t i = count; i > 0; --i)
        if (windows[i - 1].monitor == monitor && windows[i - 1].tag == destination)
            reveal(&windows[i - 1], ops, result);
    return true;
}

bool ms_move(const MSState *state, MSHandle monitor, unsigned destination,
             MSWindow *windows, size_t count, const MSOperations *ops, MSResult *result)
{
    *result = (MSResult){0};
    unsigned active = ms_active(state, monitor);
    if (!active || !ms_valid_tag(destination) || destination > ms_workspace_count(state, monitor))
        return false;
    for (size_t i = 0; i < count; ++i)
        windows[i].prepared = false;
    for (size_t i = 0; i < count; ++i) {
        MSWindow *window = &windows[i];
        bool tagged = ms_valid_tag(window->tag);
        if (window->monitor != monitor || (!tagged && (!window->visible || !window->eligible)))
            continue;
        if ((tagged ? window->tag : active) == destination && (!tagged || destination != active))
            continue;
        /* Leave the snapshot's old tag intact until the entire group is ready,
         * so a denied write can roll back without minimizing any group member. */
        if (!ops->set_tag(ops->context, window->id, destination)) {
            ++result->errors;
            for (size_t j = 0; j < i; ++j) {
                MSWindow *prepared = &windows[j];
                if (prepared->prepared) {
                    bool undone = ms_valid_tag(prepared->tag) ?
                        ops->set_tag(ops->context, prepared->id, prepared->tag) :
                        ops->clear_tag(ops->context, prepared->id);
                    if (!undone)
                        ++result->errors;
                    prepared->prepared = false;
                }
            }
            return true; /* Valid request; platform failure is reported in result. */
        }
        window->prepared = true;
    }
    for (size_t i = 0; i < count; ++i) {
        MSWindow *window = &windows[i];
        if (!window->prepared)
            continue;
        window->tag = destination;
        if (destination != active && window->visible) {
            if (ops->hide(ops->context, window->id))
                ++result->hidden;
            else
                ++result->errors;
        }
    }
    if (destination == active)
        for (size_t i = count; i > 0; --i)
            if (windows[i - 1].prepared)
                reveal(&windows[i - 1], ops, result);
    return true;
}

void ms_restore(MSState *state, MSWindow *windows, size_t count,
                const MSOperations *ops, MSResult *result)
{
    *result = (MSResult){0};
    for (size_t i = count; i > 0; --i)
        if (ms_valid_tag(windows[i - 1].tag))
            reveal(&windows[i - 1], ops, result);
    for (size_t i = 0; i < state->count; ++i)
        state->monitors[i].active = 1;
}
