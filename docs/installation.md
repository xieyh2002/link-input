# 安装到 niri

建议先用嵌套会话体验。它不修改当前桌面配置，也不占用当前会话的输入法席位：

```sh
make all build/demo-entry
bash demo/start.sh
```

## 安装包

在 [GitHub Releases](https://github.com/xieyh2002/link-input/releases) 下载对应安装包。在下载目录运行（文件名按下载的版本调整）：

Fedora 44（x86_64）：

```sh
sudo dnf install ./link-input-0.1.0-1.fc44.x86_64.rpm
systemctl --user daemon-reload
```

Debian 13（amd64）：

```sh
sudo apt install ./link-input_0.1.0_amd64.deb
systemctl --user daemon-reload
```

安装包提供 `/usr/bin/qingyin`、`qingyin-ibus.service` 和设置命令 `link-input`。它不会编辑 niri 配置或切换当前输入法。接着按下文[启用](#登录后启用)。

如果此前用源码安装过，niri 自启动和 `~/.config/systemd/user/qingyin-ibus.service` 可能仍指向旧二进制。切换到安装包前，先停止旧服务，备份并移开旧的用户服务文件，再重新加载用户服务并更新自启动路径。已有设置和词库继续使用。

在 Debian 13 上也可以自行打包：

```sh
sudo apt install build-essential pkg-config libwayland-dev libxkbcommon-dev \
  libcairo2-dev libpango1.0-dev libglib2.0-dev librime-dev libibus-1.0-dev \
  libx11-dev libxext-dev
bash packaging/deb/build.sh 0.1.0
```

生成的安装包位于 `dist/`。

## 依赖

以下是 Fedora 上的示例。其他发行版需要对应的开发文件和运行库。

```sh
sudo dnf install gcc make pkgconf-pkg-config wayland-devel libxkbcommon-devel \
  cairo-devel pango-devel glib2-devel librime-devel brise \
  ibus ibus-devel libX11-devel libXext-devel gtk4 gtk4-devel python3-gobject-base
# 可选：日文转换和彩色表情
sudo dnf install anthy-unicode google-noto-color-emoji-fonts
```

只构建核心程序时不需要 GTK 4 或 IBus 开发文件。日文转换优先使用 `libanthyinput-unicode.so.0`，也支持 Debian 13 的 `libanthyinput.so.0`。可用 `QINGYIN_RIME_DATA` 指定 Rime 共享方案目录；全拼需要 `luna_pinyin_simp`，小鹤双拼需要 `double_pinyin_flypy` 及它们的词典。

## 从源码安装

在源码目录以普通用户身份运行：

```sh
make install
systemctl --user daemon-reload
```

默认安装到 `~/.local`，并写入用户级 IBus 兼容服务文件。此步骤不会编辑 niri 配置或立即切换输入法。需保证 `~/.local/bin` 在终端的 `PATH` 内。输入 `link` 或在应用菜单打开「link输入法设置」；`link 文件1 文件2` 仍转交给系统的硬链接命令。

## 登录后启用

备份现有 niri 配置，然后将以下条目合并到配置中。已有 `environment` 块时只合并键值。

```kdl
environment {
    GTK_IM_MODULE
    QT_IM_MODULE "ibus"
    XMODIFIERS "@im=ibus"
}

spawn-at-startup "sh" "-c" "exec \"$HOME/.local/bin/qingyin\""
spawn-at-startup "systemctl" "--user" "start" "qingyin-ibus.service"
```

若使用发行版安装包，把上面的第一条启动项替换为 `spawn-at-startup "/usr/bin/qingyin"`。

- 原生 Wayland 应用使用输入法协议。同一个 Wayland 席位只允许运行一个输入法，请停止会抢占该席位的其他输入法及其 niri 自启动项。
- IBus 兼容服务使用系统的 IBus 守护进程传递 Qt/XIM 输入。服务会尝试选择 link 输入法作为 IBus 当前引擎。若已经有 IBus 守护进程，它会复用；此时 XIM 是否可用取决于已有守护进程是否以 XIM 支持启动。
- XWayland GTK 应用若没有自动选择 IBus，可以用 `GTK_IM_MODULE=ibus` 单独启动该应用。原生 Wayland GTK 一般应保留原生输入路径，避免全局设置 `GTK_IM_MODULE=ibus`。
- 检查并重新登录 niri，使应用读取新的输入法环境变量。已运行的应用需要重开。

## 更新和恢复

更新源码后重新运行 `make install`。当前会话可打开 `link`，点「保存并应用」，以重新连接两个输入接口。

若要恢复原输入法，先移除上面的 niri 启动项和环境变量改动，再恢复原来的输入法自启动并重新登录。用户设置和本地 Rime 词库不会被源码安装覆盖。

## 数据位置

- 设置：`${XDG_CONFIG_HOME:-~/.config}/link-input/settings.ini`
- 原生 Wayland 的 Rime 数据：`${XDG_DATA_HOME:-~/.local/share}/qingyin/rime`
- IBus 的 Rime 数据：`${XDG_DATA_HOME:-~/.local/share}/qingyin/rime-ibus`

项目安装保留 `qingyin` 作为内部程序名和引擎 ID。两条路径目前各自保存 Rime 学习数据。自定义 Rime 文件由用户保管；安装不会改写不属于 link 管理的默认 Rime 配置。
