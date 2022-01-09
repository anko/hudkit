#define _POSIX_C_SOURCE
// Library include           // What it's used for
// --------------------------//-------------------
#include <gtk/gtk.h>         // windowing
#include <gdk/gdk.h>         // low-level windowing
#include <gdk/gdkmonitor.h>  // monitor counting
#include <webkit2/webkit2.h> // web view
#include <stdlib.h>          // exit
#include <stdio.h>           // files
#include <inttypes.h>        // string to int conversion
#include <signal.h>          // handling SIGUSR1
#include <string.h>          // string parsing for --webkit-settings

// Overlay window handle.  Global because almost everything touches it.
GtkWidget *window;
WebKitWebInspector *inspector;

void show_inspector(bool startAttached) {
    // For some reason calling this twice makes it start detached, but the
    // inspector doesn't seem to respond in any way to the actual functions
    // that are supposed put it in detached or attached mode.  It is a
    // mysterious creature.
    webkit_web_inspector_show(inspector);
    if (!startAttached)
        webkit_web_inspector_show(inspector);
}

void on_signal_sigusr1(int signal_number) {
    show_inspector(FALSE);
}

static void screen_changed(GtkWidget *widget, GdkScreen *old_screen,
        gpointer user_data);
static void composited_changed(GdkScreen *screen, gpointer user_data);
static void on_close_web_view(WebKitWebView *web_view, gpointer user_data);

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
GArray *user_defined_input_rects;

void realize_input_shape() {
    // Our input shape for the overall window should be the rectangles set by
    // the user, and the rectangle of the attached web inspector (if
    // applicable), all merged together into one shape.

    cairo_region_t *shape = cairo_region_create_rectangle(
            &attached_inspector_input_rect);
    for (int i = 0; i < user_defined_input_rects->len; ++i) {
        cairo_rectangle_int_t rect = g_array_index(
                    user_defined_input_rects,
                    cairo_rectangle_int_t,
                    i);
        cairo_region_union_rectangle(shape, &rect);
    }

    GdkWindow *gdk_window = gtk_widget_get_window(GTK_WIDGET(window));
    if (gdk_window) // This might be NULL if this gets called during initialisation
        gdk_window_input_shape_combine_region(gdk_window, shape, 0,0);
    cairo_region_destroy(shape);
}

static void on_js_call_finished(GObject *object, GAsyncResult *result,
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

    GString *response_buffer = g_string_new(NULL);
    g_string_append_printf(response_buffer,
            "window.Hudkit._pendingCallbacks[%i].resolve(%s)"
            "\ndelete window.Hudkit._pendingCallbacks[%i]",
            callbackId, stringifiedData, callbackId);

    char *finished_buffer = g_string_free(response_buffer, FALSE);
    webkit_web_view_run_javascript(
            web_view, finished_buffer, NULL, on_js_call_finished, NULL);
    g_free(finished_buffer);
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

    GString *response_buffer = g_string_new("[");
    for (int i = 0; i < nRectangles; ++i) {

        g_string_append(response_buffer, "{name:'");

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
                    g_string_append_c(response_buffer, '\\');
                    g_string_append_c(response_buffer, charHere);
                    break;

                // Characters excluded because they're part of LineTerminator:
                // <LF> (line feed)
                case '\n':
                    g_string_append(response_buffer, "\\n");
                    break;
                // <CR> (carriage return)
                case '\r':
                    g_string_append(response_buffer, "\\r");
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
                    g_string_append_c(response_buffer, charHere);
                    break;
            }
        }

        GdkRectangle rect = rectangles[i];
        g_string_append_printf(response_buffer,
                "',x:%i,y:%i,width:%i,height:%i},",
                rect.x, rect.y, rect.width, rect.height);
    }
    g_string_append(response_buffer, "]");

    // Discard the GString structure and take ownership of the underlying
    // cstring memory.
    char *finished_buffer = g_string_free(response_buffer, FALSE);
    call_js_callback(web_view, callbackId, finished_buffer);
    g_free(finished_buffer);
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

    g_array_set_size(user_defined_input_rects, nRectangles);

    JSCValue *jsRectangles = jsc_value_object_get_property(jsValue, "rectangles");
    for (int i = 0; i < user_defined_input_rects->len; ++i) {
        JSCValue *jsRect = jsc_value_object_get_property_at_index(jsRectangles, i);
        cairo_rectangle_int_t *rect = &g_array_index(
                user_defined_input_rects, GdkRectangle, i);

        // Anything undefined is interpreted by `jsc_value_to_int32` as 0.
        rect->x = jsc_value_to_int32(
                 jsc_value_object_get_property(jsRect, "x"));
        rect->y = jsc_value_to_int32(
                 jsc_value_object_get_property(jsRect, "y"));
        rect->width = jsc_value_to_int32(
                 jsc_value_object_get_property(jsRect, "width"));
        rect->height = jsc_value_to_int32(
                jsc_value_object_get_property(jsRect, "height"));
    }

    realize_input_shape();

    call_js_callback(web_view, callbackId, "");
}
void on_js_call_show_inspector(WebKitUserContentManager *manager,
        WebKitJavascriptResult *sentData,
        gpointer arg) {
    WebKitWebView *web_view = WEBKIT_WEB_VIEW(arg);
    JSCValue *jsValue = webkit_javascript_result_get_js_value(sentData);
    int callbackId = jsc_value_to_int32(jsc_value_object_get_property(jsValue, "id"));
    bool startAttached = jsc_value_to_boolean(
            jsc_value_object_get_property(jsValue, "shouldAttachToWindow"));

    WebKitWebInspector *inspector = webkit_web_view_get_inspector(
            WEBKIT_WEB_VIEW(web_view));
    show_inspector(startAttached);

    call_js_callback(web_view, callbackId, "");
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

void show_attached_inspector_no_keyboard_advice(WebKitWebView *web_view) {
    webkit_web_view_run_javascript(
            web_view, "console.info('Note that when the Web Inspector is"
            " attached to the Hudkit window, you cannot type into it, because"
            " the overlay window does not receive keyboard events.  To type"
            " into this console, detach the Inspector into its own window.')",
            NULL, on_js_call_finished, NULL);
}
bool on_inspector_attach(WebKitWebInspector *inspector, gpointer user_data) {
    // When the web inspector attaches to the overlay window, begin tracking
    // its allocated position on screen.
    WebKitWebView *web_view = (WebKitWebView *) user_data;

    WebKitWebViewBase *inspector_web_view = webkit_web_inspector_get_web_view(
            inspector);
    inspector_size_allocate_handler_id =
        g_signal_connect(GTK_WIDGET(inspector_web_view), "size-allocate",
                G_CALLBACK(on_inspector_size_allocate), NULL);

    static GOnce show_no_keyboard_advice_once = G_ONCE_INIT;
    g_once(&show_no_keyboard_advice_once,
            (void * (*)(void *))show_attached_inspector_no_keyboard_advice,
            web_view);

    return FALSE; // Allow attach
}
bool on_inspector_detach(WebKitWebInspector *inspector, gpointer user_data) {
    // When the web inspector detaches from the overlay window, stop tracking
    // its position, and zero out its input shape rectangle.

    WebKitWebViewBase *inspector_web_view = webkit_web_inspector_get_web_view(
            inspector);
    if (inspector_size_allocate_handler_id)
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
    printf(
"USAGE: %s <URL> [--help] [--webkit-settings option1=value1,...]"
"\n"
"\n    <URL>"
"\n        Universal Resource Locator to be loaded on the overlay web view."
"\n        For example, to load a local file, you'd pass something like:"
"\n"
"\n            file:///home/mary/test.html"
"\n"
"\n        or to load from a local web server at port 4000:"
"\n"
"\n            http://localhost:4000"
"\n"
"\n    --inspect"
"\n        Open the Web Inspector (dev tools) on start."
"\n"
"\n    --webkit-settings <settings>"
"\n        The <settings> should be a comma-separated list of settings."
"\n"
"\n        Boolean settings can look like"
"\n            option-name"
"\n            option-name=TRUE"
"\n            option-name=FALSE"
"\n"
"\n        String, integer, and enum options look like"
"\n            option-name=foo"
"\n            option-name=42"
"\n"
"\n        To see settings available on your system's WebKit version, their"
"\n        valid values, and default values, pass '--webkit-settings help'."
"\n"
"\n        To see explanations of the settings, see"
"\n        https://webkitgtk.org/reference/webkit2gtk/stable/WebKitSettings.html"
"\n"
"\n    --help"
"\n        Print this help text, then exit."
"\n"
"\n    All of the standard GTK debug options and env variables are also"
"\n    supported.  You probably won't need them, but you can find a list here:"
"\n    https://developer.gnome.org/gtk3/stable/gtk-running.html"
"\n",
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

            // Fetch all the WebKitSettings object's properties, so we can
            // check whether the following argument contains keys and values
            // that exist in it.  It derives from GObject, so we can use GLib's
            // facilities to operate on its contents generically.
            //
            // This insulates us from changes in what settings are supported,
            // whether due to upstream WebKit developers adding or removing
            // them, or distros or users building libwebkit in some custom way.
            guint n_setting_properties;
            GParamSpec **setting_properties = g_object_class_list_properties(
                    G_OBJECT_GET_CLASS(wk_settings), &n_setting_properties);

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

                // If we get the special key "help", print the available WebKit
                // settings and their value types, and exit.
                if (!strcmp(key, "help")) {
                    printf("Available values for --webkit-settings (default in parentheses):\n");
                    for (int i = 0; i < n_setting_properties; ++i) {
                        GParamSpec *prop = setting_properties[i];
                        GType type = prop->value_type;

                        printf(" • ");
                        printf("%s", prop->name);

                        if (g_type_is_a(type, G_TYPE_BOOLEAN)) {
                            bool v;
                            g_object_get(wk_settings, prop->name, &v, NULL);
                            printf(" (%s)", v ? "TRUE" : "FALSE");
                        } else if (g_type_is_a(type, G_TYPE_UINT)) {
                            printf("=<integer>");
                            guint v;
                            g_object_get(wk_settings, prop->name, &v, NULL);
                            printf(" (%d)", v);
                        }
                        else if (g_type_is_a(type, G_TYPE_STRING)) {
                            printf("=<string>");
                            char *v;
                            g_object_get(wk_settings, prop->name, &v, NULL);
                            printf(" ('%s')", v == NULL ? "" : v);
                            g_free(v);
                        } else if (g_type_is_a(type, G_TYPE_ENUM)) {
                            printf("=");
                            GEnumClass *enum_class = (GEnumClass *)
                                g_type_class_ref(type);
                            for (int j = 0; j < enum_class->n_values; ++j) {
                                GEnumValue enum_value = enum_class->values[j];
                                printf("%s", enum_value.value_nick);
                                if (j < enum_class->n_values - 1) printf("|");
                            }
                            gint v;
                            g_object_get(wk_settings, prop->name, &v, NULL);
                            printf(" (%s)",
                                    g_enum_get_value(enum_class, v)->value_nick);
                        } else printf("%s", g_type_name(type));

                        if (prop->flags & G_PARAM_DEPRECATED)
                            printf( " [⚠ DEPRECATED]");

                        printf("\n");
                    }
                    exit(0);
                }

                for (int i = 0; i < n_setting_properties; ++i) {
                    GParamSpec *setting_property = setting_properties[i];

                    // Skip non-matching entries.
                    if (strcmp(setting_property->name, key)) continue;

                    // Parse the option according to what the GObject type of
                    // that settings property is.
                    //
                    // We use GObject metadata stuff to ease maintenance load,
                    // so when upstream WebKit changes things, we don't have to
                    // be updating a big hardcoded list of settings.

                    // Boolean settings can be 'key', 'key=TRUE' or 'key=FALSE'
                    if (g_type_is_a(
                                setting_property->value_type,
                                G_TYPE_BOOLEAN)) {
                        bool actual_value;
                        if (value == NULL) actual_value = TRUE;
                        else if (!strcmp(value, "TRUE")) actual_value = TRUE;
                        else if (!strcmp(value, "FALSE")) actual_value = FALSE;
                        else {
                            fprintf(stderr,
                                    "Invalid value for %s: %s ", key, value);
                            fprintf(stderr, "(expected TRUE or FALSE)\n");
                            exit(3);
                        }
                        g_object_set(wk_settings,
                                setting_property->name, actual_value,
                                NULL);
                        goto next;

                    // String settings must be 'key=value', and we can directly
                    // use the value string.
                    } else if (g_type_is_a(setting_property->value_type,
                                G_TYPE_STRING)) {
                        g_object_set(wk_settings,
                                setting_property->name, value,
                                NULL);
                        goto next;

                    // Unsigned integer settings must be 'key=value', but we
                    // have to parse the value string into an integer first.
                    } else if (g_type_is_a(setting_property->value_type,
                                G_TYPE_UINT)) {
                        guint32 actual_value = strtoimax(value, NULL, 10);
                        g_object_set(wk_settings,
                                setting_property->name, actual_value,
                                NULL);
                        goto next;

                    // Enumeration settings must be 'key=value', but the value
                    // string must be an allowed option for that enum.
                    } else if (g_type_is_a(setting_property->value_type,
                                G_TYPE_ENUM)) {

                        // Convert the GTypeClass of the property to an
                        // GEnumClass, so we can have a look through its
                        // allowed values.
                        GEnumClass *enum_class = (GEnumClass *)
                            g_type_class_ref(setting_property->value_type);
                        bool is_valid = false;
                        for (int j = 0; j < enum_class->n_values; ++j) {
                            GEnumValue enum_value = enum_class->values[j];
                            //printf("Allowed enum value: %s\n", enum_value.value_nick);
                            if (!strcmp(enum_value.value_nick, value)) {
                                is_valid = true;
                                break;
                            }
                        }

                        if (is_valid) {
                            gint actual_value =
                                g_enum_get_value_by_nick(enum_class, value)
                                ->value;
                            g_object_set(wk_settings, key, actual_value, NULL);

                        } else {
                            fprintf(stderr,
                                    "Invalid WebKit setting '%s=%s'\n",
                                    key, value);
                            fprintf(stderr, "Allowed values for '%s':\n", key);
                            for (int j = 0; j < enum_class->n_values; ++j) {
                                GEnumValue enum_value = enum_class->values[j];
                                printf("- %s\n", enum_value.value_nick);
                            }
                            exit(5);
                        }

                        g_type_class_unref(enum_class);
                        goto next;

                    } else {
                        fprintf(stderr, "Cannot parse value for setting '%s':\n",
                                setting_property->name);
                        printf("    The setting exists, but we have no parser for its type '%s'.\n",
                                g_type_name(setting_property->value_type));

                        exit(4);
                    }
                }

                fprintf(stderr, "No such webkit setting: %s\n", key);
                exit(3);

next:
                continue;
            }
            free(setting_properties);
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

    // Initialise the array of user-JS-defined clickable areas to empty
    user_defined_input_rects = g_array_new(
            FALSE, // don't NULL-terminate
            TRUE,  // zero memory
            sizeof(cairo_rectangle_int_t));

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

    //
    // Set up the WebKit web view widget
    //

    // Disable caching
    WebKitWebContext *wk_context = webkit_web_context_get_default();
    webkit_web_context_set_cache_model(wk_context,
            WEBKIT_CACHE_MODEL_DOCUMENT_VIEWER);
    WebKitWebView *web_view = WEBKIT_WEB_VIEW(
            webkit_web_view_new_with_context(wk_context));

    // Set up a callback to react to screen changes
    g_signal_connect(window, "screen-changed",
            G_CALLBACK(screen_changed), web_view);
    // Set up a callback to react to screen compositing changes
    g_signal_connect(window, "composited-changed",
            G_CALLBACK(composited_changed), web_view);

    // Set up a callback to react to window.close() being called from JS within
    // the WebView
    g_signal_connect(web_view, "close",
            G_CALLBACK(on_close_web_view), wk_context);

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
    inspector = webkit_web_view_get_inspector(
            WEBKIT_WEB_VIEW(web_view));
    g_signal_connect(inspector, "attach",
            G_CALLBACK(on_inspector_attach), web_view);
    g_signal_connect(inspector, "detach",
            G_CALLBACK(on_inspector_detach), NULL);

    if (open_inspector_immediately) {
        show_inspector(FALSE);
    }

    struct sigaction usr1_action = {
        .sa_handler = on_signal_sigusr1
    };
    sigaction(SIGUSR1, &usr1_action, NULL);

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
    screen_changed(window, NULL, web_view);

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
    gtk_window_set_skip_pager_hint  (GTK_WINDOW(window), true);
    gtk_window_set_focus_on_map     (GTK_WINDOW(window), false);
    gtk_window_set_accept_focus     (GTK_WINDOW(window), true);
    gtk_window_set_decorated        (GTK_WINDOW(window), false);
    gtk_window_set_resizable        (GTK_WINDOW(window), false);

    // "Can't touch this!" - to user actions
    //
    // Set the input shape (area where clicks are recognised) to a zero-width,
    // zero-height region a.k.a. nothing.  This makes clicks pass through the
    // window onto whatever's below.
    realize_input_shape();

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
            webkit_user_script_new(
"\nlet nextCallbackId = 0"
"\nwindow.Hudkit = {"
"\n  on: function (eventName, callback) {"
"\n    if (window.Hudkit._listeners.has(eventName)) {"
"\n      window.Hudkit._listeners.get(eventName).push(callback)"
"\n    } else {"
"\n      window.Hudkit._listeners.set(eventName, [callback])"
"\n    }"
"\n  },"
"\n  off: function (eventName, callback) {"
"\n    const listenersForThisEvent = window.Hudkit._listeners.get(eventName)"
"\n    if (listenersForThisEvent) {"
"\n      listenersForThisEvent.splice(listenersForThisEvent.indexOf(callback), 1)"
"\n    }"
"\n  },"
"\n  getMonitorLayout: async function () {"
"\n    return new Promise((resolve, reject) => {"
"\n      const id = nextCallbackId++"
"\n      window.Hudkit._pendingCallbacks[id] = { resolve, reject }"
"\n      window.webkit.messageHandlers.getMonitorLayout.postMessage(id)"
"\n    })"
"\n  },"
"\n  setClickableAreas: async function (rectangles) {"
"\n    return new Promise((resolve, reject) => {"
"\n      const id = nextCallbackId++"
"\n      window.Hudkit._pendingCallbacks[id] = { resolve, reject }"
"\n      window.webkit.messageHandlers.setClickableAreas.postMessage({id, rectangles})"
"\n    })"
"\n  },"
"\n  showInspector: async function (shouldAttachToWindow) {"
"\n    shouldAttachToWindow = shouldAttachToWindow ? true : false"
"\n    return new Promise((resolve, reject) => {"
"\n      const id = nextCallbackId++"
"\n      window.Hudkit._pendingCallbacks[id] = { resolve, reject }"
"\n      window.webkit.messageHandlers.showInspector.postMessage({id, shouldAttachToWindow})"
"\n    })"
"\n  },"
"\n}"
"\nObject.defineProperty(window.Hudkit, '_pendingCallbacks', {"
"\n  value: [],"
"\n  enumerable: false,"
"\n  configurable: false,"
"\n  writable: true,"
"\n})"
"\nObject.defineProperty(window.Hudkit, '_listeners', {"
"\n  value: new Map(),"
"\n  enumerable: false,"
"\n  configurable: false,"
"\n  writable: true,"
"\n})",
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

    GdkDisplay *display = gdk_display_get_default();
    GdkRectangle *rectangles = NULL;
    int nRectangles = get_monitor_rects(display, &rectangles);

    // I can't think of a reason why someone's monitor setup might have a
    // monitor positioned origin at negative x, y coordinates, but just in case
    // someone does, we'll cover for it.
    int x = 0, y = 0, width = 0, height = 0;
    for (int i = 0; i < nRectangles; ++i) {
        GdkRectangle rect = rectangles[i];
        int left = rect.x;
        int top = rect.y;
        int right = rect.x + rect.width;
        int bottom = rect.y + rect.height;
        if (left < x) x = left;
        if (top < y) y = top;
        if (width < right) width = right;
        if (height < bottom) height = bottom;
    }
    free(rectangles);

    gtk_window_move(GTK_WINDOW(window), x, y);
    gtk_window_set_default_size(window, width, height);
    gtk_window_resize(window, width, height);
    gtk_window_set_resizable(window, false);

    // Remove the user-defined input shape, since it's certainly in completely
    // the wrong position now.
    g_array_set_size(user_defined_input_rects, 0);
    realize_input_shape();
}


void call_js_listeners(WebKitWebView *web_view, char *eventName, char *stringifiedData) {
    // Calls the user's registered JS listener functions for the given event
    // name, simply string-placing the stringified data between its call
    // parentheses.
    //
    // Ensure `stringifiedData` is sanitised!  It will basically be `eval`ed in
    // the web page's context.

    GString *response_buffer = g_string_new(NULL);
    g_string_append_printf(response_buffer,
            "\n(() => { // IIFE"
            "\n  const listenersForEvent = window.Hudkit._listeners.get('%s')"
            "\n  if (listenersForEvent) {"
            "\n    listenersForEvent.forEach(f => f(%s))"
            "\n  }"
            "\n})()",
            eventName,
            stringifiedData);

    char *finished_buffer = g_string_free(response_buffer, FALSE);
    printf("%s\n", finished_buffer);
    webkit_web_view_run_javascript(
            web_view, finished_buffer, NULL, on_js_call_finished, NULL);
    g_free(finished_buffer);
}


gulong monitors_changed_handler_id = 0;

static void on_monitors_changed(GdkScreen *screen, gpointer user_data) {
    WebKitWebView *web_view = (WebKitWebView *)user_data;
    size_to_screen(GTK_WINDOW(window));
    call_js_listeners(web_view, "monitors-changed", "");
}

// This callback runs when the window is first set to appear on some screen, or
// when it's moved to appear on another.
static void screen_changed(GtkWidget *widget, GdkScreen *old_screen,
        gpointer user_data) {
    GdkScreen *screen = gtk_widget_get_screen(widget);

    WebKitWebView *web_view = (WebKitWebView *)user_data;

    // Die unless the screen supports compositing (alpha blending)
    if (!gdk_screen_is_composited(screen)) {
        fprintf(stderr, "Your screen does not support transparency.\n");
        fprintf(stderr, "Maybe your compositor isn't running?\n");
        gtk_widget_destroy(widget);
        exit(69); // memes
    }

    // Ensure the widget can take RGBA
    gtk_widget_set_visual(widget, gdk_screen_get_rgba_visual(screen));

    // Switch monitors-changed subscription from the old screen (if applicable)
    // to the new one
    if (old_screen)
        g_signal_handler_disconnect(old_screen, monitors_changed_handler_id);
    monitors_changed_handler_id = g_signal_connect(screen, "monitors-changed",
            G_CALLBACK(on_monitors_changed), web_view);

    size_to_screen(GTK_WINDOW(widget));
}

// This callback runs when JavaScript on the page calls window.close()
static void on_close_web_view(WebKitWebView *web_view, gpointer user_data) {
    gtk_widget_destroy(GTK_WIDGET(web_view));
    exit(0);
}

// This callback runs when the screen's composited status changes.  That is,
// the screen's ability to render transparency.
static void composited_changed(GdkScreen *s, gpointer user_data) {
    WebKitWebView *web_view = (WebKitWebView *)user_data;
    GdkScreen *screen = gtk_widget_get_screen(GTK_WIDGET(web_view));
    call_js_listeners(web_view, "composited-changed",
            gdk_screen_is_composited(screen) ? "true" : "false");
}
