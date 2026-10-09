#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
if [[ -f /.flatpak-info ]]; then
    exec flatpak-spawn --host bash "$repo_root/uninstall.sh" "$@"
fi

usage() {
    printf '%s\n' \
        'Usage: ./uninstall.sh [--dry-run] [--purge]' \
        '' \
        "Remove this checkout's per-user Dolphin Plus registration." \
        'Close Dolphin Plus windows first; its managed background handler is stopped automatically.' \
        '' \
        '  --dry-run  Show the removal plan without changing anything' \
        '  --purge    Also remove Dolphin Plus settings, bookmarks, sessions, and caches' \
        '  --help     Show this help' \
        '' \
        'Only matching installer-created links are removed; unrelated files are preserved.' \
        'Stock Dolphin becomes the folder default only if Dolphin Plus is currently selected.' \
        'Source checkout, build/staging files, and browsed-folder metadata are always preserved.'
}

fail() {
    printf 'Error: %s\n' "$*" >&2
    exit 1
}

dry_run=0
purge=0
for argument in "$@"; do
    case "$argument" in
        --dry-run) dry_run=1 ;;
        --purge) purge=1 ;;
        --help|-h) usage; exit 0 ;;
        *) fail "Unknown argument: $argument (use --help)." ;;
    esac
done

[[ "$EUID" -ne 0 ]] || fail 'Run as your regular desktop user, not root.'
[[ "$(uname -s)" == Linux ]] || fail 'This uninstaller supports Linux desktops.'
[[ -n "${HOME:-}" && "$HOME" == /* ]] || fail 'HOME must be an absolute path.'
[[ -z "${DESTDIR:-}" ]] || fail 'Unset DESTDIR before per-user uninstallation.'
data_home=${XDG_DATA_HOME:-$HOME/.local/share}
config_home=${XDG_CONFIG_HOME:-$HOME/.config}
cache_home=${XDG_CACHE_HOME:-$HOME/.cache}
state_home=${XDG_STATE_HOME:-$HOME/.local/state}
for directory in "$data_home" "$config_home" "$cache_home" "$state_home"; do
    [[ "$directory" == /* ]] || fail 'XDG directory paths must be absolute.'
done
for tool in pgrep flock readlink gio update-desktop-database; do
    command -v "$tool" >/dev/null || fail "Missing required tool: $tool."
done

cd -- "$repo_root"
build_dir=$repo_root/build-dolphin-plus
stage=$build_dir/stage
service_unit=dolphinplus-filemanager.service
unit_source=$stage/share/dolphinplus/$service_unit
sources=(
    "$stage/bin/dolphin-plus"
    "$stage/share/applications/local.dolphinplus.desktop"
    "$stage/share/icons/hicolor/scalable/apps/local.dolphinplus.svg"
    "$stage/share/dolphinplus/org.freedesktop.FileManager1.service"
    "$unit_source"
    "$unit_source"
)
destinations=(
    "$HOME/.local/bin/dolphin-plus"
    "$data_home/applications/local.dolphinplus.desktop"
    "$data_home/icons/hicolor/scalable/apps/local.dolphinplus.svg"
    "$data_home/dbus-1/services/org.freedesktop.FileManager1.service"
    "$config_home/systemd/user/$service_unit"
    "$config_home/systemd/user/graphical-session.target.wants/$service_unit"
)
owned_link() {
    [[ -L "$1" && "$(readlink -- "$1")" == "$2" ]]
}
run() {
    printf '+'
    printf ' %q' "$@"
    printf '\n'
    if (( !dry_run )); then
        "$@"
    fi
}

if (( !dry_run )); then
    mkdir -p -- "$build_dir"
    exec 9>"$build_dir/.build-and-install.lock"
    flock -n 9 || fail 'Another install/uninstall operation is already running.'
fi

manage_unit=0
manage_bus=0
daemon_pid=0
if owned_link "${destinations[4]}" "$unit_source" || owned_link "${destinations[5]}" "$unit_source"; then
    command -v systemctl >/dev/null || fail 'systemctl is required to remove the registered background handler.'
    systemctl --user show-environment >/dev/null || fail 'Run from your active desktop user session to stop the background handler safely.'
    manage_unit=1
    fragment=$(systemctl --user show "$service_unit" --property=FragmentPath --value)
    if [[ -n "$fragment" && "$(readlink -m -- "$fragment")" == "$(readlink -m -- "$unit_source")" ]]; then
        daemon_pid=$(systemctl --user show "$service_unit" --property=MainPID --value)
        [[ "$daemon_pid" =~ ^[0-9]+$ ]] || fail 'Could not determine the background handler PID.'
    elif [[ -n "$fragment" ]]; then
        printf 'Leaving unrelated loaded service untouched: %s\n' "$fragment"
    fi
fi
if owned_link "${destinations[3]}" "${sources[3]}"; then
    command -v busctl >/dev/null || fail 'busctl is required to refresh D-Bus activation.'
    busctl --user call org.freedesktop.DBus /org/freedesktop/DBus org.freedesktop.DBus GetId >/dev/null
    manage_bus=1
fi

if process_ids=$(pgrep -u "$UID" -x dolphin-plus); then
    for process_id in $process_ids; do
        if [[ "$process_id" != "$daemon_pid" ]]; then
            if (( dry_run )); then
                printf 'Before uninstalling, close Dolphin Plus GUI/unmanaged processes (PID %s).\n' "$process_id"
            else
                fail 'Close Dolphin Plus GUI windows and unmanaged daemons first; nothing has been removed.'
            fi
        fi
    done
else
    status=$?
    [[ "$status" -eq 1 ]] || fail 'Could not check whether Dolphin Plus is running.'
fi

mime_info=$(LC_ALL=C gio mime inode/directory)
default_line=${mime_info%%$'\n'*}
if [[ "$default_line" == 'Default application for '*': local.dolphinplus.desktop' ]] \
    && owned_link "${destinations[1]}" "${sources[1]}"; then
    IFS=: read -r -a data_dirs <<< "${XDG_DATA_DIRS:-/usr/local/share:/usr/share}"
    stock_available=0
    for directory in "$data_home" "${data_dirs[@]}"; do
        if [[ -f "$directory/applications/org.kde.dolphin.desktop" ]]; then
            stock_available=1
            break
        fi
    done
    (( stock_available )) || fail 'Stock Dolphin is not registered. Choose another default file manager before uninstalling.'
    run gio mime inode/directory org.kde.dolphin.desktop
fi

for index in "${!sources[@]}"; do
    destination=${destinations[index]}
    if owned_link "$destination" "${sources[index]}"; then
        run rm -- "$destination"
    elif [[ -e "$destination" || -L "$destination" ]]; then
        printf 'Preserving unrelated file or link: %s\n' "$destination"
    fi
done
if (( manage_bus )); then
    run busctl --user call org.freedesktop.DBus /org/freedesktop/DBus org.freedesktop.DBus ReloadConfig
fi
if (( daemon_pid > 0 )); then
    run systemctl --user stop "$service_unit"
fi
if (( manage_unit )); then
    run systemctl --user daemon-reload
fi

if (( purge )); then
    purge_paths=(
        "$config_home/dolphinplusrc"
        "$config_home/dolphinplusstaterc"
        "$state_home/dolphinplusstaterc"
        "$data_home/dolphinplus"
        "$state_home/dolphinplus"
        "$cache_home/dolphinplus"
    )
    shopt -s nullglob
    purge_paths+=("$config_home/session"/dolphinplus_*)
    for path in "${purge_paths[@]}"; do
        if [[ -e "$path" || -L "$path" ]]; then
            run rm -rf -- "$path"
        fi
    done
fi
if [[ -d "$data_home/applications" ]]; then
    run update-desktop-database "$data_home/applications"
fi
if command -v kbuildsycoca6 >/dev/null; then
    run kbuildsycoca6 --noincremental
fi
if (( dry_run )); then
    printf '\nDry run complete; no changes made.\n'
else
    printf '\nDolphin Plus user registration removed.\n'
    if (( !purge )); then
        printf 'Settings, bookmarks, sessions, and caches were preserved.\n'
    fi
    printf 'Source checkout and build/staging files remain at %s.\n' "$repo_root"
fi