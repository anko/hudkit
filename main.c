// Library include           // What it's used for
// --------------------------//-------------------
#include <gtk/gtk.h>         // windowing
#include <gdk/gdk.h>         // low-level windowing
#include <gdk/gdkmonitor.h>  // monitor counting
#include <webkit2/webkit2.h> // web view
#include <stdlib.h>          // exit
#include <stdio.h>           // files
#include <inttypes.h>        // string to int conversion

// Overlay window handle.  Global because almost everything touches it.
GtkWidget *window;

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
    // Ownership of the malloc'd memory transfers out
    return n;
}

// Stored rectangles out of which we can construct the window's input shape on
// demand.  The attached inspector's rectangle is stored separately, so when
// user code modifies the other rectangles, the inspector's rectangle can't be
// overwritten.
cairo_rectangle_int_t attached_inspector_input_rect = { 0, 0, 0, 0 };
cairo_rectangle_int_t *user_defined_input_rects = NULL;
int n_user_defined_input_rects = 0;

void realize_input_shape() {
    // Our input shape for the overall window should be the rectangles set by
    // the user, and the rectangle of the attached web inspector (if
    // applicable), all merged together into one shape.

    cairo_region_t *shape = cairo_region_create_rectangle(
            &attached_inspector_input_rect);
    for (int i = 0; i < n_user_defined_input_rects; ++i) {
        cairo_region_union_rectangle(shape, &user_defined_input_rects[i]);
    }

    GdkWindow *gdk_window = gtk_widget_get_window(GTK_WIDGET(window));
    gdk_window_input_shape_combine_region(GDK_WINDOW(gdk_window), shape, 0,0);
    cairo_region_destroy(shape);
}

static void on_callback_finished(GObject *object, GAsyncResult *result,
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

void call_js_callback(WebKitWebView *web_view, int callbackId, char *stringifiedData) {
    // Calls the user JS callback with the given ID, simply up string-placing
    // the stringified data between its call parentheses.
    //
    // Ensure `stringifiedData` is sanitised!  It will basically be `eval`ed in
    // the web page's context.

    char buffer[sizeof(stringifiedData) + 1024];
    snprintf(buffer, sizeof(buffer), "window.Hudkit._pendingCallbacks[%i](%s);\n\
            delete window.Hudkit._pendingCallbacks[%i]",
            callbackId, stringifiedData, callbackId);

    webkit_web_view_run_javascript(
            web_view, buffer, NULL, on_callback_finished, NULL);
}

void on_js_call_get_monitor_layout(WebKitUserContentManager *manager,
        WebKitJavascriptResult *sentData,
        gpointer arg) {
    WebKitWebView *web_view = WEBKIT_WEB_VIEW(arg);
    JSCValue *jsValue = webkit_javascript_result_get_js_value(sentData);
    int callbackId = jsc_value_to_int32(jsValue);

    // TODO get display from web_view?  Just in case it has somehow changed
    // between then and now.
    GdkDisplay *display = gdk_display_get_default();
    GdkRectangle *rectangles = NULL;
    int nRectangles = get_monitor_rects(display, &rectangles);

    GdkMonitor *monitors[nRectangles];
    for (int i = 0; i < nRectangles; ++i) {
        monitors[i] = gdk_display_get_monitor(display, i);
    }

    // TODO re-malloc buffer if data doesn't fit.  In case someone in the
    // future using this has a LOT of monitors.
    char buffer[1024] = "null, [";
    for (int i = 0; i < nRectangles; ++i) {
        // Escape the JS string contents escape, to prevent XSS via monitor
        // model string.  Yes, seriously.
        //
        // The unlikely attack (or more likely, an unlucky coincidence) that is
        // that a monitor model string could contain a character that
        // JavaScript string literals treat specially, such as a newline or
        // closing quote, which would cause a parse error, or in the worst case
        // execute the rest of the input as JS in the page context.
        const char *monitor_model_string = gdk_monitor_get_model(monitors[i]);
        // Uncomment to test sample XSS attack:
        //monitor_model_string = "evil\', attack: alert('xss'), _:\'";

        int monitor_model_string_length = strlen(monitor_model_string);

        // Worst-case the escaped output twice as long: if we need to escape
        // every character.  Plus 1 for the terminating \0.
        char escaped_monitor_model_string[2 * monitor_model_string_length + 1];
        char *end_pointer = escaped_monitor_model_string;

        for (int index = 0; index < monitor_model_string_length; ++index) {
            char charHere = monitor_model_string[index];
            // Spec for JS string literals' parsing grammar:
            // http://www.ecma-international.org/ecma-262/5.1/#sec-7.8.4
            //
            // We have to backslash-escape every character excluded from either
            // the DoubleStringCharacter or SingleStringCharacter productions.
            switch (charHere) {

                // Directly named excluded characters:
                // \ (backslash)
                case '\\':
                // ' (single quote)
                case '\'':
                // " (double quote)
                case '"':
                    // Just put a backslash in front of it
                    memset(end_pointer++, '\\', 1);
                    memset(end_pointer++, charHere, 1);
                    break;

                // Characters excluded because they're part of LineTerminator:
                // <LF> (line feed)
                case '\n':
                    memset(end_pointer++, '\\', 1);
                    memset(end_pointer++, 'n', 1);
                    break;
                // <CR> (carriage return)
                case '\r':
                    memset(end_pointer++, '\\', 1);
                    memset(end_pointer++, 'r', 1);
                    break;
                // <LS> (line separator)
                // <PS> (paragraph separator)
                //
                // Those last 2 are Unicode, and not representable in ASCII,
                // which we're working in, so we don't have to deal with them.
                //
                // Just in case, I checked that WebKit really does treat the
                // script as ASCII, discarding out-of range bit patterns that
                // would form valid Unicode.  It does this even if the target
                // document is declared with <meta charset="utf-8">.

                // Anything  else is fine in a JavaScript string literal.  Yes,
                // this even includes other control characters.
                default:
                    memset(end_pointer++, charHere, 1);
                    break;
            }
        }
        // The buffer was oversized already, and zeroed on allocation, so we
        // don't have to explicitly write a null byte to terminate it.

        GdkRectangle rect = rectangles[i];
        snprintf(
                buffer + strlen(buffer),
                sizeof(buffer),
                "{name:'%s',x:%i,y:%i,width:%i,height:%i},",
                escaped_monitor_model_string, rect.x, rect.y, rect.width, rect.height);
    }
    snprintf(buffer + strlen(buffer), sizeof(buffer), "]");

    call_js_callback(web_view, callbackId, buffer);
    free(rectangles);
}

void on_js_call_set_clickable_areas(WebKitUserContentManager *manager,
        WebKitJavascriptResult *sentData,
        gpointer arg) {
    WebKitWebView *web_view = WEBKIT_WEB_VIEW(arg);
    JSCValue *jsValue = webkit_javascript_result_get_js_value(sentData);
    //printf("%s\n", jsc_value_to_json(jsValue, 2));
    int callbackId = jsc_value_to_int32(jsc_value_object_get_property(jsValue, "id"));
    int nRectangles = jsc_value_to_int32(
            jsc_value_object_get_property(
                jsc_value_object_get_property(jsValue, "rectangles"),
                "length"));
    //printf("nRectangles %i\n", nRectangles);

    n_user_defined_input_rects = nRectangles;
    user_defined_input_rects = realloc(
            user_defined_input_rects, nRectangles * sizeof(GdkRectangle));

    JSCValue *jsRectangles = jsc_value_object_get_property(jsValue, "rectangles");
    for (int i = 0; i < nRectangles; ++i) {
        JSCValue *jsRect = jsc_value_object_get_property_at_index(jsRectangles, i);
        // Anything undefined is interpreted by `jsc_value_to_int32` as 0.
        user_defined_input_rects[i].x = jsc_value_to_int32(
                jsc_value_object_get_property(jsRect, "x"));
        user_defined_input_rects[i].y = jsc_value_to_int32(
                jsc_value_object_get_property(jsRect, "y"));
        user_defined_input_rects[i].width = jsc_value_to_int32(
                jsc_value_object_get_property(jsRect, "width"));
        user_defined_input_rects[i].height = jsc_value_to_int32(
                jsc_value_object_get_property(jsRect, "height"));
    }

    realize_input_shape();

    call_js_callback(web_view, callbackId, "null");
}
void on_js_call_show_inspector(WebKitUserContentManager *manager,
        WebKitJavascriptResult *sentData,
        gpointer arg) {
    WebKitWebView *web_view = WEBKIT_WEB_VIEW(arg);
    JSCValue *jsValue = webkit_javascript_result_get_js_value(sentData);
    int callbackId = jsc_value_to_int32(jsValue);

    WebKitWebInspector *inspector = webkit_web_view_get_inspector(
            WEBKIT_WEB_VIEW(web_view));

    webkit_web_inspector_show(WEBKIT_WEB_INSPECTOR(inspector));

    call_js_callback(web_view, callbackId, "null");
}


void on_inspector_size_allocate(GtkWidget *inspector_web_view,
        GdkRectangle *allocation,
        gpointer user_data) {
    // Whenever the inspector (which when this is called is attached to the
    // overlay window) moves or is resized, change the input shape to "follow"
    // it, so that it always remains clickable.

    attached_inspector_input_rect.x = allocation->x;
    attached_inspector_input_rect.y = allocation->y;
    attached_inspector_input_rect.width = allocation->width;
    attached_inspector_input_rect.height = allocation->height;

    realize_input_shape();
}

gulong inspector_size_allocate_handler_id = 0;

bool on_inspector_attach(WebKitWebInspector *inspector, gpointer user_data) {
    // When the web inspector attaches to the overlay window, begin tracking
    // its allocated position on screen.

    WebKitWebViewBase *inspector_web_view = webkit_web_inspector_get_web_view(
            inspector);
    inspector_size_allocate_handler_id =
        g_signal_connect(GTK_WIDGET(inspector_web_view), "size-allocate",
                G_CALLBACK(on_inspector_size_allocate), NULL);
    return FALSE; // Allow attach
}
bool on_inspector_detach(WebKitWebInspector *inspector, gpointer user_data) {
    // When the web inspector detaches from the overlay window, stop tracking
    // its position, and zero out its input shape rectangle.

    WebKitWebViewBase *inspector_web_view = webkit_web_inspector_get_web_view(
            inspector);
    g_signal_handler_disconnect(GTK_WIDGET(inspector_web_view),
            inspector_size_allocate_handler_id);
    inspector_size_allocate_handler_id = 0;
    attached_inspector_input_rect.x = 0;
    attached_inspector_input_rect.y = 0;
    attached_inspector_input_rect.width = 0;
    attached_inspector_input_rect.height = 0;
    realize_input_shape();
    return FALSE; // Allow detach
}

bool on_page_load_failed(WebKitWebView *web_view, WebKitLoadEvent load_event,
        gchar *failing_uri, GError *error, gpointer user_data) {
    // Show a custom error page
    char page_content[2000];
    snprintf(page_content, sizeof(page_content), "\
<html>\n\
<head>\n\
<style>\n\
    body { background : rgba(255,0,0,0.2) }\n\
    h1 { color : white; filter: drop-shadow(0 0 0.75rem black); }\n\
</style>\n\
</head>\n\
<h1>%s<br>%s</h1>\n\
</html>", error->message, failing_uri);
    // `failing_uri` is URL-escaped, so it's fine to inject.
    // TODO URI-escape the error message for maximum paranoia?
    webkit_web_view_load_alternate_html(
            web_view, page_content, failing_uri, NULL);

    // Don't call other handlers
    return FALSE;
}

void printUsage(char *programName) {
    fprintf(stderr, "\
USAGE: %s <URL> [--help] [--webkit-settings option1=value1,...]\n\
\n\
    <URL>\n\
        Universal Resource Locator to be loaded on the overlay web view.\n\
        For example, to load a local file, you'd pass something like:\n\
\n\
            file:///home/mary/test.html\n\
\n\
        or to load from a local web server at port 4000:\n\
\n\
            http://localhost:4000\n\
\n\
    --inspect\n\
        Open the Web Inspector (dev tools) on start.\n\
\n\
    --webkit-settings\n\
        Followed by comma-separated setting names to pass to the WebKit web\n\
        view; for details of what options are available, see the list at \n\
        https://webkitgtk.org/reference/webkit2gtk/stable/WebKitSettings.html\n\
        Please format setting names as underscore_separated_words.\n\
\n\
        Default settings are the same as WebKit's defaults, plus these two:\n\
         - enable_write_console_messages_to_stdout\n\
         - enable_developer_extras\n\
\n\
        Boolean options can look like\n\
            option_name\n\
            option_name=TRUE\n\
            option_name=FALSE\n\
        String and integer options can look like\n\
            option_name=foo\n\
            option_name=42\n\
            option_name=0xbeef\n\
        The enum option hardware_acceleration_policy has these valid values\n\
            ON_DEMAND, ALWAYS, NEVER\n\
\n\
    --help\n\
        Print this help text, then exit.\n\
\n\
    All of the standard GTK debug options and env variables are also\n\
    supported.  You probably won't need them, but you can find a list here:\n\
    https://developer.gnome.org/gtk3/stable/gtk-running.html\n\
\n",
        programName);
}

int main(int argc, char **argv) {

    gtk_init(&argc, &argv);

    //
    // Parse command line options
    //

    // Turn on some WebKit settings by default:
    WebKitSettings *wk_settings = webkit_settings_new();
    // Allow using web inspector
    webkit_settings_set_enable_developer_extras(wk_settings, TRUE);
    // Console logs are shown on stdout
    webkit_settings_set_enable_write_console_messages_to_stdout(wk_settings, TRUE);

    char *target_url = NULL;
    bool open_inspector_immediately = FALSE;

    for (int i = 1; i < argc; ++i) {
        // Handle flag arguments
        if      (!strcmp(argv[i], "--help")) { printUsage(argv[0]); exit(0); }
        else if (!strcmp(argv[i], "--inspect")) open_inspector_immediately = TRUE;
        else if (!strcmp(argv[i], "--webkit-settings")) {
            ++i;
            char *comma_separated_entries = argv[i];
            // `comma_separated_entries` should look something like
            //
            //     key1=value1,key2=value2
            //

            // Separate the entries, and loop over them.
            //
            // Note that strtok and strtok_r mutate their input string by
            // replacing the separator with \0.  We don't care, since we're not
            // going to use argv[i] anymore once we have pointers to all the
            // useful strings inside it.
            //
            // We need to use the re-entrant version (strtok_r) in the outer
            // loop, so nothing gets mixed up when the loop body calls the
            // standard version (strtok) before the loop's strtok has finished
            // iterating.
            char *strtok_savepoint;
            for (char *entry = strtok_r(
                        comma_separated_entries, ",", &strtok_savepoint);
                    entry != NULL;
                    entry = strtok_r(NULL, ",", &strtok_savepoint)) {
                // `entry` at this point looks is something like
                //
                //     key=value
                //
                // or possibly just
                //
                //     key
                //

                // We can cut at the "=" to separate the key and value.  If the
                // there was no "=", the value ends up NULL.
                char *key = strtok(entry, "=");
                char *value = strtok(NULL, "=");

                // Unfortunately, no function exists to set a WebKit setting
                // generically, by its string name.  Instead, there is a
                // separate setter function for each setting name.
                //
                // So we'll generate them using macros.

                #define BOOLEAN_SETTING(SETTING_NAME) \
                if (!strcmp(key, #SETTING_NAME)) {\
                    bool actual_value = TRUE;\
                    if (value == NULL) actual_value = TRUE;\
                    else if (!strcmp(value, "TRUE")) actual_value = TRUE;\
                    else if (!strcmp(value, "FALSE")) actual_value = FALSE;\
                    else {\
                        fprintf(stderr,\
                                "Invalid value for %s: %s ", key, value);\
                        fprintf(stderr, "(expected TRUE or FALSE)\n");\
                        exit(3);\
                    }\
                    webkit_settings_set_ ## SETTING_NAME (\
                            wk_settings, actual_value);\
                    continue;\
                }

                BOOLEAN_SETTING(enable_developer_extras);
                BOOLEAN_SETTING(allow_file_access_from_file_urls);
                BOOLEAN_SETTING(allow_modal_dialogs);
                BOOLEAN_SETTING(allow_top_navigation_to_data_urls);
                BOOLEAN_SETTING(allow_universal_access_from_file_urls);
                BOOLEAN_SETTING(auto_load_images);
                BOOLEAN_SETTING(draw_compositing_indicators);
                BOOLEAN_SETTING(enable_accelerated_2d_canvas);
                BOOLEAN_SETTING(enable_back_forward_navigation_gestures);
                BOOLEAN_SETTING(enable_caret_browsing);
                BOOLEAN_SETTING(enable_developer_extras);
                BOOLEAN_SETTING(enable_dns_prefetching);
                BOOLEAN_SETTING(enable_encrypted_media);
                BOOLEAN_SETTING(enable_frame_flattening);
                BOOLEAN_SETTING(enable_fullscreen);
                BOOLEAN_SETTING(enable_html5_database);
                BOOLEAN_SETTING(enable_html5_local_storage);
                BOOLEAN_SETTING(enable_hyperlink_auditing);
                BOOLEAN_SETTING(enable_java);
                BOOLEAN_SETTING(enable_javascript);
                BOOLEAN_SETTING(enable_javascript_markup);
                BOOLEAN_SETTING(enable_media);
                BOOLEAN_SETTING(enable_media_capabilities);
                BOOLEAN_SETTING(enable_media_stream);
                BOOLEAN_SETTING(enable_mediasource);
                BOOLEAN_SETTING(enable_mock_capture_devices);
                BOOLEAN_SETTING(enable_offline_web_application_cache);
                BOOLEAN_SETTING(enable_page_cache);
                BOOLEAN_SETTING(enable_plugins);
                // Deprecated.
                //BOOLEAN_SETTING(enable_private_browsing);
                BOOLEAN_SETTING(enable_resizable_text_areas);
                BOOLEAN_SETTING(enable_site_specific_quirks);
                BOOLEAN_SETTING(enable_smooth_scrolling);
                BOOLEAN_SETTING(enable_spatial_navigation);
                BOOLEAN_SETTING(enable_tabs_to_links);
                BOOLEAN_SETTING(enable_webaudio);
                BOOLEAN_SETTING(enable_webgl);
                BOOLEAN_SETTING(enable_write_console_messages_to_stdout);
                BOOLEAN_SETTING(enable_xss_auditor);
                BOOLEAN_SETTING(javascript_can_access_clipboard);
                BOOLEAN_SETTING(javascript_can_open_windows_automatically);
                BOOLEAN_SETTING(load_icons_ignoring_image_load_setting);
                BOOLEAN_SETTING(media_playback_allows_inline);
                BOOLEAN_SETTING(media_playback_requires_user_gesture);
                BOOLEAN_SETTING(print_backgrounds);
                BOOLEAN_SETTING(zoom_text_only);

                #define STRING_SETTING(SETTING_NAME) \
                if (!strcmp(key, #SETTING_NAME)) {\
                    webkit_settings_set_ ## SETTING_NAME (\
                            wk_settings, value);\
                    continue;\
                }

                STRING_SETTING(cursive_font_family);
                STRING_SETTING(default_charset);
                STRING_SETTING(default_font_family);
                STRING_SETTING(fantasy_font_family);
                STRING_SETTING(media_content_types_requiring_hardware_support);
                STRING_SETTING(monospace_font_family);
                STRING_SETTING(pictograph_font_family);
                STRING_SETTING(sans_serif_font_family);
                STRING_SETTING(serif_font_family);
                STRING_SETTING(user_agent);

                // TODO exit(3) when can't parse int, instead of continuing
                #define INT_SETTING(SETTING_NAME) \
                if (!strcmp(key, #SETTING_NAME)) {\
                    int actual_value = strtoimax(value, NULL, 10);\
                    webkit_settings_set_ ## SETTING_NAME (\
                            wk_settings, actual_value);\
                    continue;\
                }

                INT_SETTING(default_font_size);
                INT_SETTING(default_monospace_font_size);
                INT_SETTING(minimum_font_size);

                // This one setting takes a named constant, so we'll look up
                // the right one.
                if (!strcmp(key, "hardware_acceleration_policy")) {
                    if (strcmp(value, "ON_DEMAND")) {
                        webkit_settings_set_hardware_acceleration_policy(
                                wk_settings,
                                WEBKIT_HARDWARE_ACCELERATION_POLICY_ON_DEMAND);
                    } else if (strcmp(value, "ALWAYS")) {
                        webkit_settings_set_hardware_acceleration_policy(
                                wk_settings,
                                WEBKIT_HARDWARE_ACCELERATION_POLICY_ALWAYS);
                    } else if (strcmp(value, "NEVER")) {
                        webkit_settings_set_hardware_acceleration_policy(
                                wk_settings,
                                WEBKIT_HARDWARE_ACCELERATION_POLICY_NEVER);
                    } else {
                        fprintf(stderr,
                                "Invalid '%s' setting value '%s'.\n",
                                key, value);
                        fprintf(stderr,
                                "Valid options: ON_DEMAND, ALWAYS, NEVER");
                        exit(3);
                    }
                    continue;
                }


                // If we get this far, the option name must be something we
                // don't recognise.
                fprintf(stderr,
                        "Unknown WebKit setting '%s'.\n", key);
                exit(3);
            }
        }
        else {
            // Handle positional arguments.  Should be only 1: the target URL.
            if (!target_url) {
                target_url = argv[i];
            } else {
                fprintf(stderr, "Too many positional arguments!\n\n");
                printUsage(argv[0]);
                exit(1);
            }
        }
    }

    if (target_url == NULL) {
        fprintf(stderr, "No target URL specified!\n\n");
        printUsage(argv[0]);
        exit(2);
    }

    //
    // Create the window
    //

    // Create the window that will become our overlay
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_gravity(GTK_WINDOW(window), GDK_GRAVITY_NORTH_WEST);
    gtk_window_move(GTK_WINDOW(window), 0, 0);
    gtk_window_set_title(GTK_WINDOW(window), "hudkit overlay window");
    g_signal_connect(G_OBJECT(window), "delete-event", gtk_main_quit, NULL);
    gtk_widget_set_app_paintable(window, TRUE);

    // Set up a callback to react to screen changes
    g_signal_connect(G_OBJECT(window), "screen-changed",
            G_CALLBACK(screen_changed), NULL);

    //
    // Set up the WebKit web view widget
    //

    // Disable caching
    WebKitWebContext *wk_context = webkit_web_context_get_default();
    webkit_web_context_set_cache_model(wk_context,
            WEBKIT_CACHE_MODEL_DOCUMENT_VIEWER);
    WebKitWebView *web_view = WEBKIT_WEB_VIEW(
            webkit_web_view_new_with_context(wk_context));

    // Use the webview settings we parsed out of argv earlier
    webkit_web_view_set_settings(web_view, wk_settings);

    // Listen for page load failures, so we can show a custom error page.
    //
    // This doesn't fire for HTTP failures; those still get whatever page the
    // server sends back.  This fires for failures at a level below HTTP, for
    // when the server can't be found and such.
    g_signal_connect(web_view, "load-failed",
            G_CALLBACK(on_page_load_failed), NULL);

    // Initialise inspector, and start tracking when it's attached to or
    // detached from the overlay window.
    WebKitWebInspector *inspector = webkit_web_view_get_inspector(
            WEBKIT_WEB_VIEW(web_view));
    g_signal_connect(inspector, "attach",
            G_CALLBACK(on_inspector_attach), NULL);
    g_signal_connect(inspector, "detach",
            G_CALLBACK(on_inspector_detach), NULL);

    if (open_inspector_immediately) {
        webkit_web_inspector_show(WEBKIT_WEB_INSPECTOR(inspector));
    }

    // Make transparent
    GdkRGBA rgba = { .alpha = 0.0 };
    webkit_web_view_set_background_color(web_view, &rgba);
    gtk_container_add(GTK_CONTAINER(window), GTK_WIDGET(web_view));

    // Load the given URL
    webkit_web_view_load_uri(web_view, target_url);

    //
    // Position the overlay window, and make it input-transparent
    //

    // Initialise the window and make it active.  We need this so it can resize
    // it correctly.
    screen_changed(window, NULL, NULL);

    GdkDisplay *display = gdk_display_get_default();
    GdkRectangle *rectangles = NULL;
    int nRectangles = get_monitor_rects(display, &rectangles);

    gtk_widget_show_all(window);

    // Hide the window, so we can get our properties ready without the window
    // manager trying to mess with us.
    GdkWindow *gdk_window = gtk_widget_get_window(GTK_WIDGET(window));
    gdk_window_hide(GDK_WINDOW(gdk_window));

    // "Can't touch this!" - to the window manager
    //
    // The override-redirect flag prevents the window manager taking control of
    // the window, so it remains in our control.
    gdk_window_set_override_redirect(GDK_WINDOW(gdk_window), true);
    // But just in case, light up the flags like a Christmas tree, with all the
    // WM hints we can think of to try to convince whatever that's reading them
    // (probably a window manager) to keep this window on-top and fullscreen
    // but otherwise leave it alone.
    gtk_window_set_keep_above       (GTK_WINDOW(window), true);
    gtk_window_set_skip_taskbar_hint(GTK_WINDOW(window), true);
    gtk_window_set_accept_focus     (GTK_WINDOW(window), true);
    gtk_window_set_decorated        (GTK_WINDOW(window), false);
    gtk_window_set_resizable        (GTK_WINDOW(window), false);

    // "Can't touch this!" - to user actions
    //
    // Set the input shape (area where clicks are recognised) to a zero-width,
    // zero-height region a.k.a. nothing.  This makes clicks pass through the
    // window onto whatever's below.
    gdk_window_input_shape_combine_region(GDK_WINDOW(gdk_window),
            cairo_region_create(), 0,0);

    // Now it's safe to show the window again.  It should be click-through, and
    // the WM should ignore it.
    gdk_window_show(GDK_WINDOW(gdk_window));

    //
    // Set up the JavaScript API
    //

    // Set up listeners for calls from JavaScript.
    WebKitUserContentManager *manager =
        webkit_web_view_get_user_content_manager(WEBKIT_WEB_VIEW(web_view));
    g_signal_connect(manager, "script-message-received::getMonitorLayout",
            G_CALLBACK(on_js_call_get_monitor_layout), web_view);
    g_signal_connect(manager, "script-message-received::setClickableAreas",
            G_CALLBACK(on_js_call_set_clickable_areas), web_view);
    g_signal_connect(manager, "script-message-received::showInspector",
            G_CALLBACK(on_js_call_show_inspector), web_view);

    // Set up message handlers on the JavaScript side.  These appear under
    // window.webkit.messageHandlers.
    webkit_user_content_manager_register_script_message_handler(manager,
            "getMonitorLayout");
    webkit_user_content_manager_register_script_message_handler(manager,
            "setClickableAreas");
    webkit_user_content_manager_register_script_message_handler(manager,
            "showInspector");

    // Set up our Hudkit object to be loaded in the browser JS before anything
    // else does.  Its functions are wrappers around the appropriate WebKit
    // message handlers we just set up above.
    //
    // The `_pendingCallbacks` property is un-enumerable, so it doesn't show up
    // in console.log or such.  It would be nice to hide it properly by closing
    // over it (like `nextCallbackId` is), but it needs to be accessible
    // externally by `call_js_callback`.
    webkit_user_content_manager_add_script(
            manager,
            webkit_user_script_new("\
let nextCallbackId = 0\n\
window.Hudkit = {\n\
  getMonitorLayout: function (callback) {\n\
    const id = nextCallbackId++\n\
    window.Hudkit._pendingCallbacks[id] = callback\n\
    window.webkit.messageHandlers.getMonitorLayout.postMessage(id)\n\
  },\n\
  setClickableAreas: function (rectangles, callback) {\n\
    callback = callback || (function () {})\n\
    const id = nextCallbackId++\n\
    window.Hudkit._pendingCallbacks[id] = callback\n\
    window.webkit.messageHandlers.setClickableAreas.postMessage({id, rectangles})\n\
  },\n\
  showInspector: function (callback) {\n\
    callback = callback || (function () {})\n\
    const id = nextCallbackId++\n\
    window.Hudkit._pendingCallbacks[id] = callback\n\
    window.webkit.messageHandlers.showInspector.postMessage(id)\n\
  },\n\
}\n\
Object.defineProperty(window.Hudkit, '_pendingCallbacks', {\n\
  value: [],\n\
  enumerable: false,\n\
  configurable: false,\n\
  writable: true,\n\
})\n\
                ",
                WEBKIT_USER_CONTENT_INJECT_TOP_FRAME,
                WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_START,
                NULL, NULL));

    // Start main UI loop
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
        exit(69); // memes
    }

    // Ensure the widget can take RGBA
    gtk_widget_set_visual(widget, gdk_screen_get_rgba_visual(screen));

    size_to_screen(GTK_WINDOW(widget));
}
