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

tmpfile_output="/tmp/hudkit_test_output.txt"
tmpfile_html="/tmp/hudkit_test_input.html"
echo '''
<html>
<body>
</body>
<style>
body { background: rgba(255,255,255,0.5) }
</style>
<script>
console.log("what")
;(async () => {
  console.log(JSON.stringify(await Hudkit.getMonitorLayout()))
  console.log(JSON.stringify({ userAgent: navigator.userAgent }))
  Hudkit.on("composited-changed", hasTransparency =>
    console.log(`hasTransparency ${hasTransparency}`))
})()
</script>
</html>
''' > $tmpfile_html
echo "Wrote test page to $tmpfile_html as thus:"
cat "$tmpfile_html"
echo '- - -'

echo "Starting Hudkit"
./hudkit --webkit-settings user-agent=test_ua "file://$tmpfile_html" > "$tmpfile_output" 2>&1 & hudkit_pid=$!
# We have to redirect stderr to stdout (2>&1), because webkit's
# 'enable-write-console-messages-to-stdout' setting is a lie; it actually logs
# to stderr.
sleep 3
echo '- - -'

echo "Capturing pixel"
out=$(xwd -root -silent | convert xwd:- -depth 8 -crop "1x1+0+0" txt:- | grep -om1 '#\w\+')
echo "Pixel value at (0,0): $out"
echo '- - -'

# Deliberaretely killing the compositor first, to fire the composited-changed
# listener in JS, so we can test for that.
echo "Killing compton"
kill "$compositor_pid"
wait "$compositor_pid"
sleep 2 # Give the 'composited-changed' listener time to fire
echo "Killing hudkit"
kill "$hudkit_pid"
wait "$hudkit_pid"
echo "Killing Xvfb"
kill "$xvfb_pid"
wait "$xvfb_pid"
echo '- - -'

exit_code=0

echo "Comparing pixel value"
expected_value="#808080"
if [ "$out" != "$expected_value" ]; then
    echo "Pixel didn't match!"
    echo "Expected $expected_value, got $out"
    exit_code=1
else
    echo "Pixel matched!  OK."
fi
echo '- - -'

echo "Comparing output log"
expected_to_contain=$(cat <<END
CONSOLE LOG [{"name":"screen","x":0,"y":0,"width":1280,"height":1024}]
END
)
if grep --quiet --fixed-strings "$expected_to_contain" "$tmpfile_output"; then
    echo "Saw Hudkit.getMonitorLayout() response in log!  OK."
else
    echo "Did not see Hudkit.getMonitorLayout() response in log!"
    exit_code=1
fi

expected_to_contain=$(cat <<END
CONSOLE LOG {"userAgent":"test_ua"}
END
)
if grep --quiet --fixed-strings "$expected_to_contain" "$tmpfile_output"; then
    echo "Saw user agent set by --webkit-settings in log!  OK."
else
    echo "Did not see user agent set by --webkit-settings in log!"
    exit_code=1
fi

expected_to_contain=$(cat <<END
CONSOLE LOG hasTransparency false
END
)
if grep --quiet --fixed-strings "$expected_to_contain" "$tmpfile_output"; then
    echo "Saw output from 'composited-changed' listener in log!  OK."
else
    echo "Did not see output from 'composited-changed' listener in log!"
    exit_code=1
fi
echo '- - -'

if (( "$exit_code" > 0 )); then
    echo "Some tests failed."
    echo "Hudkit output:"
    cat "$tmpfile_output"
else
    echo "All tests passed.  :)"
fi

echo '- - -'
# Remove temporary files
rm "$tmpfile_html"
rm "$tmpfile_output"

exit "$exit_code"
