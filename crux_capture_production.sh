#!/bin/sh
# OctoCRUX — Targeted Production Diagnostic Capture for CRUX 3.8
# Purpose: Capture canonical 11-token printf output and pkginfo -o pattern behavior
#          with full execution provenance (.cmd, .out, .err, .rc, .ms).
# Usage: ./crux_capture_production.sh
# Output: A tarball octocrux-prod-capture-<timestamp>.tar.gz ready to copy to dev machine.

set -eu

TS=$(date +%Y%m%d-%H%M%S)
OUT="octocrux-prod-capture-$TS"
CMDDIR="$OUT/cmd"

mkdir -p "$CMDDIR"

echo "=== OctoCRUX Production Capture ($TS) ==="
echo "Target directory: $OUT"

run_cmd() {
    name="$1"
    shift
    echo "Running: $*"
    cmd_file="$CMDDIR/$name.cmd"
    out_file="$CMDDIR/$name.out"
    err_file="$CMDDIR/$name.err"
    rc_file="$CMDDIR/$name.rc"
    ms_file="$CMDDIR/$name.ms"

    # Save exact invocation argv
    printf "argv:" > "$cmd_file"
    for arg in "$@"; do
        printf " [%s]" "$arg" >> "$cmd_file"
    done
    printf "\nstdin: /dev/null\nLANG=%s LC_ALL=%s\n" "${LANG:-<unset>}" "${LC_ALL:-<unset>}" >> "$cmd_file"

    t0=$(date +%s%3N 2>/dev/null || python3 -c 'import time; print(int(time.time()*1000))' 2>/dev/null || date +%s)
    rc=0
    "$@" > "$out_file" 2> "$err_file" || rc=$?
    t1=$(date +%s%3N 2>/dev/null || python3 -c 'import time; print(int(time.time()*1000))' 2>/dev/null || date +%s)

    echo "$rc" > "$rc_file"
    echo "$((t1 - t0))" > "$ms_file"
}

# 1. Canonical 11-token prt-get printf (%d strictly last to isolate embedded tabs)
TAB="$(printf '\t')"
NL="$(printf '\n')"
FORMAT="%n${TAB}%v${TAB}%r${TAB}%i${TAB}%l${TAB}%p${TAB}%R${TAB}%E${TAB}%O${TAB}%e${TAB}%d${NL}"

run_cmd "printf-production-11token" prt-get printf "$FORMAT"

# 2. pkginfo -o pattern tests (testing regex vs fnmatch with '+' and wildcards)
run_cmd "pkginfo-o-gxx-abs" pkginfo -o /usr/bin/g++
run_cmd "pkginfo-o-gxx-rel" pkginfo -o usr/bin/g++
run_cmd "pkginfo-o-stdcxx-wildcard" pkginfo -o "/usr/lib/libstdc++.so*"
run_cmd "pkginfo-o-prtget-abs" pkginfo -o /usr/bin/prt-get

# 3. Environment & pkgutils version info
run_cmd "pkginfo-version" pkginfo -v
run_cmd "prtget-version" prt-get -v

# 4. Pack tarball
TARBALL="octocrux-prod-capture-$TS.tar.gz"
echo "Packaging into $TARBALL..."
tar -zcf "$TARBALL" "$OUT"
rm -rf "$OUT"

echo "=== Done! Created $TARBALL ==="
echo "Copy this file to your dev machine into the octoCRUX folder."
