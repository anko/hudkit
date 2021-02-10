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
#include <uv.h>              // child process spawning
#include <assert.h>          // reporting total failure

#define assert_message(x, msg) assert(((void) msg, x))

// Overlay window handle.  Global because almost everything touches it.
GtkWidget *window;
WebKitWebInspector *inspector;
uv_loop_t *uv_loop;

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

    char buffer[sizeof(stringifiedData) + 1024];
    snprintf(buffer, sizeof(buffer), "window.Hudkit._pendingCallbacks[%i](%s);\n\
            delete window.Hudkit._pendingCallbacks[%i]",
            callbackId, stringifiedData, callbackId);

    webkit_web_view_run_javascript(
            web_view, buffer, NULL, on_js_call_finished, NULL);
}

char *escape_js_string_content(const char *string) {
    int length = strlen(string);

    // Worst-case the escaped output twice as long: if we need to escape every
    // character.  Plus 1 for the terminating \0.
    char *escaped_string = calloc(2 * length + 1, sizeof(char));
    char *end_pointer = escaped_string;

    for (int index = 0; index < length; ++index) {
        char charHere = string[index];
        // Spec for JS string literals' parsing grammar:
        // http://www.ecma-international.org/ecma-262/5.1/#sec-7.8.4
        //
        // We have to backslash-escape every character excluded from either the
        // DoubleStringCharacter or SingleStringCharacter productions.
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
            // Those last 2 are Unicode, and not representable in ASCII, which
            // we're working in, so we don't have to deal with them.
            //
            // Just in case, I checked that WebKit really does treat the script
            // as ASCII, discarding out-of range bit patterns that would form
            // valid Unicode.  It does this even if the target document is
            // declared with <meta charset="utf-8">.

            // Anything  else is fine in a JavaScript string literal.  Yes,
            // this even includes other control characters.
            default:
                memset(end_pointer++, charHere, 1);
                break;
        }
    }
    // The buffer was oversized already, and zeroed on allocation, so we
    // don't have to explicitly write a null byte to terminate it.

    return escaped_string; // Ownership passed out.
}

char *to_js_string(const char *string) {
    // Escape the given cstring and wrap it in double-quotes
    char *escaped_content = escape_js_string_content(string);
    // +3 because 2 quotes + 1 null
    int buffer_length = 3 + strlen(escaped_content);
    char *buffer = calloc(buffer_length, sizeof(char));
    snprintf(buffer, buffer_length, "\"%s\"", escaped_content);
    free(escaped_content);

    // Ownership passed out.
    return buffer;
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

        char *escaped_monitor_model_string =
            escape_js_string_content(monitor_model_string);

        GdkRectangle rect = rectangles[i];
        snprintf(
                buffer + strlen(buffer),
                sizeof(buffer),
                "{name:'%s',x:%i,y:%i,width:%i,height:%i},",
                escaped_monitor_model_string, rect.x, rect.y, rect.width, rect.height);
        // snprintf copies, so we can free here.
        free(escaped_monitor_model_string);
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
    user_defined_input_rects = reallocarray(
            user_defined_input_rects, nRectangles, sizeof(GdkRectangle));

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

struct spawn_data {
    int callbackId;
    WebKitWebView *web_view;
    uv_stdio_container_t *stdio;
    int stdio_count;
};

void call_js_process_event_listener(struct spawn_data *spawn_data,
        char *eventName, char *eventData) {
    // eventName is controlled by us, not user code, so we don't have to worry
    // about escaping it.

    // eventData might be anything; it's up to calling code to ensure it's
    // safe and escaped.

    char buffer[1024];
    snprintf(buffer, sizeof(buffer), "\
(() => { // IIFE\n\
  const handler = window.Hudkit._processEventListeners.get(%d);\n\
  handler('%s', %s)\n\
})()", spawn_data->callbackId, eventName, eventData);
    webkit_web_view_run_javascript(
            spawn_data->web_view, buffer, NULL, on_js_call_finished, NULL);
}

void on_child_exit(uv_process_t *handle, int64_t status, int signal) {

    struct spawn_data *spawn_data = handle->data;

    char buffer[10];
    snprintf(buffer, sizeof(buffer), "%ld", status);

    call_js_process_event_listener(handle->data, "exit", buffer);

    uv_close((uv_handle_t*) handle, NULL);

    // Free the allocated pipes
    for (int i = 0; i < spawn_data->stdio_count; ++i) {
        free(spawn_data->stdio[i].data.stream);
    }
    // Free the uv_stdio_container_t
    free(spawn_data->stdio);
    free(spawn_data);
    free(handle);
}

void raise_js_process_error(uv_process_t *handle, const char *error) {
    char *js_string = to_js_string(error);
    call_js_process_event_listener(handle->data, "error", js_string);
    free(js_string);
}


void on_allocate(uv_handle_t *handle, size_t size, uv_buf_t *buffer) {
    // Allocate 1 more slot to leave space for a null.
    *buffer = uv_buf_init((char *) malloc(size + 1), size);
}

void on_read_out(uv_stream_t *stream, ssize_t n, const uv_buf_t *buffer) {
    if (n < 0) {
        if (n == UV_EOF) { // End of file
            uv_close((uv_handle_t *)stream, NULL);
            call_js_process_event_listener(stream->data, "stdoutData", "null");
        }
    } else if (n > 0) {
        // Ensure terminating null.
        buffer->base[n] = '\0';
        char *js_string = to_js_string(buffer->base);
        call_js_process_event_listener(stream->data, "stdoutData", js_string);
        free(js_string);
    }

    if (buffer->base) free(buffer->base);
}

void on_read_err(uv_stream_t *stream, ssize_t n, const uv_buf_t *buffer) {
    // TODO as above with stdout

    if (n < 0) {
        if (n == UV_EOF) { // End of file
            // TODO
            printf("err: eof\n");
            uv_close((uv_handle_t *)stream, NULL);
        }
    } else if (n > 0) {
        // TODO
        printf("err: ");
        fwrite(buffer->base, sizeof(char), buffer->len, stdout);
        printf("\n");
    }

    if (buffer->base) free(buffer->base);
}

void on_js_call_spawn(WebKitUserContentManager *manager,
        WebKitJavascriptResult *sentData,
        gpointer arg) {
    WebKitWebView *web_view = WEBKIT_WEB_VIEW(arg);

    // Get arguments from JS context
    JSCValue *jsValue = webkit_javascript_result_get_js_value(sentData);

    int callbackId = jsc_value_to_int32(
            jsc_value_object_get_property(jsValue, "id"));

    char *program = jsc_value_to_string(
            jsc_value_object_get_property(jsValue, "program"));

    JSCValue *argsArray = jsc_value_object_get_property(jsValue, "args");
    int nArgs = jsc_value_to_int32(
        jsc_value_object_get_property(argsArray, "length"));
    char *args[nArgs + 2];
    args[0] = program;
    for (int i = 0; i < nArgs; ++i) {
        args[i + 1] = jsc_value_to_string(
                jsc_value_object_get_property_at_index(argsArray, i));
    }
    args[nArgs + 1] = NULL;

    //printf("program %s with %d args:\n", program, nArgs);
    //for (int i = 0; i < nArgs + 1; ++i) {
    //    printf("args[%d] = %s\n", i, args[i]);
    //}

    // The handle needs to stay valid throughout the requested operation (the
    // child process running), so we have to heap-allocate.  This is freed in
    // the exit callback.
    uv_process_t *handle = malloc(sizeof(uv_process_t));
    uv_process_options_t options = {0}; // Clear
    options.file = args[0];
    options.args = args;
    options.exit_cb = on_child_exit;

    uv_pipe_t *pipe_in  = malloc(sizeof(uv_pipe_t));
    uv_pipe_t *pipe_out = malloc(sizeof(uv_pipe_t));
    uv_pipe_t *pipe_err = malloc(sizeof(uv_pipe_t));
    uv_pipe_init(uv_loop, pipe_in,  0 /*no ipc*/);
    uv_pipe_init(uv_loop, pipe_out, 0 /*no ipc*/);
    uv_pipe_init(uv_loop, pipe_err, 0 /*no ipc*/);

    uv_stdio_container_t *stdio = calloc(3, sizeof(uv_pipe_t));
    // "Readable" and "writable" here are from the child process' perspective.
    stdio[STDIN_FILENO] .flags = UV_CREATE_PIPE | UV_READABLE_PIPE;
    stdio[STDIN_FILENO] .data.stream = (uv_stream_t *) pipe_in;
    stdio[STDOUT_FILENO].flags = UV_CREATE_PIPE | UV_WRITABLE_PIPE;
    stdio[STDOUT_FILENO].data.stream = (uv_stream_t *) pipe_out;
    stdio[STDERR_FILENO].flags = UV_CREATE_PIPE | UV_WRITABLE_PIPE;
    stdio[STDERR_FILENO].data.stream = (uv_stream_t *) pipe_err;

    options.stdio = stdio;
    options.stdio_count = 3;

    // Store the callback ID and a reference to the web view, in the handle's
    // user data property.
    struct spawn_data *spawn_data = malloc(sizeof(struct spawn_data));
    spawn_data->callbackId = callbackId;
    spawn_data->web_view = web_view;
    spawn_data->stdio = stdio;
    spawn_data->stdio_count = 3;
    handle->data = spawn_data;
    // Same stuff in the pipes too, so on_allocate and on_read can see them.
    pipe_in ->data = spawn_data;
    pipe_out->data = spawn_data;
    pipe_err->data = spawn_data;

    int r;
    if ((r = uv_spawn(uv_loop, handle, &options))) {
        const char *error = uv_strerror(r);

        fprintf(stderr, "Error spawning program '%s': %s\n", program, error);
        raise_js_process_error(handle, error);
    } else {
        char buffer[100];
        snprintf(buffer, sizeof(buffer), "%d", handle->pid);

        call_js_process_event_listener(spawn_data, "start", buffer);

        // Start listening to pipes.  This is fine and necessary to do after
        // uv_spawn, not before.  We're still in the same uv loop tick as the
        // spawn, and so no data has been read or written yet.

        r = uv_read_start((uv_stream_t *)pipe_out, on_allocate, on_read_out);
        // TODO expose pipe errors with js callback
        if (r) {
            const char *error = uv_strerror(r);
            fprintf(stderr, "Error reading stdout from '%s': %s\n",
                    program, error);
            raise_js_process_error(handle, error);
        }
        r = uv_read_start((uv_stream_t *)pipe_err, on_allocate, on_read_err);
        if (r) {
            const char *error = uv_strerror(r);
            fprintf(stderr, "Error reading stderr from '%s': %s\n",
                    program, error);
            raise_js_process_error(handle, error);
        }
        // TODO write initial stdin
    }
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

bool iterate_uv_loop(uv_loop_t *loop) {
    uv_run(loop, UV_RUN_NOWAIT);
    return true; // Keep calling this
}

void printUsage(char *programName) {
    printf("\
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
        view.  Which settings are available depends on what version of WebKit\n\
        this program was compiled against.  See a list of ones supported on\n\
        your system by passing '--webkit-settings help'.\n\
\n\
        For details of what the options do, see the list at\n\
        https://webkitgtk.org/reference/webkit2gtk/stable/WebKitSettings.html\n\
        Pass setting names as underscore_separated_words.\n\
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

                // If we get the special key "help", we should only print the
                // available options, and exit.
                bool help_mode = !strcmp(key, "help");

                // Unfortunately, no function exists to set a WebKit setting
                // generically, by its string name.  Instead, there is a
                // separate setter function for each setting name.
                //
                // So we'll generate them using macros.

                #define BOOLEAN_SETTING(SETTING_NAME) \
                if (help_mode) printf(#SETTING_NAME "\n");\
                else if (!strcmp(key, #SETTING_NAME)) {\
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
#if WEBKIT_MINOR_VERSION >= 10
                BOOLEAN_SETTING(allow_file_access_from_file_urls);
#endif
                BOOLEAN_SETTING(allow_modal_dialogs);
#if WEBKIT_MINOR_VERSION >= 28
                BOOLEAN_SETTING(allow_top_navigation_to_data_urls);
#endif
#if WEBKIT_MINOR_VERSION >= 14
                BOOLEAN_SETTING(allow_universal_access_from_file_urls);
#endif
                BOOLEAN_SETTING(auto_load_images);
                BOOLEAN_SETTING(draw_compositing_indicators);
#if WEBKIT_MINOR_VERSION >= 2
                BOOLEAN_SETTING(enable_accelerated_2d_canvas);
#endif
#if WEBKIT_MINOR_VERSION >= 24
                BOOLEAN_SETTING(enable_back_forward_navigation_gestures);
#endif
                BOOLEAN_SETTING(enable_caret_browsing);
                BOOLEAN_SETTING(enable_developer_extras);
                BOOLEAN_SETTING(enable_dns_prefetching);
#if WEBKIT_MINOR_VERSION >= 20
                BOOLEAN_SETTING(enable_encrypted_media);
#endif
                BOOLEAN_SETTING(enable_frame_flattening);
                BOOLEAN_SETTING(enable_fullscreen);
                BOOLEAN_SETTING(enable_html5_database);
                BOOLEAN_SETTING(enable_html5_local_storage);
                BOOLEAN_SETTING(enable_hyperlink_auditing);
                BOOLEAN_SETTING(enable_java);
                BOOLEAN_SETTING(enable_javascript);
#if WEBKIT_MINOR_VERSION >= 24
                BOOLEAN_SETTING(enable_javascript_markup);
#endif
#if WEBKIT_MINOR_VERSION >= 26
                BOOLEAN_SETTING(enable_media);
#endif
#if WEBKIT_MINOR_VERSION >= 22
                BOOLEAN_SETTING(enable_media_capabilities);
#endif
#if WEBKIT_MINOR_VERSION >= 4
                BOOLEAN_SETTING(enable_media_stream);
                BOOLEAN_SETTING(enable_mediasource);
#endif
#if WEBKIT_MINOR_VERSION >= 24
                BOOLEAN_SETTING(enable_mock_capture_devices);
#endif
                BOOLEAN_SETTING(enable_offline_web_application_cache);
                BOOLEAN_SETTING(enable_page_cache);
                BOOLEAN_SETTING(enable_plugins);
                // Deprecated.
                //BOOLEAN_SETTING(enable_private_browsing);
                BOOLEAN_SETTING(enable_resizable_text_areas);
                BOOLEAN_SETTING(enable_site_specific_quirks);
                BOOLEAN_SETTING(enable_smooth_scrolling);
#if WEBKIT_MINOR_VERSION >= 4
                BOOLEAN_SETTING(enable_spatial_navigation);
#endif
                BOOLEAN_SETTING(enable_tabs_to_links);
                BOOLEAN_SETTING(enable_webaudio);
                BOOLEAN_SETTING(enable_webgl);
#if WEBKIT_MINOR_VERSION >= 2
                BOOLEAN_SETTING(enable_write_console_messages_to_stdout);
#endif
                BOOLEAN_SETTING(enable_xss_auditor);
                BOOLEAN_SETTING(javascript_can_access_clipboard);
                BOOLEAN_SETTING(javascript_can_open_windows_automatically);
                BOOLEAN_SETTING(load_icons_ignoring_image_load_setting);
                BOOLEAN_SETTING(media_playback_allows_inline);
                BOOLEAN_SETTING(media_playback_requires_user_gesture);
                BOOLEAN_SETTING(print_backgrounds);
                BOOLEAN_SETTING(zoom_text_only);

                #define STRING_SETTING(SETTING_NAME) \
                if (help_mode) printf(#SETTING_NAME "\n");\
                else if (!strcmp(key, #SETTING_NAME)) {\
                    webkit_settings_set_ ## SETTING_NAME (\
                            wk_settings, value);\
                    continue;\
                }

                STRING_SETTING(cursive_font_family);
                STRING_SETTING(default_charset);
                STRING_SETTING(default_font_family);
                STRING_SETTING(fantasy_font_family);
#if WEBKIT_MINOR_VERSION >= 30
                STRING_SETTING(media_content_types_requiring_hardware_support);
#endif
                STRING_SETTING(monospace_font_family);
                STRING_SETTING(pictograph_font_family);
                STRING_SETTING(sans_serif_font_family);
                STRING_SETTING(serif_font_family);
                STRING_SETTING(user_agent);

                // TODO exit(3) when can't parse int, instead of continuing
                #define INT_SETTING(SETTING_NAME) \
                if (help_mode) printf(#SETTING_NAME "\n");\
                else if (!strcmp(key, #SETTING_NAME)) {\
                    int actual_value = strtoimax(value, NULL, 10);\
                    webkit_settings_set_ ## SETTING_NAME (\
                            wk_settings, actual_value);\
                    continue;\
                }

                INT_SETTING(default_font_size);
                INT_SETTING(default_monospace_font_size);
                INT_SETTING(minimum_font_size);

#if WEBKIT_MINOR_VERSION >= 16
                // This one setting takes a named constant, so we'll look up
                // the right one.

                if (help_mode) printf("hardware_acceleration_policy\n");
                else if (!strcmp(key, "hardware_acceleration_policy")) {
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
#endif

                if (help_mode) exit(0);

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
    // Set up libuv loop
    //

    uv_loop = uv_default_loop();

    // Run the libuv loop in parallel with GTK's event loop, by iterating it
    // whenever GTK is "idle" (meaning when it's not in the middle of painting,
    // or dealing with resize events, etc).
    g_idle_add(G_SOURCE_FUNC(iterate_uv_loop), uv_loop);

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
            G_CALLBACK(on_inspector_attach), NULL);
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
    g_signal_connect(manager, "script-message-received::spawn",
            G_CALLBACK(on_js_call_spawn), web_view);

    // Set up message handlers on the JavaScript side.  These appear under
    // window.webkit.messageHandlers.
    webkit_user_content_manager_register_script_message_handler(manager,
            "getMonitorLayout");
    webkit_user_content_manager_register_script_message_handler(manager,
            "setClickableAreas");
    webkit_user_content_manager_register_script_message_handler(manager,
            "showInspector");
    webkit_user_content_manager_register_script_message_handler(manager,
            "spawn");

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
let nextSpawnId = 0\n\
window.Hudkit = {\n\
  on: function (eventName, callback) {\n\
    if (window.Hudkit._listeners.has(eventName)) {\n\
      window.Hudkit._listeners.get(eventName).push(callback)\n\
    } else {\n\
      window.Hudkit._listeners.set(eventName, [callback])\n\
    }\n\
  },\n\
  off: function (eventName, callback) {\n\
    const listenersForThisEvent = window.Hudkit._listeners.get(eventName)\n\
    if (listenersForThisEvent) {\n\
      listenersForThisEvent.splice(listenersForThisEvent.indexOf(callback), 1)\n\
    }\n\
  },\n\
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
  showInspector: function (shouldAttachToWindow, callback) {\n\
    if (shouldAttachToWindow === undefined) callback = shouldAttachToWindow\n\
    callback = callback || (function () {})\n\
    shouldAttachToWindow = shouldAttachToWindow ? true : false\n\
    const id = nextCallbackId++\n\
    window.Hudkit._pendingCallbacks[id] = callback\n\
    window.webkit.messageHandlers.showInspector.postMessage({id, shouldAttachToWindow})\n\
  },\n\
  spawn: function (program, args = [], handlers = {}) {\n\
    const id = nextSpawnId++\n\
    let fullStdoutBuffer = ''\n\
    let fullStderrBuffer = ''\n\
    const handlerFunction = (eventName, data) => {\n\
      switch (eventName) {\n\
        case 'start':\n\
          if (handlers.start) handlers.start(data)\n\
          break\n\
        case 'error':\n\
          if (handlers.error) handlers.error(data)\n\
          break\n\
        case 'exit':\n\
          if (handlers.exit) handlers.exit(data)\n\
          if (handlers.finish) handlers.finish(fullStdoutBuffer, fullStderrBuffer, data)\n\
          window.Hudkit._processEventListeners.delete(id)\n\
          break\n\
        case 'stdoutData':\n\
          if (handlers.stdout) handlers.stdout(data)\n\
          if (handlers.fullStdout) fullStdoutBuffer += data\n\
          break\n\
        case 'stderrData':\n\
          if (handlers.stderr) handlers.stderr(data)\n\
          if (handlers.fullStderr) fullStderrBuffer += data\n\
          break\n\
        default:\n\
          console.error(`Unexpected spawn event name ${eventName}`)\n\
          break\n\
      }\n\
    }\n\
    window.Hudkit._processEventListeners.set(id, handlerFunction)\n\
    window.webkit.messageHandlers.spawn.postMessage({id, program, args})\n\
  },\n\
}\n\
Object.defineProperty(window.Hudkit, '_pendingCallbacks', {\n\
  value: [],\n\
  enumerable: false,\n\
  configurable: false,\n\
  writable: true,\n\
})\n\
Object.defineProperty(window.Hudkit, '_listeners', {\n\
  value: new Map(),\n\
  enumerable: false,\n\
  configurable: false,\n\
  writable: true,\n\
})\n\
Object.defineProperty(window.Hudkit, '_processEventListeners', {\n\
  value: new Map(),\n\
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
    n_user_defined_input_rects = 0;
    user_defined_input_rects = realloc(user_defined_input_rects, 0);
    realize_input_shape();
}


void call_js_listeners(WebKitWebView *web_view, char *eventName, char *stringifiedData) {
    // Calls the user's registered JS listener functions for the given event
    // name, simply string-placing the stringified data between its call
    // parentheses.
    //
    // Ensure `stringifiedData` is sanitised!  It will basically be `eval`ed in
    // the web page's context.

    char buffer[sizeof(stringifiedData) + 1024];
    snprintf(buffer, sizeof(buffer), "\
(() => { // IIFE\n\
  const listenersForEvent = window.Hudkit._listeners.get('%s');\n\
  if (listenersForEvent) {\n\
    listenersForEvent.forEach(listener => listener(%s))\n\
  }\n\
})()", eventName, stringifiedData);

    webkit_web_view_run_javascript(
            web_view, buffer, NULL, on_js_call_finished, NULL);
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
