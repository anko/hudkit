#include <gtk/gtk.h>
#include <webkit/webkit.h>

static void destroy_cb(GtkWidget* widget, gpointer data) {
  gtk_main_quit();
}

gboolean supports_alpha = true;

static gboolean draw(GtkWidget *widget, cairo_t *cr, gpointer userdata)
{
   cairo_t *new_cr = gdk_cairo_create(gtk_widget_get_window(widget));

    if (supports_alpha)
    {
        cairo_set_source_rgba (new_cr, 0.5, 1.0, 0.50, 0.5); /* transparent */
    }
    else
    {
        cairo_set_source_rgb (new_cr, 1.0, 1.0, 1.0); /* opaque white */
    }

    /* draw the background */
    cairo_set_operator (new_cr, CAIRO_OPERATOR_SOURCE);
    cairo_paint (new_cr);

    cairo_destroy(new_cr);

    return FALSE;
}

int main(int argc, char* argv[]) {
  gtk_init(&argc, &argv);
 

  // Create a Window, set colormap to RGBA
  GtkWidget* window = gtk_window_new(GTK_WINDOW_TOPLEVEL);

  g_signal_connect(G_OBJECT(window), "draw", G_CALLBACK(draw), NULL);

  GdkScreen *screen = gtk_widget_get_screen(window);
  GdkVisual *rgba = gdk_screen_get_rgba_visual (screen);

  if (rgba && gdk_screen_is_composited (screen)) {
    gtk_widget_set_visual(GTK_WIDGET(window), rgba);
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
  webkit_web_view_load_uri(web_view, "http://localhost:5004/");

  // Show it and continue running until the window closes
  gtk_widget_grab_focus(GTK_WIDGET(web_view));
  gtk_widget_show_all(window);
  gtk_main();
  return 0;
}

