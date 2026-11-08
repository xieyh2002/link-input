#!/bin/bash
set -euo pipefail

if [[ $# != 1 || ! $1 =~ ^[0-9]+\.[0-9]+\.[0-9]+([+~.-][a-zA-Z0-9.+~]+)?$ ]]; then
    printf '%s\n' '用法：bash packaging/deb/build.sh 0.1.0' >&2
    exit 2
fi
version=$1
. /etc/os-release
if [[ $ID != debian || ${VERSION_ID:-} != 13 ]]; then
    printf '%s\n' '请在 Debian 13 环境中构建此安装包。' >&2
    exit 1
fi

root=$(cd -- "$(dirname -- "$0")/../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
stage="$work/stage"
arch=$(dpkg --print-architecture)
make -C "$root" -B install DESTDIR="$stage" PREFIX=/usr \
    SYSTEMD_USER_DIR=/usr/lib/systemd/user SETTINGS_COMMAND=link-input

mkdir -p "$work/debian" "$stage/DEBIAN" "$root/dist"
cat > "$work/debian/control" <<'CONTROL'
Source: link-input
Section: utils
Priority: optional
Maintainer: Link Input <92992753+xieyh2002@users.noreply.github.com>

Package: link-input
Architecture: any
Description: Link input method for niri
CONTROL
libs=$(cd "$work"; dpkg-shlibdeps -O -e"$stage/usr/bin/qingyin" -e"$stage/usr/bin/qingyin-ibus")
libs=${libs#shlibs:Depends=}

cat > "$stage/DEBIAN/control" <<CONTROL
Package: link-input
Version: $version
Architecture: $arch
Maintainer: Link Input <92992753+xieyh2002@users.noreply.github.com>
Section: utils
Priority: optional
Installed-Size: $(du -sk "$stage/usr" | cut -f1)
Depends: $libs, bash, librime-data, rime-data-luna-pinyin, rime-data-double-pinyin, ibus, python3, python3-gi, gir1.2-gtk-4.0, systemd, util-linux
Recommends: libanthyinput0t64, fonts-noto-cjk, fonts-noto-color-emoji
Homepage: https://github.com/xieyh2002/link-input
Description: Link input method for niri
 Lightweight Chinese, English and Japanese input for Wayland, with IBus
 compatibility for Qt and XWayland.
CONTROL

dpkg-deb --root-owner-group --build "$stage" "$root/dist/link-input_${version}_${arch}.deb"
