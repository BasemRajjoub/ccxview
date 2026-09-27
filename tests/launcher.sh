#!/usr/bin/env sh
# launcher.sh -- the Linux launcher runs bin/ccxview with the system's
# libraries first and falls back to lib/ only when the loader failed.
# A fake bin/ccxview stands in: it fails like the loader unless lib/ is on
# LD_LIBRARY_PATH.
set -e
cd "$(dirname "$0")/.."
t=$(mktemp -d)
mkdir -p "$t/bin" "$t/lib"
cat > "$t/bin/ccxview" <<'FAKE'
#!/bin/sh
case ":$LD_LIBRARY_PATH:" in
    *"/lib:"*) echo "ran with lib: $*"; exit 0 ;;
    *) echo "$0: error while loading shared libraries: libXi.so.6: cannot open shared object file" >&2; exit 127 ;;
esac
FAKE
chmod +x "$t/bin/ccxview"
# the launcher text lives in bundle.sh: extract it the same way bundle.sh writes it
sed -n "/^cat > \"\$OUT\/ccxview\" <<'EOF'$/,/^EOF$/p" scripts/bundle.sh | sed '1d;$d' > "$t/ccxview"
chmod +x "$t/ccxview"
out=$("$t/ccxview" a b 2>&1)
rm -rf "$t"
[ "$out" = "ran with lib: a b" ] || { echo "launcher test failed: '$out'" >&2; exit 1; }
echo "launcher: ok"
