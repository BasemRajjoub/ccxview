#!/usr/bin/env bash
# symbolize.sh -- the stack of a ccxview crash report as functions and file:line.
#   scripts/symbolize.sh ccxview-crash.txt [BINARY]
# BINARY is the ccxview.exe / ccxview that crashed, built with debug info (a local
# build), or for a release the matching file from ccxview-symbols-<version>.zip
# (ccxview.exe.debug, ccxview.debug). Default: build/win/ccxview.exe for a Windows
# report, build/bin/ccxview for a Linux one. Needs binutils (addr2line, objdump);
# x86_64-w64-mingw32-addr2line works too for Windows reports on Linux.
# Frames of other modules (system DLLs, libc) are printed as they are.
set -euo pipefail
[ $# -ge 1 ] || { sed -n '2,8p' "$0"; exit 2; }
report=$1
bin=${2:-}
a2l=${ADDR2LINE:-addr2line}
if grep -q '\.exe+0x' "$report"; then
    bin=${bin:-build/win/ccxview.exe}
    # the report has offsets in the loaded image; addr2line wants the linked address
    base=$(objdump -p "$bin" | awk '$1 == "ImageBase" { print $2 }')
    base=$((16#$base))
else
    bin=${bin:-build/bin/ccxview}
    base=0                                   # PIE: backtrace's (+0x...) is the file offset
fi
[ -e "$bin" ] || { echo "symbolize: no $bin; name the binary or its .debug file" >&2; exit 1; }
sed -n '1,/^stack/p' "$report"
n=0
while IFS= read -r line; do
    [[ $line == "last log lines:"* ]] && { printf '%s\n' "$line"; cat; break; }
    # Windows "  #3   ccxview.exe+0x1a2b3", Linux "ccxview(+0x1a2b3) [0x...]" (PIE) or
    # "ccxview() [0x41a2b3]" (not PIE: the address itself)
    if [[ $line =~ ccxview(\.exe)?\+0x([0-9a-fA-F]+) || $line =~ ccxview\(\+0x([0-9a-fA-F]+)\) ||
          $line =~ ccxview\(\)\ \[0x([0-9a-fA-F]+)\] ]]; then
        off=${BASH_REMATCH[-1]}
        # past frame 0 the address is a return address: step back into the call
        addr=$(printf '0x%x' $((base + 16#$off - (n > 0 ? 1 : 0))))
        printf '  #%-3d %s\n' "$n" "$("$a2l" -f -i -C -p -s -e "$bin" "$addr" 2>/dev/null | tr '\n' ' ')"
    elif [[ $line =~ ^\ +#|\[0x ]]; then
        printf '  #%-3d %s\n' "$n" "$(sed 's/^ *#[0-9]* *//' <<< "$line")"
    else
        continue
    fi
    n=$((n + 1))
done < <(sed -n '/^stack/,$p' "$report" | tail -n +2)
