# Verification

Verified on 2026-11-22 using Linux-hosted tools. The GUI executable was inspected
without launching it on the host's Windows desktop.

## Automated checks completed

`make test` compiles `src/workspaces.c` with the native C compiler and runs fourteen
workspace simulation scenarios against the same workspace engine linked into the GUI:

1. Two monitors switch independently; a window opened in workspace 2 returns
   only to that workspace while the other monitor retains its active workspace.
2. All four workspaces retain independently opened windows and global restore
   shows every tagged window.
3. Discarding the utility's state simulates an interrupted run. Foreign-window
   properties survive; recovery restores tagged windows, including those moved
   off the original monitor or no longer eligible. Untagged hidden windows remain hidden.
4. Excluded panel/shell stand-ins remain visible. Selecting the current workspace
   is a no-op; invalid destinations and unknown monitors cannot change windows.
5. Injected tag, hide, show, and tag-removal failures leave windows visible or
   tagged for retry as appropriate. Repeated recovery is safe.
6. An owner that implicitly hides its popup cannot hide it before the popup's
   recovery tag has been written.
7. A moved window follows its current monitor; recovery precedes replacing
   monitor state, and the new monitor starts on workspace 1.
8. Invalid, duplicate, missing, and excessive monitor configurations are rejected
   without replacing existing state; 64 monitors are accepted.
9. Previous/next navigation wraps between workspaces 1 and 4, visits intermediate
   workspaces in both directions, restores their windows, and leaves the other
   monitor's state and windows independent.
10. Inactive windows that retain `WS_VISIBLE` (as minimization does) keep their
    original membership across all four workspaces. Selecting another workspace
    reveals only its tagged windows, leaves the other monitor independent, and
    recovery after process-state loss still clears every inactive membership.
11. Moving apps between active and inactive workspaces changes only their
    membership, leaves active monitor workspaces untouched, handles repeat
    selection and invalid destinations, and remains recoverable after interruption.
12. Owner/popup group moves tag every member before minimizing. Denied writes
    roll back new and existing membership; hidden/ineligible tagged windows can
    move, and failed reveals remain tagged and can be retried.
13. Monitors start with four spaces, grow independently, preserve their active
    workspace when adding, and wrap navigation through their configured counts.
    Invalid saved counts, shrinking requests, and values above 99 are rejected. Recovery
    preserves counts, and a newly discovered monitor accepts a valid saved count.
14. Apps opened in additional spaces retain membership, can move into extra
    spaces through the same engine used by the carousel, and remain independent
    of the other monitor. Tags 10 and 99 recover correctly even before counts load.

`make test` also compiles `src/navigation.c` and exercises input sequences for
both Win keys, left/right navigation, Win+Up overview commands, repeat suppression, release order, overlapping
arrow presses, and holding both Win keys. Plain arrows, other Win shortcuts,
extra Ctrl/Alt/Shift modifiers, and arrows held before Win pass through.
Shell menu masking is requested once per Win hold. These are state-machine tests;
the Win32 hook and synthetic input are not executed in this simulation.

`tests/test_slide.c` exercises the exact frame calculations used by the overlay:
left/right wraparound, direct numbered selection, monotonic progress, no gaps
between outgoing/incoming images, and exact endpoints. Widths include odd sizes,
HD/4K/8K, and `INT_MAX`; elapsed time beyond the duration is clamped. This validates
slide geometry, not Windows capture, painting, or animation smoothness.

`tests/test_overview.c` validates the same portable layout functions used by the
full-screen overview: floating-preview bounds and non-overlap, centered incomplete
rows, three-window and nine-window layouts without scrolling at 1280×720, reaching the last app
through scrolling, negative monitor coordinates, 100/150/200% scaling, tiny/empty
and invalid viewports, landscape/portrait aspect fitting, wallpaper cover fitting,
miniature projection/clipping from negative/offscreen window coordinates, and taskbar-edge strips
on all four edges. These tests exercise geometry and hit boundaries, not native
DWM thumbnails or Explorer's taskbar behavior.

`tests/test_fullscreen.c` compiles the actual `src/fullscreen_win32.c` with a small
host-side Win32 boundary in `tests/win32_stubs`. It exercises recovery journal
ordering, denied writes before frame/position changes, style/registry failures,
failed-placement recovery with retained retry data, negative monitor coordinates,
idle polling without repeated resizing, interrupted-run recovery, Explorer restart
without overwriting the original taskbar setting, existing shell properties,
minimized/maximized restoration, exclusions, and saved mode preference. Sanitizers
run the same code. The boundary supplies API results; these checks do not execute
Explorer, actual Windows styles, taskbar auto-hide, or foreign app callbacks.

Overview animation tests also sample fractional frame times for 60, 75, 120,
144, 165, and 240 Hz. They verify monotonic easing and rectangle interpolation,
exact endpoints, negative/large coordinates, invalid times, and movement between
sub-millisecond samples that used to be truncated to the same frame time.
The Win32 entrance now updates DWM positions without per-frame GDI painting.
It caches target/start/miniature geometry, primes the first frame before starting
the clock, and draws borders/captions once settled. A bounded input drain avoids
posted frame messages starving hardware input. Drag painting uses clipped dirty
regions. These are implementation changes, not measured Windows frame-rate results.

The renderer now submits whole frames with `UpdateLayeredWindow`, measures elapsed
time with `QueryPerformanceCounter`, and uses `DwmFlush` presentation pacing
instead of a 16 ms `WM_TIMER`. Workspace state changes share one DWM barrier
before restoring per-window transition settings. A shortcut during an active
slide on the same monitor reuses the displayed frame without removing the overlay;
generation checks discard stale frame messages. These changes address identified
sources of uneven pacing; the reported visual stutter is not considered verified
fixed without testing the updated executable on Windows.

AddressSanitizer and UndefinedBehaviorSanitizer passed with
`ASAN_OPTIONS=detect_leaks=0 make test-sanitize`. LeakSanitizer's process inspection
is unavailable in this sandbox, so no leak-detection result is claimed.

The packaged executable builds with MinGW-w64 GCC 13 (Win32 thread variant), MinGW-w64 headers 13,
and cross-binutils 2.45.90. Strict warnings are errors. The build uses static
compiler support, stack protection, DEP, and high-entropy ASLR. This release uses
the installed cross-compiler toolchain.

`make inspect` and GNU `objdump -p` confirmed:

- PE32+, AMD64 machine, executable image, Windows GUI subsystem.
- Expected window enumeration, monitor lookup, property, visibility, shortcut,
  keyboard-hook, input-injection, notification-icon, and icon-rendering API imports.
- DLL imports: ADVAPI32, dwmapi, GDI32, KERNEL32, msvcrt, SHELL32, and USER32.
  No MinGW runtime DLL is needed.
- Embedded version and DPI resources; `PerMonitorV2` and `asInvoker` manifest entries.
- Dynamic-base, high-entropy-ASLR, and NX-compatible flags; no certificate table.

The ZIP creation checks its CRCs and verifies that the packaged executable equals
the inspected executable. `BUILD-INFO.json` inside the ZIP records the executable
size, SHA-256, architecture, subsystem, imports, and security/DPI flags. An adjacent
`.zip.sha256` file records the ZIP checksum. Packaging also copies the inspected
executable to `dist/MonitorSpaces.exe` and writes `dist/MonitorSpaces.exe.sha256`
for standalone download.

## CI and release checks

`.github/workflows/ci.yml` runs sanitizer checks and `make package` on Ubuntu 24.04,
then verifies the EXE and ZIP checksums before saving them as an Actions artifact.
Only successful `v*` tag pushes enter the publishing job, which verifies the
downloaded checksums again and attaches the EXE, ZIP, and both checksum files to a
GitHub Release. A prominent link at the top of the release notes downloads the
standalone EXE for that release's tag; the ZIP and checksum files are linked too.
Release creation uses a draft until all four uploads succeed, supports resuming unfinished
drafts, and leaves published releases unchanged. Prerelease tags contain a hyphen.

The workflow can be checked locally with `actionlint .github/workflows/ci.yml`.
Live execution on GitHub still requires pushing the workflow; actual release
publication requires a version-tag push. These checks do not execute the GUI.

## Live Windows checklist — not yet performed

Use two physical displays and ordinary test windows with no unsaved work:

1. Start the utility; it should remain in the background with a numbered tray
   icon, and both displays must start on workspace 1. Point at the first
   display without clicking and switch to workspace 2. Its windows should minimize
   while remaining in the taskbar and Alt+Tab;
   the second display's windows and workspace number should stay unchanged.
2. Open a window on workspace 2. Switch the other display to workspace 3, then
   switch the first back to workspace 1. Confirm each display retains its windows.
   Exercise direct selection, Win+Left/Right, repeated keys, AltGr, and the numeric
   keypad. Verify left from workspace 1 selects 4 and right from 4 selects 1.
3. Exercise Ctrl+Alt+0, the Restore all button, and Restore and exit from both the
   panel and tray menu. Every tagged window should return; all active workspace
   numbers should reset to 1. Closing or minimizing the panel must instead hide it
   and preserve the current workspaces and shortcut handling.
4. Switch away from windows, end MonitorSpaces.exe using Task Manager, and relaunch
   it. Verify recovery before shortcuts become available. Repeat with `--restore`
   and with windows hidden by the previous release to verify legacy recovery.
5. Minimize windows by switching, unplug a display, change resolution/position/primary display,
   reconnect it, and test logoff/shutdown. Confirm recovery and workspace resets.
6. Test 100%, 150%, and 200% scaling, displays with negative coordinates, a window
   spanning displays, and moving the panel between DPI settings.
7. Test minimized/maximized windows, modal dialogs, tool windows, Explorer folders,
   browser apps, terminals, tray apps, full-screen apps, and apps already on another
   Windows Task View desktop. Pay particular attention to owner/popup pairs spanning
   displays and apps that change their own visibility or HWNDs.
8. Run a test app elevated and verify that denied property writes leave it visible.
   Test recovery at the same privilege level used to manage the window.
9. Reserve one shortcut with another tool before launch. The utility should name
   the unavailable shortcut, release any shortcuts already registered, and exit.
10. Launch a second copy; it should reveal the existing panel. A second copy with
    `--restore` should request recovery from the running instance.
11. Verify that Win+Left/Right suppresses shell window snapping while the utility
    runs and that releasing Win does not open Start. Test both Win keys, holding
    an arrow, releasing Win before the arrow, and releasing one Win key while the
    other remains held. Verify plain arrows, Win+E, Win+Ctrl+Left/Right, and
    Win+Shift+Left/Right retain their normal behavior. After exit, verify the
    original Win+Left/Right behavior returns. Include elevated foreground apps
    and another keyboard-remapping utility in this check.
12. Leave each monitor on a different workspace and move the pointer between
    them. Verify the tray number follows the pointer's monitor within 250 ms and
    updates immediately after a switch. Check its tooltip, icon overflow, both
    taskbar themes, and 100%/150%/200% taskbar scaling. The icon's position follows
    Windows notification-area policy, rather than creating an icon on every taskbar.
13. Test tray click, right-click, keyboard selection (Win+B), menu dismissal with
    Escape, and the Open panel / Restore all windows / Restore and exit actions.
    Launch with `--panel`; launch a second copy while the first is hidden. Restart
    Explorer and verify the icon returns. Verify icon removal and window recovery
    on explicit exit and session shutdown, and panel fallback if the tray is absent.
14. Keep apps on all four workspaces and verify their taskbar buttons and Alt+Tab
    entries remain available. Select an inactive app from each and confirm its
    monitor switches to its workspace within 250 ms, including when the pointer
    is on the other monitor. Check that the selected app keeps focus, maximized
    windows return maximized, and previously minimized windows stay minimized
    until explicitly selected. Repeat after killing/relaunching the utility, with
    grouped taskbar buttons, modal dialogs, and apps that minimize to the tray.
15. Switch in both directions, including 1↔4 wraparound and direct numbered
    shortcuts. Confirm a horizontal slide replaces the minimize/restore effect,
    the taskbar/other monitor remain stationary, and normal/maximized window
    positions stay unchanged. Test negative monitor coordinates, mixed DPI,
    empty workspaces, and protected/video content. Rapidly switch/reverse, then
    restore, exit, kill the process, or change display settings mid-slide. No
    overlay should remain, input must pass through, and recovery must still work.
    Turn Windows animations off, test RDP and Windows 10 before version 2004,
    and verify instant switching. Watch GDI handles/memory during repeated
    switching and after cancellation. Compare per-app native animations before
    and after switching and exiting; global animation settings must not change.
    Specifically compare sustained frame cadence on 60/120/144 Hz displays and
    mixed refresh-rate setups, with many apps and under load. Confirm every slide
    presents the final incoming frame before removing the overlay, repeated
    shortcuts do not flash the underlying desktop, and input/recovery stay usable.
16. Point at each monitor and press Win+Up. Verify full-monitor coverage, the four
    miniature desktops over the softened wallpaper strip, active dot/browsing
    outline, floating app previews with icons/titles below them, hover outlines,
    centered incomplete rows, empty states, and wheel scrolling.
    Verify the 400 ms eased entrance follows compositor pacing; quick clicks,
    keyboard navigation, closing/reopening, reduced-motion settings, and Remote
    Desktop must not leave stale frames or block input. Check clicking a moving
    preview opens that app and clicking wallpaper activates the browsed desktop.
    Compare entrance and dragging at 60/120/144/165/240 Hz, with several live app
    previews and at 4K/mixed DPI. Verify labels appear after zooming, stationary
    icon fallbacks remain usable, dragging leaves no trails, drop highlights clear,
    and input during the entrance responds without waiting for it to finish.
    Drag apps active→inactive, inactive→inactive, and inactive→active; verify
    membership when switching afterward, original minimized/maximized state,
    taskbar retention, and the other monitor's independence. Test owner/dialog
    groups, separate windows of the same app, elevated apps, and app termination
    or relocation while the carousel is open. F5 must refresh the snapshot.
    Check Escape, outside clicks, Win+Up repeat/release order, lost mouse capture,
    dropping outside cards, and display/recovery/exit during a drag: canceled
    drags must not move apps. Verify clicking cards browses, double-click/Enter
    switches workspace, and clicking a preview activates its app (including
    originally minimized windows and owned dialogs). Verify Win+Up
    suppresses native maximization while running, and Win+Shift+Up/Win+Ctrl+Up
    pass through. Test negative coordinates and 100/150/200% DPI; verify focus
    and GDI/icon resources return to normal after repeated opening/closing.
17. Add workspace 5 with the strip's **+** control. Confirm the
    active workspace, window assignments, and other monitor's count stay unchanged.
    Page with < / >, drag an app across pages by hovering over an arrow, cancel a
    drag on an arrow/outside the window, and move apps among spaces 1, 5, 10, and
    99. Check first/last page bounds, max-count disabled state, two-digit tray
    icons, tooltips/panel totals, and wrapping at each monitor's own last space.
    Restart and reconnect/reconfigure displays; counts must reload from the
    current user's registry values while apps recover to workspace 1. Test
    malformed/missing/out-of-range values and denied registry writes: defaults
    or a clear save-failure message should result, without losing app recovery.
    Confirm only Ctrl+Alt+0–4 is registered and no extra shortcut conflicts appear.
    Drop an app onto **+**: exactly one workspace should be added and the app
    group should move into it. Cancel outside +, test the 99-space limit, denied
    registry/property writes, and an app exiting during the drop. A failed move
    may leave the new empty space; recovery tags must still protect the app.
18. With the overview open, confirm the taskbar is covered until hovering at its
    screen edge, remains usable while the pointer is over it, and is covered
    again after moving away. Test normal and auto-hide taskbars, primary and
    secondary taskbars, each supported screen edge, negative coordinates, and
    different DPI/refresh rates. Select apps and Start from the revealed taskbar,
    restart Explorer, and close/kill the utility while the taskbar is covered or
    revealed. The overview itself must leave shell visibility alone. Full-screen
    app mode temporarily enables Windows auto-hide; disabling that mode/Restore
    and exit must restore its original preference. After killing the utility,
    run --restore to recover the saved taskbar preference as well as app layouts.
    Cancel a drag over the revealed taskbar and confirm no workspace move occurs.
    Check live/minimized/protected app previews, aspect ratios, popup groups,
    closed windows, source sizes changing, and repeated refresh/open/close for
    thumbnail/GDI/icon leaks. Opening or browsing previews must not unminimize
    source windows or change their geometry or workspace membership.
19. Check the wallpaper against the Windows desktop on each monitor: JPEG/PNG/BMP,
    different per-monitor images, solid colors, slideshow changes after reopening,
    and fill/fit/stretch/center/tile/span settings. Test unreadable/unavailable
    wallpaper and image-decoder failures; the overview must remain usable with a
    color fallback. Check miniature app positions for normal, maximized, minimized,
    partially offscreen windows and a top/left taskbar. Reopen/refresh repeatedly
    and change displays during wallpaper loading; COM objects, DIBs, DCs, icons,
    and both thumbnail relationships must be released.
20. Check app icons at 100/125/150/200/250% DPI in captions, miniature fallbacks,
    and the large icon fallback when DWM previews are unavailable. Compare apps
    exposing only a 16-pixel small window icon but larger executable resources,
    DPI-aware large window icons, legacy/monochrome icons, transparent edges,
    Store apps, elevated/inaccessible processes, and apps exiting during refresh.
    Verify filtering preserves transparency without dark halos, artwork is not
    enlarged from the small title-bar icon when a larger source is available,
    and generic/existing window icons remain available if extraction fails.
    Repeated F5/open/close and display changes during icon loading must release
    all owned large/caption/miniature HICONs, WIC/COM objects and temporary bitmaps,
    preserve other apps' shared icon handles, and keep zoom/drag rendering free
    of icon extraction or scaling work. Native icon quality needs Windows review.
21. With **Full-screen apps and auto-hide taskbar** checked in the tray menu,
    verify normal resizable apps fill their monitor without standard frame/title
    bars during normal use, across workspace switches, and after preview activation.
    New foreground windows should enter within 250 ms and background windows
    within one second. Dialogs/tool/fixed-size/already-borderless/minimized/hung
    apps and denied windows must not be resized. Test custom chrome, maximized
    apps, top/left taskbars, multiple monitors, mixed DPI and native app F11 modes.
    Hover at each taskbar's edge: it must reveal above the app, accept app/Start/
    tray clicks, and hide again when leaving. Never hide the actual taskbar HWND.
    Auto-hide's work-area change must not disable the mode/reset workspaces a
    second later; actual display removal/reconfiguration must still recover.
    Turn the mode off while some apps are in inactive spaces: styles/normal
    placements must restore without revealing those apps or losing membership.
    Test original normal/maximized states, user-minimized windows, focus on another
    monitor, repeated toggles, registry/property/placement denials, and Explorer
    restart. Check the original auto-hide value when it starts both on and off.
    Kill the utility after entering full screen, then run --restore: frames,
    placements and taskbar preference must recover from HWND/registry records.
    A normal restart should recover first, then apply the saved mode preference.
    Restore/Ctrl+Alt+0 must leave the mode off for that run; the tray toggle can
    re-enable it. Failed restoration must keep journals and report recovery errors.

These simulations do not validate Windows message delivery, actual property
lifetime after process termination, application responsiveness, focus behavior,
or driver/DPI behavior. Tray drawing, visibility, mouse/keyboard callbacks, Explorer
restart recovery, and background panel behavior require live Windows validation.
The new minimize/restore calls, original-state properties, taskbar retention, and
foreground polling also require live Windows validation; engine simulations do
not execute these Win32 calls.
The slide overlay, capture exclusion, per-window DWM transition suppression,
reduced-motion fallback, resource cleanup, and interruption behavior have been
reviewed in source and cross-compiled but still require live Windows validation.
The Win+Up carousel's native painting, focus, drag capture, icons, and interaction
also require live Windows validation; automated tests cover input and membership.
The add-space control, paging/drag hover timer, registry persistence, monitor ID
mapping, and multi-digit tray drawing also require live Windows validation.
The full-screen overview's DWM thumbnail rendering, explicit app activation,
region-based taskbar reveal, auto-hide interaction, and persistent drawing buffer
also require live Windows validation. Automated overview checks cover geometry.
The Mission Control-style painting, wallpaper COM/WIC loading and positioning,
dual DWM previews, entrance pacing, hover/click interactions, and drag-to-create
also require live Windows validation; successful cross-compilation and portable
geometry/sanitizer tests do not establish their visual appearance or smoothness.
Full-screen app geometry/style changes, the NonRudeHWND shell hint, real auto-hide
edge reveal, taskbar preference restoration, and Windows placement recovery still
require live Windows validation. Host boundary tests validate recovery logic only.
The packaged executable is unsigned.
