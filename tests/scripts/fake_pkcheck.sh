#!/bin/sh
# fake_pkcheck.sh - stands in for pkcheck(1) in tests: logs its arguments to
# $OCTOCRUX_PROBE_LOG and exits $FAKE_PKCHECK_EXIT (default 2, "challenge":
# authorization would need interaction).
[ -n "$OCTOCRUX_PROBE_LOG" ] && echo "pkcheck $*" >> "$OCTOCRUX_PROBE_LOG"
exit "${FAKE_PKCHECK_EXIT:-2}"
