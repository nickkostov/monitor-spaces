#ifndef MONITOR_SPACES_WALLPAPER_WIN32_H
#define MONITOR_SPACES_WALLPAPER_WIN32_H

#include <windows.h>

/* Owned, monitor-sized 32-bit DIB; caller deletes it with DeleteObject. */
HBITMAP ms_wallpaper_load(HMONITOR monitor);

#endif
