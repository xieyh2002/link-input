#!/bin/bash
set -eu
if [[ ":${XDG_CURRENT_DESKTOP:-}:" != *:niri:* || -z "${WAYLAND_DISPLAY:-}" ]]; then exit 0; fi
runtime=${XDG_RUNTIME_DIR:?}
exec 9>"$runtime/qingyin-ibus.lock"
flock -n 9 || exit 0
engine_pid=
daemon_pid=
cleanup() {
    if [[ -n "$engine_pid" ]]; then kill "$engine_pid" 2>/dev/null || true; fi
    if [[ -n "$daemon_pid" ]]; then kill "$daemon_pid" 2>/dev/null || true; fi
}
trap cleanup EXIT
trap 'exit 0' TERM INT
if ! ibus list-engine >/dev/null 2>&1; then
    GDK_BACKEND=x11 ibus-daemon --desktop=niri --xim --panel=disable --emoji-extension=disable 9>&- &
    daemon_pid=$!
    for ((i=0; i<40; i++)); do
        if ibus address 2>/dev/null | grep -q '^unix:'; then break; fi
        kill -0 "$daemon_pid" 2>/dev/null || exit 1
        sleep .1
    done
fi
"$HOME/.local/bin/qingyin-ibus" 9>&- &
engine_pid=$!
for ((i=0; i<600; i++)); do
    kill -0 "$engine_pid" 2>/dev/null || exit 1
    if ibus engine qingyin >/dev/null 2>&1; then
        systemctl --user set-environment QT_IM_MODULE=ibus XMODIFIERS=@im=ibus
        wait "$engine_pid" || true
        exit 1
    fi
    sleep .1
done
exit 1
