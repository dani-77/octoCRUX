#!/bin/sh
# fake_sudo.sh - stands in for sudo(8) in tests: logs, then either fails the
# way sudo -n does when a password is needed (FAKE_SUDO_NEEDS_PASSWORD set;
# message as observed with sudo 1.9.17p2, tests/fixtures/crux3.8/qtsudo-auth)
# or drops "-n" and runs the command unprivileged. With FAKE_SUDO_PROMPT_SECONDS
# set, it first shows sudo's prompt (no newline) and "answers" it that much later.
[ -n "$OCTOCRUX_PROBE_LOG" ] && echo "sudo $*" >> "$OCTOCRUX_PROBE_LOG"
if [ -n "$FAKE_SUDO_NEEDS_PASSWORD" ]; then
    echo "sudo: a password is required" >&2
    exit 1
fi
if [ -n "$FAKE_SUDO_PROMPT_SECONDS" ]; then
    printf 'Password: ' >&2
    sleep "$FAKE_SUDO_PROMPT_SECONDS"
    printf '\n' >&2
fi
[ "$1" = "-n" ] && shift
exec "$@"
