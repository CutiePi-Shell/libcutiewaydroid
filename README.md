# libcutiewaydroid

This library provides a Waydroid lifecycle and app-list helper for Cutie.
It wraps the `waydroid` CLI (via `QProcess`) to report init/container/
session status and to drive init/start/stop/install, and reads Waydroid's
own `.desktop` launcher files directly for the installed-app list instead
of calling `waydroid app list`.

## Building and installing

```
mkdir build
cd build
cmake ..
make
sudo make install
```

## Using the module

```qml
import Cutie.Waydroid 1.0
```

### 1. Status, on page open

```qml
Component.onCompleted: CutieWaydroidManager.refreshStatus()

Label {
    visible: !CutieWaydroidManager.initialized
    text: "Waydroid has not been set up on this device yet."
}
Button {
    visible: !CutieWaydroidManager.initialized
    text: "Initialize Waydroid"
    onClicked: CutieWaydroidManager.initWaydroid()
}
```

`initialized`, `containerRunning` and `sessionRunning` all update on their
own as soon as `refreshStatus()` returns; there's no need to poll unless
you want to detect Waydroid being stopped/crashed from outside this page.

### 2. Start/stop as a single on/off switch

```qml
Switch {
    id: waydroidSwitch
    enabled: !CutieWaydroidManager.busy
    onToggled: checked ? CutieWaydroidManager.startWaydroid() : CutieWaydroidManager.stopWaydroid()
}
Connections {
    target: CutieWaydroidManager
    function onRunningChanged() { waydroidSwitch.checked = CutieWaydroidManager.running }
}
Component.onCompleted: waydroidSwitch.checked = CutieWaydroidManager.running
```

`checked` is synced explicitly (rather than bound with `checked:
CutieWaydroidManager.running`) because Switch overwrites `checked` itself
on tap, which would silently break a plain property binding the first
time the user touches it.

`startWaydroid()` checks the container, then the session, then launches
the full UI - skipping any stage that's already up. `stopWaydroid()` does
the same in reverse. Both are no-ops while `busy` is already true, and
`lastError` carries the reason if a stage fails or times out.

### 3. Installed apps list

```qml
property var appModel: CutieWaydroidManager.fetchInstalledApps()

ListView {
    model: appModel
    delegate: RowLayout {
        Image { source: icon; /* may be a path or a themed icon name */ }
        Label { text: name; Layout.fillWidth: true }
        Button {
            text: hidden ? "Show in app drawer" : "Hide from app drawer"
            onClicked: appModel.toggleHidden(index)
        }
        Button {
            text: "Uninstall"
            onClicked: appModel.uninstallApp(index)
        }
    }
}
```

Each call to `fetchInstalledApps()` returns a new, independent model (same
pattern as `CutieDesktopFileParser.fetchAllEntriesModel()`), scanned from
the `.desktop` files Waydroid writes into the user's applications
directory. This works even if the container/session aren't running -
there's no dependency on a live Waydroid session, unlike `waydroid app
list`.

`toggleHidden(index)` edits only the `NoDisplay=` line of that app's
`.desktop` file in place, so app drawers that read the same file (this
one included) pick it up immediately. Note that Waydroid's own background
sync isn't aware of this and can regenerate the file later (e.g. after an
app update), which may reset the flag - this is a property of how Waydroid
manages these files, not a bug in this module.

`uninstallApp(index)` runs `waydroid app remove <package>` and removes the
row once that finishes; listen for `appModel.uninstallFinished(packageName,
success, message)` to show a toast/error.

### 4. Installing an APK

```qml
Button {
    text: "Install APK"
    onClicked: CutieWaydroidManager.installApp(filePicker.selectedPath)
}
Connections {
    target: CutieWaydroidManager
    function onInstallFinished(success, message) { /* toast */ }
}
```

## Notes / things to wire up at the integration level

- `waydroid init` (and possibly `waydroid container start`/`stop`,
  depending on how this device's polkit/sudoers is set up) needs root.
  This module intentionally does **not** shell out through `pkexec`
  itself - that needs a polkit authentication agent running in the
  compositor session, which a custom shell may not have. If plain calls
  fail with a permissions-related message in `lastError`, either grant the
  default user passwordless rights to these specific `waydroid`
  subcommands (the usual approach on single-user mobile images), or wrap
  the relevant `QProcess::start()` call in `cutiewaydroid.cpp` with
  whichever elevation helper this project already uses for other
  root-only settings actions.
- This library includes `qWarning()`/`qDebug()`-friendly failure paths
  (e.g. when a `.desktop` file can't be opened) - check the application
  log if the app list looks wrong or a toggle silently doesn't persist.
