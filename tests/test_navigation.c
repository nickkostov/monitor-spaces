#include "navigation.h"

#include <assert.h>
#include <stdio.h>

static void plain_and_modified_arrows(void)
{
    MSNavigation state = {0};
    assert(!ms_navigation_key(&state, MS_KEY_LEFT, true).swallow);
    assert(!ms_navigation_key(&state, MS_KEY_LEFT, false).swallow);
    assert(!ms_navigation_key(&state, MS_KEY_LWIN, true).swallow);
    const unsigned modifiers[] = {
        MS_KEY_CONTROL, MS_KEY_LCONTROL, MS_KEY_RCONTROL,
        MS_KEY_SHIFT, MS_KEY_LSHIFT, MS_KEY_RSHIFT,
        MS_KEY_ALT, MS_KEY_LALT, MS_KEY_RALT
    };
    for (unsigned i = 0; i < sizeof(modifiers) / sizeof(modifiers[0]); ++i) {
        assert(!ms_navigation_key(&state, modifiers[i], true).swallow);
        MSNavigationAction action = ms_navigation_key(&state, MS_KEY_RIGHT, true);
        assert(!action.swallow && !action.direction && !action.mask_menu);
        assert(!ms_navigation_key(&state, MS_KEY_RIGHT, false).swallow);
        assert(!ms_navigation_key(&state, modifiers[i], false).swallow);
    }
    /* Other Win shortcuts (such as Win+E) must still pass through. */
    assert(!ms_navigation_key(&state, 'E', true).swallow);
    assert(!ms_navigation_key(&state, 'E', false).swallow);
    assert(!ms_navigation_key(&state, MS_KEY_LWIN, false).swallow);
    assert(!ms_navigation_key(&state, MS_KEY_COUNT, true).swallow);
    puts("PASS plain arrows, extra modifiers, and other Win shortcuts pass through");
}

static void navigation_and_repeat(void)
{
    const unsigned win_keys[] = {MS_KEY_LWIN, MS_KEY_RWIN};
    for (unsigned i = 0; i < 2; ++i) {
        MSNavigation state = {0};
        ms_navigation_key(&state, win_keys[i], true);
        MSNavigationAction action = ms_navigation_key(&state, MS_KEY_LEFT, true);
        assert(action.swallow && action.direction == -1 && action.mask_menu);
        for (unsigned repeat = 0; repeat < 10; ++repeat) {
            action = ms_navigation_key(&state, MS_KEY_LEFT, true);
            assert(action.swallow && !action.direction && !action.mask_menu);
        }
        action = ms_navigation_key(&state, MS_KEY_LEFT, false);
        assert(action.swallow && !action.direction);
        action = ms_navigation_key(&state, MS_KEY_RIGHT, true);
        assert(action.swallow && action.direction == 1 && !action.mask_menu);
        assert(ms_navigation_key(&state, MS_KEY_RIGHT, false).swallow);
        ms_navigation_key(&state, win_keys[i], false);
        ms_navigation_key(&state, win_keys[i], true);
        action = ms_navigation_key(&state, MS_KEY_RIGHT, true);
        assert(action.swallow && action.direction == 1 && action.mask_menu);
    }
    puts("PASS both Win keys, one step per press, and menu masking once per Win hold");
}

static void release_order_and_overlapping_keys(void)
{
    MSNavigation state = {0};
    ms_navigation_key(&state, MS_KEY_LWIN, true);
    assert(ms_navigation_key(&state, MS_KEY_LEFT, true).direction == -1);
    assert(ms_navigation_key(&state, MS_KEY_RIGHT, true).direction == 1);
    ms_navigation_key(&state, MS_KEY_LWIN, false);
    MSNavigationAction action = ms_navigation_key(&state, MS_KEY_LEFT, true);
    assert(action.swallow && !action.direction);
    assert(ms_navigation_key(&state, MS_KEY_LEFT, false).swallow);
    assert(ms_navigation_key(&state, MS_KEY_RIGHT, false).swallow);
    assert(!ms_navigation_key(&state, MS_KEY_LEFT, true).swallow);
    ms_navigation_key(&state, MS_KEY_LEFT, false);

    /* A second Win key keeps the shell mask in effect after the first goes up. */
    ms_navigation_key(&state, MS_KEY_LWIN, true);
    assert(ms_navigation_key(&state, MS_KEY_LEFT, true).mask_menu);
    ms_navigation_key(&state, MS_KEY_LEFT, false);
    ms_navigation_key(&state, MS_KEY_RWIN, true);
    ms_navigation_key(&state, MS_KEY_LWIN, false);
    action = ms_navigation_key(&state, MS_KEY_RIGHT, true);
    assert(action.direction == 1 && !action.mask_menu);
    puts("PASS release Win before arrow, overlapping arrows, and both Win keys held");
}

static void arrow_pressed_before_win(void)
{
    MSNavigation state = {0};
    assert(!ms_navigation_key(&state, MS_KEY_LEFT, true).swallow);
    ms_navigation_key(&state, MS_KEY_LWIN, true);
    assert(!ms_navigation_key(&state, MS_KEY_LEFT, true).swallow);
    assert(!ms_navigation_key(&state, MS_KEY_LEFT, false).swallow);
    assert(ms_navigation_key(&state, MS_KEY_LEFT, true).direction == -1);
    puts("PASS an arrow held before Win does not become a new shortcut on repeat");
}

static void modifiers_held_at_startup(void)
{
    const unsigned modifiers[] = {MS_KEY_LCONTROL, MS_KEY_RCONTROL,
        MS_KEY_LSHIFT, MS_KEY_RSHIFT, MS_KEY_LALT, MS_KEY_RALT};
    for (unsigned i = 0; i < sizeof(modifiers) / sizeof(modifiers[0]); ++i) {
        MSNavigation state = {0};
        /* The adapter seeds currently held, side-specific modifier keys. */
        state.down[modifiers[i]] = true;
        state.down[MS_KEY_LWIN] = true;
        assert(!ms_navigation_key(&state, MS_KEY_LEFT, true).swallow);
        ms_navigation_key(&state, MS_KEY_LEFT, false);
        ms_navigation_key(&state, modifiers[i], false);
        MSNavigationAction action = ms_navigation_key(&state, MS_KEY_LEFT, true);
        assert(action.swallow && action.direction == -1);
    }
    puts("PASS modifiers held at startup stop blocking navigation when released");
}

static void overview_shortcut(void)
{
    const unsigned win_keys[] = {MS_KEY_LWIN, MS_KEY_RWIN};
    for (unsigned i = 0; i < 2; ++i) {
        MSNavigation state = {0};
        assert(!ms_navigation_key(&state, MS_KEY_UP, true).swallow);
        ms_navigation_key(&state, win_keys[i], true);
        assert(!ms_navigation_key(&state, MS_KEY_UP, true).overview); /* Held before Win. */
        ms_navigation_key(&state, MS_KEY_UP, false);
        MSNavigationAction action = ms_navigation_key(&state, MS_KEY_UP, true);
        assert(action.overview && action.swallow && action.mask_menu && !action.direction);
        action = ms_navigation_key(&state, MS_KEY_UP, true);
        assert(action.swallow && !action.overview && !action.mask_menu);
        assert(ms_navigation_key(&state, MS_KEY_RIGHT, true).direction == 1);
        ms_navigation_key(&state, win_keys[i], false);
        action = ms_navigation_key(&state, MS_KEY_UP, false);
        assert(action.swallow && !action.overview);
        assert(!ms_navigation_key(&state, MS_KEY_UP, true).swallow);
    }
    const unsigned modifiers[] = {MS_KEY_CONTROL, MS_KEY_ALT, MS_KEY_SHIFT,
        MS_KEY_LCONTROL, MS_KEY_RCONTROL, MS_KEY_LSHIFT, MS_KEY_RSHIFT, MS_KEY_LALT, MS_KEY_RALT};
    for (unsigned i = 0; i < sizeof(modifiers) / sizeof(modifiers[0]); ++i) {
        MSNavigation state = {0};
        ms_navigation_key(&state, MS_KEY_LWIN, true);
        ms_navigation_key(&state, modifiers[i], true);
        assert(!ms_navigation_key(&state, MS_KEY_UP, true).swallow);
    }
    puts("PASS Win+Up overview, repeats, release order, overlapping navigation, and extra modifiers");
}

int main(void)
{
    plain_and_modified_arrows();
    navigation_and_repeat();
    release_order_and_overlapping_keys();
    arrow_pressed_before_win();
    modifiers_held_at_startup();
    overview_shortcut();
    puts("All keyboard simulations passed.");
    return 0;
}
