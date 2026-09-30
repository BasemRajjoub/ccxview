#!/usr/bin/env sh
# solve_showcase.sh [showcase|elements] -- regenerate samples/NAME/NAME.inp with
# scripts/gen_NAME.py and solve it with ccx (on PATH or in $CCX). Leaves NAME.frd /
# .dat / .sta / .cvg beside it.
set -e
cd "$(dirname "$0")/.."
n="${1:-showcase}"
python3 "scripts/gen_$n.py"
cd "samples/$n"
rm -f "$n.frd" "$n.dat" "$n.sta" "$n.cvg" "$n.12d" spooles.out
"${CCX:-ccx}" "$n" > ccx.log 2>&1 || { tail -20 ccx.log; exit 1; }
grep -c "^ -4" "$n.frd" | sed 's/^/fields: /'
