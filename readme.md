# Hudkit&ensp;[![](https://img.shields.io/github/actions/workflow/status/anko/hudkit/ci.yml?style=flat-square)](https://github.com/anko/hudkit/actions/workflows/ci.yml) ![](https://img.shields.io/badge/coolness-maximum-brightgreen.svg?style=flat-square)

Transparent click-through web browser overlay ("[HUD][wiki-hud]") over your
whole desktop, using [WebKit][webkit].

If you know web development, you can use Hudkit to make the _coolest statusbar
in existence_, or _SVG desktop fireworks_, or whatever else you can think of
using a fullscreen transparent web view for.

## Features

 - Works with multiple monitors, and connecting/disconnecting them.
 - Has a [JavaScript API](#javascript-api), so scripts on the page can query
   monitor layout and change which areas of the overlay are clickable, for
   example.
 - Small executable.  Uses native GTK and WebKit libraries.
 - Supports modern web APIs like WebSockets, WebAudio, WebGL, etc.

Platforms: &emsp;:heavy\_check\_mark: Linux (X11) &emsp;:no\_entry\_sign:
~~Linux (Wayland)~~ &emsp;:no\_entry\_sign: ~~Windows~~ &emsp;:no\_entry\_sign:
~~OS X~~ (but see https://github.com/progrium/topframe!)

## Quick start

```sh
cd /tmp
git clone https://github.com/anko/hudkit.git
cd hudkit
make
cd example
./run.sh
```

If `make` complains, check [dependencies](#dependencies).

You should see something like this, if you have 2 monitors:

![hudkit example running on 2 monitors](https://user-images.githubusercontent.com/5231746/132109661-34cde61f-f1bf-49d1-ac0a-02af9993e94e.png)

The code for what you see there is in the [`example/` directory](example/).  It
contains some explanatory comments, so it might make a good starting point for
your experiments.  If you come up with a (fairly compact) example of your own,
please PR.

## Usage

```
USAGE: ./hudkit <URL> [--help] [--webkit-settings option1=value1,...]

    <URL>
        Universal Resource Locator to be loaded on the overlay web view.
        For example, to load a local file, you'd pass something like:

            file:///home/mary/test.html

        or to load from a local web server at port 4000:

            http://localhost:4000

    --inspect
        Open the Web Inspector (dev tools) on start.

    --webkit-settings <settings>
        The <settings> should be a comma-separated list of settings.

        Boolean settings can look like
            option-name
            option-name=TRUE
            option-name=FALSE

        String, integer, and enum options look like
            option-name=foo
            option-name=42

        To see settings available on your system's WebKit version, their
        valid values, and default values, pass '--webkit-settings help'.

        To see explanations of the settings, see
        https://webkitgtk.org/reference/webkit2gtk/stable/WebKitSettings.html

    --help
        Print this help text, then exit.

    All of the standard GTK debug options and env variables are also
    supported.  You probably won't need them, but you can find a list here:
    https://developer.gnome.org/gtk3/stable/gtk-running.html
```

## JavaScript API

JavaScript on the web page context has a `Hudkit` object, with these properties:

### `async Hudkit.getMonitorLayout()`

Return: an Array of `{name, x, y, width, height}` objects, representing your
monitors' device names and dimensions.

Example:

```js
const monitors = await Hudkit.getMonitorLayout()

monitors.forEach((m) => {
  console.log(`${m.name} pos:${m.x},${m.y} size:${m.width},${m.height}`)
})
```

### `Hudkit.on(eventName, listener)`

Registers the given `listener` function to be called on events by the string
name `eventName`.

Currently listenable events:

 - `monitors-changed`: fired when a monitor is logically connected or
   disconnected, such as through `xrandr`.

   No arguments are passed to the `listener`.  Call `Hudkit.getMonitorLayout`
   to get the updated layout.

 - `composited-changed`: fired when the ability of your desktop environment to
   render transparency changes; typically when your compositor is killed or
   restarted.

   The main reason this exists is so if you accidentally kill your compositor,
   you won't be stuck with the now fully opaque overlay window blocking your
   whole desktop, as long as your page listens for this event and calls
   `window.close()` in response.

   Arguments passed to listener:

   - `haveTransparency` (Boolean).  True if compositing is now supported, false
     otherwise.

### `Hudkit.off(eventName, listener)`

De-registers the given `listener` from the given `eventName`, so it will no
longer be called.

### `async Hudkit.setClickableAreas(rectangles)`

Sets which areas of the overlay window are clickable.  By default, it is not
clickable.  The given area replaces the previous.

Parameters:

 - `rectangles`: Array of objects with properties `x`, `y`, `width`, and
   `height`.  Other properties are ignored, and missing properties are treated
   as 0.  Can be an empty Array, to make everything non-clickable.

   The area of the desktop represented by the union of the given rectangles
   become input-opaque (able to receive mouse events).  All other areas become
   input-transparent.

Return:  `undefined`

Example:

```js
// Make a tall narrow strip of the overlay window clickable, in the top left
// corner of the screen. The dimensions are in pixels.
Hudkit.setClickableAreas([
  { x: 0, y: 0, width: 200, height: 1000 }
], err => console.error(err))
```

Notes:

 - If the Web Inspector is attached to the overlay window, the area it occupies
   is automatically kept clickable, independently of calls to this function.

 - When monitors are connected or disconnected, your clickable areas are reset
   to nothing being clickable, because their positioning would be
   unpredictable.  Subscribe to the `'monitors-changed'` event
   (`Hudkit.on('monitors-changed', () => { ... })`) and update your clickable
   areas accordingly!

### `async Hudkit.showInspector([attached])`

Opens the Web Inspector (also known as Developer Tools), for debugging the page
loaded in the web view.

Parameters:

 - `attached`: Boolean.  If `true`, starts the inspector attached to the
   overlay window.  If `false`, start the inspector in its own window.
   (Optional.  Default: `false`.)

Return:  `undefined`

:information\_source: You can also start the inspector with the `--inspect`
flag.  That's usually better, because it works even if your JS crashes before
calling this function.

### Other Web APIs that work specially

 - [`window.close`](https://developer.mozilla.org/en-US/docs/Web/API/Window/close)
   exits Hudkit.

## Install

In the root directory of this project,

    make

If you're missing any dependencies, the error should tell you which.

### Dependencies

You'll need—

- Standard *C compilation tools*: `make`, `pkg-config`, and any C compiler of
  your choice (`gcc` or `clang`, probably).

  Any Linux distro has these; many have them installed by default.  If not,
  consult your distro's documentation on how to install them.  (Many distros
  have a single package containing all the C tools, for convenience, like
  [Arch][arch]'s [`base-devel`
  package](https://archlinux.org/groups/x86_64/base-devel/).)

- *GTK 3*, and a corresponding *webkit2gtk*.

  On [Arch][arch], the packages are called `gtk3` and `webkit2gtk`.

  On [Void][void], they are `gtk+3-devel` and `webkit2gtk-devel`.

  On [Ubuntu][ubuntu], they are `libgtk-3-dev` and `libwebkit2gtk-4.0-devel`.

  On [Mint][mint], they are `libgtk-3-dev` and `libwebkit2gtk-4.0`.

  If you build on another distro, I'm interested in how it went.

## Bugs

Probably.  [Report them][new-issue].

## FAQ

> Is it safe to direct Hudkit at some random untrusted web page on the
> internet?

No.  Hudkit is built on a web browser engine, but is not intended for use as a
general-purpose web browser.  The `window.Hudkit` object and other background
stuff are exposed to every page Hudkit loads.  Although I've tried my best to
mitigate possible attacks, the API simply is not designed to be exposed to the
full badness of the wider internet.

Please point Hudkit only at a locally hosted web pages, unless you really know
what you're doing.

> How can I ensure my HUD doesn't accidentally load remote resources?

Define a
[Content-Security-Policy](https://developer.mozilla.org/en-US/docs/Web/HTTP/Headers/Content-Security-Policy)
(CSP), like you'd do when developing any other web page.  Hudkit supports those
through WebKit.

I recommend the following: Add this meta tag inside your document's `<head>`:

```html
<meta http-equiv="Content-Security-Policy" content="default-src 'self' 'unsafe-inline'">
```

This makes that page only able to load resources from the same host it was
loaded from (your local computer).  All requests to anywhere else are blocked.
The `'unsafe-inline'` part allows inline `<script>` and `<style>` tags, which
are innocuous if you're never loading remote resources anyway.

You can test this by e.g. adding a `<img src="http://example.com/">` tag to
your page.  You'll see a log entry like this when it gets blocked:

```
CONSOLE SECURITY ERROR Refused to load http://example.com/ because it appears
in neither the img-src directive nor the default-src directive of the Content
Security Policy.
```

For further documentation on CSP, consult [MDN Web
Docs](https://developer.mozilla.org/en-US/docs/Web/HTTP/Headers/Content-Security-Policy)
or [content-security-policy.com](https://content-security-policy.com/).

> Hudkit says my screen doesn't support transparency.  What does this mean?

You're probably running a plain window manager (like i3, XMonad, or awesomewm),
which doesn't have a [built-in
compositor](https://en.wikipedia.org/wiki/Compositing_window_manager).  So
you'll need to install and run a standalone compositor.  I recommend
[compton][compton], or [picom][picom].

> I can't type anything into the Web Inspector while it's attached to the
> overlay window!

Yep, it's a known problem.  You can work around it by detaching the web
inspector into its own window, with one of the buttons in the top-left corner.
It works normally when detached.

This bug is hard to fix for complex technical reasons:  In short, we have to
set X11's *override-redirect* flag on the overlay window, to guarantee that
window managers cannot reposition it, reparent it, or otherwise mess with it.
A side-effect of doing this is that the window does cannot receive input focus,
which is fine for mouse events (since they aren't dependent on input focus),
but it means no keyboard events.  Unless you grab the keyboard device, which
has its own problems.

> My currently running Hudkit instance's page is in a weird state that I want
> to debug, but I forgot to pass the `--inspect` flag, and restarting it would
> lose its current state.  What do?

You can send the Hudkit process a `SIGUSR1` signal to open the Web Inspector.
For example, `killall hudkit --signal SIGUSR1`.

> Why am I getting a `SyntaxError` when I try to `await` a Hudkit function?

Probably because you're trying to use `await` at the top-level of your
JavaScript file.  This wart in the JavaScript standard is unfortunate, and the
wording of WebKit's error message for it even more so.

The fix is to create an async function at the top-level, and immediately call
it, doing all of your async stuff inside it:

```js
(async function () {
  // You can use `await` here
})()
```

This will improve in the near future:  There is a [TC39 proposal for top-level
await](https://github.com/tc39/proposal-top-level-await), which is [backed by
WebKit developers](https://github.com/whatwg/html/pull/4352), and
[implementation is in
progress](https://bugs.webkit.org/show_bug.cgi?id=202484).

> Why is Xvfb complaining about _extension "GLX" missing on display ":99"_ if I
> run the automated test?

Probably because you're running an Nvidia proprietary driver which are [kind of
garbage](https://www.youtube.com/watch?v=_36yNWw_07g).  The test starts a
background instance of X11 so your desktop's settings don't interfere with it,
but inside that context OpenGL is randomly bricked on some versions of the
proprietary NV driver, and on some versions it works.  The actual program
should work just fine either way though.  Try the `example/`.

## Related programs

- [Electron][electron].  I've heard it's possible to make Electron
  click-through and fullscreen.  I have not gotten it to work, but maybe you
  can?  Let me know if you do.

  Possible starting point:  Start `electron` with `--enable-transparent-visuals
  --disable-gpu`.  Call [`win.setIgnoreMouseEvents`][electron_ignoremouse], set
  all the possible "*please dear WM, do not touch this window*"-flags, call the
  [`screen` API](https://electronjs.org/docs/api/screen) for monitor
  arrangements and position your window accordingly.  Sacrifice 55 paperclips
  to Eris of Discordia, kneel and pray.

## JS libraries that I think work well with Hudkit

 - [dnode][dnode] makes it possible to make remote procedure calls from JS in
   the web page to JS on a web server.
 - [SockJS][sockjs] lets is a simple abstraction over WebSockets, for
   transferring data otherwise.
 - [D3][d3] is a great data visualisation library.

## Programs that I think work well with Hudkit

 - [`xkbcat`][xkbcat] can capture keystrokes everywhere in X11, for making a
   keyboard visualiser for livestreaming, or for triggering eye candy.
 - `sxhkd` is a fairly minimal X11 keyboard shortcut daemon.  Can use it to run
   arbitrary commands in response to key combinations, such as throwing data
   into a named pipe read by a locally running web server that's in contact
   with hudkit by WebSocket.
 - `mpv` with the `--input-ipc-server` flag can be queried for the currently
   playing music track.  Various other music players can do this too if you
   google around.
 - `sensors` can show hardware temperatures and fan data.

## License

[ISC](https://en.wikipedia.org/wiki/ISC_license).


[arch]: https://www.archlinux.org/
[compton]: https://github.com/chjj/compton
[d3]: https://d3js.org/
[dnode]: https://github.com/substack/dnode
[electron]: https://electronjs.org/
[electron_ignoremouse]: https://electronjs.org/docs/api/browser-window#winsetignoremouseeventsignore-options
[mint]: https://linuxmint.com/
[new-issue]: https://github.com/anko/hudkit/issues/new
[picom]: https://github.com/yshui/picom
[pscircle]: https://gitlab.com/mildlyparallel/pscircle
[sockjs]: https://github.com/sockjs/sockjs-client
[twitch]: https://www.twitch.tv/
[ubuntu]: https://ubuntu.com/
[void]: https://voidlinux.org/
[webkit]: https://www.webkit.org/
[wiki-hud]: http://en.wikipedia.org/wiki/Head-up_display
[wk-settings]: https://webkitgtk.org/reference/webkit2gtk/stable/WebKitSettings.html
[xkbcat]: https://github.com/anko/xkbcat
