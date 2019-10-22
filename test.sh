#!/usr/bin/env bash
#
# A simple headless automated smoke test to see if the semitransparency of the
# window works.  Doesn't test for click-through!
#
# Required programs:
#
# - Xvfb
# - metacity
# - xwd (apt: x11-apps, pacman: xorg-xwd)
# - convert (from imagemagick)
#
echo "Starting Xvfb"
Xvfb +extension Composite :99 & xvfb_pid=$!
sleep 3
echo '- - -'
echo
echo "Starting metacity"
DISPLAY=:99 metacity & metacity_pid=$!
sleep 3
echo '- - -'
echo
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
echo
echo "Starting Hudkit"
DISPLAY=:99 ./hudkit "file://$tmpfile" & hudkit_pid=$!
sleep 3
echo '- - -'
echo
echo "Capturing pixel"
out=$(DISPLAY=:99 xwd -root -silent | convert xwd:- -depth 8 -crop "1x1+0+0" txt:- | grep -om1 '#\w\+')
echo "Pixel value at (0,0): $out"
echo '- - -'
echo
echo "Killing the background processes"
kill "$hudkit_pid"
kill "$metacity_pid"
kill "$xvfb_pid"
echo '- - -'
echo
echo "Checking value"
rm "$tmpfile"
if [ "$out" != "#7F7F7F" ]; then
    echo "Pixel didn't match!"
    exit 1;
else
    echo "Pixel matched.  Exiting happily."
    exit 0
fi
