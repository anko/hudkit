# hudkit

A [Head-Up Display](http://en.wikipedia.org/wiki/Head-up_display) with [WebKit](https://www.webkit.org/).

*Hudkit* sets up a transparent full-screen window with a `GTKWebView` (so you have a full-screen web browser with a transparent background), nulls its input shape (so you can click right through it) and lights up its window manager hints like a christmas tree (so your WM doesn't touch it). It then opens up the web page specified as an argument.

Basically, this lets you draw things with HTML, CSS and JavaScript on a transparent layer over your desktop.
