argv: [sh] [-c] [MANWIDTH=100 man "$1" | { if command -v col >/dev/null 2>&1; then col -b; else cat; fi; }] [sh] [pkgmk]
stdin: /dev/null
timeout: 300s
LANG=pt_PT.utf8 LC_ALL=<unset>
