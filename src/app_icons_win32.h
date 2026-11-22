#ifndef MONITOR_SPACES_APP_ICONS_WIN32_H
#define MONITOR_SPACES_APP_ICONS_WIN32_H

#include <windows.h>
#include <stdbool.h>

typedef struct {
    void *factory;
    bool apartment;
} MSIconLoader;

typedef struct { HICON large, caption, miniature; } MSAppIcons;

/* Loader lives on the UI thread for one snapshot. Every returned icon is owned. */
void ms_icon_loader_open(MSIconLoader *loader);
void ms_icon_loader_close(MSIconLoader *loader);
MSAppIcons ms_app_icons_load(MSIconLoader *loader, HWND window, DWORD process,
                            unsigned dpi, ULONGLONG deadline);

#endif
