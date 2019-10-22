#!/usr/bin/env bash
#
# A simple headless automated smoke test to see if the semitransparency of the
# window works.  Doesn't test for click-through!
#
# Required programs:
#
# - Xvfb
# - compton
# - hsetroot
# - xwd (apt: x11-apps, pacman: xorg-xwd)
# - convert (from imagemagick)
#
export DISPLAY=:99
echo "Starting Xvfb"
# The Xvfb that comes with Ubuntu, has defaults to 8-bit depth, so the
# -screen spec is necessary for compositing to work correctly...
Xvfb -screen 0 1280x1024x24 +extension Composite "$DISPLAY" & xvfb_pid=$!
sleep 3
echo '- - -'
echo "Starting compositor"
compton --config /dev/null & compositor_pid=$!
sleep 3
echo '- - -'
echo "Setting background to black"
hsetroot -solid "#000000"
sleep 0.5
echo '- - -'

tmpfile="/tmp/hudkit_testfile.html"
echo '''
<html>
<body>
</body>
<style>
body { background: rgba(255,255,255,0.5) }
</style>
</html>
''' > $tmpfile
echo "Wrote test page to $tmpfile as thus:"
cat "$tmpfile"
echo '- - -'

echo "Starting Hudkit"
./hudkit "file://$tmpfile" & hudkit_pid=$!
sleep 3
echo '- - -'

echo "Capturing pixel"
out=$(xwd -root -silent | convert xwd:- -depth 8 -crop "1x1+0+0" txt:- | grep -om1 '#\w\+')
echo "Pixel value at (0,0): $out"
echo '- - -'

echo "Killing the background processes"
kill "$hudkit_pid"
kill "$compositor_pid"
kill "$xvfb_pid"
echo '- - -'

echo "Checking value"
rm "$tmpfile"
if [ "$out" != "#7F7F7F" ]; then
    echo "Pixel didn't match!"
    exit 1;
else
    echo "Pixel matched.  Exiting happily."
    exit 0
fi
