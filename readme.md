# Hudkit&ensp;[![](https://img.shields.io/travis/anko/hudkit?style=flat-square)](https://travis-ci.org/anko/hudkit)

A web-based [Head-Up Display][wiki-hud] for your desktop, using the [WebKit][webkit] browser engine.

Hudkit lets you use web technologies (HTML, CSS, JavaScript, WebSockets, etc.) to draw and animate anything on a *click-through transparent fullscreen layer over your desktop*.  You know, like military planes have!  Except on your monitor(s)!  As a chromeless web browser!

## Fastest way to check if this works

```
cd /tmp
git clone https://github.com/anko/hudkit.git
cd hudkit
make
cd example
./run.sh
```

Install the [dependencies](#dependencies) if `make` complains.

## General usage

Ensure your windowing environment has compositing enabled!  If you're running a plain window manager, a standalone compositor like [compton][compton] running should be enough.

 1. Start a web server, with Python or Node.js or whatever you like.  Serve up
    an HTML page with a transparent background.  Let's say the server is at
    `localhost:8000`.
 2. Run `hudkit http://localhost:8000`
 3. Enjoy the eye candy! :rainbow:

See [`example/`](example/) for a quick test script and starting point.

### JavaScript API

The JavaScript on the page context has an object `Hudkit` preloaded.  `Hudkit.monitors` is an array with each of your monitors represented by a `{x,y,width,height}`-object.  You can use this to position statusbars or notification messages.

DOM APIs like SVG work as you'd expect.

WebSockets work normally too.  You can use them to send live data from your system to the overlay.  If you're writing your server in Node.js, maybe use [dnode][dnode] to remote-procedure-call the Node.js API from page context, or just [SockJS][sockjs] if just a stream is enough, or both.

Ideas for what to send it that might be fun:

 - keystrokes captured by [xkbcat][xkbcat],
 - [Twitch][twitch] chat ![Kappa](https://static-cdn.jtvnw.net/emoticons/v1/25/1.0),
 - name of the currently playing music track,
 - recently received chat messages,
 - `pstree` (port [pscircle][pscircle] to JS with a [D3.js tree layout][d3_tree_example]?),
 - `sensors` for hardware temperatures and fan data,
 - more JavaScript code to `eval`.

WebGL doesn't work.  You're still welcome to try.

## Install

    make

### Dependencies

You'll need *GTK 3*, and a corresponding *webkit2gtk*.

On [Arch][arch], the packages are called `gtk3` and `webkit2gtk`.

On [Void][void], they are `gtk+3-devel` and `webkit2gtk-devel`.

On [Ubuntu][ubuntu], they are `libgtk-3-dev` and `libwebkit2gtk-4.0-devel`.

If you build on another distro, tell me how it went!  If it failed, [raise an issue][new-issue].  If it worked, submit a PR to this readme with the packages you needed, or just email [me][anko] if you don't have a Github.

## Limitations

 - **Requires a restart if rearranging monitors**.  Hudkit can handle multi-monitor setups: it detects their arrangement on startup, not dynamically.
 - **It's only WebKit**.  Consequently, you probably can't use WebGL, WebAudio, WebVR, or any other brand new web API.

## Alternatives

- [Electron][electron].  I've heard it's possible:  Start it with `--enable-transparent-visuals --disable-gpu`.  Call [`win.setIgnoreMouseEvents`][electron_ignoremouse], set all the possible "please dear WM, do not touch this window"-flags, call the [`screen` API](https://electronjs.org/docs/api/screen) for monitor arrangements and position your window accordingly.  Sacrifice 55 paperclips to Eris of Discordia.

  It didn't work for me.  I just couldn't get a transparent or click-through window out of Electron, but maybe you can.  Let me know if you do.

  You'd get a nicer API (:sparkles:Page-context Node.js integration!  Chromium web engine!:sparkles:), though continuing support for your use-case will probably be even more fragile than what Hudkit relies on.

## License

[ISC](https://en.wikipedia.org/wiki/ISC_license).


[anko]: https://github.com/anko
[arch]: https://www.archlinux.org/
[compton]: https://github.com/chjj/compton
[d3_tree_example]: https://bl.ocks.org/mbostock/4063550
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
