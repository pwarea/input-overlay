# Input Overlay

[![Downloads](https://img.shields.io/github/downloads/pwarea/input-overlay/Input-Overlay-windows-x64.zip?displayAssetName=false&label=downloads)](https://github.com/pwarea/input-overlay/releases)

A portable keyboard, mouse, and controller overlay for Windows 10 (1703+) and Windows 11, written in C++17 with Win32 and GDI+. No installer, web runtime, service, or administrator access is required.

## Download

Download the portable Windows x64 ZIP from [Releases](https://github.com/pwarea/input-overlay/releases/latest), extract it to a writable folder, and run `Input Overlay.exe`. The automatically generated GitHub source archives contain source code, not a built application. Release packages contain the executable and license/documentation files; your personal settings are created on first use.

The download badge counts release ZIP downloads across versions, not unique people or active users. No application telemetry is used. The badge becomes available after the first public release.

## Use

Run `Input Overlay.exe`. Settings are saved beside it in `Input Overlay.ini`; keep the folder writable.

- **Ctrl + Alt + F10** instantly hides or shows the overlay. Change the shortcut in **General**.
- Right-click the system tray icon for settings, visibility, positioning, or **Exit Input Overlay**. Closing settings keeps the overlay running.
- **Overlay** offers Pearl, Outline, Neon, Glass, and Circuit styles, ANSI/ISO English layout, mouse visibility, opacity, color, and scale from 10% to 200%. Pearl uses transparent glass surfaces, reflective edges, and a slightly larger mouse. Styles keep the same compact canvas and use no animation or background panel. **Move overlay** temporarily enables dragging; finish positioning to restore click-through. Reset restores 100% scale at the bottom-left.
- **Bindings** separates a visual key's label from its input. Capture a keyboard key, mouse button, or wheel direction. Assigning Q to a mouse side button removes Q from its previous visual; it does not alter keyboard input sent to games. **Unbind** removes the selected element's input. **Reset to default** restores that element's original input, label, and visibility.
- **Applications** lets you choose running windows. Rules match the executable's complete path, survive restarts and title changes, and activate only while that application's window is foreground. An empty restricted list shows nothing.
- **Controller** switches to a gamepad view with Xbox or PlayStation button appearance, automatic selection or a specific XInput controller, and a 0%–40% stick deadzone. Sticks, stick clicks, triggers, shoulders, face buttons, and D-pad directions respond independently. Controller mode shares the overlay style, scale, position, shortcut, and application rules. Keyboard and mouse bindings are preserved when switching views.
- **Run at startup** is optional and off by default. It starts quietly in the tray using a per-user Windows startup entry. Turn it off before moving or deleting the folder; re-enable it after moving.
- **Start minimized** opens directly in the system tray when launching the executable. It does not change overlay visibility. The last visibility setting, selected applications, bindings, and appearance are restored on each launch, even when a selected application is closed.
- **Updates** checks GitHub once at startup by default. Turn off **Check for updates at startup** to use manual checks only. **Check for updates** compares your build with the latest commit on `main`. Available updates show release notes; **Download and install** downloads the verified build and restarts the app while preserving your settings. A commit whose build is not ready is shown as pending. There is no periodic checking or automatic installation.

The default overlay is 357 × 154 physical pixels at 100%, anchored 32 pixels from the bottom-left screen edges. Moving it saves the nearest horizontal and vertical edges and monitor. Resolution and DPI changes preserve its physical pixel size and edge offsets. If the chosen monitor disappears, the overlay temporarily uses the primary monitor. Positions are kept on-screen where possible; an overlay larger than the screen cannot fit without reducing its scale.

## Compatibility

Borderless fullscreen and windowed games are supported. True exclusive fullscreen and protected/elevated applications can block desktop overlays or input. No game injection or anti-cheat bypass is used. Use borderless fullscreen when needed. Vendor mouse software must expose a standard mouse button or keyboard input for capture.

Controller input uses XInput and supports one displayed controller from up to four slots. PlayStation appearance changes the drawing only; a PlayStation controller must already be exposed to Windows as an XInput device. Native PlayStation HID, touchpad, motion controls, and controller remapping are not supported in this version. The app does not send controller input or vibration commands.

Input is processed locally and is never recorded to a log or sent over a network. Rendering happens only when displayed state changes; mouse movement does not redraw the overlay. Connected controllers are sampled approximately every 16 ms while the controller view is visible; missing devices are retried every two seconds. Controller polling stops while hidden or in keyboard and mouse mode. Keyboard and mouse input listening stops while hidden or in controller mode unless a binding is being captured.

Update checks contact GitHub over HTTPS. Downloads contact GitHub's release asset servers only after you choose to install. No input history, application list, personal settings, analytics identifier, or account token is sent. Update files are checked against a commit-specific manifest and SHA-256 hashes before installation; failed replacements are rolled back. The portable folder must be writable for in-app updates.

Selected application paths, labels, bindings, and preferences are stored locally in `Input Overlay.ini`. Do not upload this file when sharing the project or reporting a problem.

To remove the app, disable startup, choose **Exit Input Overlay**, then delete the portable folder.

## License and attribution

The source is available under the custom [Media Attribution License 1.0](LICENSE). Public videos and livestreams displaying the overlay must include a visible link to the origin repository, as specified in the license. Private use does not require credit. This is a source-available project, not an OSI-approved open-source project. Third-party runtime terms are in [THIRD_PARTY_NOTICES.txt](THIRD_PARTY_NOTICES.txt).

## Use with games

Check the rules of your game, platform, tournament, and anti-cheat provider before use. No approval, compatibility, or protection against account suspensions or bans is promised. The software is provided without warranty, and liability is limited to the extent permitted by applicable law, as set out in [LICENSE](LICENSE).

## Build

With Visual Studio 2022 C++ build tools and CMake:

```powershell
cmake -S . -B build -A x64 -DBUILD_TESTING=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Or with Zig 0.15.2 on Windows:

```powershell
./scripts/build.ps1 -Zig C:/tools/zig/zig.exe -Test
```

The Zig build produces `dist/Input Overlay.exe`. Both builds link the C++ runtime statically. Tests cover input remapping, Unicode/atomic persistence, invalid settings, application matching, screen anchoring, controller normalization and reconnect handling, and renderer lifecycle behavior.

For interactive checks, configure CMake with `-DINPUT_OVERLAY_BUILD_MANUAL_TESTS=ON` and run `window_fixture.exe`. F11 toggles borderless fullscreen; its click counter verifies click-through. Add this window in Applications to check foreground visibility and Alt-Tab behavior.

## Release

Create a portable package from a release build:

```powershell
./scripts/package.ps1 -ExecutablePath 'dist/Input Overlay.exe' -Version 0.1.0
```

Upload `dist/releases/v0.1.0/Input-Overlay-windows-x64.zip` and its `.sha256` file to the GitHub release. Keep the ZIP asset name the same in each release so the download badge counts it across versions. The ZIP includes only the executable, README, license, and third-party notices. The packager never includes local settings or debug files.

Every push to `main` runs the Windows build and tests, then publishes a release tagged `build-<full commit SHA>`. The release includes the portable ZIP and the four individually hashed files used by the updater. Files are uploaded to a draft before publication, so clients do not install partial releases. Only a successful build of the current `main` commit is marked as the latest release. Pull requests build and test without publishing.

To write the update notes displayed in the application, replace the bullets in [UPDATE_NOTES.md](UPDATE_NOTES.md) and include that change in the same commit as your update. Use up to 32 single-line bullet points of at most 1000 characters. When that file is unchanged in the commit, its nonempty commit-message lines become the notes instead; attribution trailers are omitted. Notes are displayed as plain text.

Pushing a `v0.1.0` tag matching the CMake version separately prepares a draft versioned release for manual review. In-app updates continue to follow `main` and its commit-specific releases.
