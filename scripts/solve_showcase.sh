#!/usr/bin/env sh
# solve_showcase.sh [showcase|elements|symbols|cantilever|contact|tie] [JOB ...] -- regenerate
# samples/NAME/NAME.inp with scripts/gen_NAME.py and solve it with ccx (on PATH or in $CCX).
# Leaves NAME.frd / .dat / .sta / .cvg (and .cel, *.nam where ccx writes them) beside it.
# Further JOBs: other decks the generator wrote in samples/NAME/, solved too
# (contact contact_s2s).
set -e
cd "$(dirname "$0")/.."
n="${1:-showcase}"
python3 "scripts/gen_$n.py"
cd "samples/$n"
[ $# -gt 0 ] && shift
for j in "$n" "$@"; do
    rm -f "$j.frd" "$j.dat" "$j.sta" "$j.cvg" "$j.cel" "$j.12d" "$j"_Warn*.nam spooles.out
    "${CCX:-ccx}" "$j" > ccx.log 2>&1 || { tail -20 ccx.log; exit 1; }
    rm -f "$j.12d" spooles.out
    grep -c "^ -4" "$j.frd" | sed "s/^/$j fields: /"
done
