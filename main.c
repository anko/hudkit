#include <gtk/gtk.h>
#include <webkit/webkit.h>

static void destroy_cb(GtkWidget* widget, gpointer data) {
  gtk_main_quit();
}

int main(int argc, char* argv[]) {
  gtk_init(&argc, &argv);

  if(!g_thread_supported())
    g_thread_init(NULL);

  // Create a Window, set colormap to RGBA
  GtkWidget* window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  GdkScreen *screen = gtk_widget_get_screen(window);
  GdkColormap *rgba = gdk_screen_get_rgba_colormap (screen);

  if (rgba && gdk_screen_is_composited (screen)) {
    gtk_widget_set_default_colormap(rgba);
    gtk_widget_set_colormap(GTK_WIDGET(window), rgba);
  }

  gtk_window_set_default_size(GTK_WINDOW(window), 800, 800);
  g_signal_connect(window, "destroy", G_CALLBACK(destroy_cb), NULL);

  // Optional: for dashboard style borderless windows
  gtk_window_set_decorated(GTK_WINDOW(window), FALSE);


  // Create a WebView, set it transparent, add it to the window
  WebKitWebView* web_view = web_view = WEBKIT_WEB_VIEW(webkit_web_view_new());
  webkit_web_view_set_transparent(web_view, TRUE);
  gtk_container_add (GTK_CONTAINER(window), GTK_WIDGET(web_view));

  // Load a default page
  webkit_web_view_load_uri(web_view, "http://stackoverflow.com/");

  // Show it and continue running until the window closes
  gtk_widget_grab_focus(GTK_WIDGET(web_view));
  gtk_widget_show_all(window);
  gtk_main();
  return 0;
}

