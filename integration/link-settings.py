#!/usr/bin/python3
import configparser
import io
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
        "font_size": "14",
        "spacing": "compact",
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
FUZZY_GROUPS = ("zh/z", "ch/c", "sh/s", "n/l", "en/eng", "in/ing")
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
    "spacing": (("紧凑", "compact"), ("标准", "standard")),
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


def normalize_integer(value, fallback, lower, upper):
    try:
        value = int(value)
    except (TypeError, ValueError):
        return fallback
    return str(value) if lower <= value <= upper else fallback


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
            elif key == "font_size":
                values[key] = normalize_integer(parser.get(section, key, fallback=default), default, 10, 28)
            else:
                values[key] = normalize_integer(parser.get(section, key, fallback=default), default, 1, 9)
    if result["Input"]["japanese"] == "false" and result["Input"]["language"] == "ja":
        result["Input"]["language"] = "zh"
    return result


def atomic_save(path, settings):
    parser = configparser.ConfigParser()
    parser.read_dict(settings)
    stream = io.StringIO()
    parser.write(stream, space_around_delimiters=False)
    atomic_write(path, stream.getvalue())


def atomic_write(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = tempfile.mkstemp(prefix=f".{path.name}-", dir=path.parent)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8") as stream:
            stream.write(text)
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


def read_pinyin():
    parser = configparser.ConfigParser(interpolation=None)
    try:
        with PINYIN_PATH.open(encoding="utf-8") as stream:
            parser.read_file(stream)
    except FileNotFoundError:
        pass
    correction = parser.getboolean("Pinyin", "correction", fallback=False)
    fuzzy = parser.get("Pinyin", "fuzzy", fallback="")
    return correction, {group.strip().lower() for group in fuzzy.split(",")} & set(FUZZY_GROUPS)


def read_phrase(path):
    try:
        return path.read_text(encoding="utf-8")
    except FileNotFoundError:
        return ""


def merge_phrases(native, ibus):
    if native == ibus or not ibus:
        return native
    if not native:
        return ibus
    existing = set(native.splitlines())
    extra = [line for line in ibus.splitlines() if line and line not in existing]
    return native.rstrip("\n") + "\n" + "\n".join(extra) + "\n" if extra else native


def save_phrases(text, original):
    current = tuple(map(read_phrase, PHRASE_PATHS))
    native, ibus = PHRASE_PATHS
    if text == merge_phrases(*original) and ibus.is_symlink() and ibus.resolve() == native.resolve():
        return
    if current != original:
        raise OSError("短语文件已在外部修改，请重新打开设置后再编辑")
    atomic_write(native.resolve(), text)
    if ibus.is_symlink() and ibus.resolve() == native.resolve():
        return
    ibus.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".phrases-", dir=ibus.parent) as directory:
        link = Path(directory) / ibus.name
        link.symlink_to("../rime/custom_phrase.txt")
        link.replace(ibus)


class SettingsWindow(Gtk.ApplicationWindow):
    def __init__(self, application):
        super().__init__(application=application, title="link输入法 设置", default_width=760, default_height=640)
        self.set_icon_name("link-input")
        self.settings = read_settings(CONFIG_PATH)
        self.choice_buttons = {"language": [], "chinese": [], "theme": [], "spacing": []}
        self.japanese_switch = None
        self.spinners = {}
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
        self.make_spin(input_group, "每页候选词数量", "Input", "candidates", 1, 9)
        content.append(input_group)

        pinyin_group = self.make_group("拼音")
        self.make_pinyin(pinyin_group)
        content.append(pinyin_group)

        appearance_group = self.make_group("外观")
        self.make_choices(appearance_group, "主题", "theme")
        self.make_choices(appearance_group, "候选栏间距", "spacing")
        self.make_spin(appearance_group, "候选栏字号（pt）", "Appearance", "font_size", 10, 28)
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
        if self.pinyin_error:
            self.set_status(self.pinyin_error)

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

    def make_spin(self, group, title, section, key, lower, upper):
        row = self.make_row()
        row.append(self.make_label(title))
        adjustment = Gtk.Adjustment(value=int(self.settings[section][key]), lower=lower, upper=upper, step_increment=1, page_increment=1)
        spinner = Gtk.SpinButton(adjustment=adjustment, climb_rate=1.0, digits=0, valign=Gtk.Align.CENTER)
        self.spinners[section, key] = spinner
        spinner.connect("value-changed", self.spin_value, section, key)
        row.append(spinner)
        group.list_box.append(Gtk.ListBoxRow(child=row))

    def make_pinyin(self, group):
        self.pinyin_error = ""
        self.pinyin_readable = self.phrases_readable = True
        try:
            correction, fuzzy = read_pinyin()
        except (OSError, UnicodeError, ValueError, configparser.Error) as error:
            correction, fuzzy = False, set()
            self.pinyin_readable = False
            self.pinyin_error = f"无法读取拼音配置：{error}"
        self.original_pinyin = correction, fuzzy
        try:
            self.original_phrases = tuple(map(read_phrase, PHRASE_PATHS))
        except (OSError, UnicodeError) as error:
            self.original_phrases = ("", "")
            self.phrases_readable = False
            self.pinyin_error = f"无法读取自定义短语：{error}"

        row = self.make_row()
        row.append(self.make_label("自动纠错（全拼）"))
        self.correction_switch = Gtk.Switch(active=correction, valign=Gtk.Align.CENTER, sensitive=self.pinyin_readable)
        self.correction_switch.connect("notify::active", self.pinyin_changed)
        row.append(self.correction_switch)
        group.list_box.append(Gtk.ListBoxRow(child=row))

        row = self.make_row()
        row.append(self.make_label("模糊音（全拼）"))
        choices = Gtk.FlowBox(selection_mode=Gtk.SelectionMode.NONE, max_children_per_line=6, column_spacing=8)
        self.fuzzy_buttons = {}
        for name in FUZZY_GROUPS:
            button = Gtk.CheckButton(label=name, active=name in fuzzy, sensitive=self.pinyin_readable)
            button.connect("toggled", self.pinyin_changed)
            self.fuzzy_buttons[name] = button
            choices.append(button)
        row.append(choices)
        group.list_box.append(Gtk.ListBoxRow(child=row))

        row = self.make_row()
        row.set_orientation(Gtk.Orientation.VERTICAL)
        row.append(self.make_label("自定义短语（全拼和小鹤双拼）"))
        hint = Gtk.Label(label="每行一条：短语、一个 Tab、输入码。以 # 开头的行是注释。", xalign=0, wrap=True)
        hint.add_css_class("dim-label")
        row.append(hint)
        editor = Gtk.TextView(monospace=True, editable=self.phrases_readable, accepts_tab=True)
        self.phrase_buffer = editor.get_buffer()
        self.phrase_buffer.set_text(merge_phrases(*self.original_phrases))
        self.phrase_buffer.connect("changed", self.pinyin_changed)
        scrolled = Gtk.ScrolledWindow(child=editor, min_content_height=112, has_frame=True)
        row.append(scrolled)
        group.list_box.append(Gtk.ListBoxRow(child=row))

    def pinyin_changed(self, *_args):
        self.set_status("设置已更新，点击「保存并应用」生效")

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

    def spin_value(self, spinner, section, key):
        self.settings[section][key] = str(int(spinner.get_value()))
        self.set_status("设置已更新，点击应用生效")

    def color_changed(self, picker, _parameter, section, key):
        rgba = picker.get_rgba()
        self.settings[section][key] = "#%02x%02x%02x" % (round(rgba.red * 255), round(rgba.green * 255), round(rgba.blue * 255))
        self.set_status("设置已更新，点击应用生效")

    def reset_defaults(self, _button):
        self.settings = {section: values.copy() for section, values in DEFAULTS.items()}
        self.refresh_ui()
        self.correction_switch.set_active(False)
        for button in self.fuzzy_buttons.values():
            button.set_active(False)
        self.set_status("已载入默认值，点击应用生效")

    def refresh_ui(self):
        for key, choices in CHOICES.items():
            section = "Input" if key in ("language", "chinese") else "Appearance"
            for button in self.choice_buttons[key]:
                button.set_active(self.settings[section][key] == VALUE_BY_LABEL[key][button.get_label()])
        self.japanese_switch.set_active(self.settings["Input"]["japanese"] == "true")
        self.update_language_sensitivity()
        for (section, key), spinner in self.spinners.items():
            spinner.set_value(int(self.settings[section][key]))
        for section in ("Light", "Dark"):
            for key in COLOR_KEYS:
                self.color_buttons[section][key].set_rgba(self.gdk_color(self.settings[section][key]))

    def apply_settings(self, _button):
        try:
            if self.phrases_readable:
                start, end = self.phrase_buffer.get_bounds()
                text = self.phrase_buffer.get_text(start, end, True)
                save_phrases(text, self.original_phrases)
                self.original_phrases = tuple(map(read_phrase, PHRASE_PATHS))
            if self.pinyin_readable:
                correction = self.correction_switch.get_active()
                fuzzy = {name for name, button in self.fuzzy_buttons.items() if button.get_active()}
                if (correction, fuzzy) != self.original_pinyin:
                    if read_pinyin() != self.original_pinyin:
                        raise OSError("拼音配置已在外部修改，请重新打开设置后再编辑")
                    atomic_save(PINYIN_PATH, {"Pinyin": {
                        "correction": "true" if correction else "false",
                        "fuzzy": ",".join(name for name in FUZZY_GROUPS if name in fuzzy),
                    }})
                    self.original_pinyin = correction, fuzzy
            atomic_save(CONFIG_PATH, self.settings)
        except (OSError, UnicodeError, ValueError, configparser.Error) as error:
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
PINYIN_PATH = CONFIG_PATH.with_name("pinyin.ini")
DATA_PATH = Path(os.environ.get("XDG_DATA_HOME") or Path.home() / ".local" / "share") / "qingyin"
PHRASE_PATHS = tuple(DATA_PATH / mode / "custom_phrase.txt" for mode in ("rime", "rime-ibus"))


if __name__ == "__main__":
    SettingsApp().run(sys.argv)
