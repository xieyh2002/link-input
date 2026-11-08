#include <gtk/gtk.h>
static void activate(GtkApplication *app, void *data) {
    GtkWidget *win = gtk_application_window_new(app), *entry = gtk_entry_new();
    gtk_window_set_title(GTK_WINDOW(win), "link输入法 · 输入体验");
    gtk_window_set_default_size(GTK_WINDOW(win), 480, 140);
    gtk_window_set_child(GTK_WINDOW(win), entry);
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "输入 nihao，按空格选词；Ctrl+空格切换");
    gtk_window_present(GTK_WINDOW(win));
    gtk_widget_grab_focus(entry);
}
int main(int argc, char **argv) {
    GtkApplication *app = gtk_application_new("local.qingyin.Probe", G_APPLICATION_NON_UNIQUE);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    return g_application_run(G_APPLICATION(app), argc, argv);
}
