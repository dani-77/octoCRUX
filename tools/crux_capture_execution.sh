#!/bin/sh
# crux_capture_execution.sh - OctoCRUX read-only capture for the execution engine
#
# Collects, from a real CRUX system, the facts the BuildLogAnalyzer and the
# transaction executor depend on: pkgmk messages and exit codes as shipped,
# existing build logs, and prt-get dry runs. Nothing is built, installed,
# removed, locked or synced. Refuses to run as root.
#
# Usage: sh crux_capture_execution.sh [--dryrun-port PORT]
#   --dryrun-port PORT  extra non-installed port for 'prt-get --test depinst'
# Output: octocrux-exec-capture-<timestamp>.tar.gz
# Review before sharing: build logs may contain paths and host names.

set -u

DRY_PORT=""
while [ $# -gt 0 ]; do
    case $1 in
        --dryrun-port) shift; DRY_PORT=${1:?--dryrun-port needs a port name} ;;
        -h|--help) sed -n '2,/^$/{s/^# \{0,1\}//;p;}' "$0"; exit 0 ;;
        *) printf 'unknown option: %s\n' "$1" >&2; exit 2 ;;
    esac
    shift
done

if [ "$(id -u)" -eq 0 ]; then
    echo "Refusing to run as root: run this as the normal desktop user." >&2
    exit 1
fi

TS=$(date +%Y%m%d-%H%M%S)
OUT="octocrux-exec-capture-$TS"
mkdir -p "$OUT/cmd" "$OUT/files" || exit 1
SUMMARY="$OUT/SUMMARY.txt"
: > "$SUMMARY"

note() { printf '%s\n' "$*" | tee -a "$SUMMARY"; }

# run NAME CMD [ARGS...] - argv/stdout/stderr/rc recorded separately, stdin closed
run() {
    name=$1; shift
    base="$OUT/cmd/$name"
    { printf 'argv:'; for a in "$@"; do printf ' [%s]' "$a"; done
      printf '\nstdin: /dev/null\nLANG=%s LC_ALL=%s\n' "${LANG-<unset>}" "${LC_ALL-<unset>}"; } > "$base.cmd"
    if ! command -v "$1" >/dev/null 2>&1; then
        printf 'not found: %s\n' "$1" > "$base.err"; : > "$base.out"; echo 127 > "$base.rc"; return 127
    fi
    "$@" > "$base.out" 2> "$base.err" < /dev/null
    rc=$?
    echo "$rc" > "$base.rc"
    return $rc
}

note "== Versions"
run prtget-version prt-get version
run pkgmk-version pkgmk -v
note "prt-get: $(head -n 1 "$OUT/cmd/prtget-version.out")"
note "pkgmk:   $(head -n 1 "$OUT/cmd/pkgmk-version.out")"

note ""
note "== pkgmk as shipped (exit codes, messages, phase marker)"
PKGMK=$(command -v pkgmk 2>/dev/null || true)
if [ -n "$PKGMK" ] && [ -r "$PKGMK" ]; then
    cp "$PKGMK" "$OUT/files/pkgmk"
    run pkgmk-exitcodes grep -n -E '^[[:space:]]*E_[A-Z0-9_]+=' "$PKGMK"
    run pkgmk-error-msgs grep -n -E '(error|warning|info) "' "$PKGMK"
    run pkgmk-phase-marker grep -n -E '=+>' "$PKGMK"
    note "exit code definitions: $(wc -l < "$OUT/cmd/pkgmk-exitcodes.out")"
    note "error/warning/info messages: $(wc -l < "$OUT/cmd/pkgmk-error-msgs.out")"
    note "phase marker lines (first 3):"
    head -n 3 "$OUT/cmd/pkgmk-phase-marker.out" | sed 's/^/  /' | tee -a "$SUMMARY"
else
    note "pkgmk not found or not readable"
fi
PRTGET=$(command -v prt-get 2>/dev/null || true)
if [ -n "$PRTGET" ]; then
    run prtget-strings sh -c 'strings "$1" | grep -i -E "fail|error|not installed|locked|skip|ignor" | sort -u' sh "$PRTGET"
    note "prt-get message strings: $(wc -l < "$OUT/cmd/prtget-strings.out")"
fi

note ""
note "== Existing build logs (with rmlog_on_success yes, leftovers are usually failures)"
LOGFILE=$(awk '$1=="logfile"{print $2}' /etc/prt-get.conf 2>/dev/null)
note "prt-get.conf logfile: ${LOGFILE:-<not set>}"
LOGDIR=$(dirname "${LOGFILE:-/var/log/pkgbuild/x}")
if [ -d "$LOGDIR" ]; then
    run ls-logdir ls -lan "$LOGDIR"
    n=0
    for f in $(ls -t "$LOGDIR" 2>/dev/null); do
        [ -f "$LOGDIR/$f" ] && [ -r "$LOGDIR/$f" ] || continue
        mkdir -p "$OUT/files/logs"
        tail -n 400 "$LOGDIR/$f" > "$OUT/files/logs/$f"
        n=$((n + 1))
        [ "$n" -ge 10 ] && break
    done
    note "build logs copied (last 400 lines each, newest 10): $n"
else
    note "log directory $LOGDIR does not exist"
fi

note ""
note "== prt-get dry runs (--test: nothing is built or installed)"
run dry-update-locked prt-get --test update llvm
note "update --test llvm (locked): rc=$(cat "$OUT/cmd/dry-update-locked.rc")"
run dry-sysup prt-get --test sysup
note "sysup --test: rc=$(cat "$OUT/cmd/dry-sysup.rc")"
if [ -n "$DRY_PORT" ]; then
    run dry-depinst prt-get --test depinst "$DRY_PORT"
    note "depinst --test $DRY_PORT: rc=$(cat "$OUT/cmd/dry-depinst.rc")"
fi
run dry-remove-core prt-get --test remove glibc
note "remove --test glibc: rc=$(cat "$OUT/cmd/dry-remove-core.rc")"

note ""
note "== Privilege helpers"
for h in qt-sudo sudo doas su; do note "$h: $(command -v "$h" 2>/dev/null || echo 'not found')"; done
run qt-sudo-version qt-sudo -v

tar -czf "$OUT.tar.gz" "$OUT" && rm -rf "$OUT"
printf '\nDone: %s.tar.gz\nReview the build logs inside before sharing.\n' "$OUT"
