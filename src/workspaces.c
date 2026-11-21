#include "workspaces.h"

bool ms_valid_tag(unsigned tag)
{
    return tag >= 1 && tag <= MS_WORKSPACE_COUNT;
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
        state->monitors[i] = (MSMonitor){ids[i], 1};
    return true;
}

unsigned ms_active(const MSState *state, MSHandle monitor)
{
    for (size_t i = 0; i < state->count; ++i)
        if (state->monitors[i].id == monitor)
            return state->monitors[i].active;
    return 0;
}

unsigned ms_adjacent(const MSState *state, MSHandle monitor, int direction)
{
    unsigned active = ms_active(state, monitor);
    if (!ms_valid_tag(active) || (direction != -1 && direction != 1))
        return 0;
    if (direction < 0)
        return active == 1 ? MS_WORKSPACE_COUNT : active - 1;
    return active == MS_WORKSPACE_COUNT ? 1 : active + 1;
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
    if (!source || !ms_valid_tag(destination))
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
