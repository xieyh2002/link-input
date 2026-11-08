#!/usr/bin/env bash
set -eu
cd -- "$(dirname -- "$0")/.."

if [[ ! -x build/qingyin || ! -x build/demo-entry ]]; then
    printf '%s\n' '请先运行 make all build/demo-entry。' >&2
    exit 1
fi
if [[ -z ${WAYLAND_DISPLAY:-} ]]; then
    printf '%s\n' '请从当前 Wayland 桌面启动试用窗口。' >&2
    exit 1
fi

export XDG_DATA_HOME="${XDG_CACHE_HOME:-$HOME/.cache}/qingyin/demo-data"
export QINGYIN_PARENT_DISPLAY=$WAYLAND_DISPLAY
export QINGYIN_PARENT_SOCKET=${NIRI_SOCKET:-}
unset NIRI_SOCKET GTK_IM_MODULE QT_IM_MODULE
exec niri -c "$PWD/demo/niri.kdl" -- bash -c '
    set -eu
    if [[ -z ${NIRI_SOCKET:-} || $NIRI_SOCKET == "$QINGYIN_PARENT_SOCKET" ||
          $WAYLAND_DISPLAY == "$QINGYIN_PARENT_DISPLAY" ]]; then
        printf "%s\n" "未进入隔离的 niri 会话。" >&2
        exit 1
    fi
    ./build/qingyin &
    ime=$!
    trap '\''kill "$ime" 2>/dev/null || true'\'' EXIT
    GTK_IM_MODULE=wayland GDK_BACKEND=wayland ./build/demo-entry
    niri msg action quit --skip-confirmation
'
