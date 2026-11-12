#!/bin/sh
# fake_d77crux_cache.sh - stands in for d77crux-cache(1) in tests: accepts only
# -n (report only), logs its arguments, and prints the report file named by
# FAKE_CACHE_REPORT (default: the real 2026-11-12 capture). FAKE_CACHE_FAIL
# makes it fail like a run without network.
[ -n "$OCTOCRUX_PROBE_LOG" ] && echo "d77crux-cache $*" >> "$OCTOCRUX_PROBE_LOG"
[ "$1" = "-n" ] || { echo "fake d77crux-cache: only -n is supported" >&2; exit 2; }
if [ -n "$FAKE_CACHE_FAIL" ]; then
    echo "d77crux-cache: cannot download the manifest" >&2
    exit 1
fi
echo "d77crux-cache: ACCEPT_GENERIC=yes: placing generic x86-64 binaries" >&2
cat "${FAKE_CACHE_REPORT:?set FAKE_CACHE_REPORT}"
