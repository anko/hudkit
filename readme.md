# Hudkit

A web-based [Head-Up Display][wiki-hud] for your desktop, using the [WebKit][webkit] browser engine.

Hudkit lets you use web technologies (HTML, CSS, JavaScript, WebSockets, etc.) to draw and animate anything on a click-through transparent fullscreen layer over your desktop.  You know, like military planes have!  Except on your monitor!  As a chromeless web browser!

## Use it

 1. Start a web server, with Python or Node.js or whatever you like.  Serve up
    an HTML page with a transparent background.  Let's say the server is at
    `localhost:8000`.
 2. Run `hudkit http://localhost:8000`
 3. Enjoy the eye candy! :rainbow:

See [`example/`](example/) for a quick test script and starting point.

Make sure your windowing environment has compositing enabled!  If you're running a plain window manager, a standalone compositor like [compton][compton] should do it.

## Install

    make

### Dependencies

You'll need *GTK 3*, and a corresponding *webkitgtk*.

On [Arch][arch], the packages are called `gtk3` and `webkitgtk`.

If you build on another distro, tell me how it went!  If it failed, [raise an issue][new-issue].  If it worked, submit a PR to this readme with the packages you needed, or just email [me][anko] if you don't have a Github.

## Limitations

 - **Single-display**. At the moment, hudkit derives the size of its overlay window from the size of the first graphics display it encounters.  So it really doesn't work very well for multiple displays.  If you've got ideas, tell me.  (Connecting more displays shouldn't confuse it: it'll just stick to the one it saw first.)

## License

[ISC](https://en.wikipedia.org/wiki/ISC_license).


[anko]: https://github.com/anko
[arch]: https://www.archlinux.org/
[compton]: https://github.com/chjj/compton
[webkit]: https://www.webkit.org/
[wiki-hud]: http://en.wikipedia.org/wiki/Head-up_display
[new-issue]: https://github.com/anko/hudkit/issues/new
