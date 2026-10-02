#!/bin/sh
# Run the interface script (src/ui_test.c): clicks through the real program and
# checks that every panel, list and window still answers. Needs a display; without
# one it starts Xvfb (xvfb-run or Xvfb). Failure pictures go to build/ui-test/.
#   scripts/ui-test.sh [binary] [model.frd]
bin=${1:-build/ccxview}
frd=${2:-samples/showcase/showcase.frd}
out=build/ui-test
mkdir -p "$out"
rm -f "$out"/ui-test-*.png
run() { "$@" "$bin" "$frd" --ui-test "$out" --size 1400x900; }
if [ -n "$UI_TEST_DISPLAY" ]; then DISPLAY=$UI_TEST_DISPLAY run
elif command -v xvfb-run >/dev/null 2>&1; then run xvfb-run -a -s "-screen 0 1600x1000x24"
elif command -v Xvfb >/dev/null 2>&1; then
    Xvfb :87 -screen 0 1600x1000x24 >/dev/null 2>&1 &
    xpid=$!
    for i in 1 2 3 4 5 6 7 8 9 10; do [ -e /tmp/.X11-unix/X87 ] && break; sleep 0.3; done
    DISPLAY=:87 run; rc=$?
    kill $xpid 2>/dev/null
    exit $rc
else run
fi
