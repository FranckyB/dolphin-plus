#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
if [[ -f /.flatpak-info ]]; then
    exec flatpak-spawn --host bash "$repo_root/build-and-install.sh" "$@"
fi

usage() {
    printf '%s\n' \
        'Usage: ./build-and-install.sh [--jobs N] [--test] [--dry-run] [--file-manager-service|--no-file-manager-service]' \
        '' \
        'Build Dolphin Plus and register the staged build for the current user.' \
        'Requires installed Qt/KDE development dependencies; never runs sudo.' \
        '' \
        '  --jobs N   Parallel build jobs (default: 4)' \
        '  --test     Run focused workflow tests in a disposable private session' \
        '  --dry-run  Print commands without building, installing, or registering' \
        '  --help     Show this help' \
        '  --file-manager-service     Enable the user-session Show in Folder handler' \
        '  --no-file-manager-service  Disable that handler and remove its user links' \
        '' \
        'Shared file-manager handling is opt-in and requires a systemd user session.' \
        'Stock Dolphin files and default MIME associations are not changed.' \
        'Keep this checkout in place: desktop launchers link to its staged build.'
}

fail() {
    printf 'Error: %s\n' "$*" >&2
    exit 1
}

jobs=4
run_tests=0
dry_run=0
service_mode=0
while (( $# )); do
    case "$1" in
        --help|-h) usage; exit 0 ;;
        --jobs)
            (( $# >= 2 )) || fail '--jobs requires a positive integer.'
            jobs=$2
            shift
            [[ "$jobs" =~ ^[1-9][0-9]*$ ]] || fail '--jobs requires a positive integer.'
            ;;
        --test) run_tests=1 ;;
        --dry-run) dry_run=1 ;;
        --file-manager-service)
            (( service_mode != -1 )) || fail 'Choose only one file-manager service option.'
            service_mode=1
            ;;
        --no-file-manager-service)
            (( service_mode != 1 )) || fail 'Choose only one file-manager service option.'
            service_mode=-1
            ;;
        *) fail "Unknown argument: $1 (use --help)." ;;
    esac
    shift
done

[[ "$EUID" -ne 0 ]] || fail 'Run this script as your regular desktop user, not root.'
[[ "$(uname -s)" == Linux ]] || fail 'This installer supports Linux desktops.'
[[ -n "${HOME:-}" && "$HOME" == /* ]] || fail 'HOME must be an absolute path.'
data_home=${XDG_DATA_HOME:-$HOME/.local/share}
[[ "$data_home" == /* ]] || fail 'XDG_DATA_HOME must be an absolute path.'
config_home=${XDG_CONFIG_HOME:-$HOME/.config}
[[ "$config_home" == /* ]] || fail 'XDG_CONFIG_HOME must be an absolute path.'
[[ -z "${DESTDIR:-}" ]] || fail 'Unset DESTDIR before this per-user installation.'

for tool in cmake ninja c++ pgrep flock readlink desktop-file-validate update-desktop-database; do
    command -v "$tool" >/dev/null || fail "Missing tool: $tool. Install the build/desktop dependencies first."
done
if (( run_tests )); then
    for tool in dbus-run-session ark bsdtar; do
        command -v "$tool" >/dev/null || fail "--test requires $tool."
    done
fi

cd -- "$repo_root"
build_dir=$repo_root/build-dolphin-plus
stage=$build_dir/stage
sources=(
    "$stage/bin/dolphin-plus"
    "$stage/share/applications/local.dolphinplus.desktop"
    "$stage/share/icons/hicolor/scalable/apps/local.dolphinplus.svg"
)
destinations=(
    "$HOME/.local/bin/dolphin-plus"
    "$data_home/applications/local.dolphinplus.desktop"
    "$data_home/icons/hicolor/scalable/apps/local.dolphinplus.svg"
)

service_unit=dolphinplus-filemanager.service
service_sources=("$stage/share/dolphinplus/org.freedesktop.FileManager1.service" "$stage/share/dolphinplus/$service_unit")
service_destinations=("$data_home/dbus-1/services/org.freedesktop.FileManager1.service" "$config_home/systemd/user/$service_unit")
manage_service=$service_mode
for index in "${!service_sources[@]}"; do
    if [[ -L "${service_destinations[index]}" && "$(readlink -- "${service_destinations[index]}")" == "${service_sources[index]}" ]]; then
        if (( service_mode == 0 )); then
            manage_service=1
        fi
    fi
done
if (( manage_service )); then
    for tool in systemctl busctl; do
        command -v "$tool" >/dev/null || fail "File-manager service management requires $tool."
    done
fi

check_link() {
    local destination=$1 source=$2
    if [[ -L "$destination" ]]; then
        [[ "$(readlink -- "$destination")" == "$source" ]] || fail "Refusing to replace an existing link: $destination"
    elif [[ -e "$destination" ]]; then
        fail "Refusing to replace an existing file: $destination"
    fi
}

check_links() {
    for index in "${!sources[@]}"; do
        check_link "${destinations[index]}" "${sources[index]}"
    done
    if (( manage_service )); then
        for index in "${!service_sources[@]}"; do
            check_link "${service_destinations[index]}" "${service_sources[index]}"
        done
    fi
}

check_stopped() {
    local status
    if pgrep -u "$UID" -x dolphin-plus >/dev/null; then
        fail 'Close Dolphin Plus before building/installing, then rerun this script. Stock Dolphin can stay open.'
    else
        status=$?
        [[ "$status" -eq 1 ]] || fail 'Could not check whether Dolphin Plus is running.'
    fi
}

run() {
    printf '+'
    printf ' %q' "$@"
    printf '\n'
    if (( !dry_run )); then
        "$@"
    fi
}

check_links
service_was_active=0
restore_service() {
    if (( service_was_active )); then
        systemctl --user start "$service_unit" || printf 'Could not restart %s; run systemctl --user start %s.\n' "$service_unit" "$service_unit" >&2
    fi
}
if (( !dry_run )); then
    mkdir -p -- "$build_dir"
    exec 9>"$build_dir/.build-and-install.lock"
    flock -n 9 || fail 'Another build-and-install script is already running.'
    if (( manage_service )); then
        systemctl --user show-environment >/dev/null || fail 'A running systemd user session is required.'
        if systemctl --user is-active --quiet "$service_unit"; then
            [[ -L "${service_destinations[1]}" ]] || fail "Refusing to stop an unmanaged $service_unit."
            service_was_active=1
            trap restore_service EXIT
            systemctl --user stop "$service_unit"
        fi
    fi
    check_stopped
fi

run cmake --preset dolphin-plus-dev "-DCMAKE_INSTALL_PREFIX=$stage"
targets=(dolphin)
have_coexistence=0
if (( run_tests )); then
    targets+=(foldercovertest archiveextractiontest dolphinmainwindowtest)
    if command -v dolphin >/dev/null; then
        targets+=(dolphinpluscoexistencetest)
        have_coexistence=1
    else
        printf '%s\n' 'Stock Dolphin is unavailable; coexistence test will be skipped.'
    fi
fi
run cmake --build --preset dolphin-plus-dev --parallel "$jobs" --target "${targets[@]}"
run desktop-file-validate "$build_dir/src/local.dolphinplus.desktop"

if (( run_tests )); then
    test_home=$build_dir/installer-test.DRY-RUN
    if (( !dry_run )); then
        test_home=$(mktemp -d "$build_dir/installer-test.XXXXXX")
    fi
    test_env=(env "HOME=$test_home" "TMPDIR=$test_home" "XDG_CONFIG_HOME=$test_home/config"
        "XDG_DATA_HOME=$test_home/data" "XDG_CACHE_HOME=$test_home/cache" "XDG_STATE_HOME=$test_home/state"
        QT_QPA_PLATFORM=offscreen)
    printf 'Disposable test files remain under %s\n' "$test_home"
    run "${test_env[@]}" dbus-run-session -- "$build_dir/bin/foldercovertest" -nocrashhandler
    run "${test_env[@]}" dbus-run-session -- "$build_dir/bin/archiveextractiontest" -nocrashhandler
    run "${test_env[@]}" dbus-run-session -- "$build_dir/bin/dolphinmainwindowtest" testGroupFiles testCreateDirectoryFocus testFolderCovers testThumbnailAfterRename -nocrashhandler
    run ctest --test-dir "$build_dir" --output-on-failure --no-tests=error -R '^no_bare_qwait_in_tests$'
    run ctest --test-dir "$build_dir" --output-on-failure --no-tests=error -R '^dolphinplusuninstalltest$'
    if (( have_coexistence )); then
        run "${test_env[@]}" ctest --test-dir "$build_dir" --output-on-failure --no-tests=error -R '^dolphinpluscoexistencetest$'
    fi
fi

if (( !dry_run )); then
    check_links
    check_stopped
fi
run cmake --install "$build_dir"
run desktop-file-validate "$stage/share/applications/local.dolphinplus.desktop"
run "$stage/bin/dolphin-plus" --version
if (( have_coexistence )); then
    run "${test_env[@]}" "DOLPHINPLUS_TEST_EXECUTABLE=$stage/bin/dolphin-plus" \
        ctest --test-dir "$build_dir" --output-on-failure --no-tests=error -R '^dolphinpluscoexistencetest$'
fi

for index in "${!sources[@]}"; do
    destination=${destinations[index]}
    run mkdir -p -- "$(dirname -- "$destination")"
    if [[ ! -L "$destination" ]]; then
        run ln -sT -- "${sources[index]}" "$destination"
    fi
done
if (( manage_service )); then
    if (( service_mode == -1 )); then
        if [[ -L "${service_destinations[1]}" ]]; then
            run systemctl --user disable "$service_unit"
        fi
        for destination in "${service_destinations[@]}"; do
            if [[ -L "$destination" ]]; then
                run rm -- "$destination"
            fi
        done
        service_was_active=0
    else
        for index in "${!service_sources[@]}"; do
            destination=${service_destinations[index]}
            run mkdir -p -- "$(dirname -- "$destination")"
            if [[ ! -L "$destination" ]]; then
                run ln -sT -- "${service_sources[index]}" "$destination"
            fi
        done
    fi
    run systemctl --user daemon-reload
    run busctl --user call org.freedesktop.DBus /org/freedesktop/DBus org.freedesktop.DBus ReloadConfig
    if (( service_mode == 1 )); then
        run systemctl --user enable --now "$service_unit"
        service_was_active=0
    elif (( service_was_active )); then
        run systemctl --user start "$service_unit"
        service_was_active=0
    fi
fi
run update-desktop-database "$data_home/applications"
if command -v kbuildsycoca6 >/dev/null; then
    run kbuildsycoca6 --noincremental
fi

if (( dry_run )); then
    printf '\nDry run complete; no changes made.\n'
else
    printf '\nDolphin Plus is available in your application launcher.\n'
    printf 'Terminal launcher: %s\n' "${destinations[0]}"
    printf 'Keep this checkout in place: %s\n' "$repo_root"
    printf 'Stock Dolphin files and default MIME associations were not changed.\n'
    if (( service_mode == 1 )); then
        printf 'Dolphin Plus now handles shared Show in Folder requests.\n'
    elif (( service_mode == -1 )); then
        printf 'Dolphin Plus shared file-manager handling is disabled.\n'
    fi
fi