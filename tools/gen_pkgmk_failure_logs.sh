#!/bin/sh
# gen_pkgmk_failure_logs.sh - real pkgmk failure output for BuildLogAnalyzer fixtures
#
# Runs the installed pkgmk(8) as the normal user against small synthetic ports
# inside a private sandbox directory, one port per failure class, and records
# the combined stdout/stderr (the same stream prt-get writes to its build log)
# and the exit code. Every line in the resulting logs is printed by the real
# pkgmk, bash, cc, curl or bsdtar of this system; nothing is hand-written.
#
# Safety: refuses to run as root. pkgmk is invoked with -cf <sandbox conf>, so
# /etc/pkgmk.conf (and any signing key it names) is never read; all source,
# work and package directories live in the sandbox; no -i/-u, so pkgadd is
# never run. No network access: the download case is a file:// URL to a missing
# local path, and the signature case uses the port's distfile already on disk
# (the case is skipped if it is not there, rather than downloading it).
#
# Usage: sh gen_pkgmk_failure_logs.sh OUTDIR [SANDBOX_PARENT]
# Output: OUTDIR/<case>.log, OUTDIR/<case>.rc, OUTDIR/PROVENANCE.txt

set -u

OUTDIR=${1:?usage: gen_pkgmk_failure_logs.sh OUTDIR [SANDBOX_PARENT]}
PARENT=${2:-${TMPDIR:-/tmp}}

if [ "$(id -u)" -eq 0 ]; then
    echo "Refusing to run as root: run this as the normal desktop user." >&2
    exit 1
fi
command -v pkgmk >/dev/null 2>&1 || { echo "pkgmk not found" >&2; exit 1; }

mkdir -p "$OUTDIR" || exit 1
OUTDIR=$(cd "$OUTDIR" && pwd)
SB=$(mktemp -d "$PARENT/octocrux-pkgmk-sandbox.XXXXXX") || exit 1

# port CASE - create sandbox port dir with its own pkgmk.conf
port() {
    d="$SB/$1"
    mkdir -p "$d/src" "$d/pkg" "$d/work"
    cat > "$d/pkgmk.conf" <<EOF
PKGMK_SOURCE_DIR="$d/src"
PKGMK_PACKAGE_DIR="$d/pkg"
PKGMK_WORK_DIR="$d/work/\$name"
PKGMK_COMPRESSION_MODE="zst"
EOF
}

# pkgfile CASE SOURCES BUILD_BODY
pkgfile() {
    cat > "$SB/$1/Pkgfile" <<EOF
# Description: OctoCRUX sandbox port ($1)
name=octo-$1
version=1.0
release=1
source=($2)

build() {
$3
}
EOF
}

# run CASE - run pkgmk as prt-get does (-d), stdin closed
run() {
    ( cd "$SB/$1" && pkgmk -cf "$SB/$1/pkgmk.conf" -d > "$OUTDIR/$1.log" 2>&1 < /dev/null
      echo $? > "$OUTDIR/$1.rc" )
    printf '%-14s rc=%s\n' "$1" "$(cat "$OUTDIR/$1.rc")"
}

port success
pkgfile success '' '	install -d $PKG/usr/share/octo
	echo ok > $PKG/usr/share/octo/ok'
run success

port build-fail
pkgfile build-fail '' '	printf "#include <octocrux_missing_header.h>\nint x;\n" > a.c
	cc -c a.c'
run build-fail

port build-eacces
pkgfile build-eacces '' '	install -m 644 /dev/null /usr/octocrux-sandbox-probe'
run build-eacces

port enospc
pkgfile enospc '' '	echo octocrux > /dev/full'
run enospc

port footprint
pkgfile footprint '' '	install -d $PKG/usr/bin
	printf "#!/bin/sh\n" > $PKG/usr/bin/octo-new
	chmod 755 $PKG/usr/bin/octo-new'
printf 'drwxr-xr-x\troot/root\tusr/\ndrwxr-xr-x\troot/root\tusr/bin/\n-rwxr-xr-x\troot/root\tusr/bin/octo-old\n' \
    > "$SB/footprint/.footprint"
run footprint

port md5
printf 'hello\n' > "$SB/md5/data.txt"
pkgfile md5 'data.txt' '	true'
echo '00000000000000000000000000000000  data.txt' > "$SB/md5/.md5sum"
run md5

port download
pkgfile download 'file:///nonexistent-octocrux/octo-download-1.0.tar.gz' '	true'
run download

port unpack
printf 'not an archive\n' > "$SB/unpack/octo-unpack-1.0.tar.gz"
pkgfile unpack 'octo-unpack-1.0.tar.gz' '	true'
( cd "$SB/unpack" && md5sum octo-unpack-1.0.tar.gz > .md5sum )
run unpack

port pkgfile
pkgfile pkgfile '' '	true'
sed -i '/^version=/d' "$SB/pkgfile/Pkgfile"
run pkgfile

port dirperm
pkgfile dirperm '' '	true'
sed -i "s|^PKGMK_PACKAGE_DIR=.*|PKGMK_PACKAGE_DIR=\"$SB/dirperm/does-not-exist\"|" "$SB/dirperm/pkgmk.conf"
run dirperm

# Signature mismatch: a real signed port whose Pkgfile no longer matches .signature.
# Its source tarball must already be on disk (port dir or /usr/ports/distfiles);
# it is copied into the sandbox so pkgmk -d finds it and never downloads.
SIGPORT=""
SIGSRC=""
for p in /usr/ports/contrib/lz4 /usr/ports/*/lz4; do
    [ -f "$p/.signature" ] && [ -f "$p/Pkgfile" ] || continue
    for f in "$p"/lz4-*.tar.gz /usr/ports/distfiles/lz4-*.tar.gz; do
        [ -f "$f" ] && { SIGPORT=$p; SIGSRC=$f; break 2; }
    done
done
if [ -n "$SIGPORT" ]; then
    port signature
    cp "$SIGPORT/Pkgfile" "$SIGPORT/.signature" "$SB/signature/"
    cp "$SIGSRC" "$SB/signature/src/"
    [ -f "$SIGPORT/.footprint" ] && cp "$SIGPORT/.footprint" "$SB/signature/"
    echo '# octocrux sandbox change' >> "$SB/signature/Pkgfile"
    run signature
else
    echo "signature: skipped (no signed lz4 port with its distfile on disk)"
fi

# Interrupted: SIGTERM to pkgmk while build() runs (what QProcess::terminate()
# sends). SIGINT is not usable here: a non-interactive shell starts background
# jobs with SIGINT ignored, and bash cannot trap a signal ignored at entry.
port interrupt
pkgfile interrupt '' '	sleep 20'
( cd "$SB/interrupt" && {
    pkgmk -cf "$SB/interrupt/pkgmk.conf" -d > "$OUTDIR/interrupt.log" 2>&1 < /dev/null &
    pid=$!; sleep 2; kill -TERM "$pid"; wait "$pid"; echo $? > "$OUTDIR/interrupt.rc"; } )
printf '%-14s rc=%s\n' interrupt "$(cat "$OUTDIR/interrupt.rc")"

{
    echo "Generated by tools/gen_pkgmk_failure_logs.sh"
    echo "date: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "pkgmk: $(pkgmk -v 2>&1 | head -n 1)"
    echo "bash: $(bash --version | head -n 1)"
    echo "cc: $(cc --version | head -n 1)"
    echo "curl: $(curl --version | head -n 1)"
    echo "bsdtar: $(bsdtar --version | head -n 1)"
    echo "uid: $(id -u) (unprivileged)"
    echo "signature source port: ${SIGPORT:-<none>} (distfile: ${SIGSRC:-<none>})"
} > "$OUTDIR/PROVENANCE.txt"

rm -rf "$SB"
