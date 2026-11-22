#ifndef MONITOR_SPACES_NAVIGATION_H
#define MONITOR_SPACES_NAVIGATION_H

#include <stdbool.h>

/* Windows virtual-key values, kept platform-independent for input simulation. */
enum {
    MS_KEY_SHIFT = 0x10, MS_KEY_CONTROL = 0x11, MS_KEY_ALT = 0x12,
    MS_KEY_LEFT = 0x25, MS_KEY_UP = 0x26, MS_KEY_RIGHT = 0x27,
    MS_KEY_LWIN = 0x5b, MS_KEY_RWIN = 0x5c,
    MS_KEY_LSHIFT = 0xa0, MS_KEY_RSHIFT = 0xa1,
    MS_KEY_LCONTROL = 0xa2, MS_KEY_RCONTROL = 0xa3,
    MS_KEY_LALT = 0xa4, MS_KEY_RALT = 0xa5,
    MS_KEY_COUNT = 256
};

typedef struct {
    bool down[MS_KEY_COUNT];
    bool captured_left;
    bool captured_right;
    bool captured_up;
    bool menu_masked;
} MSNavigation;

typedef struct {
    bool swallow;
    bool mask_menu;
    int direction;
    bool overview;
} MSNavigationAction;

MSNavigationAction ms_navigation_key(MSNavigation *state, unsigned key, bool down);

#endif
