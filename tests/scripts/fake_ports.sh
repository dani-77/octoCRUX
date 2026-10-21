#!/bin/sh
# fake_ports.sh - stands in for ports(8) in tests. Appends its arguments to
# $OCTOCRUX_PROBE_LOG (if set) and exits with $FAKE_PORTS_EXIT (default 0).
if [ -n "$OCTOCRUX_PROBE_LOG" ]; then
    echo "$@" >> "$OCTOCRUX_PROBE_LOG"
fi
echo "Updating collection core"
exit "${FAKE_PORTS_EXIT:-0}"
