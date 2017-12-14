/**
 * Original transparent window code by Mike - http://plan99.net/~mike/blog (now
 *     a dead link. I can't find a copy.)
 * Modified by karlphillip for StackExchange -
 *     http://stackoverflow.com/questions/3908565/how-to-make-gtk-window-background-transparent
 * Re-worked for Gtk 3 by Louis Melahn, L.C., January 30, 2014.
 * Extended with WebKit and input shape kill by Anko<an@cyan.io>, June 18, 2014.
 */

// Library include           // What it's used for
// --------------------------//-------------------
#include <gtk/gtk.h>         // windowing
#include <webkit2/webkit2.h> // web view
#include <stdlib.h>          // exit

static void screen_changed(GtkWidget *widget, GdkScreen *old_screen,
        gpointer user_data);

int main(int argc, char **argv) {
    gtk_init(&argc, &argv);
    if (argc < 2) {
        fprintf(stderr, "No argument found.\n\
Pass a running web server's URI as argument.\n");
        exit(1);
    }

    // Create the window, set basic properties
    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);
    gtk_window_set_title(GTK_WINDOW(window), "hudkit overlay window");
    g_signal_connect(G_OBJECT(window), "delete-event", gtk_main_quit, NULL);
    gtk_widget_set_app_paintable(window, TRUE);

    // Set up callback for when something about the screen changes
    g_signal_connect(G_OBJECT(window), "screen-changed", G_CALLBACK(screen_changed), NULL);

    // Set up and add the WebKit web view widget
    WebKitWebView *web_view = WEBKIT_WEB_VIEW(webkit_web_view_new());
    GdkRGBA rgba = { .alpha = 0.0 };
    webkit_web_view_set_background_color(web_view, &rgba);

    gtk_container_add(GTK_CONTAINER(window), GTK_WIDGET(web_view));

    // Load the specified URI
    webkit_web_view_load_uri(web_view, argv[1]);

    // Initialise the window and make it active.  We need this so it can
    // fullscreen to the correct size.
    screen_changed(window, NULL, NULL);
    gtk_widget_show_all(window);
    gtk_window_fullscreen(GTK_WINDOW(window));

    // Hide the window, so we can get our properties ready without the window
    // manager trying to mess with us.
    GdkWindow *gdk_window = gtk_widget_get_window(GTK_WIDGET(window));
    gdk_window_hide(GDK_WINDOW(gdk_window));

    // "Can't touch this!" - to the window manager
    //
    // The override-redirect flag prevents the window manager taking control of
    // anything, so the window remains in our control.  This should be enough
    // on its own.
    gdk_window_set_override_redirect(GDK_WINDOW(gdk_window), true);
    // But just to be careful, light up the flags like a Christmas tree, with
    // all the WM hints we can think of to try to convince whatever that's
    // reading them (probably a window manager) to keep this window on-top and
    // fullscreen but otherwise leave it alone.
    gtk_window_set_keep_above       (GTK_WINDOW(window), true);
    gtk_window_set_skip_taskbar_hint(GTK_WINDOW(window), true);
    gtk_window_set_accept_focus     (GTK_WINDOW(window), false);
    gtk_window_set_decorated        (GTK_WINDOW(window), false);
    gtk_window_set_accept_focus     (GTK_WINDOW(window), false);
    gtk_window_set_resizable        (GTK_WINDOW(window), false);

    // "Can't touch this!" - to the user
    //
    // Set the input shape (area where clicks are recognised) to a zero-width,
    // zero-height region a.k.a. nothing.  This makes clicks pass through the
    // window onto whatever's below.
    gdk_window_input_shape_combine_region(GDK_WINDOW(gdk_window), cairo_region_create(), 0,0);

    // Now it's safe to show the window again.  It should be click-through, and
    // the WM should ignore it.
    gdk_window_show(GDK_WINDOW(gdk_window));

    gtk_main();
    return 0;
}

// This callback runs when the window is first set to appear on some screen, or
// when it's moved to appear on another.
static void screen_changed(GtkWidget *widget, GdkScreen *old_screen, gpointer userdata) {

    // Die unless the screen supports compositing (alpha blending)
    GdkScreen *screen = gtk_widget_get_screen(widget);
    if (!gdk_screen_is_composited(screen)) {
        fprintf(stderr, "Your screen does not support alpha channels!\n");
        fprintf(stderr, "Check your compositor is running.\n");
        exit(2);
    }

    // Make sure the widget (the window, actually) can take RGBA
    gtk_widget_set_visual(widget, gdk_screen_get_rgba_visual(screen));

    GdkDisplay *display = gdk_display_get_default();
    GdkMonitor *monitor = gdk_display_get_primary_monitor(display);
    GdkRectangle geometry;
    gdk_monitor_get_geometry(monitor, &geometry);

    // Inherit window size from screen
    gint w = geometry.width;
    gint h = geometry.height;
    gtk_window_set_default_size(GTK_WINDOW(widget), w, h);
}
