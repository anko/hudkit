# Hudkit

A web-based [Head-Up Display][1] for your desktop, using the [WebKit][2] browser engine.

## What?

Yes.

Hudkit lets you use HTML, CSS and JavaScript to draw and animate whatever you like on a click-through transparent fullscreen layer over your desktop.  You know, like on a military plane!  Except in JavaScript!  Basically just put some fun stuff on a local web server and treat hudkit like a web browser, except, well… transparent, fullscreen and click-through.

I made this because I wanted to have the world's awesomest statusbar: partially transparent, floating over everything else and rendered in SVG with [D3][3] because it's beautiful and quick to iterate on.  And because I wanted to see what would happen.

It should be usable for all sorts of other experiments too.

## Installing

Just run `make`.  You'll need `gtk3` and a corresponding `webkitgtk`.  (At least that's what the packages are called on [Arch][4].)

If you're building this on some other distro, I'd like to hear how it went, regardless of the outcome!  If it exploded, raise an issue.  If it worked, consider submitting a PR to this `README` with what packages you needed.

## Running

Serve the stuff you want on it from a web server of some kind (`python -m http.server` is nice), then run `hudkit http://whateverrr` for an appropriate value of `whateverrr` (such as `localhost:8000`).  See `examples/` for a dead simple commented base to test with.

You'll probably want to serve a page with a transparent background.

Make sure your windowing environment has compositing enabled.  If you're running a plain window manager, a standalone compositor like [compton][5] should do it.

## Limitations

At the moment, hudkit derives the size of its overlay window from the size of the first graphics display it encounters.  So it really doesn't work very well for multiple displays.  I really would like it to, so if you've got ideas, tell me.  Connecting new displays shouldn't confuse it; it'll just keep covering the one display it found first.


[1]: http://en.wikipedia.org/wiki/Head-up_display
[2]: https://www.webkit.org/
[3]: http://d3js.org/
[4]: https://www.archlinux.org/
[5]: https://github.com/chjj/compton
