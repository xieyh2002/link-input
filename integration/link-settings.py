#!/usr/bin/python3
import configparser
import os
import re
import sys
import tempfile
from pathlib import Path

import gi

gi.require_version("Gtk", "4.0")
gi.require_version("Gdk", "4.0")
from gi.repository import Gdk, Gio, GLib, Gtk


DEFAULTS = {
    "Input": {
        "language": "zh",
        "chinese": "full",
        "japanese": "true",
        "candidates": "7",
    },
    "Appearance": {
        "theme": "system",
    },
    "Light": {
        "background": "#ffffff",
        "text": "#1c293b",
        "selected": "#007aff",
        "border": "#d1d1d1",
    },
    "Dark": {
        "background": "#1f2126",
        "text": "#f0f2f7",
        "selected": "#007aff",
        "border": "#4d525e",
    },
}

COLOR_KEYS = ("background", "text", "selected", "border")
COLOR_LABELS = {
    "background": "背景",
    "text": "文字",
    "selected": "选中",
    "border": "边框",
}
CHOICES = {
    "language": (("简体中文", "zh"), ("English", "en"), ("日本語", "ja")),
    "chinese": (("全拼", "full"), ("双拼", "double")),
    "theme": (("跟随系统", "system"), ("浅色", "light"), ("深色", "dark")),
}
VALUE_BY_LABEL = {
    key: {text: value for text, value in choices}
    for key, choices in CHOICES.items()
}


def config_path():
    base = os.environ.get("XDG_CONFIG_HOME")
    return Path(base).expanduser() / "link-input" / "settings.ini" if base else Path.home() / ".config" / "link-input" / "settings.ini"


def normalize_color(value, fallback):
    value = (value or "").strip().lower()
    return value if re.fullmatch(r"#[0-9a-f]{6}", value) else fallback


def normalize_choice(value, key, fallback):
    value = (value or "").strip().lower()
    return value if value in dict(CHOICES[key]).values() else fallback


def normalize_bool(value, fallback):
    value = (value or "").strip().lower()
    return value if value in {"true", "false"} else fallback


def normalize_candidates(value, fallback):
    try:
        value = int(value)
    except (TypeError, ValueError):
        return fallback
    return str(value) if 1 <= value <= 9 else fallback


def read_settings(path):
    parser = configparser.ConfigParser()
    if path.exists():
        try:
            parser.read(path, encoding="utf-8")
        except configparser.Error:
            pass
    result = {section: values.copy() for section, values in DEFAULTS.items()}
    for section, values in result.items():
        if not parser.has_section(section):
            continue
        for key, default in values.items():
            if key in COLOR_KEYS:
                values[key] = normalize_color(parser.get(section, key, fallback=default), default)
            elif key in CHOICES:
                values[key] = normalize_choice(parser.get(section, key, fallback=default), key, default)
            elif key == "japanese":
                values[key] = normalize_bool(parser.get(section, key, fallback=default), default)
            else:
                values[key] = normalize_candidates(parser.get(section, key, fallback=default), default)
    if result["Input"]["japanese"] == "false" and result["Input"]["language"] == "ja":
        result["Input"]["language"] = "zh"
    return result


def atomic_save(path, settings):
    parser = configparser.ConfigParser()
    parser.read_dict(settings)
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = tempfile.mkstemp(prefix=".settings.ini-", dir=path.parent)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8") as stream:
            parser.write(stream, space_around_delimiters=False)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary_name, path)
        directory = os.open(path.parent, os.O_RDONLY | os.O_DIRECTORY)
        try:
            os.fsync(directory)
        finally:
            os.close(directory)
    except BaseException:
        try:
            os.unlink(temporary_name)
        except FileNotFoundError:
            pass
        raise


class SettingsWindow(Gtk.ApplicationWindow):
    def __init__(self, application):
        super().__init__(application=application, title="link输入法 设置", default_width=760, default_height=640)
        self.set_icon_name("link-input")
        self.settings = read_settings(CONFIG_PATH)
        self.choice_buttons = {"language": [], "chinese": [], "theme": []}
        self.japanese_switch = None
        self.candidates_spin = None
        self.color_buttons = {"Light": {}, "Dark": {}}
        self.apply_process = None

        root = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=0)
        content = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=24)
        content.set_margin_top(28)
        content.set_margin_bottom(28)
        content.set_margin_start(28)
        content.set_margin_end(28)
        root.append(content)

        title = Gtk.Label(label="link输入法 设置", xalign=0)
        title.add_css_class("title-2")
        content.append(title)

        input_group = self.make_group("输入")
        self.make_choices(input_group, "默认语言", "language")
        self.make_choices(input_group, "中文方案", "chinese")
        self.make_switch(input_group, "启用日语", "japanese")
        self.make_spin(input_group, "每页候选词数量", "candidates", 1, 9)
        content.append(input_group)

        appearance_group = self.make_group("外观")
        self.make_choices(appearance_group, "主题", "theme")
        content.append(appearance_group)

        colors_group = self.make_group("颜色")
        self.make_colors(colors_group, "浅色模式", "Light")
        self.make_colors(colors_group, "深色模式", "Dark")
        content.append(colors_group)

        bottom = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=12, hexpand=True)
        reset = Gtk.Button(label="恢复默认")
        reset.add_css_class("flat")
        reset.connect("clicked", self.reset_defaults)
        self.status = Gtk.Label(label="", xalign=0, hexpand=True)
        self.status.add_css_class("dim-label")
        self.status.set_ellipsize(3)
        apply = self.apply_button = Gtk.Button(label="保存并应用")
        apply.add_css_class("suggested-action")
        apply.connect("clicked", self.apply_settings)
        bottom.append(reset)
        bottom.append(self.status)
        bottom.append(apply)
        bottom.set_margin_top(12)
        bottom.set_margin_bottom(12)
        bottom.set_margin_start(20)
        bottom.set_margin_end(20)

        scrolled = Gtk.ScrolledWindow(child=root, hscrollbar_policy=Gtk.PolicyType.NEVER)
        scrolled.set_vexpand(True)
        layout = Gtk.Box(orientation=Gtk.Orientation.VERTICAL)
        layout.append(scrolled)
        layout.append(Gtk.Separator())
        layout.append(bottom)
        self.set_child(layout)
        self.update_language_sensitivity()

    def make_group(self, title):
        group = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=14)
        label = Gtk.Label(label=title, xalign=0)
        label.add_css_class("title-4")
        label.add_css_class("dim-label")
        group.append(label)
        frame = Gtk.Frame()
        list_box = Gtk.ListBox(selection_mode=Gtk.SelectionMode.NONE)
        list_box.add_css_class("boxed-list")
        frame.set_child(list_box)
        group.append(frame)
        group.list_box = list_box
        return group

    def make_row(self):
        row = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=16)
        row.set_margin_top(14)
        row.set_margin_bottom(14)
        row.set_margin_start(18)
        row.set_margin_end(18)
        return row

    def make_label(self, text):
        label = Gtk.Label(label=text, xalign=0, hexpand=True)
        label.add_css_class("body")
        return label

    def make_choices(self, group, title, key):
        row = self.make_row()
        row.append(self.make_label(title))
        chooser = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=0)
        chooser.add_css_class("linked")
        first_button = None
        for text, value in CHOICES[key]:
            button = Gtk.CheckButton(label=text)
            if self.settings["Input" if key in ("language", "chinese") else "Appearance"][key] == value:
                button.set_active(True)
            if first_button is None:
                first_button = button
            else:
                button.set_group(first_button)
            button.connect("toggled", self.choose_value, key, value)
            chooser.append(button)
            self.choice_buttons[key].append(button)
        row.append(chooser)
        group.list_box.append(Gtk.ListBoxRow(child=row))

    def make_switch(self, group, title, key):
        row = self.make_row()
        row.append(self.make_label(title))
        switch = Gtk.Switch(active=self.settings["Input"][key] == "true", valign=Gtk.Align.CENTER)
        self.japanese_switch = switch
        switch.connect("state-set", self.switch_value, key)
        row.append(switch)
        group.list_box.append(Gtk.ListBoxRow(child=row))

    def make_spin(self, group, title, key, lower, upper):
        row = self.make_row()
        row.append(self.make_label(title))
        adjustment = Gtk.Adjustment(value=int(self.settings["Input"][key]), lower=lower, upper=upper, step_increment=1, page_increment=1)
        spinner = Gtk.SpinButton(adjustment=adjustment, climb_rate=1.0, digits=0, valign=Gtk.Align.CENTER)
        self.candidates_spin = spinner
        spinner.connect("value-changed", self.spin_value, key)
        row.append(spinner)
        group.list_box.append(Gtk.ListBoxRow(child=row))

    def make_colors(self, group, title, section):
        row = self.make_row()
        label = self.make_label(title)
        label.set_valign(Gtk.Align.CENTER)
        row.append(label)
        palette = Gtk.FlowBox(selection_mode=Gtk.SelectionMode.NONE, homogeneous=True, min_children_per_line=4, max_children_per_line=4, row_spacing=8, column_spacing=8)
        for key in COLOR_KEYS:
            item = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=4)
            picker = Gtk.ColorDialogButton(valign=Gtk.Align.CENTER, hexpand=True)
            picker.set_size_request(60, -1)
            picker.set_dialog(Gtk.ColorDialog(with_alpha=False, title=f"选择{COLOR_LABELS[key]}颜色"))
            picker.set_rgba(self.gdk_color(self.settings[section][key]))
            self.color_buttons[section][key] = picker
            picker.connect("notify::rgba", self.color_changed, section, key)
            label = Gtk.Label(label=COLOR_LABELS[key])
            label.add_css_class("caption")
            label.add_css_class("dim-label")
            item.append(picker)
            item.append(label)
            palette.append(item)
        row.append(palette)
        group.list_box.append(Gtk.ListBoxRow(child=row))

    @staticmethod
    def gdk_color(value):
        rgba = Gdk.RGBA()
        if not rgba.parse(value):
            rgba.parse("#000000")
        return rgba

    def choose_value(self, button, key, value):
        if not button.get_active():
            return
        section = "Input" if key in ("language", "chinese") else "Appearance"
        self.settings[section][key] = value
        self.set_status("设置已更新，点击应用生效")

    def update_language_sensitivity(self):
        allowed = self.settings["Input"]["japanese"] == "true"
        for button in self.choice_buttons["language"]:
            if VALUE_BY_LABEL["language"][button.get_label()] == "ja":
                button.set_sensitive(allowed)

    def switch_value(self, switch, state, key):
        self.settings["Input"][key] = "true" if state else "false"
        if not state and self.settings["Input"]["language"] == "ja":
            self.set_choice("language", "zh")
        self.update_language_sensitivity()
        self.set_status("设置已更新，点击「保存并应用」生效")
        return False

    def set_choice(self, key, value):
        for button in self.choice_buttons[key]:
            if VALUE_BY_LABEL[key][button.get_label()] == value:
                button.set_active(True)
                break

    def spin_value(self, spinner, key):
        self.settings["Input"][key] = str(int(spinner.get_value()))
        self.set_status("设置已更新，点击应用生效")

    def color_changed(self, picker, _parameter, section, key):
        rgba = picker.get_rgba()
        self.settings[section][key] = "#%02x%02x%02x" % (round(rgba.red * 255), round(rgba.green * 255), round(rgba.blue * 255))
        self.set_status("设置已更新，点击应用生效")

    def reset_defaults(self, _button):
        self.settings = {section: values.copy() for section, values in DEFAULTS.items()}
        self.refresh_ui()
        self.set_status("已载入默认值，点击应用生效")

    def refresh_ui(self):
        for key, choices in CHOICES.items():
            section = "Input" if key in ("language", "chinese") else "Appearance"
            for button in self.choice_buttons[key]:
                button.set_active(self.settings[section][key] == VALUE_BY_LABEL[key][button.get_label()])
        self.japanese_switch.set_active(self.settings["Input"]["japanese"] == "true")
        self.update_language_sensitivity()
        self.candidates_spin.set_value(int(self.settings["Input"]["candidates"]))
        for section in ("Light", "Dark"):
            for key in COLOR_KEYS:
                self.color_buttons[section][key].set_rgba(self.gdk_color(self.settings[section][key]))

    def apply_settings(self, _button):
        try:
            atomic_save(CONFIG_PATH, self.settings)
        except OSError as error:
            self.set_status(f"保存失败：{error}")
            return

        helper = Path(sys.argv[0]).resolve().parent / "link-input-apply"
        if not helper.is_file() or not os.access(helper, os.X_OK):
            self.set_status(f"配置已保存，但缺少 {helper}")
            return

        self.set_status("正在应用设置…")
        try:
            self.apply_process = Gio.Subprocess.new(
                [str(helper)],
                Gio.SubprocessFlags.STDOUT_PIPE | Gio.SubprocessFlags.STDERR_PIPE,
            )
        except GLib.Error as error:
            self.set_status(f"配置已保存，启动应用失败：{error.message}")
            return
        self.apply_button.set_sensitive(False)
        self.apply_process.communicate_utf8_async(None, None, self.apply_finished)

    def apply_finished(self, process, result):
        try:
            _ok, _stdout, stderr = process.communicate_utf8_finish(result)
            if process.get_successful():
                self.set_status("设置已应用")
            else:
                message = (stderr or "").strip() or "输入法未能重新连接"
                self.set_status(f"配置已保存，应用失败：{message}")
        except GLib.Error as error:
            self.set_status(f"配置已保存，应用失败：{error.message}")
        finally:
            self.apply_process = None
            self.apply_button.set_sensitive(True)

    def set_status(self, text):
        self.status.set_text(text)


class SettingsApp(Gtk.Application):
    def __init__(self):
        super().__init__(application_id="io.github.qingyin.LinkSettings")

    def do_activate(self):
        window = self.get_active_window() or SettingsWindow(self)
        window.present()


CONFIG_PATH = config_path()


if __name__ == "__main__":
    SettingsApp().run(sys.argv)
