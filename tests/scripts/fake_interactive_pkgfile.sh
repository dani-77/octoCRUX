#!/bin/sh
# fake_interactive_pkgfile.sh - simulates a Pkgfile build() that prompts on stdin.
# With stdin at EOF, 'read' fails at once and the script exits 1; with stdin left
# open as an unused pipe, it blocks forever (the hang CruxProcessRunner must avoid).

echo "=======> Building 'interactive-port'."
echo "Accept the license? [y/N]"
if read -r answer; then
    echo "answer: $answer"
    exit 0
fi
echo "stdin: EOF"
exit 1
