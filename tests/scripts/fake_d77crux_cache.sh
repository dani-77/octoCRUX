#!/bin/sh
# fake_d77crux_cache.sh - stands in for d77crux-cache(1) in tests. Logs its
# arguments to OCTOCRUX_PROBE_LOG.
#   -n         report only: prints the file named by FAKE_CACHE_REPORT;
#              FAKE_CACHE_FAIL makes it fail like a run without network;
#              FAKE_CACHE_DELAY=<seconds> makes it slow, like the real one
#              (about 2.3 s on the owner's T480s: it downloads the manifest).
#   (no args)  the placing run (root in real life): prints one "cached" line
#              and exits with FAKE_CACHE_PLACE_EXIT (default 0). It places nothing.
[ -n "$OCTOCRUX_PROBE_LOG" ] && echo "d77crux-cache $*" | sed 's/ $//' >> "$OCTOCRUX_PROBE_LOG"
if [ "$#" -eq 0 ]; then
    echo "cached           fake                         1.0-1"
    exit "${FAKE_CACHE_PLACE_EXIT:-0}"
fi
[ "$1" = "-n" ] || { echo "fake d77crux-cache: unsupported arguments: $*" >&2; exit 2; }
[ -n "$FAKE_CACHE_DELAY" ] && sleep "$FAKE_CACHE_DELAY"
if [ -n "$FAKE_CACHE_FAIL" ]; then
    echo "d77crux-cache: cannot download the manifest" >&2
    exit 1
fi
echo "d77crux-cache: ACCEPT_GENERIC=yes: placing generic x86-64 binaries" >&2
cat "${FAKE_CACHE_REPORT:?set FAKE_CACHE_REPORT}"
