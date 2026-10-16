#include <gtk/gtk.h>
#include <unistd.h>
#include <fcntl.h>
#include <string>
#include <cstdio>
static GtkWidget *entry;
static std::string pending;
static gboolean command(gpointer) {
    char b[512];
    for (;;) {
        auto n = read(0, b, sizeof(b));
        if (n <= 0)
            break;
        pending.append(b, n);
    }
    for (;;) {
        auto end = pending.find('\n');
        if (end == std::string::npos)
            break;
        auto s = pending.substr(0, end);
        pending.erase(0, end + 1);
        if (s == "snapshot") {
            const auto text = gtk_entry_get_text(GTK_ENTRY(entry));
            auto encoded = g_base64_encode(reinterpret_cast<const guchar *>(text), strlen(text));
            std::printf("DATA:%s\n", encoded);
            std::fflush(stdout);
            g_free(encoded);
        } else if (s == "password")
            gtk_entry_set_visibility(GTK_ENTRY(entry), FALSE);
        else if (s == "plain")
            gtk_entry_set_visibility(GTK_ENTRY(entry), TRUE);
        else if (s == "quit")
            gtk_main_quit();
        else if (s.rfind("text:", 0) == 0) {
            gtk_entry_set_text(GTK_ENTRY(entry), s.substr(5).c_str());
            gtk_editable_set_position(GTK_EDITABLE(entry), -1);
        } else if (s == "select-all")
            gtk_editable_select_region(GTK_EDITABLE(entry), 0, -1);
    }
    return TRUE;
}
int main(int argc, char **argv) {
    gtk_init(&argc, &argv);
    auto window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Reword integration test");
    entry = gtk_entry_new();
    gtk_container_add(GTK_CONTAINER(window), entry);
    gtk_entry_set_text(GTK_ENTRY(entry), "teh ");
    gtk_editable_set_position(GTK_EDITABLE(entry), 4);
    if (g_getenv("REWORD_FIXTURE_MAXIMIZE"))
        gtk_window_maximize(GTK_WINDOW(window));
    gtk_widget_show_all(window);
    gtk_window_present(GTK_WINDOW(window));
    gtk_widget_grab_focus(entry);
    g_timeout_add(
        100,
        [](gpointer window) -> gboolean {
            gdk_window_focus(gtk_widget_get_window(GTK_WIDGET(window)), GDK_CURRENT_TIME);
            gtk_widget_grab_focus(entry);
            return FALSE;
        },
        window);
    fcntl(0, F_SETFL, O_NONBLOCK);
    g_timeout_add(25, command, nullptr);
    g_timeout_add_seconds(
        60,
        [](gpointer) -> gboolean {
            gtk_main_quit();
            return FALSE;
        },
        nullptr);
    gtk_main();
}
