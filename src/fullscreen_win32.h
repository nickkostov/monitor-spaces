#ifndef MONITOR_SPACES_FULLSCREEN_WIN32_H
#define MONITOR_SPACES_FULLSCREEN_WIN32_H

#include <windows.h>
#include <stdbool.h>
#include <stddef.h>

void ms_fullscreen_configure(bool (*eligible)(HWND));
bool ms_fullscreen_enabled(void);
bool ms_fullscreen_preferred(void);
bool ms_fullscreen_enable(void);
void ms_fullscreen_suspend(void);
void ms_fullscreen_save_preference(bool enabled);
void ms_fullscreen_apply(HWND window);
/* Exempt a moved app from automatic full-screen and undo this mode's resizing.
 * Windows not resized by this mode keep their current geometry and frame. */
bool ms_fullscreen_preserve_size(HWND window);
void ms_fullscreen_update(void);
unsigned ms_fullscreen_original_state(HWND window);
/* Disables the mode and restores window journals/taskbar state, even after a crash. */
size_t ms_fullscreen_restore(void);

#endif
