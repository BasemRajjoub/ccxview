#!/usr/bin/env sh
# check-sources.sh -- the source lists of Makefile and build.bat must agree.
cd "$(dirname "$0")/.."
mk=$(sed -n 's/^CORE = //p; s/^APP  = //p' Makefile | tr ' ' '\n' | grep '\.c$' | sort)
bat=$(sed -n 's/^set SRC=//p' build.bat | tr ' ' '\n' | tr '\\' '/' | grep '^src/' | grep -v 'sokol_impl' | sort)
if [ "$mk" != "$bat" ]; then
    echo "check-sources: Makefile and build.bat list different sources:" >&2
    t="${TMPDIR:-/tmp}/ccxview_sources.$$"
    echo "$mk" > "$t.mk"; echo "$bat" > "$t.bat"
    diff "$t.mk" "$t.bat" >&2; rm -f "$t.mk" "$t.bat"
    exit 1
fi
echo "check-sources: ok ($(echo "$mk" | wc -l | tr -d ' ') files)"
