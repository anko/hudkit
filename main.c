/**
 * Original code by: Mike - http://plan99.net/~mike/blog (now a dead link--unable to find it).
 * Modified by karlphillip for StackExchange:
 *     (http://stackoverflow.com/questions/3908565/how-to-make-gtk-window-background-transparent)
 * Re-worked for Gtk 3 by Louis Melahn, L.C., January 30, 2014.
 * Further extended with WebKit and input shape kill by Anko<an@cyan.io>, June 18, 2014.
 */

#include <gtk/gtk.h>        // windowing
#include <webkit/webkit.h>  // web view
#include <stdlib.h>         // exit

static void screen_changed(GtkWidget *widget, GdkScreen *old_screen,
        gpointer user_data);
static gboolean draw (GtkWidget *widget, cairo_t *new_cr, gpointer user_data);

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("No argument found.\n\
Pass a running web server's URI as argument.\n");
        exit(1);
    }
    gtk_init(&argc, &argv);

    // Window setup
    GtkWidget *window = gtk_window_new(GTK_WINDOW_POPUP);
    gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);
    gtk_window_set_title(GTK_WINDOW(window), "hudkit overlay window");
    g_signal_connect(G_OBJECT(window), "delete-event", gtk_main_quit, NULL);
    gtk_widget_set_app_paintable(window, TRUE);

    // Callback setup
    g_signal_connect(G_OBJECT(window), "draw", G_CALLBACK(draw), NULL);
    g_signal_connect(G_OBJECT(window), "screen-changed", G_CALLBACK(screen_changed), NULL);

    // WebKit widget setup
    WebKitWebView *web_view = WEBKIT_WEB_VIEW(webkit_web_view_new());
    webkit_web_view_set_transparent(web_view, TRUE);
    gtk_container_add(GTK_CONTAINER(window), GTK_WIDGET(web_view));
    // Load specified URI
    webkit_web_view_load_uri(web_view, argv[1]);

    // Initialise
    screen_changed(window, NULL, NULL);
    gtk_widget_show_all(window);

    // Set input shape (area where clicks are recognised) to nothing. This
    // means all clicks will pass "through" the window onto whatever's below,
    // so this window is just an overlay.
    GdkWindow *gdk_window = gtk_widget_get_window(GTK_WIDGET(window));
    gdk_window_input_shape_combine_region(GDK_WINDOW(gdk_window), cairo_region_create(), 0,0);

    gtk_main();
    return 0;
}

static void screen_changed(GtkWidget *widget, GdkScreen *old_screen, gpointer userdata) {
    // Check the display's alpha channel support
    GdkScreen *screen = gtk_widget_get_screen(widget);
    if (gdk_screen_is_composited(screen)) {
        printf("Your screen supports alpha channels! (Good.)\n");
    } else {
        printf("Your screen does not support alpha channels!\n");
        printf("Check your compositor is running.\n");
        exit(2);
    }

    // OK, let's use the RGBA visual on the widget then.
    gtk_widget_set_visual(widget, gdk_screen_get_rgba_visual(screen));

    // Inherit window size from screen
    gint w = gdk_screen_get_width(screen);
    gint h = gdk_screen_get_height(screen);
    gtk_window_set_default_size(GTK_WINDOW(widget), w, h);
}

static gboolean draw(GtkWidget *widget, cairo_t *cr, gpointer userdata) {

    cairo_t *new_cr = gdk_cairo_create(gtk_widget_get_window(widget));

        cairo_set_source_rgba (new_cr, 1, 1, 1, 0); /* transparent */

        /* draw the background */
        cairo_set_operator (new_cr, CAIRO_OPERATOR_SOURCE);
        cairo_paint (new_cr);

    cairo_destroy(new_cr);

    return FALSE;
}
