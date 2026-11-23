#ifndef MONITOR_SPACES_WORKSPACES_H
#define MONITOR_SPACES_WORKSPACES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MS_DEFAULT_WORKSPACES 4u
#define MS_MIN_WORKSPACES 1u
#define MS_MAX_WORKSPACES 99u
#define MS_MAX_MONITORS 64u

typedef uintptr_t MSHandle;

typedef struct {
    MSHandle id;
    unsigned active;
    unsigned workspaces;
} MSMonitor;

typedef struct {
    MSMonitor monitors[MS_MAX_MONITORS];
    size_t count;
} MSState;

/* A complete snapshot must be collected before changing any window. Hiding an
 * owner can also hide its popups, which still need their own recovery tags. */
typedef struct {
    MSHandle id;
    MSHandle monitor;
    unsigned tag;
    bool visible;
    bool eligible;
    bool prepared;
} MSWindow;

typedef struct {
    void *context;
    bool (*set_tag)(void *, MSHandle, unsigned);
    bool (*clear_tag)(void *, MSHandle);
    /* Remove from the desktop; minimizing may leave MSWindow.visible true. */
    bool (*hide)(void *, MSHandle);
    bool (*show)(void *, MSHandle);
} MSOperations;

typedef struct {
    size_t hidden;
    size_t shown;
    size_t errors;
} MSResult;

bool ms_valid_tag(unsigned tag);
bool ms_set_monitors(MSState *state, const MSHandle *ids, size_t count);
unsigned ms_active(const MSState *state, MSHandle monitor);
unsigned ms_workspace_count(const MSState *state, MSHandle monitor);
bool ms_set_workspace_count(MSState *state, MSHandle monitor, unsigned count);
unsigned ms_add_workspace(MSState *state, MSHandle monitor);
unsigned ms_adjacent(const MSState *state, MSHandle monitor, int direction);
bool ms_switch(MSState *state, MSHandle monitor, unsigned destination,
               MSWindow *windows, size_t count, const MSOperations *ops,
               MSResult *result);
void ms_restore(MSState *state, MSWindow *windows, size_t count,
                const MSOperations *ops, MSResult *result);
/* Move a supplied window group without switching the monitor's active workspace.
 * All members are tagged before any owner can implicitly minimize its popups. */
bool ms_move(const MSState *state, MSHandle monitor, unsigned destination,
             MSWindow *windows, size_t count, const MSOperations *ops, MSResult *result);
/* Merge into the previous workspace (the next for workspace 1), then renumber.
 * Tag writes are prepared before committing the count; failed writes roll back.
 * A false return leaves the workspace open; recovery can retry any failed undo. */
bool ms_remove_workspace(MSState *state, MSHandle monitor, unsigned workspace,
                         MSWindow *windows, size_t count, const MSOperations *ops, MSResult *result);

#endif
