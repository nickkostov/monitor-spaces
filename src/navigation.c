#include "navigation.h"

MSNavigationAction ms_navigation_key(MSNavigation *state, unsigned key, bool down)
{
    MSNavigationAction action = {0};
    if (key >= MS_KEY_COUNT)
        return action;
    bool repeated = state->down[key];
    state->down[key] = down;
    bool win = state->down[MS_KEY_LWIN] || state->down[MS_KEY_RWIN];
    if (!win)
        state->menu_masked = false;
    if (key != MS_KEY_LEFT && key != MS_KEY_RIGHT && key != MS_KEY_UP)
        return action;

    bool *captured = key == MS_KEY_LEFT ? &state->captured_left :
                     key == MS_KEY_RIGHT ? &state->captured_right : &state->captured_up;
    if (*captured) {
        /* Swallow repeats and the matching release, even if Win went up first. */
        *captured = down;
        action.swallow = true;
        return action;
    }
    bool extra_modifier = state->down[MS_KEY_SHIFT] || state->down[MS_KEY_CONTROL] ||
        state->down[MS_KEY_ALT] || state->down[MS_KEY_LSHIFT] || state->down[MS_KEY_RSHIFT] ||
        state->down[MS_KEY_LCONTROL] || state->down[MS_KEY_RCONTROL] ||
        state->down[MS_KEY_LALT] || state->down[MS_KEY_RALT];
    if (down && !repeated && win && !extra_modifier) {
        *captured = true;
        action.swallow = true;
        action.overview = key == MS_KEY_UP;
        action.direction = key == MS_KEY_UP ? 0 : key == MS_KEY_LEFT ? -1 : 1;
        action.mask_menu = !state->menu_masked;
        state->menu_masked = true;
    }
    return action;
}
