# Byebye

A small native session and power menu inspired by the original TUI screenshot.
It opens as a centered, floating window, with mouse, number and letter shortcuts.
The app stays open and displays a native error dialog if a request fails; it exits
when the OS accepts the request. Desktop confirmation prompts and applications
with unsaved documents can still delay or cancel an accepted logout/shutdown.

## Build and launch

C11 and CMake 3.20 or newer. No package downloads or dependency installation are
performed by the build. There is no webview, JavaScript runtime, or background daemon.

### macOS

Uses Swift and SwiftUI on macOS 12 or newer, with a small AppKit bridge for
floating-window behavior and keyboard events, and Carbon Apple Events for system
actions. Requires Apple's command-line developer tools (Swift 5.7 or newer) and
CMake already installed. CMake invokes `swiftc` directly so the default Makefiles
generator works without installing Ninja or the full Xcode app.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
open build/Byebye.app
```

CLI (also suitable as a command for a hotkey tool):

```sh
./build/Byebye.app/Contents/MacOS/Byebye
./build/Byebye.app/Contents/MacOS/Byebye --dry-run
./build/Byebye.app/Contents/MacOS/Byebye --simulate-error
./build/Byebye.app/Contents/MacOS/Byebye --list
```

Move `build/Byebye.app` into Applications to make it available to normal app
launchers. For a system shortcut, create a macOS Shortcut with an **Open App**
action pointing to Byebye and assign a keyboard shortcut. Existing hotkey tools
can run `open /absolute/path/to/Byebye.app`.

Lock invokes the native Control–Command–Q shortcut through `/usr/bin/osascript`
and System Events. macOS may require permission under **Privacy & Security →
Accessibility** and **Automation** for Byebye or its launching terminal. A denied
request shows the OS error and permission guidance. No private lock API is used;
if the system shortcut is remapped, this route may not activate Lock Screen.

The build is local and unsigned. Signing/notarization for distributing it to other
Macs is a separate release step.

### Windows

Uses Win32 and the system power-management libraries. Requires CMake, a C compiler
and the Windows SDK already installed; there are no third-party runtime libraries.
For Visual Studio's developer terminal:

```powershell
cmake -S . -B build
cmake --build build --config Release
.\build\Release\byebye.exe
ctest --test-dir build -C Release --output-on-failure
```

Create a shortcut to the executable for the Start menu/launcher. Assign a shortcut
key through the Windows shortcut's Properties. Starting the executable directly
from a terminal also opens the GUI. `--dry-run` and `--simulate-error` work here too.
The GUI executable does not open an extra console window.

### Linux

Uses GTK 3.22+ (including its GLib/GIO D-Bus support), the desktop session services,
and systemd-logind or a compatible service such as elogind. Build prerequisites:
C compiler, CMake, pkg-config and GTK 3 development headers. Common package names
are `libgtk-3-dev` on Debian/Ubuntu and `gtk3-devel` on Fedora. Install missing
prerequisites only after deciding to do so; this project never installs them.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/byebye
```

Optional per-user installation of the app and desktop launcher:

```sh
cmake --install build --prefix "$HOME/.local"
```

Ensure `$HOME/.local/bin` is on your desktop session's PATH, or set the installed
desktop file's `Exec` to the absolute executable path. Assign `byebye` (or its full
path) in your desktop/window manager's custom keyboard shortcut settings.

The window requests centering and keep-above. Wayland compositors control placement
and stacking; some require a window rule for Byebye to center/float it. Tiling window
managers may likewise require a floating rule (window title/class: `Byebye`/`byebye`).

Lock is offered when a supported desktop screensaver service is present. Logout
supports GNOME, KDE Plasma and Xfce session services. Other desktops keep their
supported power actions but omit unsupported session actions. Power capability
queries include actions requiring authentication; logind can present a Polkit
prompt through the desktop's authentication agent. The app never runs `sudo` or
forcibly terminates a session as a logout fallback.

## Controls and available actions

| Action | Key | macOS | Windows | Linux |
| --- | --- | --- | --- | --- |
| Suspend / Sleep | `s` | Sleep | Sleep, if supported | Suspend, if supported |
| Lock | `l` | System shortcut | LockWorkStation | Desktop service, if present |
| Logout | `o` | Apple Event | ExitWindowsEx | Supported desktop service |
| Shutdown | `p` (power off) | Apple Event | ExitWindowsEx | logind, if permitted |
| Restart | `r` | Apple Event | ExitWindowsEx | logind, if permitted |
| Hibernate | `h` | Omitted | If available | If available |

Sleep and suspend describe the same operation; they are not duplicate buttons.
macOS manages hibernation itself and does not expose a separate supported Hibernate
operation here. Windows and Linux detect hibernation availability at startup.

Numbers `1`–`9` select the displayed button numbers, renumbered after filtering.
Arrow keys and Tab move through buttons in reading order; Shift–Tab moves backward.
Enter/Space activates the focused selection. Escape or `q` exits. Clicking a button
also activates it. Selection immediately requests the action; no extra app-level
confirmation is added. Requests run off the UI thread and cannot be submitted twice
while pending. Some sleep APIs return only after waking; in that case the app exits
on resume. Success means the OS accepted the request, not that the eventual power
transition can be observed or guaranteed by this process.

Global launch shortcuts belong to the OS or your existing hotkey tool; Byebye does
not stay resident to register a global key hook.

## Safe verification

```sh
ctest --test-dir build --output-on-failure
```

`--dry-run` renders the normal menu and closes on selection without invoking a
system operation. `--simulate-error` renders the menu and shows a simulated error
on selection. If both are supplied, simulated errors take precedence. `--list`
prints detected actions without opening a window; `--help` prints usage.

On macOS, the additional UI harness requires a graphical login session:

```sh
./build/macos_ui_tests ./build/preview.png
```

It renders the SwiftUI menu and tests keyboard navigation, number and letter
shortcuts, duplicate-request prevention, successful termination requests, and
error state dismissal/retry through the model used by the buttons. Native system
actions are replaced by the safe test modes, and application termination is
intercepted. The optional argument saves a rendered window-content preview.
Use `--simulate-error` to manually check native alert interaction and mouse input.

## Implementation references

- [Apple's system-process Apple Events](https://developer.apple.com/library/archive/documentation/mac/pdf/Processes/Shutdown.pdf)
- [Windows power state requests](https://learn.microsoft.com/en-us/windows/win32/api/powrprof/nf-powrprof-setsuspendstate)
- [Windows logout and shutdown requests](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-exitwindowsex)
- [logind D-Bus interface](https://github.com/systemd/systemd/blob/main/man/org.freedesktop.login1.xml)
- [GTK window stacking requests and limitations](https://docs.gtk.org/gtk3/method.Window.set_keep_above.html)

The shared core contains action metadata, command-line parsing and keyboard mapping.
Windows and Linux each have a native C UI/backend source file. The macOS frontend
uses a SwiftUI app entry point, observable menu model and views, and a Swift system
action backend. A Clang module imports the shared C core into Swift. This keeps
external dependencies low while maintaining three native frontends.
