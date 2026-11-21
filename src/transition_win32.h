#ifndef MONITOR_SPACES_TRANSITION_WIN32_H
#define MONITOR_SPACES_TRANSITION_WIN32_H

#include <windows.h>

/* Cosmetic only: failure must never prevent a workspace switch or recovery. */
/* Always pair begin with commit/cancel, including when animation is disabled:
 * window animation settings are batched independently of the visual overlay. */
void ms_transition_begin(HMONITOR monitor, int direction);
void ms_transition_commit(void);
void ms_transition_cancel(void);
void ms_show_without_animation(HWND window, int command);

#endif
