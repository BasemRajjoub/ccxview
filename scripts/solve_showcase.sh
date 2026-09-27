#!/usr/bin/env sh
# solve_showcase.sh -- regenerate samples/showcase/showcase.inp and solve it with ccx
# (ccx must be on PATH or in $CCX). Leaves showcase.frd / .dat / .sta / .cvg beside it.
set -e
cd "$(dirname "$0")/.."
python3 scripts/gen_showcase.py
cd samples/showcase
rm -f showcase.frd showcase.dat showcase.sta showcase.cvg showcase.12d spooles.out
"${CCX:-ccx}" showcase > ccx.log 2>&1 || { tail -20 ccx.log; exit 1; }
grep -c "^ -4" showcase.frd | sed 's/^/fields in showcase.frd: /'
