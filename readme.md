# Hudkit&ensp;[![](https://img.shields.io/travis/anko/hudkit?style=flat-square)](https://travis-ci.org/anko/hudkit)

Transparent click-through web browser overlay (or [HUD][wiki-hud]) over your
whole desktop, using [WebKit][webkit].

If you know web development, you can use Hudkit to make the _coolest statusbar
in existence_, or _SVG desktop fireworks_, or whatever else you can think of
using a fullscreen transparent web view for.

## Features

 - Works with multiple monitors.
 - Has a [JavaScript API](#javascript-api), so scripts on the page can query
   monitor layout and change which areas of the overlay are clickable, for
   example.
 - Small executable.  Uses system GTK and WebKit libraries, not some big
   Chromium bundle.
 - Supports relatively modern web APIs like WebSockets, WebAudio, WebGL, etc.

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

The [`example/` directory](example/) contains various inspirations and starting
points.  If you come up with any of your own (fairly compact) example ideas,
please PR.

## Usage

```
hudkit [--help] [--webkit-settings <1>,<2>,...] <URL>
```

 - `<URL>` is whatever link you want the overlay web view to open.  Probably
   either a `file:///home/you/hud.html`, or if you have a local web server
   running on port 4000, then `http://localhost:4000` or similar.

 - `--webkit-settings` expects the next argument to be a comma-separated list
   of [valid WebKit settings][wk-settings] to be passed to the web view.  Write
   the name of the option with underscore-separated words (for example,
   `auto_load_images`).

   - Boolean options can be set with `option_name=TRUE`, `option_name=FALSE`.
     Assumed to be `TRUE` if you only type the `option_name`.
   - String and integer options can be set with `option_name=value`.
   - The only enum option `hardware_acceleration_policy` has valid values
     `ON_DEMAND`, `ALWAYS`, and `NEVER`.

 - `--help` prints usage help.

## JavaScript API

JavaScript on the web page context has a `Hudkit` object, with these properties:

### `Hudkit.showInspector(callback)`

Opens the Web Inspector (also known as Developer Tools), for debugging the page
loaded in the web view.

Arguments:

 - `callback(err, monitors)`: *[optional]* a function called when the request
   completes:

   - `err`: `null` if successful.  Otherwise, an `Error` object.

Return:  `undefined`

### `Hudkit.getMonitorLayout(callback)`

Retrieves monitor size and position data.

Arguments:

 - `callback(err, monitors)`: a function called when the request completes:

   - `err`: `null` if successful.  Otherwise, an `Error` object.
   - `monitors`: If successful, contains an Array of `{x, y, width, height}`
     objects, representing the dimensions of your monitors.

Return:  `undefined`

Example:

```js
Hudkit.getMonitorLayout((err, monitors) => {
 if (err) { console.error(err) }

 // Show each object
 monitors.forEach((monitor) => {
   console.log(JSON.stringify(monitor))
 })
})
```

### `Hudkit.setClickableAreas(rectangles, callback)`

Parameters:

 - `rectangles`: Array of objects with properties `x`, `y`, `width`, and
   `height`.  Other properties are ignored, and missing properties are treated
   as 0.  Can be an empty Array.

   The area of the desktop represented by the union of the given rectangles
   become input-opaque (able to receive mouse events).  All other areas become
   input-transparent.

   The coordinate space corresponds to that of `getMonitorLayout`.

 - `callback(err, monitors)`: a function called when the request completes:

   - `err`: `null` if successful.  Otherwise, an `Error` object.

Return:  `undefined`

Example:

```js
// Make a tall narrow strip of the overlay window clickable, in the top left
// corner of the screen. The dimensions are in pixels.
Hudkit.setClickableAreas([
  { x: 0, y: 0, width: 200, height: 1000 }
], err => console.error(err))
```

Note that each time this is called, the total given area _replaces_ the
previous one.

If the Web Inspector is attached to the overlay window, the area it occupies is
automatically kept clickable, indepdendently of calls to this function.

I imagine a typical use-case for this would be to make your statusbar
clickable, so you can make contextual pop-ups appear when you hover over parts
of it.  If you want the pop-up to be clickable too, call this again with its
`.getBoundingClientRect()` included.

### `Hudkit.showInspector(callback)`

Parameters:

 - `callback(err, monitors)`: a function called when the request completes:

   - `err`: `null` if successful.  Otherwise, an `Error` object.

Return:  `undefined`

Shows the Web Inspector, also known as dev tools.

## Install

In the root directory of this project,

    make

### Dependencies

You'll need *GTK 3*, and a corresponding *webkit2gtk*.

On [Arch][arch], the packages are called `gtk3` and `webkit2gtk`.

On [Void][void], they are `gtk+3-devel` and `webkit2gtk-devel`.

On [Ubuntu][ubuntu], they are `libgtk-3-dev` and `libwebkit2gtk-4.0-devel`.

If you build on another distro, I'm interested in how it went.

## Bugs

Probably.  [Report them][new-issue].

## Limitations

 - **Requires a restart if rearranging monitors**.  Hudkit can handle multi-monitor setups: it detects their arrangement on startup, not dynamically.

## Solutions to common problems

> Is it safe to direct Hudkit at some random untrusted web page on the
> internet?

No.  The `window.Hudkit` object and other background stuff are exposed to every
page Hudkit loads.  The API is not designed to resist attacks.  Please only
load pages you trust.

> How do ensure my page doesn't accidentally load remote resources?

Define a
[Content-Security-Policy](https://developer.mozilla.org/en-US/docs/Web/HTTP/Headers/Content-Security-Policy),
(CSP).  Hudkit supports those through WebKit.

A reasonable starting point:  You want to allow requests to the same host your
page was loaded from (your own computer), but block all requests to anywhere
else.  Also, allow inline `<script>` and `<style>` tags.

You can make Hudkit enforce that by adding the appropriate tag inside `<head>`:

```html
<meta http-equiv="Content-Security-Policy" content="default-src 'self' 'unsafe-inline'">
```

You can test that's working by e.g. adding a `<img src="http://example.com/">`
tag to the page.  You'll see a log entry when it gets blocked:

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

> I can't type anything into the Web Inspector if it's attached to the overlay
> window!

It's a known problem.  You can detach the web inspector into its own window
with one of the buttons in the top-left corner.  It works normally when
detached.

This bug is hard to fix for complex technical reasons:  In short, we have to
set X11's *override-redirect* flag on the overlay window, to guarantee that
window managers cannot reposition it, reparent it, or otherwise mess with it.
A side-effect of doing this is that the window does cannot receive input focus,
which is fine for mouse events (since they aren't dependent on input focus),
but it means no keyboard events.  Unless you grab the keyboard device, which
has its own problems.

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
   keyboard visualiser for livestreaming, or for triggering firework effects.
 - `mpv` with the `--input-ipc-server` flag can be queried for the currently
   playing music track.  Various other music players can do this too if you
   google around.
 - `sensors` can show hardware temperatures and fan data.

## License

[ISC](https://en.wikipedia.org/wiki/ISC_license).


[arch]: https://www.archlinux.org/
[compton]: https://github.com/chjj/compton
[picom]: https://github.com/yshui/picom
[d3]: https://d3js.org/
[dnode]: https://github.com/substack/dnode
[electron]: https://electronjs.org/
[electron_ignoremouse]: https://electronjs.org/docs/api/browser-window#winsetignoremouseeventsignore-options
[new-issue]: https://github.com/anko/hudkit/issues/new
[pscircle]: https://gitlab.com/mildlyparallel/pscircle
[sockjs]: https://github.com/sockjs/sockjs-client
[twitch]: https://www.twitch.tv/
[ubuntu]: https://ubuntu.com/
[void]: https://voidlinux.org/
[webkit]: https://www.webkit.org/
[wiki-hud]: http://en.wikipedia.org/wiki/Head-up_display
[xkbcat]: https://github.com/anko/xkbcat
[wk-settings]: https://webkitgtk.org/reference/webkit2gtk/stable/WebKitSettings.html
