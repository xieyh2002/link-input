Name:           link-input
Version:        0.1.1
Release:        1%{?dist}
Summary:        Link input method for niri

# Link has no project-wide license declaration. The Wayland protocols use MIT.
License:        LicenseRef-Link-Input AND MIT
URL:            https://github.com/xieyh2002/link-input
Source0:        %{url}/archive/refs/tags/v%{version}.tar.gz#/%{name}-%{version}.tar.gz

BuildRequires:  gcc
BuildRequires:  make
BuildRequires:  pkgconf-pkg-config
BuildRequires:  wayland-devel
BuildRequires:  libxkbcommon-devel
BuildRequires:  cairo-devel
BuildRequires:  pango-devel
BuildRequires:  glib2-devel
BuildRequires:  librime-devel
BuildRequires:  ibus-devel
BuildRequires:  libX11-devel
BuildRequires:  libXext-devel

Requires:       brise
Requires:       gtk4
Requires:       ibus
Requires:       python3-gobject-base
Requires:       systemd
Requires:       util-linux-core
Recommends:     anthy-unicode
Recommends:     google-noto-sans-cjk-fonts
Recommends:     google-noto-color-emoji-fonts

%description
Lightweight Chinese, English and Japanese input for Wayland, with IBus
compatibility for Qt and XWayland.

%prep
%autosetup

%build
%set_build_flags
%make_build all build/qingyin-ibus

%install
%make_install PREFIX=%{_prefix} SYSTEMD_USER_DIR=%{_userunitdir} SETTINGS_COMMAND=link-input

%files
%doc README.md THIRD_PARTY.md
%license protocol/input-method-unstable-v2.xml protocol/virtual-keyboard-unstable-v1.xml
%{_bindir}/link-input
%{_bindir}/qingyin
%{_bindir}/qingyin-ibus
%{_bindir}/qingyin-start-ibus
%{_userunitdir}/qingyin-ibus.service
%{_prefix}/libexec/link-input/
%{_datadir}/applications/io.github.qingyin.LinkSettings.desktop
%{_datadir}/icons/hicolor/scalable/apps/link-input.svg
