# Monitor Spaces

A small C/Win32 utility giving each monitor four workspaces. Point at a display
and use a shortcut to switch just that display. Every monitor starts on workspace 1.

| Control | Action |
| --- | --- |
| Win + Left | Previous workspace on the monitor under the pointer; workspace 1 wraps to 4 |
| Win + Right | Next workspace on the monitor under the pointer; workspace 4 wraps to 1 |
| Ctrl + Alt + 1–4 | Switch the monitor under the pointer to workspace 1–4 |
| Ctrl + Alt + 0 | Restore every window hidden by Monitor Spaces; reset all monitors to workspace 1 |
| Restore all button | Same as Ctrl + Alt + 0 |
| Numbered tray icon | Show the workspace on the monitor under the pointer; click to open the panel |
| Right-click tray icon | Open the panel, restore windows, or restore and exit |
| Close / minimize panel | Hide the panel and keep running in the background |
| Restore and exit | Restore managed windows, remove the tray icon, unregister shortcuts, exit |

Use the number row above the letters. The numeric keypad is not registered.
The app starts in the background. A small number (1–4) in the taskbar's notification
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

Win + Left/Right takes over those combinations while the utility runs. Press an
arrow again for each step; holding it does not cycle repeatedly. Both Win keys
work. Combinations with additional Ctrl, Alt, or Shift modifiers pass through,
including Win + Ctrl + Left/Right and Win + Shift + Left/Right.

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
Win + Left/Right uses a dedicated keyboard-hook thread because Windows reserves
Win-key shortcuts. If the hook cannot be installed, startup reports the error and
exits. **Restore and exit** removes the hook and restores normal key handling.

## Window behavior

- A switch uses `GetCursorPos` and `MonitorFromPoint`, so selecting a monitor does
  not require clicking a window. Spanning windows belong to the monitor with
  the largest intersection, as reported by `MonitorFromWindow`.
- Visible, eligible top-level windows on the departing workspace get a numeric
  recovery property before any are hidden. Newly opened windows join the active
  workspace when you leave it. Hidden windows tagged for the destination reappear.
- The panel, desktop, taskbars, known shell surfaces, child windows, no-activate
  surfaces, and DWM-cloaked windows are excluded. Owned dialogs and ordinary tool
  windows participate. Explorer folder windows participate too.
- Complete snapshots and tagging before hiding protect owned dialogs from an
  owner's implicit hide. Reveals run in reverse enumeration order to bring owners
  back before their popups.
- `SetWindowPos` toggles visibility while requesting no geometry, Z-order, or
  focus change. Minimized and maximized state is left in place. Windows and apps
  still control their own activation and visibility behavior.
- Windows hidden by their applications without a Monitor Spaces tag stay hidden.
  Windows that close while hidden are simply gone. There is no session persistence
  across logoff or reboot.

These workspaces use window visibility. Applications keep running while hidden;
Windows Task View desktops and application-specific workspace features are separate.
A visible window dragged to another display joins that display's active workspace
on its next switch. A hidden window's current monitor determines where its numbered
workspace is revealed.

## Recovery

The stable, namespaced `MonitorSpaces.HiddenWorkspace.v1` window property stores
an integer from 1 to 4 on the foreign window itself. It contains no pointer into
the utility's memory, so surviving windows can be found after an unexpected exit.
The tag is written before hiding and removed only after visibility is confirmed.
Failed operations retain their tags for recovery.

Recovery scans all tagged top-level windows, including ones no longer eligible
or attached to the original monitor. It runs at startup, on Ctrl + Alt + 0, on
**Restore and exit**, and during session shutdown. Display changes also trigger
recovery and reset all workspace numbers to 1. A one-second topology check covers
missed notifications, monitor repositioning, and work-area changes.

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
Run recovery at the same privilege level as the run that hid the windows.

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
verifies its checksum. Successful builds save the package and checksum as the
`windows-x64-package` Actions artifact for 14 days.

After committing and pushing the changes you want to release, push an unused
version tag:

```sh
git tag -a v1.0.0 -m "Monitor Spaces v1.0.0"
git push origin v1.0.0
```

A successful `v*` tag build automatically creates a GitHub Release with:

- `monitor-spaces-windows-x64.zip`: executable, source, resources, tests, build
  instructions, workflow, and executable inspection report.
- `monitor-spaces-windows-x64.zip.sha256`: SHA-256 checksum of that ZIP.

Tags containing a hyphen, such as `v1.1.0-rc.1`, create prereleases. Branch pushes,
pull requests, and manual runs build artifacts without publishing releases.
The release stays a draft until both assets are uploaded. Rerunning a failed job
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
reports an error. Synchronous visibility calls allow confirmation before tag
removal, but an unresponsive application can delay a switch, recovery, or shutdown.

Windows can automatically hide/show owned popups with their owner, including
popups on a different monitor. Such application-owned groups may therefore cross
monitor boundaries. Apps may also show their own hidden windows, create replacement
windows, or ignore visibility changes; there is no continuous enforcement.
The supported monitor limit is 64. No live Windows or Wine execution was performed
while producing this release, and the executable is unsigned.

## API references

The implementation follows Microsoft's documentation for
[window properties and UIPI](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setpropw),
[visibility and positioning](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowpos),
[global shortcuts](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-registerhotkey),
[low-level keyboard hooks](https://learn.microsoft.com/en-us/windows/win32/winmsg/lowlevelkeyboardproc),
[notification icons](https://learn.microsoft.com/en-us/windows/win32/api/shellapi/nf-shellapi-shell_notifyiconw),
[window-to-monitor selection](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-monitorfromwindow),
and [display notifications](https://learn.microsoft.com/en-us/windows/win32/gdi/wm-displaychange).
