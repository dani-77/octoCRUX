#!/bin/sh
# fake_pkexec.sh - stands in for pkexec(1) in tests: logs, then runs the
# command unprivileged (the command is itself a fake in tests).
[ -n "$OCTOCRUX_PROBE_LOG" ] && echo "pkexec $*" >> "$OCTOCRUX_PROBE_LOG"
exec "$@"
