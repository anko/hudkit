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
# Xvfb at least on Ubuntu defaults to 8-bit depth, so the -screen spec is
# necessary to specify 24 bits, so compositing to works correctly.
Xvfb -screen 0 1280x1024x24 +extension Composite "$DISPLAY" & xvfb_pid=$!
sleep 3
echo '- - -'
echo "Starting compositor (compton)"
compton --config /dev/null & compositor_pid=$!
sleep 3
echo '- - -'
echo "Setting X root window background to #000000 (black)"
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

echo "Killing hudkit"
kill "$hudkit_pid"
wait "$hudkit_pid"
echo "Killing compton"
kill "$compositor_pid"
wait "$compositor_pid"
echo "Killing Xvfb"
kill "$xvfb_pid"
wait "$xvfb_pid"
echo '- - -'

echo "Comparing pixel value"
expected_value="#808080"
rm "$tmpfile"
if [ "$out" != "$expected_value" ]; then
    echo "Pixel didn't match!"
    echo "Expected $expected_value, got $out"
    exit 1;
else
    echo "Pixel matched!  Test passed."
    exit 0
fi
