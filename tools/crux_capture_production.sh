#!/bin/sh
# OctoCRUX — Targeted Production Diagnostic Capture for CRUX 3.8
# Purpose: Capture canonical 11-token printf output and pkginfo -o pattern behavior
#          with full execution provenance (.cmd, .out, .err, .rc, .ms).
# Read-only: runs only query commands. Refuses to run as root.
# Usage: sh tools/crux_capture_production.sh
# Output: A tarball octocrux-prod-capture-<timestamp>.tar.gz ready to copy to dev machine.

set -eu

if [ "$(id -u)" -eq 0 ]; then
    echo "Refusing to run as root: run this as the normal desktop user." >&2
    exit 1
fi

TS=$(date +%Y%m%d-%H%M%S)
OUT="octocrux-prod-capture-$TS"
CMDDIR="$OUT/cmd"

mkdir -p "$CMDDIR"

echo "=== OctoCRUX Production Capture ($TS) ==="
echo "Target directory: $OUT"

now_ms() {
    t=$(date +%s%3N 2>/dev/null || true)
    case $t in
        *N*|'') t=$(( $(date +%s) * 1000 )) ;;
    esac
    printf '%s' "$t"
}

run_cmd() {
    name="$1"
    shift
    echo "Running: $name"
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

    t0=$(now_ms)
    rc=0
    "$@" > "$out_file" 2> "$err_file" < /dev/null || rc=$?
    t1=$(now_ms)

    echo "$rc" > "$rc_file"
    echo "$((t1 - t0))" > "$ms_file"
}

# 1. Canonical 11-token prt-get printf (%d strictly last to isolate embedded tabs).
#    Real TAB and NEWLINE characters, exactly as QProcess will pass them.
#    NOTE: NL must be a literal newline; "$(printf '\n')" would be EMPTY,
#    because command substitution strips trailing newlines.
TAB="$(printf '\t')"
NL='
'
FORMAT="%n${TAB}%v${TAB}%r${TAB}%i${TAB}%l${TAB}%p${TAB}%R${TAB}%E${TAB}%O${TAB}%e${TAB}%d${NL}"

run_cmd "printf-production-11token" prt-get printf "$FORMAT"

# Sanity check of the capture itself (never trust an unchecked fixture).
P="$CMDDIR/printf-production-11token"
lines=$(wc -l < "$P.out")
bad=$(awk -F'\t' 'NF != 11' "$P.out" | wc -l)
echo "printf: rc=$(cat "$P.rc"), $lines lines, $bad lines with field count != 11"
if [ "$(cat "$P.rc")" != 0 ] || [ "$lines" -lt 2 ] || [ "$bad" -ne 0 ]; then
    echo "WARNING: printf capture looks wrong; inspect $P.out / $P.err before using it as a fixture." >&2
fi

# 2. pkginfo -o pattern semantics.
#    Literal paths containing regex metacharacters:
run_cmd "pkginfo-o-gxx-abs" pkginfo -o /usr/bin/g++
run_cmd "pkginfo-o-gxx-rel" pkginfo -o usr/bin/g++
run_cmd "pkginfo-o-prtget-abs" pkginfo -o /usr/bin/prt-get
#    Previous capture showed regex substring semantics ("/usr/bin/g++" matched
#    192 files under usr/bin/g*). These confirm it and test the safe form:
#    - '.' matches any char only if the pattern is a REGEX
run_cmd "pkginfo-o-disc-regex-dot" pkginfo -o usr/bin/prt.get
#    - '?' matches one char only if the pattern is a GLOB (fnmatch)
run_cmd "pkginfo-o-disc-glob-qmark" pkginfo -o "usr/bin/prt?get"
#    - a literal path that, read as a regex, should NOT match itself
run_cmd "pkginfo-o-stdcxx-literal" pkginfo -o /usr/lib/libstdc++.so.6
#    - the same path regex-escaped (the form OctoCRUX would send)
run_cmd "pkginfo-o-stdcxx-escaped" pkginfo -o 'usr/lib/libstdc\+\+\.so\.6'
#    - escaped and anchored, without and with a leading slash
run_cmd "pkginfo-o-gxx-escaped-anchored" pkginfo -o '^usr/bin/g\+\+$'
run_cmd "pkginfo-o-gxx-escaped-anchored-slash" pkginfo -o '^/usr/bin/g\+\+$'
#    - unescaped wildcard behaviour (kept for comparison with the previous run)
run_cmd "pkginfo-o-stdcxx-wildcard" pkginfo -o "/usr/lib/libstdc++.so*"

# 3. Environment & tool version info
run_cmd "pkginfo-version" pkginfo -v
run_cmd "prtget-version" prt-get version

# 4. Pack tarball
TARBALL="octocrux-prod-capture-$TS.tar.gz"
echo "Packaging into $TARBALL..."
tar -zcf "$TARBALL" "$OUT"
rm -rf "$OUT"

echo "=== Done! Created $TARBALL ==="
echo "Copy this file to your dev machine into the octoCRUX folder."
