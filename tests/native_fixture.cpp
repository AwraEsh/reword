#include <gtk/gtk.h>
#include <unistd.h>
#include <fcntl.h>
#include <string>
static GtkWidget *entry;
static gboolean command(gpointer){char b[256];auto n=read(0,b,sizeof(b));if(n>0){std::string s(b,n);if(s.find("password")!=std::string::npos)gtk_entry_set_visibility(GTK_ENTRY(entry),FALSE);if(s.find("quit")!=std::string::npos)gtk_main_quit();}return TRUE;}
int main(int argc,char**argv){gtk_init(&argc,&argv);auto window=gtk_window_new(GTK_WINDOW_TOPLEVEL);gtk_window_set_title(GTK_WINDOW(window),"Reword integration test");entry=gtk_entry_new();gtk_container_add(GTK_CONTAINER(window),entry);gtk_entry_set_text(GTK_ENTRY(entry),"teh ");gtk_editable_set_position(GTK_EDITABLE(entry),4);gtk_widget_show_all(window);gtk_window_present(GTK_WINDOW(window));gtk_widget_grab_focus(entry);fcntl(0,F_SETFL,O_NONBLOCK);g_timeout_add(50,command,nullptr);g_timeout_add_seconds(15,[](gpointer)->gboolean{gtk_main_quit();return FALSE;},nullptr);gtk_main();}
