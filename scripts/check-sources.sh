#!/usr/bin/env sh
# check-sources.sh -- the source lists of Makefile and build.bat must agree.
cd "$(dirname "$0")/.."
mk=$(sed -n 's/^CORE = //p; s/^APP  = //p' Makefile | tr ' ' '\n' | grep '\.c$' | sort)
bat=$(sed -n 's/^set SRC=//p' build.bat | tr ' ' '\n' | tr '\\' '/' | grep '^src/' | grep -v 'sokol_impl' | sort)
if [ "$mk" != "$bat" ]; then
    echo "check-sources: Makefile and build.bat list different sources:" >&2
    diff <(echo "$mk") <(echo "$bat") >&2 2>/dev/null || { echo "$mk" > /tmp/ccxview_mk.$$; echo "$bat" > /tmp/ccxview_bat.$$; diff /tmp/ccxview_mk.$$ /tmp/ccxview_bat.$$ >&2; rm -f /tmp/ccxview_mk.$$ /tmp/ccxview_bat.$$; }
    exit 1
fi
echo "check-sources: ok ($(echo "$mk" | wc -l | tr -d ' ') files)"
