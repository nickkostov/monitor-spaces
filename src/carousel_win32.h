#ifndef MONITOR_SPACES_CAROUSEL_WIN32_H
#define MONITOR_SPACES_CAROUSEL_WIN32_H

#include <windows.h>
#include "workspaces.h"

typedef struct {
    MSHandle window;
    DWORD process;
    unsigned workspace;
    wchar_t title[256];
    HICON icon; /* Large preview icon; owned by the carousel, not the snapshot provider. */
} MSCarouselItem;

typedef struct {
    bool (*load)(MSHandle monitor, MSCarouselItem **items, size_t *count, unsigned *active, unsigned *spaces);
    bool (*move)(MSHandle monitor, MSHandle window, DWORD process, unsigned destination,
                 wchar_t *message, size_t capacity);
    void (*activate)(MSHandle monitor, unsigned workspace);
    unsigned (*add)(MSHandle monitor, wchar_t *message, size_t capacity);
    void (*focus)(MSHandle monitor, MSHandle window, DWORD process);
    bool (*remove)(MSHandle monitor, unsigned workspace, wchar_t *message, size_t capacity);
} MSCarouselActions;

bool ms_carousel_open(HINSTANCE instance, MSHandle monitor, const MSCarouselActions *actions);
void ms_carousel_close(void);
bool ms_carousel_visible(void);

#endif
