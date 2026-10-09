#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd -P)
if [[ -f /.flatpak-info ]]; then
    exec flatpak-spawn --host bash "$repo_root/src/tests/uninstalltest.sh" "$@"
fi
sandbox=$(mktemp -d)
trap 'rm -rf -- "$sandbox"' EXIT
mkdir -p "$sandbox/bin"
cat > "$sandbox/bin/mock" <<'MOCK'
#!/usr/bin/env bash
set -euo pipefail
tool=${0##*/}
case "$tool" in
    gio)
        if (( $# == 2 )); then
            printf 'Default application for inode/directory: %s\n' "$(<"$TEST_DEFAULT_FILE")"
        else
            printf '%s\n' "$3" > "$TEST_DEFAULT_FILE"
            printf 'default:%s\n' "$3" >> "$TEST_LOG"
        fi
        ;;
    systemctl)
        case "$2" in
            show-environment) ;;
            show)
                case "$4" in
                    --property=FragmentPath) printf '%s\n' "${TEST_UNIT_FRAGMENT:-$TEST_STAGE/share/dolphinplus/dolphinplus-filemanager.service}" ;;
                    --property=MainPID) cat "$TEST_PID_FILE" ;;
                    *) exit 2 ;;
                esac
                ;;
            stop)
                printf 'stop\n' >> "$TEST_LOG"
                printf '0\n' > "$TEST_PID_FILE"
                ;;
            daemon-reload) printf 'unit-reload\n' >> "$TEST_LOG" ;;
            *) exit 2 ;;
        esac
        ;;
    pgrep)
        found=0
        if [[ -n "${TEST_GUI_PID:-}" ]]; then
            printf '%s\n' "$TEST_GUI_PID"
            found=1
        fi
        if [[ "$(<"$TEST_PID_FILE")" != 0 ]]; then
            cat "$TEST_PID_FILE"
            found=1
        fi
        (( found ))
        ;;
    busctl)
        if [[ "${*: -1}" == GetId ]]; then
            printf 'test-bus\n'
        else
            printf 'bus-reload\n' >> "$TEST_LOG"
        fi
        ;;
    update-desktop-database|kbuildsycoca6) printf 'cache-refresh\n' >> "$TEST_LOG" ;;
    *) exit 2 ;;
esac
MOCK
chmod +x "$sandbox/bin/mock"
for tool in gio systemctl pgrep busctl update-desktop-database kbuildsycoca6; do
    ln -s mock "$sandbox/bin/$tool"
done
export PATH="$sandbox/bin:$PATH"

fixture() {
    case_root=$sandbox/$1
    export HOME=$case_root/home
    export XDG_DATA_HOME=$case_root/data XDG_CONFIG_HOME=$case_root/config
    export XDG_CACHE_HOME=$case_root/cache XDG_STATE_HOME=$case_root/state
    export TEST_STAGE=$case_root/checkout/build-dolphin-plus/stage
    export TEST_LOG=$case_root/calls TEST_DEFAULT_FILE=$case_root/default TEST_PID_FILE=$case_root/pid
    unset TEST_GUI_PID TEST_UNIT_FRAGMENT DESTDIR
    mkdir -p "$case_root/checkout" "$HOME/.local/bin" "$XDG_DATA_HOME/applications" \
        "$XDG_DATA_HOME/icons/hicolor/scalable/apps" "$XDG_DATA_HOME/dbus-1/services" \
        "$XDG_CONFIG_HOME/systemd/user/graphical-session.target.wants" "$XDG_CONFIG_HOME/session" \
        "$XDG_DATA_HOME/dolphinplus" "$XDG_CACHE_HOME/dolphinplus" "$XDG_STATE_HOME/dolphinplus" \
        "$TEST_STAGE/bin" "$TEST_STAGE/share/applications" "$TEST_STAGE/share/icons/hicolor/scalable/apps" \
        "$TEST_STAGE/share/dolphinplus" "$case_root/browsed-folder"
    cp -- "$repo_root/uninstall.sh" "$case_root/checkout/uninstall.sh"
    printf 'local.dolphinplus.desktop\n' > "$TEST_DEFAULT_FILE"
    printf '654321\n' > "$TEST_PID_FILE"
    : > "$TEST_LOG"
    sources=("$TEST_STAGE/bin/dolphin-plus" "$TEST_STAGE/share/applications/local.dolphinplus.desktop"
        "$TEST_STAGE/share/icons/hicolor/scalable/apps/local.dolphinplus.svg"
        "$TEST_STAGE/share/dolphinplus/org.freedesktop.FileManager1.service"
        "$TEST_STAGE/share/dolphinplus/dolphinplus-filemanager.service"
        "$TEST_STAGE/share/dolphinplus/dolphinplus-filemanager.service")
    links=("$HOME/.local/bin/dolphin-plus" "$XDG_DATA_HOME/applications/local.dolphinplus.desktop"
        "$XDG_DATA_HOME/icons/hicolor/scalable/apps/local.dolphinplus.svg"
        "$XDG_DATA_HOME/dbus-1/services/org.freedesktop.FileManager1.service"
        "$XDG_CONFIG_HOME/systemd/user/dolphinplus-filemanager.service"
        "$XDG_CONFIG_HOME/systemd/user/graphical-session.target.wants/dolphinplus-filemanager.service")
    for index in "${!links[@]}"; do
        printf 'fixture\n' > "${sources[index]}"
        ln -s -- "${sources[index]}" "${links[index]}"
    done
    printf 'stock\n' > "$XDG_DATA_HOME/applications/org.kde.dolphin.desktop"
    for path in "$XDG_CONFIG_HOME/dolphinplusrc" "$XDG_CONFIG_HOME/dolphinrc" \
        "$XDG_CONFIG_HOME/session/dolphinplus_test" "$XDG_CONFIG_HOME/session/dolphin_test" \
        "$XDG_DATA_HOME/dolphinplus/bookmarks.xml" "$XDG_CACHE_HOME/dolphinplus/cover.png" \
        "$XDG_STATE_HOME/dolphinplusstaterc" "$case_root/browsed-folder/.directory"; do
        printf 'preserve\n' > "$path"
    done
}
uninstall() {
    bash "$case_root/checkout/uninstall.sh" "$@" > "$case_root/output" 2>&1
}
assert_links() {
    for path in "${links[@]}"; do
        [[ -L "$path" ]]
    done
}
assert_removed() {
    for path in "${links[@]}"; do
        [[ ! -e "$path" && ! -L "$path" ]]
    done
}

fixture dry-run
uninstall --dry-run --purge
assert_links
[[ ! -s "$TEST_LOG" && -f "$XDG_CONFIG_HOME/dolphinplusrc" ]]
[[ "$(<"$TEST_DEFAULT_FILE")" == local.dolphinplus.desktop ]]
printf 'PASS: dry-run makes no changes\n'

fixture normal
uninstall
assert_removed
[[ "$(<"$TEST_DEFAULT_FILE")" == org.kde.dolphin.desktop ]]
[[ -f "$XDG_CONFIG_HOME/dolphinplusrc" && -f "$XDG_DATA_HOME/dolphinplus/bookmarks.xml" ]]
[[ -f "$XDG_CACHE_HOME/dolphinplus/cover.png" && -f "$TEST_STAGE/bin/dolphin-plus" ]]
grep -q '^stop$' "$TEST_LOG"
[[ "$(sed -n '2,3p' "$TEST_LOG")" == $'bus-reload\nstop' ]]
uninstall
[[ "$(grep -c '^default:' "$TEST_LOG")" == 1 ]]
printf 'PASS: normal removal preserves data and is repeatable\n'

fixture unrelated
printf 'other.desktop\n' > "$TEST_DEFAULT_FILE"
rm -- "${links[0]}" "${links[3]}"
ln -s /dev/null "${links[0]}"
printf 'unrelated service\n' > "${links[3]}"
uninstall
[[ "$(readlink "${links[0]}")" == /dev/null ]]
[[ "$(<"${links[3]}")" == 'unrelated service' ]]
[[ "$(<"$TEST_DEFAULT_FILE")" == other.desktop ]]
printf 'PASS: unrelated files, links, and defaults are preserved\n'

fixture other-checkout
rm -- "${links[1]}"
ln -s "$case_root/another-checkout.desktop" "${links[1]}"
uninstall
[[ -L "${links[1]}" && "$(<"$TEST_DEFAULT_FILE")" == local.dolphinplus.desktop ]]
printf 'PASS: another checkout keeps its desktop registration and default\n'

fixture purge
mkdir -p "$case_root/external"
printf 'preserve\n' > "$case_root/external/keep"
rm -rf -- "$XDG_STATE_HOME/dolphinplus"
ln -s "$case_root/external" "$XDG_STATE_HOME/dolphinplus"
uninstall --purge
assert_removed
[[ ! -e "$XDG_CONFIG_HOME/dolphinplusrc" && ! -e "$XDG_DATA_HOME/dolphinplus" ]]
[[ ! -e "$XDG_CACHE_HOME/dolphinplus" && ! -e "$XDG_STATE_HOME/dolphinplusstaterc" ]]
[[ ! -e "$XDG_CONFIG_HOME/session/dolphinplus_test" && -f "$XDG_CONFIG_HOME/session/dolphin_test" ]]
[[ -f "$XDG_CONFIG_HOME/dolphinrc" && -f "$case_root/browsed-folder/.directory" ]]
[[ -f "$case_root/external/keep" && -f "$TEST_STAGE/bin/dolphin-plus" ]]
printf 'PASS: purge is scoped and does not follow data symlinks\n'

fixture open-window
export TEST_GUI_PID=123456
if uninstall; then exit 1; fi
assert_links
[[ ! -s "$TEST_LOG" && "$(<"$TEST_DEFAULT_FILE")" == local.dolphinplus.desktop ]]
printf 'PASS: open GUI blocks removal before any changes\n'

fixture foreign-unit
export TEST_UNIT_FRAGMENT=$case_root/foreign.service
if uninstall; then exit 1; fi
assert_links
[[ ! -s "$TEST_LOG" ]]
printf 'PASS: unrelated loaded service is never stopped\n'

fixture broken-links
rm -rf -- "$TEST_STAGE"
uninstall
assert_removed
printf 'PASS: matching dangling links can be removed\n'

fixture missing-stock
rm -- "$XDG_DATA_HOME/applications/org.kde.dolphin.desktop"
export XDG_DATA_DIRS=$case_root/empty-system-data
if uninstall; then exit 1; fi
assert_links
[[ ! -s "$TEST_LOG" ]]
unset XDG_DATA_DIRS
printf 'PASS: missing replacement default blocks removal safely\n'

fixture locked
exec 8>"$case_root/checkout/build-dolphin-plus/.build-and-install.lock"
flock -n 8
if uninstall; then exit 1; fi
exec 8>&-
assert_links
[[ ! -s "$TEST_LOG" ]]
printf 'PASS: installer lock prevents concurrent removal\n'