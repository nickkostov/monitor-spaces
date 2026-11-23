# Monitor Spaces

A small C/Win32 utility giving each monitor four workspaces by default, expandable
to 99. Point at a display and use a shortcut to switch just that display. Every
monitor starts on workspace 1; added workspace counts are remembered per monitor.

| Control | Action |
| --- | --- |
| Win + Left | Previous workspace on the monitor under the pointer; workspace 1 wraps to its last workspace |
| Win + Right | Next workspace on the monitor under the pointer; its last workspace wraps to 1 |
| Win + Up | Open/close a full-screen app overview on the monitor under the pointer; drag previews to workspaces |
| Ctrl + Alt + 1–4 | Switch the monitor under the pointer to workspace 1–4 |
| Ctrl + Alt + 0 | Restore managed windows, original app layouts and taskbar setting; reset all monitors to workspace 1 |
| Restore all button | Same as Ctrl + Alt + 0 |
| Numbered tray icon | Show the workspace on the monitor under the pointer; click to open the panel |
| Right-click tray icon | Open the panel, restore windows, or restore and exit |
| Full-screen apps and auto-hide taskbar (tray menu) | Toggle borderless full-screen app windows and Windows taskbar auto-hide; choice is remembered |
| Close / minimize panel | Hide the panel and keep running in the background |
| Restore and exit | Restore managed windows, remove the tray icon, unregister shortcuts, exit |

All open apps keep their normal Windows taskbar buttons and Alt+Tab entries in
every workspace. Inactive windows are minimized. Select an app in the taskbar or
Alt+Tab to switch its monitor to that app's workspace (within 250 ms), regardless
of where the pointer is. Windows you had already minimized stay minimized when
switching back, unless you select them yourself.

Workspace changes use a 400 ms horizontal slide across the selected monitor
(its work area when full-screen mode is off). Left/right shortcuts keep their
direction when wrapping between 1 and 4; direct numbered shortcuts slide toward
the selected number. Another shortcut interrupts the current slide. The utility
suppresses native minimize/restore transitions while changing window states.
Frames use a high-resolution clock, complete layered-window updates, and DWM
presentation pacing. Repeated shortcuts continue from the currently displayed
image; window-state changes share a composition barrier across the workspace.

Use the number row above the letters. The numeric keypad is not registered.
The app starts in the background. A small workspace number in the taskbar's notification
area shows the active workspace on the monitor under the pointer. It updates
immediately after switching and checks pointer movement every 250 ms. Hover over
the icon for the display number/device and workspace. Windows chooses the location
of the notification area; the icon may initially be inside the tray's overflow
menu. Drag it into the visible notification area to keep the number in view.

Click the icon to open the optional panel. Its display list shows every active
workspace and marks the display under the pointer with `>`. Closing or minimizing
the panel hides it while shortcuts and the indicator keep running. Use **Restore
and exit** in the panel or tray menu to quit and recover all managed windows.
Only one instance runs per Windows session. Running another copy opens its panel.
The app recreates its icon after Explorer restarts and retries failed icon updates.
If the icon cannot be created at startup, the panel opens as a fallback.

Win + Left/Right/Up takes over those combinations while the utility runs. Press an
arrow again for each step; holding it does not cycle repeatedly. Both Win keys
work. Combinations with additional Ctrl, Alt, or Shift modifiers pass through,
including Win + Ctrl + Left/Right and Win + Shift + Left/Right.

## Full-screen apps and taskbar auto-hide

By default, regular resizable main windows fill their monitor without the standard
title bar or resize frame. This works during normal app use as well as workspace
switching. New foreground windows are checked every 250 ms; other windows are
checked every second. Dialogs, tool windows, fixed-size windows, already-borderless
apps, minimized windows, and windows that cannot be managed keep their normal
behavior. App controls drawn inside the client area can remain visible.

Dragging an app to a workspace in **Win + Up** preserves its window size and frame.
If this mode had made the app borderless full-screen, the drop restores its saved
windowed layout on its current monitor, fitting inside the work area only if
necessary. That app stays exempt from automatic full-screen when switching
workspaces, opening its preview, or selecting it through the taskbar. Windows
already at a user-chosen size keep that layout. Turning full-screen mode off and
on again resets these exceptions; recovery/restarting also clears them.

The mode enables Windows' taskbar auto-hide. Move to the taskbar's screen edge
to reveal it; moving away lets Windows hide it again. The setting applies to all
Windows taskbars. App windows remain below the taskbar when it reveals, and the
utility never calls Hide on Explorer's taskbar windows.

Right-click the numbered tray icon and uncheck **Full-screen apps and auto-hide
taskbar** to restore the original window frames/positions and taskbar preference
without changing workspace assignments. Recheck it to enable the mode again.
This choice is saved as `FullscreenEnabled` under `HKCU\Software\MonitorSpaces`.
**Ctrl + Alt + 0**, **Restore and exit**, shutdown, and monitor configuration
changes also restore layouts and the prior taskbar preference. Recovery leaves
full-screen mode off for that run; the saved preference applies on the next launch.
Changing taskbar work area alone does not reset workspaces or disable the mode.

Original styles and placements are journaled as numeric properties on each window
before changes. The original auto-hide value is saved in the current user's
registry before enabling it. These records survive an interrupted utility run;
the next launch recovers them before reapplying your preference. Run `--restore`
to recover and exit without re-enabling the mode. Failed recovery records are
retained for retry. Borderless mode and taskbar reveal require live Windows checks.

## Run

Extract `dist/monitor-spaces-windows-x64.zip`, then run `MonitorSpaces.exe`
inside the extracted folder. No installation is needed. The ZIP also contains
all source, resources, tests, build scripts, these instructions, and
`BUILD-INFO.json` with executable checksums and PE inspection results.

To launch with the control panel visible:

```powershell
.\MonitorSpaces.exe --panel
```

Target: Windows 10 version 1607 or later, or Windows 11, x64. The executable is
unsigned. Live Windows behavior has not been verified; see [VERIFICATION.md](VERIFICATION.md).

If a required shortcut cannot be registered, the utility reports which shortcut
failed and exits before switching any windows. Startup recovery still runs.
Win + Left/Right/Up uses a dedicated keyboard-hook thread because Windows reserves
Win-key shortcuts. If the hook cannot be installed, startup reports the error and
exits. **Restore and exit** removes the hook and restores normal key handling.

## Organize apps with Win + Up

Point at a monitor and press **Win + Up**. A Mission Control-style overview
shows floating app previews over that monitor's wallpaper, with four miniature
desktops in a softened strip across the top. Windows keep their aspect ratios,
with app icons and titles underneath, hover outlines, and centered partial rows.
The thumbnails zoom into place over 400 ms with smooth easing and compositor
pacing. Only the DWM previews move during the zoom; borders and labels appear
when they settle, avoiding a full-screen software repaint on every frame.
Geometry is cached and timing preserves fractional milliseconds for high-refresh
displays. Reduced-motion settings and Remote Desktop skip the entrance animation;
clicking or typing finishes it immediately. Dragging repaints the old/new preview
areas and changed drop targets, rather than the entire screen for each mouse move.
App icons use the window's large icon or a larger embedded executable icon when
available. Separate 48/20/16-logical-pixel preview, caption, and miniature variants
are prepared at the monitor's DPI with alpha-preserving, filtered resizing.
Icon loading happens during opening/refresh, outside animation and drag rendering.
Apps that only supply low-resolution artwork can still look softer at high DPI.
The wallpaper is read from Windows, including per-monitor images, solid colors,
and the normal fill/fit/stretch/center/tile/span positioning. JPEG, PNG, and BMP
decoding uses Windows' built-in imaging components; no image assets or codecs
are bundled. The wallpaper and softened backdrop are cached for each opening.
Unavailable wallpaper falls back to the desktop color or a muted background.
The actual active workspace is marked separately from the workspace being browsed.
The strip labels these workspaces **Desktop 1**, **Desktop 2**, and so on; a dot
marks the active one and a blue outline marks the one being browsed.
Click a desktop preview to browse its apps without switching your desktop. Scroll
to reach more previews, click a preview to open that app, or double-click a
desktop / click the wallpaper / press **Enter** to switch to the desktop being browsed. Plain
Left/Right arrows browse workspaces inside the overview.
Click the **+** at the right of the strip to create another space on that monitor,
up to 99. Drag a window onto **+** to create a space and move the window there in
one gesture; if the move fails, the new empty space remains available.
Adding a space preserves the active workspace and all app assignments. Use
the strip's **‹ / ›** arrows to browse additional desktops; while dragging an app, hover over an arrow
for half a second to page without releasing it. Win+Left/Right cycles through
every configured space, while Ctrl+Alt+1–4 retains the original direct shortcuts.
Drag an app preview onto a miniature desktop to move that app. Dropping onto an inactive
workspace minimizes it there; dropping onto the active workspace restores it
according to its saved state. Moving an app does not switch your current
workspace or change the other monitor's workspace. Owned dialogs on the same
monitor move with their app; separate main windows can be moved independently.
Successful drops also preserve the app group's windowed sizes instead of forcing
full-screen. This includes dropping onto **+** to add a desktop and move there.

Press **F5** to refresh opened/closed windows. **Esc**, the bottom-right **×**,
switching focus to another app, or **Win + Up** again closes the overview.
Dropping outside the workspace cards, losing mouse capture, or closing mid-drag
cancels the move. Restore, exit, and display changes also close the overview. The utility rechecks the app
at drop time, and denied property writes are rolled back before minimizing the
group. Failed operations retain recovery tags for **Ctrl + Alt + 0**.

The overview covers the selected monitor's taskbar. Move the pointer to the
taskbar's screen edge to reveal it; move back into the overview to cover it
again. This works by temporarily cutting a taskbar-sized strip out of the
overview window. The overview itself leaves shell visibility/settings alone;
the full-screen app mode described above manages the temporary auto-hide setting.
Closing or terminating the utility removes the covering window. Other monitors
keep their own taskbars. Opening a taskbar app or Start can dismiss
the overview as focus changes.

App previews use DWM thumbnails and preserve the source window's aspect ratio.
Miniature desktops show the wallpaper and smaller app previews in their original
positions, with a window outline/icon fallback when a miniature is unavailable.
Opening/browsing the overview does not maximize, restore, or move application
windows. Minimized apps can show their last available frame; unsupported or
protected previews may show an app icon or blank content instead. The overview
organizes Monitor Spaces workspaces independently of Windows' own Win+Tab desktops.

Workspace counts are saved for the current Windows user under
`HKEY_CURRENT_USER\Software\MonitorSpaces\WorkspaceCounts`, keyed by the monitor
interface ID when available (otherwise its display name). Counts reload on launch
and display reconfiguration. New monitors start with four; invalid saved values
are ignored. If a count cannot be saved, the new space still works for that run
and the carousel reports the save failure. Restoring all windows resets the active
workspace to 1 and preserves the configured counts. Window-to-workspace membership
is not persisted across a restart; startup recovery brings all managed windows back.

## Window behavior

- A switch uses `GetCursorPos` and `MonitorFromPoint`, so selecting a monitor does
  not require clicking a window. Spanning windows belong to the monitor with
  the largest intersection, as reported by `MonitorFromWindow`.
- Visible, eligible top-level windows on the departing workspace get numeric
  recovery and original-state properties before any are minimized. Newly opened
  windows join the active workspace when you leave it. Inactive windows retain
  their membership even though minimized windows are still visible to Windows.
- The panel, desktop, taskbars, known shell surfaces, child windows, no-activate
  surfaces, and DWM-cloaked windows are excluded. Owned dialogs and ordinary tool
  windows participate. Explorer folder windows participate too.
- Complete snapshots and tagging before minimizing protect owned dialogs from an
  owner's implicit hide. Reveals run in reverse enumeration order to bring owners
  back before their popups.
- `ShowWindow(SW_SHOWMINNOACTIVE)` minimizes inactive windows without requesting
  activation. Returning restores their normal/maximized state; windows that were
  already minimized stay minimized. Maximized restores can activate a window;
  the utility requests focus back for a previously focused, unminimized window.
  Windows and apps still control their own activation and visibility behavior.
- Windows hidden by their applications without a Monitor Spaces tag stay hidden.
  Windows that close while inactive are simply gone. There is no session persistence
  across logoff or reboot.

These workspaces use window minimization. Applications keep running while inactive;
Windows Task View desktops and application-specific workspace features are separate.
A visible window dragged to another display joins that display's active workspace
on its next switch. An inactive window's current monitor determines where its numbered
workspace is revealed.

The slide uses temporary before/after screen snapshots in a click-through,
non-activating overlay, without moving application windows. Snapshots stay in
memory and are freed when the slide finishes or is canceled. Recovery, exit,
and display changes remove the overlay. Animation requires Windows 10 version
2004 or later and respects Windows' client-area animation setting. Remote sessions,
capture/allocation failures, and work areas over 16 million pixels use an instant
switch. Screenshots may omit protected content, and the overlay is excluded from
screen capture. Per-window DWM transition settings are restored after the batch
of state changes (to the default when the existing value cannot be queried); global Windows
animation preferences are never changed. Apps selected through the taskbar may
start their own restore animation before foreground polling detects the selection.

## Recovery

The stable, namespaced `MonitorSpaces.HiddenWorkspace.v1` window property stores
an integer from 1 to 99 on the foreign window itself. It contains no pointer into
the utility's memory, so surviving windows can be found after an unexpected exit.
The tag is written before minimizing and removed only after restoration is confirmed.
`MonitorSpaces.OriginalState.v1` stores the original normal/minimized/maximized
state as another integer property. Failed operations retain their tags for recovery.
Startup also restores windows hidden by older versions that lack the state property.
Recovery accepts all supported tags even before saved counts are loaded. Before
using an older executable that only recognizes spaces 1–4, restore all windows
with this version so that extra-space windows are no longer tagged.

Recovery scans all tagged top-level windows, including ones no longer eligible
or attached to the original monitor. It runs at startup, on Ctrl + Alt + 0, on
**Restore and exit**, and during session shutdown. Display changes also trigger
recovery and reset all workspace numbers to 1. A one-second topology check covers
missed notifications and monitor repositioning. Taskbar work-area changes are
updated without resetting workspace membership.

After an interrupted run, launch the utility again, or run:

```powershell
.\MonitorSpaces.exe --restore
```

With no instance running, `--restore` restores tagged windows and exits. If an
instance is running, it posts a recovery request to that instance; its exit code
indicates whether the request was posted, not whether recovery completed. See the
running panel for results. Standalone recovery returns 0 on success and 1 on error.

If recovery fails, switching pauses after startup or a display change until you
retry recovery successfully. A final failed recovery reports a warning on exit.
Run recovery at the same privilege level as the run that managed the windows.

## Build on Linux / WSL

Install GCC/Make, MinGW-w64, and Python 3.9 or later. On Debian/Ubuntu:

```sh
sudo apt-get install build-essential gcc-mingw-w64-x86-64 binutils-mingw-w64-x86-64 python3
make test
make windows
make inspect
make package
```

Outputs:

```text
build/MonitorSpaces.exe
dist/MonitorSpaces.exe
dist/MonitorSpaces.exe.sha256
dist/monitor-spaces-windows-x64.zip
dist/monitor-spaces-windows-x64.zip.sha256
```

`make package` runs the native simulation and executable inspection before
creating the ZIP, then checks ZIP integrity and the packaged executable's bytes.
No third-party Python modules are required. The package uses an explicit file
allowlist and excludes Git metadata, credentials, and compiler downloads.

Compiler names can be overridden:

```sh
make package WINCC=x86_64-w64-mingw32-gcc-posix WINDRES=x86_64-w64-mingw32-windres
```

Run the optional memory/undefined-behavior checks on a host supporting GCC's
sanitizers:

```sh
make test-sanitize
```

In a sandbox where LeakSanitizer cannot inspect processes, use
`ASAN_OPTIONS=detect_leaks=0 make test-sanitize`; address and undefined-behavior
checks remain enabled.

## CI and GitHub Releases

[Build and release](.github/workflows/ci.yml) runs on branch pushes, pull requests,
version-tag pushes, and manual runs. It runs the native sanitizer checks, builds
the Windows x64 executable, inspects its format/imports, creates the ZIP, and
verifies both checksums. Successful builds save the EXE, ZIP, and checksums as the
`windows-x64-package` Actions artifact for 14 days.

After committing and pushing the changes you want to release, push an unused
version tag:

```sh
git tag -a v1.0.0 -m "Monitor Spaces v1.0.0"
git push origin v1.0.0
```

A successful `v*` tag build automatically creates a GitHub Release with:

- `MonitorSpaces.exe`: the standalone executable, linked prominently at the top
  of the release notes for direct download.
- `MonitorSpaces.exe.sha256`: SHA-256 checksum of the executable.
- `monitor-spaces-windows-x64.zip`: executable, source, resources, tests, build
  instructions, workflow, and executable inspection report.
- `monitor-spaces-windows-x64.zip.sha256`: SHA-256 checksum of that ZIP.

Tags containing a hyphen, such as `v1.1.0-rc.1`, create prereleases. Branch pushes,
pull requests, and manual runs build artifacts without publishing releases.
The release stays a draft until all four assets are uploaded. Rerunning a failed job
can resume an unfinished draft; published releases and assets are left unchanged.
The workflow uses GitHub's built-in token, with release-write permission limited
to the publishing job. No additional repository secret is required. The executable
remains unsigned, and CI does not validate live Windows desktop behavior.

## Build on Windows with MSYS2

In the **MSYS2 MINGW64** shell, install the toolchain and build:

```sh
pacman -S --needed mingw-w64-x86_64-gcc mingw-w64-x86_64-binutils make python
make windows WINCC=gcc WINDRES=windres
make inspect WINCC=gcc WINDRES=windres
make package WINCC=gcc WINDRES=windres HOSTCC=gcc
```

The native Windows build instructions have not been exercised here. Linux/WSL
cross-compilation is the verified build path. The resulting GUI executable has
its compiler support linked statically and imports only Windows-provided DLLs.

## Limits and Windows validation

Live validation is still required for shortcut conflicts, keyboard layouts
(including AltGr), mixed-DPI displays, graphics drivers, minimized/maximized
windows, focus, and individual applications. The embedded manifest requests
Per-Monitor V2 DPI awareness, with a per-monitor fallback; the panel rescales on
`WM_DPICHANGED`.

The keyboard hook posts navigation commands to the panel without switching
windows in the input callback. It swallows the captured arrow's repeats and
release, and sends a brief, marked Ctrl press/release to prevent opening Start
when Win is released. That interaction, shell shortcut suppression, and behavior
with elevated foreground apps still require live Windows validation. If input
masking or command posting fails, the arrow passes through and the panel reports
an error. Ctrl + Alt + 1–4 remains available for direct selection.

Windows privilege isolation can prevent managing elevated applications. When
writing a recovery tag is denied, the utility leaves that window visible and
reports an error. Synchronous window-state calls allow confirmation before tag
removal, but an unresponsive application can delay a switch, recovery, or shutdown.

Windows can automatically hide/show owned popups with their owner, including
popups on a different monitor. Such application-owned groups may therefore cross
monitor boundaries. Apps may minimize to their own tray icon, create replacement
windows, or ignore state changes. Their own taskbar policies still apply. Only
foreground selection is monitored continuously; background self-restores are not enforced.
The supported monitor limit is 64. No live Windows or Wine execution was performed
while producing this release, and the executable is unsigned.

## API references

The implementation follows Microsoft's documentation for
[window properties and UIPI](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setpropw),
[visibility and positioning](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowpos),
[minimizing and restoring windows](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-showwindow),
[global shortcuts](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-registerhotkey),
[low-level keyboard hooks](https://learn.microsoft.com/en-us/windows/win32/winmsg/lowlevelkeyboardproc),
[notification icons](https://learn.microsoft.com/en-us/windows/win32/api/shellapi/nf-shellapi-shell_notifyiconw),
[window-to-monitor selection](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-monitorfromwindow),
and [display notifications](https://learn.microsoft.com/en-us/windows/win32/gdi/wm-displaychange).
