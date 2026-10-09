# Dolphin Plus

Development checkout for an independently installed Dolphin-derived file manager.
Based on Dolphin 26.08.2, with separate application settings, sessions, D-Bus
routing, folder-view preferences, bookmarks, and private core libraries.
See [Dolphin Plus development](DOLPHIN-PLUS.md) for build, launch, and verification
instructions. Native sibling navigation and an integrated image viewer are
implemented; grouping and scripting remain planned work.

## Build and Install

From a checkout on Linux, run:

```sh
./build-and-install.sh
```

This builds the current source and registers **Dolphin Plus** in your application
launcher. Run the same command after updating the source. Close Dolphin Plus
first; stock Dolphin can stay open and remains the default file manager.
The script automatically uses host tools from Flatpak VS Code terminals.

```sh
./build-and-install.sh --test          # Also run focused isolated tests
./build-and-install.sh --jobs 2        # Limit parallel compilation (default: 4)
./build-and-install.sh --dry-run       # Inspect commands without making changes
```

Install your distribution's Dolphin/Qt 6/KDE Frameworks development dependencies
first, including a C++ compiler, CMake, Ninja, Extra CMake Modules, and the
`desktop-file-utils` tools. CMake checks the required library versions and reports
missing packages. The script also needs Bash, `pgrep`, and `flock` (typically
provided by procps/procps-ng and util-linux). `--test` additionally requires Ark,
`bsdtar` from libarchive, and `dbus-run-session`; the coexistence check runs when
stock Dolphin is installed. Gwenview supplies the image viewer, and FFmpeg enables
video folder covers.

Do not run the installer as root. It builds into `build-dolphin-plus`, stages
there, and creates user-local desktop/icon/executable links. Keep the checkout
and its build directory in place: this is a development install, not a standalone
package. Existing unrelated launcher files are never overwritten. The script
does not change default applications, shell startup files, or system packages.
See [development notes](DOLPHIN-PLUS.md) for manual build steps and limitations.

## Tabs and Type-to-Find

Opening a new tab, including middle-clicking a folder, switches to that tab.

Type in the file view to find any part of a filename. Matching text is
highlighted without hiding other files. Matching ignores case and accents.
Tab advances to the next match and wraps at the end; Backspace edits the query.
Escape ends the search while keeping the selected file. Normal navigation,
clicking in the view, changing folders, or leaving the pane also ends the search.

Typing automatically opens a Find field in the filter-bar position without
taking focus from the file list. Click the field to edit or paste a query;
Tab still advances through matches, and Enter returns focus to the file list.
Escape, the close button, or clearing the query closes Find. An existing filter
is preserved and its bar returns when Find closes; Find never changes its
pattern, case sensitivity, or matching mode.

This is enabled by default. To restore Dolphin's timed, prefix-only search and
normal Tab focus navigation, turn off **Match any part of the filename** under
**Settings > Configure Dolphin Plus > Interface > Folders & Tabs > Typing to find**.

## Native Sibling Navigation

Use **Go > Previous Sibling Folder** or **Go > Next Sibling Folder** to navigate
the active pane without opening or closing tabs. Assign either command in
**Settings > Configure Keyboard Shortcuts**, or add it through **Configure
Toolbars**. No default shortcuts are reserved.

Navigation uses case-insensitive, locale-aware natural name ordering, skips
hidden folders and files, and stops at the first or last sibling. It currently
supports local folders only. Lookups are asynchronous; repeated triggers are
ignored while busy, and changing tabs, panes, or location cancels pending work.
Back/Forward history is preserved. No service menu or full-path window title is
needed.

## Integrated Image Viewer

Open a supported image normally to view it in a separate, uncluttered Dolphin
Plus window. It uses Gwenview's embeddable image component, without Gwenview's
folder browser, sidebars, or toolbars. The installed `gwenview` package supplies
the component; supported image formats depend on its installed decoders.

OpenEXR (`.exr`) is also accepted when the `kimageformats` EXR decoder is installed,
even when absent from Gwenview's advertised formats. This is an image preview,
not an HDR grading tool; Gwenview may not apply color profiles to floating-point EXR images.

- Mouse wheel or Left/Right: previous/next image in the originating pane's
	displayed order, respecting its filter and sorting at the time of opening.
- Home/End: first/last image in the current folder's image list.
- Past the last image: first image in the next sibling folder containing images.
	Before the first: last image in the previous sibling folder containing images.
- Previous/Next Sibling Folder: jump directly to the first image in that sibling
	direction. Assign shortcuts in Image Viewer preferences; none are reserved by default.
- Enter, Escape, or left double-click: close, return to the originating tab and pane, and select
	and reveal the last viewed image, navigating to its folder if needed.
- Ctrl+wheel: zoom. `0`: fit to window. `1`: actual size. Drag a zoomed image to pan.
- F11: toggle fullscreen. Right-click for viewer controls and file actions.

In fullscreen, the cursor hides after two seconds without mouse activity and
returns when you move or use the mouse. Menus and dialogs keep a visible cursor.
The overview map stays hidden in Zoom to Fit; it remains available when zoomed
in and only part of the image is visible.

Use **Settings > Configure Dolphin Plus > Image Viewer**, or right-click the
image and choose **Configure Image Viewer**, to set fullscreen startup, choose
**Fit larger images only** or **Fit all images**, and edit viewer shortcuts.
Settings and shortcuts are saved separately from standalone Gwenview. Apply
updates open viewers; fullscreen startup applies the next time a viewer opens.
Cancel leaves unapplied changes unsaved, and Restore Defaults resets this page.

**Keep zoom and position between images** is enabled by default. A new viewer
starts in Fit; after that, Fit, Actual Size, or your custom zoom remains selected
as you browse. Zoomed images retain the same relative viewing position when local
image dimensions are available, including when the next image has a different
size or aspect ratio. Position is limited by the new image's edges. Uncheck this
option in Image Viewer preferences to reset every image to Fit.

Actual Size and Fit are mutually exclusive modes; manual zoom can leave neither
selected. Fit preserves aspect ratio and follows window resizing. The fit policy
applies to local images with readable dimensions; other images retain native
component behavior. Very small images remain subject to the component's maximum
zoom. Menu shortcut labels match the configured viewer bindings.

Sibling browsing supports local folders, preserves displayed symlink paths, and
uses natural, case-insensitive folder order. Newly entered folders use natural
filename order rather than the original pane's sorting/filter. Hidden folders
and hidden images are skipped; scanning is non-recursive and stops at the
parent's ends without wrapping. Unreadable folders report an error and stop the
search. Remote/search/mixed-folder lists retain in-list navigation only.

There is one viewer per pane. Each image list is a snapshot. Closing in another
folder loads it in the original pane and clears its name filter so the image can
be selected. Returning to the original folder does not reload it; a name filter
hiding the returned image is cleared. Removed images are not selected. Manually
changing the originating folder cancels its viewer; closing its tab also destroys
it. Home/End, replacing the image list, and closing cancel a pending sibling search.
The filename and position appear in the normal window title.

**Open With** still uses external applications. Unsupported files, multi-file
activation, or a missing viewer component retain Dolphin's external-opening
behavior. This first version does not provide image editing, video playback,
live selection tracking, or a thumbnail strip.

The following sections describe upstream Dolphin, not fork support.

## Upstream Dolphin

Dolphin is KDE's file manager that lets you navigate and browse the contents of your hard drives, USB sticks, SD cards, and more. Creating, moving, or deleting files and folders is simple and fast. See more information [on Dolphin's homepage](https://apps.kde.org/dolphin/).

![Screenshot](https://cdn.kde.org/screenshots/dolphin/dolphin.png)

## User Documentation

See https://userbase.kde.org/Special:myLanguage/Dolphin.

## Contributing

Like other projects in the KDE ecosystem, contributions are welcome from all. This repository is managed in [KDE Invent](https://invent.kde.org/system/dolphin), our GitLab instance.

* Want to contribute code? See the [GitLab wiki page](https://community.kde.org/Infrastructure/GitLab) for a tutorial on how to send a merge request.
* Reporting a bug? Please submit it on the [KDE Bugtracking System](https://bugs.kde.org/enter_bug.cgi?format=guided&product=dolphin). Please do not use the Issues
tab to report bugs.
* Is there a part of Dolphin that's not translated? See the [Getting Involved in Translation wiki page](https://community.kde.org/Get_Involved/translation) to see how
you can help translate!

If you get stuck or need help with anything at all, head over to the [KDE New Contributors room](https://go.kde.org/matrix/#/#kde-welcome:kde.org) on Matrix. For questions about Dolphin, please ask in the [KDE File Management room](https://go.kde.org/matrix/#/#kde-fm:kde.org). See [Matrix](https://community.kde.org/Matrix) for more details.

## Development Philosophy

Dolphin is a file manager focusing on usability. When reading the term Usability people often assume that the focus is on newbies and only basic features are offered. This is not the case; Dolphin is quite full-featured, but the features are carefully chosen so as to not impede any of the users in the target user groups.

### Target User Groups

Focusing on usability means that features are discoverable and efficient to use. The feature set is defined indirectly by the target user group of Dolphin:

- **Lisa**: Lisa has been familiar with computers for 10 years. From her job, she has experience with Word, Excel and Outlook. At home she mainly uses the computer for browsing the web and writing e-mails. She requires a file manager for managing photos from the camera, documents she gets via e-mail, or PDFs she downloads with a browser. Lisa knows concepts like folders and a file hierarchy, but she is not familiar with the file hierarchy of Linux.

- **Simon**: Simon has been a developer at a software company for 8 years. At home he uses a file manager to maintain his large collection of photos and music. Additionally he owns a small homepage and needs to transfer updated files on the FTP server. Moving and copying files are regular tasks in Simon's workflow.

Not part of the target user group of Dolphin are Fred and Jeff:

- **Fred**: Fred is 75 years old and is able to write e-mails and browsing the web. He is not familiar with file hierarchies and stores all his documents on the desktop.

- **Jeff**: Jeff is Linux-freak since the age of 16 a few years ago. He is a developer and in his spare time he acts as administrator for a small company. Jeff has two monitors to keep the overview about his huge number of opened applications.

This does not mean that Fred or Jeff cannot work with Dolphin. But there might be features and concepts of Dolphin that overburden Fred. Also Jeff might miss some features which are a must-have for his daily work. This is acceptable; there are other tools that cater specifically to their needs.

### Non-Intrusive Features

Before a feature is added in Dolphin, check whether the feature is mandatory for the target user group. If this is not the case, then this does not mean that the feature cannot be added; first it must be clarified whether the feature might be non-intrusive, so that it adds value for users outside the primary target user group of Dolphin. The term "non-intrusive" is mainly related to the user interface. A feature that adds a lot of clutter to the main menu, context menus or toolbar might harm the target user group. In this case the feature should not be added.

A good example of a feature that is non-intrusive is the embedded terminal in Dolphin. It only requires one entry inside a sub-menu, but adds great value for Jeff, who is not part of the target user group.

### Options

Options are mandatory as the "average Joe" user does not exist. Still it is not the goal of Dolphin to offer options for all kind of things. Again the focus is on the possible needs of the target user group. Each additional option makes it harder finding other options, so the same rules for features are applied to options too.
