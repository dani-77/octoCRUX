#!/bin/sh
# fake_stubborn_helper.sh - simulates a privilege helper that does not stop on the
# first SIGTERM (e.g. it relays the signal to a root child that keeps running).
# The first SIGTERM is logged and ignored; the second one makes it exit 143.
# A SIGKILL would end it silently, without the "second SIGTERM" line.

TERMS=0
on_term() {
    TERMS=$((TERMS + 1))
    if [ "$TERMS" -ge 2 ]; then
        echo "fake helper: second SIGTERM, exiting"
        exit 143
    fi
    echo "fake helper: SIGTERM ignored"
}
trap on_term TERM

echo "fake helper: ready"
i=0
while [ "$i" -lt 300 ]; do    # at most ~30 s, so a failed test cannot leave it behind
    sleep 0.1
    i=$((i + 1))
done
exit 0
