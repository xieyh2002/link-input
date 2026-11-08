PKGS = wayland-client xkbcommon pangocairo gio-2.0
RIME_CFLAGS ?= $(shell pkg-config --cflags rime 2>/dev/null)
RIME_LIBS ?= $(shell pkg-config --libs rime 2>/dev/null || echo -lrime)
CPPFLAGS += -Ibuild $(shell pkg-config --cflags $(PKGS)) $(RIME_CFLAGS)
CFLAGS ?= -O2 -g
CFLAGS += -std=c11 -Wall -Wextra -Wno-unused-parameter
LDLIBS += $(shell pkg-config --libs $(PKGS)) $(RIME_LIBS) -lm
PROTOCOLS = input-method-unstable-v2 virtual-keyboard-unstable-v1
HEADERS = $(PROTOCOLS:%=build/%.h)
SOURCES = $(PROTOCOLS:%=build/%.c)

all: build/qingyin

build:
	mkdir -p $@

build/%.h: protocol/%.xml | build
	wayland-scanner client-header $< $@

build/%.c: protocol/%.xml | build
	wayland-scanner private-code $< $@

build/qingyin: main.c panel.c panel.h emoji.h theme.c theme.h input-mode.h japanese.c japanese.h settings.c settings.h $(HEADERS) $(SOURCES)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ main.c panel.c theme.c japanese.c settings.c $(SOURCES) $(LDLIBS) -ldl

clean:
	rm -rf build

.PHONY: all clean

build/demo-entry: demo/probe.c | build
	$(CC) $(CFLAGS) -o $@ $< $(shell pkg-config --cflags --libs gtk4)

build/panel-preview: panel.c panel.h settings.c settings.h | build
	$(CC) $(CFLAGS) -DPANEL_PREVIEW -o $@ panel.c settings.c $(shell pkg-config --cflags --libs pangocairo)

preview: build/panel-preview
	cd build && ./panel-preview && ./panel-preview --dark

.PHONY: preview

IBUS_CFLAGS ?= $(shell pkg-config --cflags ibus-1.0)
IBUS_LIBS ?= $(shell pkg-config --libs ibus-1.0)

X11_PANEL_PKGS = cairo-xlib x11 xext
build/qingyin-ibus: ibus.c x11-panel.c x11-panel.h panel.c panel.h emoji.h theme.c theme.h input-mode.h japanese.c japanese.h settings.c settings.h | build
	$(CC) $(CPPFLAGS) $(IBUS_CFLAGS) $(shell pkg-config --cflags $(X11_PANEL_PKGS)) $(CFLAGS) $(LDFLAGS) -o $@ ibus.c x11-panel.c panel.c theme.c japanese.c settings.c $(LDLIBS) -ldl $(IBUS_LIBS) $(shell pkg-config --libs $(X11_PANEL_PKGS))

PREFIX ?= $(HOME)/.local
SYSTEMD_USER_DIR ?= $(HOME)/.config/systemd/user
install-settings: integration/link integration/link-settings.py integration/link-input-apply integration/io.github.qingyin.LinkSettings.desktop assets/icon.svg
	install -Dm755 integration/link "$(DESTDIR)$(PREFIX)/bin/link"
	install -Dm644 integration/link-settings.py "$(DESTDIR)$(PREFIX)/libexec/link-input/link-settings.py"
	install -Dm755 integration/link-input-apply "$(DESTDIR)$(PREFIX)/libexec/link-input/link-input-apply"

	install -Dm644 assets/icon.svg "$(DESTDIR)$(PREFIX)/share/icons/hicolor/scalable/apps/link-input.svg"
	install -Dm644 integration/io.github.qingyin.LinkSettings.desktop "$(DESTDIR)$(PREFIX)/share/applications/io.github.qingyin.LinkSettings.desktop"
	sed -i 's|^Exec=link --settings$$|Exec="$(PREFIX)/bin/link" --settings|' "$(DESTDIR)$(PREFIX)/share/applications/io.github.qingyin.LinkSettings.desktop"

install: build/qingyin build/qingyin-ibus install-settings
	install -Dm755 build/qingyin "$(DESTDIR)$(PREFIX)/bin/qingyin"
	install -Dm755 build/qingyin-ibus "$(DESTDIR)$(PREFIX)/bin/qingyin-ibus"
	install -Dm755 integration/start-ibus.sh "$(DESTDIR)$(PREFIX)/bin/qingyin-start-ibus"
	install -Dm644 integration/qingyin-ibus.service "$(DESTDIR)$(SYSTEMD_USER_DIR)/qingyin-ibus.service"

.PHONY: install install-settings
