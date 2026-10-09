# Dolphin Plus Development

## Baseline

- Upstream: https://invent.kde.org/system/dolphin.git (remote: `upstream`).
- Release: `v26.08.2`.
- Commit: `f19f070e7dc140213a2c6dd5d11f26122c89e1a0`.
- Full upstream Git history is retained. No publishing remote is configured.
- The checkout starts at the release tag in detached HEAD state. Create a working
  branch before making commits; no project commits have been created during setup.
- Keep upstream attribution and licenses. Distribution must comply with the
  applicable licenses; renaming the application does not change those obligations.

## Build and Stage

Run from the repository root in a host terminal:

```sh
cmake --preset dolphin-plus-dev
cmake --build --preset dolphin-plus-dev --parallel 4
cmake --install build-dolphin-plus
```

For this machine's Flatpak VS Code terminal, use host tools instead of the
sandbox's compiler and libraries:

```sh
flatpak-spawn --host cmake --preset dolphin-plus-dev
flatpak-spawn --host cmake --build --preset dolphin-plus-dev --parallel 4
flatpak-spawn --host cmake --install build-dolphin-plus
```

Build output, compile commands, and the development install prefix are under the
ignored `build-dolphin-plus/` directory. Tests are enabled. Configuration reports
missing dependencies; package installation is a separate, explicit operation.

The preset installs only into `build-dolphin-plus/stage/`, not system directories.
Configure the intended prefix before building; do not override it with
`cmake --install --prefix`, because the launcher embeds the configured executable
path. No system package, default file association, or desktop launcher outside
this repository has been installed.

Launch from a host terminal:

```sh
./build-dolphin-plus/stage/bin/dolphin-plus
```

From Flatpak VS Code:

```sh
flatpak-spawn --host "$PWD/build-dolphin-plus/stage/bin/dolphin-plus"
```

Use the staged executable, not any older `build-dolphin-plus/bin/dolphin` left
over from the initial upstream baseline. Do not source the generated environment
script into your normal session; the staged executable finds its own libraries
through a relative runtime path.

Configuration succeeded on the setup host with Qt 6.12.0, KDE Frameworks 6.30.0,
CMake 4.4.4, and GCC 16.2.1. KF6DocTools, PackageKitQt6, Ruby test-unit, and
SeleniumWebDriverATSPI were unavailable. These limit documentation generation,
software-manager integration, and test coverage, but do not block configuration.

The minimum Baloo Widgets version is explicitly 26.08.1. The 26.08.1-to-26.08.2
source changes introduce no new Baloo-facing API use; the fork builds with the
host's 26.08.1 library and retains the information panel. This is a dependency
floor, not a claim that future Dolphin versions will work with the same library.

## Coexistence

The current staged application has:

- Executable `dolphin-plus`, application name `dolphinplus`, and desktop ID
   `local.dolphinplus`. The upstream icon is reused under the separate desktop ID.
- Settings in `dolphinplusrc`, application state and data under the `dolphinplus`
   identity, separate session files, and a separate bookmarks menu.
- D-Bus services `local.dolphinplus-<pid>` and a window object at
   `/dolphinplus/Dolphin_1`. The internal `org.kde.dolphin.MainWindow` interface
   remains for compatibility; it does not select or control stock Dolphin.
- Folder-view preferences in `dolphinplus.viewproperties#1` extended attributes,
   with `.dolphinplus` as fallback. Existing Dolphin preferences are not imported
   or modified. Application-data fallback paths are separate as well.
- Private `libdolphinplusprivate` and `libdolphinplusvcs` libraries under
   `lib/dolphinplus/`, loaded through relative runtime paths.
- No registration or queueing for `org.freedesktop.FileManager1`, no default-file-
   manager daemon, no MIME association changes, and no KDE telemetry submission.

The install manifest has seven files: executable, launcher, icon, and two private
libraries with their major-version symlinks. Upstream SDK files, settings modules,
migration helpers, service-menu installers, item-action plugins, manuals, and
translation catalogs are not installed over their stock equivalents. Standard
KDE plugins and available upstream translations/manuals are reused from the host;
without those optional host components, corresponding integrations may be absent
or untranslated. Native packaging and a menu launcher installation remain separate
follow-up work.

Places, trash, KIO workers, KDE-wide preferences, service menus, thumbnailers,
tags, ratings, and custom folder icons remain intentionally shared. Changing
shared KDE preferences or filesystem metadata can affect both applications.
Separate application identities do not sandbox file operations or user scripts.

Qt, KDE Frameworks, KIO workers, and thumbnailers remain shared dependencies in
the initial native packaging approach. OS updates cannot overwrite separately
owned application files, but dependency changes may still require rebuilds or
fixes. Upstream Dolphin changes are adopted deliberately, not automatically.

## Verification

The application builds with metadata support. The non-GUI metadata regression
suite passes, including preservation of stock `.directory` contents and the
`kde.fm.viewproperties#1` attribute when saving and resetting fork preferences.

The coexistence test starts real stock Dolphin and Dolphin Plus offscreen on a
private D-Bus session with disposable home, configuration, data, and state paths.
It checks service ownership, routing a second invocation to the fork, native
New Window behavior, independent shutdown, preservation of stock configuration,
and separate session files. It has passed against the staged executable.

Run that test from a host terminal after building and staging:

```sh
DOLPHINPLUS_TEST_EXECUTABLE="$PWD/build-dolphin-plus/stage/bin/dolphin-plus" \
   ctest --test-dir build-dolphin-plus --output-on-failure \
   --no-tests=error -R '^dolphinpluscoexistencetest$'
```

For Flatpak VS Code, prefix the command with `flatpak-spawn --host env` before
the environment assignment. The test is registered only when `dbus-run-session`
and stock `dolphin` are available. Do not run the entire upstream GUI suite in
your normal session; it needs its own disposable environment. Interactive desktop
appearance, installed-package upgrade/uninstall, and the full GUI suite have not
been verified by these offscreen tests.

## Initial Workflows

### Single-Cover Folder Previews

Enable **Show Previews** in the file view. **Configure Dolphin Plus > Interface >
Folder Covers** selects Standard Dolphin previews, Single cover (the default),
or No folder previews. File thumbnails are unaffected. Single covers apply to
local folders; remote folders keep native previews.

The bundled blue folder template comes from the folder-thumbnail generator's
`script_files/folder.png`. A custom image can replace it. Content X/Y, width,
height, and corner radius use a fixed 256 x 256 coordinate system, independent
of the template resolution or displayed icon size. Images are center-cropped
to fill the rounded content region. A live preview accepts an optional sample
image; the sample is not saved. Apply updates open file views; Cancel leaves
saved settings unchanged.

Readable images are preferred in this order: `fanart`, `landscape`, `poster`,
then other filenames alphabetically, ignoring case. Supported extensions are
JPEG, PNG, WebP, BMP, and GIF, subject to installed Qt decoders. Optional video
fallback uses `ffmpeg` and `ffprobe` from PATH to extract a midpoint frame
(one second when duration is unavailable). Missing tools or unreadable media
fall back to the next candidate or the normal folder icon.

Subfolder fallback defaults to one level and can be disabled or set up to four.
Each folder tries images before videos; a current-folder video precedes a
subfolder image. Searches skip hidden entries and do not descend through
symlink directories. Work is bounded to 10,000 entries, 128 subfolders per
level, and 32 decoding attempts per cover. Each media subprocess has a
10-second timeout. Generation runs on a bounded worker pool and is cancelled
when its view no longer needs it.

**Keep existing custom folder icons** is enabled by default: a nonempty `Icon`
entry in `.directory` takes precedence. Uncheck it to preview folders already
customized by the Python generator without modifying those files. No covers,
metadata, or temporary media files are written into browsed folders.

Settings live in `dolphinplusrc` under `FolderCovers`; composed images use the
private `$XDG_CACHE_HOME/dolphinplus/folder-covers` cache (normally
`~/.cache/dolphinplus/folder-covers`). It is pruned to 1,024 images and 256 MiB.
Source path, modification time, size, template pixels, geometry, and output
size determine cache identity. Selection is rescanned on generation; refresh
the parent view after editing child media, because child-file changes are not
watched continuously. Stock Dolphin and its thumbnail cache are unchanged.

`foldercovertest` covers cropping, radius, source priority, cache invalidation,
corrupt media, real video extraction, custom icons, subfolder fallback,
cancellation, and preferences. `dolphinmainwindowtest testFolderCovers` checks
rendered pixels and live settings changes in the native file view. These are
offscreen tests, not a desktop performance benchmark.

### Integrated Image Viewer

Normal single-image activation now opens a Dolphin Plus-owned top-level window
using the installed Qt 6 Gwenview KPart (`kf6/parts/gvpart`). No Gwenview source
or private headers are copied or linked directly. The plugin and its private
dependencies are supplied by the host's `gwenview` package, not the staged fork.
The component retains its image rendering; Dolphin Plus owns the viewer actions
and context menu, including configurable shortcuts and zoom-mode indicators.
There is no embedded Gwenview application chrome or folder browser.

The originating view supplies an ordered snapshot of supported image URLs and
its directory URL. Home/End select the current list's first/last image. At list
boundaries, wheel/arrow navigation searches sibling folders: forward selects the
next folder's first image; backward selects the previous folder's last image.
The configurable `viewer_previous_sibling` and `viewer_next_sibling` actions
instead select the first image in either direction, with no default bindings.
The shared action factory also exposes configurable `viewer_first`/`viewer_last`.

Sibling searches use asynchronous KIO listings, natural case-insensitive folder
and filename order, and preserve displayed symlink paths. They skip hidden entries
and folders without supported images, never recurse or wrap, and stop with a
nonmodal message at errors or the outer boundary. The original displayed order
and filter are used for the initial snapshot; entering any sibling creates a new
natural-filename snapshot without the originating pane's filter. Root, hidden,
remote, search, and mixed-folder source lists do not enable sibling traversal.
Repeated navigation is ignored while searching. Home/End, list replacement,
closing, and owner destruction cancel pending jobs.

Ctrl+wheel zooms, `0` fits, `1` shows actual size, and F11 toggles fullscreen.
Enter, Escape, or the window close button return to the original tab and split
pane. A different destination is loaded once with deferred current-item and exact
selection requests; its name filter is cleared. Same-folder return selects and
reveals without a reload, clearing a name filter only if it hides the image.
A pane reuses its viewer when another image is activated. Manual location changes
cancel that session, and pane/tab destruction also destroys its viewer. Deleted
local files do not replace the selection or navigate the pane.

Supported formats follow the plugin metadata and installed image decoders.
Unsupported files and a missing plugin retain normal external opening; plugin
construction failure reports a warning before falling back. Open With and
multi-file activation remain external. The snapshot does not live-update after
sorting, filtering, adding, renaming, or deleting files. Viewing is in-process,
so a decoder crash can affect the file manager. No editing, video, configurable
background, or live file-manager selection tracking is added.

The Image Viewer preferences page is available in the file-manager settings and
from the viewer's context menu. Fullscreen startup and fit policy are stored in
the `ImageViewer` group of `dolphinplusrc`; native KActionCollection shortcuts use
`ImageViewer Shortcuts`. The preferences editor uses its own action copies, so
Cancel cannot change a live viewer. Apply refreshes open viewers; fullscreen
startup is only used when opening a viewer, not while applying other settings.

Fit and Actual Size are explicit, mutually exclusive viewer modes. Manual zoom
may leave neither selected. Local image fitting uses QImageReader dimensions,
EXIF orientation when enabled by the renderer, device-pixel ratio, and the
viewing area, with optional enlargement. A guarded runtime adapter finds the
component's Qt `zoom`/`zoomToFit` properties without private Gwenview headers.
This is version-sensitive rather than a standard KParts API; missing properties,
remote images, or unavailable dimensions fall back to native fit. The component's
zoom bounds still apply. No Gwenview configuration is written. Changes to the
component require running the real-plugin tests, especially fit/resizing tests.

Fit mode explicitly hides the component's `Gwenview::BirdEyeView` graphics item;
leaving Fit restores its native visibility/fade behavior. This runtime class check
is also component-version-sensitive. New image overviews are hidden immediately
while Fit is pending, without changing standalone Gwenview settings.

A viewer-local precise timer hides the image viewport cursor after two seconds
without mouse activity in active fullscreen mode. Mouse activity restores it;
dragging, menus, dialogs, deactivation, fullscreen exit, and closing stop idle
hiding. Cursor state is restored locally, without an application-wide override.

Focused tests use the real installed KPart in a disposable offscreen environment:

```sh
dbus-run-session -- ./build-dolphin-plus/bin/dolphinimageviewertest
dbus-run-session -- ./build-dolphin-plus/bin/dolphinmainwindowtest \
   testImageViewerReturn testImageViewerSiblingReturn testImageViewerCancellation \
   testImageViewerRemovedFileAndTabClosure
```

Set disposable HOME, XDG_CONFIG_HOME, XDG_DATA_HOME, XDG_CACHE_HOME, and
XDG_STATE_HOME paths and `QT_QPA_PLATFORM=offscreen` before running these commands.
The viewer test checks rendered pixels at landscape and portrait sizes, zoom
actions, wheel navigation, fullscreen, unreadable-image recovery, ownership,
exclusive zoom modes, fit enlargement/resizing, preference persistence/cancel,
shortcut rebinding/default restoration, and preservation of Gwenview settings.
Overview tests cover Fit, zooming, resizing, mouse movement, and image changes.
Cursor tests cover the elapsed idle delay, mouse restoration, component cursor
changes, dragging, fullscreen exit, dialog isolation, and closing. Physical cursor
presentation under the desktop compositor still needs real-session confirmation.
Navigation checks include Home/End, both boundary directions, explicit first-image
sibling jumps, natural ordering, empty/hidden/nested entries, symlink paths,
outer boundaries, custom sibling bindings, repeated triggers, and cancellation.
Set `DOLPHINPLUS_VIEWER_SCREENSHOT_DIR` to save the two viewport captures. The
main-window tests cover ordered/filtered browsing, exact selection/current item,
tab and split-pane restoration, no directory reload, cancellation, reuse, file
removal, cross-folder return with a cleared name filter, round trips to filtered
images without reload, and closing the source tab. These tests skip explicitly if the image
KPart is absent. Remote image loading, large-image performance, color-managed
photographs, and real desktop focus/compositor behavior remain unverified.

### File Operations And Navigation

Behavior references are in the neighboring `../CachyOS-Tools/` repository.
Those scripts and their installations are not modified by this project.

1. Sibling navigation (implemented): native `go_previous_sibling` and
   `go_next_sibling` actions in the Go menu, keyboard-shortcut settings, and toolbar
   configuration. Navigate the active pane in place, preserving history. Local
   folders only, using case-insensitive, locale-aware natural name ordering with a
   deterministic tie-breaker. Hidden folders and files are excluded; boundaries
   do not wrap. Parent listing is asynchronous, repeated triggers while busy are
   ignored, and tab/pane/location changes cancel outstanding work. No default keys
   are assigned. Directory symlinks retain their displayed path rather than
   jumping into the target's sibling hierarchy.
2. Group selection: Ctrl+G prompts for a folder name and moves a snapshot of the
   selected files and folders into it. Avoid using the clipboard to discover the
   selection. Define collisions, cancellation, partial failures, and undo.
3. Paste into folder: reuse Dolphin's native operation for exactly one selected
   directory, preserving clipboard cut/copy semantics.
4. Move contents up: move the current directory's contents to its parent, navigate
   there, and trash the original only after confirming it is empty. The existing
   tool overwrites collisions; changing that policy requires an explicit decision.
5. Folder artwork: custom templates, images or midpoint video frames, recursive
   generation, and explicit refresh. Distinguish persistent folder icons from
   cached previews; shared folder metadata can also change Dolphin's appearance.
6. Scriptable actions: shortcuts, toolbar buttons, menus, and external scripts use
   the same native commands with captured window/tab/pane identity, selected URLs,
   asynchronous results, and errors. Scripts must not silently retarget after a
   tab switch. Treat installed scripts as trusted executable code.

Sibling navigation is covered by focused native main-window tests for sorting,
boundaries, history, tab counts, split panes, cancellation, special-character
paths, symlinks, and matching desktop/mobile menus. Run them in a disposable
home/configuration environment with `QT_QPA_PLATFORM=offscreen` and a private
D-Bus session:

```sh
dbus-run-session -- ./build-dolphin-plus/bin/dolphinmainwindowtest \
   testSyncDesktopAndPhoneUi testSiblingNavigation \
   testSiblingNavigationCancellation testSiblingNavigationSplitView \
   testSiblingNavigationSymlink
```

Grouping and scripting remain planned work. Keep new
code localized and retain upstream internal names where possible to limit merge
conflicts. A complete scripting runtime and changes to shared KDE thumbnail
components are not prerequisites for the first two workflows.