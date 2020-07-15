/*
   * Original transparent window code by Mike - http://plan99.net/~mike/blog
     Dead link, but on Internet Archive most recently at:
     https://web.archive.org/web/20121127025149/http://mikehearn.wordpress.com/2006/03/26/gtk-windows-with-alpha-channels/
   * Modified by karlphillip for StackExchange -
     http://stackoverflow.com/questions/3908565/how-to-make-gtk-window-background-transparent
   * Re-worked for Gtk 3 by Louis Melahn, L.C., January 30, 2014.
   * Extended with WebKit and input shape kill by Antti Korpi <an@cyan.io>, on
     June 18, 2014.
   * Updated to WebKit 2 by Antti Korpi <an@cyan.io> on December 12, 2017.
 */

// Library include           // What it's used for
// --------------------------//-------------------
#include <gtk/gtk.h>         // windowing
#include <gdk/gdk.h>         // low-level windowing
#include <gdk/gdkmonitor.h>  // monitor counting
#include <webkit2/webkit2.h> // web view
#include <stdlib.h>          // exit
#include <stdio.h>           // files

static void screen_changed(GtkWidget *widget, GdkScreen *old_screen,
        gpointer user_data);

static int get_monitor_rects(GdkDisplay *display, GdkRectangle **rectangles) {
    int n = gdk_display_get_n_monitors(display);
    GdkRectangle *new_rectangles = (GdkRectangle*)malloc(n * sizeof(GdkRectangle));
    for (int i = 0; i < n; ++i) {
        GdkMonitor *monitor = gdk_display_get_monitor(display, i);
        gdk_monitor_get_geometry(monitor, &new_rectangles[i]);
    }
    *rectangles = new_rectangles;
    return n;
}

static void web_view_javascript_finished(GObject *object, GAsyncResult *result,
        gpointer user_data) {
    WebKitJavascriptResult *js_result;
    JSValueRef value;
    JSGlobalContextRef context;
    GError *error = NULL;

    js_result = webkit_web_view_run_javascript_finish(WEBKIT_WEB_VIEW(object), result, &error);
    if (!js_result) {
        g_warning("Error running JavaScript: %s", error->message);
        g_error_free(error);
        return;
    }
    webkit_javascript_result_unref(js_result);
}

int main(int argc, char **argv) {
    gtk_init(&argc, &argv);
    if (argc < 2) {
        fprintf(stderr, "Expected 1 argument, got 0:\n");
        fprintf(stderr, "You should pass a running web server's URI.\n\n");
        fprintf(stderr, "For example, start a server on port 4000, then\n\n");
        fprintf(stderr, "    %s \"http://localhost:4000\"\n\n", argv[0]);
        exit(1);
    }

    // Create the window, set basic properties
    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_gravity(GTK_WINDOW(window), GDK_GRAVITY_NORTH_WEST);
    gtk_window_move(GTK_WINDOW(window), 0, 0);

    gtk_window_set_title(GTK_WINDOW(window), "hudkit overlay window");
    g_signal_connect(G_OBJECT(window), "delete-event", gtk_main_quit, NULL);
    gtk_widget_set_app_paintable(window, TRUE);

    // Set up a callback to react to screen changes
    g_signal_connect(G_OBJECT(window), "screen-changed",
            G_CALLBACK(screen_changed), NULL);

    // Set up and add the WebKit web view widget
    // Disable caching
    WebKitWebContext *wk_context = webkit_web_context_get_default();
    webkit_web_context_set_cache_model(wk_context, WEBKIT_CACHE_MODEL_DOCUMENT_VIEWER);
    WebKitWebView *web_view = WEBKIT_WEB_VIEW(webkit_web_view_new_with_context(wk_context));
    // Enable browser console logging to stdout
    WebKitSettings *wk_settings = webkit_settings_new();
    webkit_settings_set_enable_write_console_messages_to_stdout(wk_settings, true);
    webkit_web_view_set_settings(web_view, wk_settings);
    // Make transparent
    GdkRGBA rgba = { .alpha = 0.0 };
    webkit_web_view_set_background_color(web_view, &rgba);
    gtk_container_add(GTK_CONTAINER(window), GTK_WIDGET(web_view));

    // Load the specified URI
    webkit_web_view_load_uri(web_view, argv[1]);

    // Initialise the window and make it active.  We need this so it can
    // fullscreen to the correct size.
    screen_changed(window, NULL, NULL);

    GdkDisplay *display = gdk_display_get_default();
    GdkRectangle *rectangles = NULL;
    int nRectangles = get_monitor_rects(display, &rectangles);

    // snprintf-ing and then strncat-ing strings safely in C is hard, and it's
    // 3am, so let's write to a temporary file and read the result back.
    char filename[] = "/tmp/hudkit_js_init_XXXXXX";
    int fd = mkstemp(filename);
    FILE *fp = fdopen(fd, "w+");
    if (fp == NULL) {
        fprintf(stderr, "Error opening temp file\n");
        exit(1);
    }
    fprintf(fp, "window.Hudkit = {monitors:[");
    for (int i = 0; i < nRectangles; ++i) {
        GdkRectangle rect = rectangles[i];
        fprintf(fp, "{x:%i,y:%i,width:%i,height:%i},\n", rect.x, rect.y, rect.width, rect.height);
    }
    fprintf(fp, "]};");
    fseek(fp, 0, SEEK_SET);

    char buffer[1000];
    int nRead = fread(buffer, 1, 1000, fp);
    buffer[nRead] = '\0';
    fclose(fp);

    webkit_web_view_run_javascript(web_view, buffer, NULL, web_view_javascript_finished, NULL);

    gtk_widget_show_all(window);

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
    gtk_window_set_resizable        (GTK_WINDOW(window), false);

    // "Can't touch this!" - to the user
    //
    // Set the input shape (area where clicks are recognised) to a zero-width,
    // zero-height region a.k.a. nothing.  This makes clicks pass through the
    // window onto whatever's below.
    gdk_window_input_shape_combine_region(GDK_WINDOW(gdk_window),
            cairo_region_create(), 0,0);

    // Now it's safe to show the window again.  It should be click-through, and
    // the WM should ignore it.
    gdk_window_show(GDK_WINDOW(gdk_window));

    // XXX KLUDGE WARNING! XXX
    //
    // This sleep is necessary at least on my system with a proprietary Nvidia
    // driver.  Without this sleep, the transparent overlay window will usually
    // randomly not show.  No errors from it or the compositor, or anything;
    // there's no discernible reason why it doesn't work.  Except sometimes it
    // does, rarely, like once every 30 tries, with again no other difference
    // from what I can tell.  It's clearly a race condition of some sort.
    //
    // If I `watch xwininfo -tree -root`, I see the Hudkit window and
    // WebKitWebProcess windows get created, but I don't understand GTK
    // internals well enough to understand if it's right.  The same windows are
    // created both when successful and when not.
    //
    // Things I tried that didn't help:
    //  - adding calls to XSync all over the place,
    //  - doing `gdk_x11_grab_server` and `gdk_x11_ungrab_server` around
    //    various large and small bits of code, and
    //  - forcing full composition pipeline in `nvidia-settings`.
    usleep(200000);

    gtk_main();
    return 0;
}

static void size_to_screen(GtkWindow *window) {
    GdkScreen *screen = gtk_widget_get_screen(GTK_WIDGET(window));

    // Get total screen size.  This involves finding all physical monitors
    // connected, and examining their positions and sizes.  This is as complex
    // as it is because monitors can be configured to have relative
    // positioning, causing overlapping areas and a non-rectangular total
    // desktop area.
    //
    // We want our window to cover the minimum axis-aligned bounding box of
    // that total desktop area.  This means it's too large (even large bits of
    // it may be outside the accessible desktop) but it's easier to manage than
    // multiple windows.

    // TODO Find the min x and y too, just in case someone's weird setup
    // has something other than 0,0 as top-left.

    GdkDisplay *display = gdk_display_get_default();
    GdkRectangle *rectangles = NULL;
    int nRectangles = get_monitor_rects(display, &rectangles);

    int width = 0, height = 0;
    for (int i = 0; i < nRectangles; ++i) {
        GdkRectangle rect = rectangles[i];
        int actualWidth = rect.x + rect.width;
        int actualHeight = rect.y + rect.height;
        if (width < actualWidth) width = actualWidth;
        if (height < actualHeight) height = actualHeight;
    }
    free(rectangles);

    gtk_window_set_default_size(window, width, height);
    gtk_window_resize(window, width, height);
    gtk_window_set_resizable(window, false);
}

// This callback runs when the window is first set to appear on some screen, or
// when it's moved to appear on another.
static void screen_changed(GtkWidget *widget, GdkScreen *old_screen,
        gpointer userdata) {

    // Die unless the screen supports compositing (alpha blending)
    GdkScreen *screen = gtk_widget_get_screen(widget);
    if (!gdk_screen_is_composited(screen)) {
        fprintf(stderr, "Your screen does not support transparency.\n");
        fprintf(stderr, "Maybe your compositor isn't running?\n");
        exit(2);
    }

    // Ensure the widget (the window, actually) can take RGBA
    gtk_widget_set_visual(widget, gdk_screen_get_rgba_visual(screen));

    size_to_screen(GTK_WINDOW(widget));
}
