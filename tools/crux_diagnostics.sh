#!/bin/sh
# crux_diagnostics.sh - OctoCRUX read-only environment diagnostics
#
# Captures the real behaviour of CRUX package/ports tooling and of the build
# environment, to serve as Level 1/2 fixtures (see OPEN_QUESTIONS.md).
#
# Guarantees:
#   - Read-only: never installs, removes, syncs, locks or edits anything.
#   - Refuses to run as root (it must observe what the desktop user sees).
#   - Every command is executed as an explicit argv (no shell strings), with
#     stdin closed, and recorded as separate files: argv, stdout, stderr,
#     exit code and duration. Output is never filtered or "cleaned".
#   - The effective pkgmk.conf is sourced in a minimal environment to obtain
#     its effective values. This EXECUTES the file, exactly as pkgmk does.
#     It is skipped unless the file is root-owned and not group/world-writable.
#
# Options:
#   --with-dryrun    also run 'prt-get --test depinst <available sample>', only
#                    if prt-get's own usage text advertises --test.
#   --installed P    sample installed port (default: glibc, else first installed)
#   --available P    sample non-installed port (default: first with %i=no,
#                    preferring one that declares dependencies)
#   -h, --help       show this help
#
# Output: ./octocrux-diag-<timestamp>/ plus a .tar.gz of it.
# Review before sharing: it contains hostname, user names, uids and paths.

set -u

RUN_TIMEOUT=300
WITH_DRYRUN=0
SAMPLE_INST=""
SAMPLE_AVAIL=""
TAB=$(printf '\t')
NL='
'

usage() {
    sed -n '2,/^$/{s/^# \{0,1\}//;p;}' "$0"
}

while [ $# -gt 0 ]; do
    case $1 in
        --with-dryrun) WITH_DRYRUN=1 ;;
        --installed) shift; SAMPLE_INST=${1:?--installed needs a port name} ;;
        --available) shift; SAMPLE_AVAIL=${1:?--available needs a port name} ;;
        -h|--help) usage; exit 0 ;;
        *) printf 'unknown option: %s\n' "$1" >&2; usage >&2; exit 2 ;;
    esac
    shift
done

if [ "$(id -u)" -eq 0 ]; then
    echo "Refusing to run as root: run this as the normal desktop user." >&2
    exit 1
fi

TS=$(date +%Y%m%d-%H%M%S)
OUT="$PWD/octocrux-diag-$TS"
mkdir -p "$OUT/cmd" "$OUT/files" || exit 1
SUMMARY="$OUT/SUMMARY.txt"
: > "$SUMMARY"
: > "$OUT/mounts.txt"
: > "$OUT/files/_perms.txt"

TIMEOUT_BIN=$(command -v timeout 2>/dev/null || true)

# ---------------------------------------------------------------- helpers --

note() {
    printf '%s\n' "$*" | tee -a "$SUMMARY"
}

section() {
    printf '\n## %s\n' "$*" | tee -a "$SUMMARY"
}

now_ms() {
    t=$(date +%s%3N 2>/dev/null)
    case $t in
        *N*|'') t=$(( $(date +%s) * 1000 )) ;;
    esac
    printf '%s' "$t"
}

# run NAME CMD [ARGS...]
# Executes argv directly and records argv/stdout/stderr/rc/duration.
run() {
    name=$1
    shift
    base="$OUT/cmd/$name"
    {
        printf 'argv:'
        for a in "$@"; do printf ' [%s]' "$a"; done
        printf '\nstdin: /dev/null\n'
        printf 'timeout: %s\n' "${TIMEOUT_BIN:+${RUN_TIMEOUT}s}"
        printf 'LANG=%s LC_ALL=%s\n' "${LANG-<unset>}" "${LC_ALL-<unset>}"
    } > "$base.cmd"

    if ! command -v "$1" >/dev/null 2>&1; then
        printf 'not found: %s\n' "$1" > "$base.err"
        : > "$base.out"
        echo 127 > "$base.rc"
        echo 0 > "$base.ms"
        return 127
    fi

    t0=$(now_ms)
    if [ -n "$TIMEOUT_BIN" ]; then
        "$TIMEOUT_BIN" "$RUN_TIMEOUT" "$@" > "$base.out" 2> "$base.err" < /dev/null
    else
        "$@" > "$base.out" 2> "$base.err" < /dev/null
    fi
    rc=$?
    t1=$(now_ms)
    echo "$rc" > "$base.rc"
    echo $(( t1 - t0 )) > "$base.ms"
    [ "$rc" -eq 124 ] && [ -n "$TIMEOUT_BIN" ] && note "TIMEOUT: $name"
    return $rc
}

# grab FILE - copy a file into files/ preserving its path, and log perms.
grab() {
    src=$1
    dst="$OUT/files$src"
    if [ -r "$src" ] && [ -f "$src" ]; then
        mkdir -p "$(dirname "$dst")"
        cp -p "$src" "$dst"
        ls -ln "$src" >> "$OUT/files/_perms.txt"
    elif [ -e "$src" ]; then
        echo "unreadable: $(ls -ln "$src" 2>&1)" >> "$OUT/files/_perms.txt"
    else
        echo "absent: $src" >> "$OUT/files/_perms.txt"
    fi
}

# existing_parent PATH - nearest existing ancestor (values may contain
# components that only exist during a build, e.g. per-port names).
existing_parent() {
    ep=$1
    while [ -n "$ep" ] && [ "$ep" != "/" ] && [ ! -e "$ep" ]; do
        ep=$(dirname "$ep")
    done
    printf '%s' "${ep:-/}"
}

# mountinfo LABEL PATH - record the real mount (not fstab) behind PATH.
mountinfo() {
    ml=$1
    mp=$2
    {
        printf '== %s: %s\n' "$ml" "$mp"
        if [ -e "$mp" ]; then
            ls -ldn "$mp"
            command -v findmnt >/dev/null 2>&1 &&
                findmnt -n -T "$mp" -o TARGET,SOURCE,FSTYPE,OPTIONS
            df -P "$mp"
            df -Pi "$mp"
        else
            echo "path does not exist"
        fi
        echo
    } >> "$OUT/mounts.txt" 2>&1

    [ -e "$mp" ] || { note "$ml: $mp does not exist"; return 0; }
    command -v findmnt >/dev/null 2>&1 || return 0
    mopts=$(findmnt -n -T "$mp" -o OPTIONS 2>/dev/null)
    mfs=$(findmnt -n -T "$mp" -o FSTYPE 2>/dev/null)
    mtgt=$(findmnt -n -T "$mp" -o TARGET 2>/dev/null)
    avail=$(df -Pk "$mp" 2>/dev/null | awk 'NR==2{print $4}')
    note "$ml: $mp -> mount $mtgt ($mfs), ${avail:-?} KiB free"
    case ",$mopts," in
        *,noexec,*) note "  WARNING: $ml is on a noexec mount" ;;
    esac
    case ",$mopts," in
        *,ro,*) note "  WARNING: $ml is on a read-only mount" ;;
    esac
}

# succeeded NAME - true only if the recorded command exited 0 AND produced
# stdout. Conclusions must never be drawn from a failed or absent command.
succeeded() {
    [ "$(cat "$OUT/cmd/$1.rc" 2>/dev/null)" = 0 ] && [ -s "$OUT/cmd/$1.out" ]
}

# exited_ok NAME - true if the recorded command exited 0 (empty output allowed,
# e.g. quickdiff when nothing is outdated).
exited_ok() {
    [ "$(cat "$OUT/cmd/$1.rc" 2>/dev/null)" = 0 ]
}

# status NAME - short human-readable outcome of a recorded command.
status() {
    if [ ! -f "$OUT/cmd/$1.rc" ]; then
        printf 'not run'
        return 0
    fi
    printf 'rc=%s' "$(cat "$OUT/cmd/$1.rc")"
    [ -s "$OUT/cmd/$1.err" ] && printf ' err="%s"' "$(head -n 1 "$OUT/cmd/$1.err")"
    return 0
}

# getvar NAME - value of NAME from the sourced pkgmk.conf dump.
getvar() {
    gf="$OUT/cmd/pkgmk-conf-effective.out"
    [ -s "$gf" ] || return 0
    sed -n "s/^$1=//p" "$gf" | head -n 1 | sed "s/^'//; s/'\$//"
}

# ------------------------------------------------------ system / versions --

section "System and tool versions"
run uname uname -a
grab /etc/os-release
grab /usr/lib/os-release
run crux-version crux
run prtget-version prt-get version
run prtget-noargs prt-get
run prtget-help prt-get help
run pkgmk-version pkgmk -v
run pkgadd-version pkgadd -v
run pkginfo-version pkginfo -v
run ports-version ports -v
for t in crux-version prtget-version pkgmk-version pkgadd-version ports-version; do
    note "$t: $(status "$t") $(head -n 1 "$OUT/cmd/$t.out" 2>/dev/null)"
done

section "Man pages (verify syntax against the installed version)"
for page in prt-get prt-get.conf prt-cache pkgmk pkgmk.conf pkgadd pkgrm pkginfo ports; do
    # 'man -w' tells whether the page exists; the rendering pipeline below
    # would otherwise report success for "no manual" banners.
    run "manpath-$page" man -w "$page"
    mpath=$(head -n 1 "$OUT/cmd/manpath-$page.out" 2>/dev/null)
    if succeeded "manpath-$page" && [ -f "$mpath" ]; then
        # shellcheck disable=SC2016  # $1 is expanded by the inner sh, on purpose
        run "man-$page" sh -c 'MANWIDTH=100 man "$1" | { if command -v col >/dev/null 2>&1; then col -b; else cat; fi; }' sh "$page"
        note "man $page: captured ($mpath)"
    else
        note "man $page: NOT available ($(status "manpath-$page"))"
    fi
done

# ------------------------------------------------------------ config files --

section "Configuration files"
for f in /etc/prt-get.conf /etc/pkgmk.conf /etc/pkgadd.conf \
         /etc/prt-get.aliases /var/lib/pkg/prt-get.aliases \
         /var/lib/pkg/prt-get.locker /var/lib/pkg/prt-get.cache; do
    grab "$f"
done
for f in /etc/ports/*; do
    [ -f "$f" ] && grab "$f"
done
run ls-etc-ports ls -lan /etc/ports
run ls-var-lib-pkg ls -lan /var/lib/pkg

PRTCONF=/etc/prt-get.conf
if [ -r "$PRTCONF" ]; then
    note "prt-get.conf directives:"
    grep -v '^[[:space:]]*#' "$PRTCONF" | sed '/^[[:space:]]*$/d' |
        sed 's/^/  /' | tee -a "$SUMMARY"
fi

makecmd=""
[ -r "$PRTCONF" ] &&
    makecmd=$(awk '$1=="makecommand"{$1=""; sub(/^ /,""); print}' "$PRTCONF")

# A makecommand may point pkgmk at another config file.
PKGMKCONF=/etc/pkgmk.conf
alt=$(printf '%s\n' "$makecmd" |
    awk '{for (i = 1; i < NF; i++) if ($i == "-cf" || $i == "--config-file") { print $(i+1); exit }}')
[ -n "$alt" ] && PKGMKCONF=$alt
note "makecommand: ${makecmd:-<not set>}"
note "effective pkgmk config (deduced): $PKGMKCONF"
[ "$PKGMKCONF" != /etc/pkgmk.conf ] && grab "$PKGMKCONF"

section "Effective pkgmk configuration"
if [ -r "$PKGMKCONF" ]; then
    own=$(stat -c '%u' "$PKGMKCONF")
    mode=$(stat -c '%a' "$PKGMKCONF")
    if [ "$own" = 0 ] && [ $(( 0$mode & 022 )) -eq 0 ]; then
        # shellcheck disable=SC2016  # $1 is expanded by the inner sh, on purpose
        run pkgmk-conf-effective env -i PATH=/usr/bin:/bin HOME=/nonexistent \
            sh -c '. "$1" >/dev/null 2>&1; set' sh "$PKGMKCONF"
        note "sourced $PKGMKCONF in a minimal environment (see cmd/pkgmk-conf-effective.out)"
    else
        note "SKIPPED sourcing $PKGMKCONF: uid=$own mode=$mode (want root-owned, not group/world-writable)"
    fi
else
    note "$PKGMKCONF not readable"
fi

PKGMK_BIN=$(command -v pkgmk 2>/dev/null || true)
if [ -n "$PKGMK_BIN" ]; then
    run pkgmk-defaults grep -n -E '^[[:space:]]*(export[[:space:]]+)?PKGMK_[A-Z_]+=' "$PKGMK_BIN"
fi

WORK=$(getvar PKGMK_WORK_DIR)
SRC=$(getvar PKGMK_SOURCE_DIR)
PKGD=$(getvar PKGMK_PACKAGE_DIR)
BPATH=$(getvar PATH)
for v in PKGMK_WORK_DIR PKGMK_SOURCE_DIR PKGMK_PACKAGE_DIR PKGMK_COMPRESSION_MODE \
         CFLAGS CXXFLAGS MAKEFLAGS PATH CCACHE_DIR DISTCC_HOSTS; do
    note "  $v=$(getvar "$v")"
done
note "NOTE: values are as seen when sourcing; pkgmk may expand per-port names later."

section "Footprint and integrity overrides"
run pkgmk-help pkgmk -h
if succeeded pkgmk-conf-effective; then
    if grep -q '^PKGMK_IGNORE_' "$OUT/cmd/pkgmk-conf-effective.out"; then
        grep '^PKGMK_IGNORE_' "$OUT/cmd/pkgmk-conf-effective.out" |
            sed 's/^/  pkgmk.conf sets: /' | tee -a "$SUMMARY"
    else
        note "  pkgmk.conf sets no PKGMK_IGNORE_* variable"
    fi
else
    note "  pkgmk.conf PKGMK_IGNORE_*: UNKNOWN ($(status pkgmk-conf-effective))"
fi
if exited_ok pkgmk-defaults; then
    grep 'PKGMK_IGNORE_' "$OUT/cmd/pkgmk-defaults.out" |
        sed 's/^/  pkgmk default: /' | tee -a "$SUMMARY"
fi

# advertises LABEL OPTION NAME... - does the usage text of the recorded
# commands NAME... list OPTION? A command counts only if it actually ran
# (rc != 127); usage output often exits non-zero, so rc 0 is not required.
advertises() {
    al=$1
    ao=$2
    shift 2
    found=0
    seen=0
    for an in "$@"; do
        arc=$(cat "$OUT/cmd/$an.rc" 2>/dev/null || echo missing)
        case $arc in missing|127) continue ;; esac
        for af in "$OUT/cmd/$an.out" "$OUT/cmd/$an.err"; do
            [ -s "$af" ] || continue
            seen=1
            grep -q -E -e "(^|[][[:space:],|])$ao([][[:space:],=|]|\$)" "$af" && found=1
        done
    done
    if [ "$seen" = 0 ]; then
        note "  $al $ao: UNKNOWN (no usage text captured)"
    elif [ "$found" = 1 ]; then
        note "  $al $ao: advertised"
    else
        note "  $al $ao: not advertised"
    fi
}
for o in -if -is -im -uf --margs --test --pre-install --post-install; do
    advertises prt-get "$o" prtget-noargs prtget-help man-prt-get
done
for o in -if -is -im -uf -um -us --ignore-footprint; do
    advertises pkgmk "$o" pkgmk-help man-pkgmk
done

# ----------------------------------------------------------- ports queries --

section "prt-get printf: delimiters and escapes"
run printf-escaped prt-get printf '%n\t%v\t%r\t%i\t%p\t%d\t%R\t%e\n'
run printf-literaltab prt-get printf "%n${TAB}%v${TAB}%r${TAB}%i${TAB}%p${TAB}%d${TAB}%R${TAB}%e${NL}"

note "printf-escaped: $(status printf-escaped); printf-literaltab: $(status printf-literaltab)"
if succeeded printf-escaped; then
    if grep -q "$TAB" "$OUT/cmd/printf-escaped.out"; then
        note "backslash escapes in the format ARE interpreted"
    else
        note "backslash escapes in the format are NOT interpreted"
    fi
else
    note "escape interpretation: UNKNOWN (command failed or produced nothing)"
fi
if succeeded printf-literaltab; then
    blank=$(grep -c '^$' "$OUT/cmd/printf-literaltab.out" || true)
    note "blank lines with explicit newline in format: ${blank:-0} (non-zero suggests prt-get appends its own newline)"
fi

LT="$OUT/cmd/printf-literaltab.out"
if succeeded printf-literaltab; then
    total=$(wc -l < "$LT")
    bad=$(awk -F'\t' 'NF != 8' "$LT" | wc -l)
    note "literal-tab output: $total lines, $bad with field count != 8"
    awk -F'\t' 'NF != 8 { print NR ": NF=" NF ": " $0 }' "$LT" | head -n 50 \
        > "$OUT/printf-bad-lines.txt"
    note "%i value distribution:"
    cut -f4 "$LT" | sort | uniq -c | sed 's/^/  /' | tee -a "$SUMMARY"
    note "empty field counts (desc %d / deps %e): $(awk -F'\t' '$6==""' "$LT" | wc -l) / $(awk -F'\t' '$8==""' "$LT" | wc -l)"
    note "sample %e value: $(awk -F'\t' '$8 != "" { print $8; exit }' "$LT")"
fi

section "prt-get printf: individual tokens"
for t in n p v r d e u P M R E O l i; do
    case $t in
        [A-Z]) fn="printf-token-upper-$t" ;;
        *) fn="printf-token-$t" ;;
    esac
    run "$fn" prt-get printf "%$t${NL}"
    first=$(head -n 1 "$OUT/cmd/$fn.out" 2>/dev/null)
    vals=$(sort -u "$OUT/cmd/$fn.out" 2>/dev/null | head -n 4 | cut -c1-60 | tr '\n' '|')
    if ! succeeded "$fn"; then
        st="FAILED/EMPTY ($(status "$fn"))"
    else
        case $first in
            "%$t") st="LITERAL (unsupported?)" ;;
            *) st="ok" ;;
        esac
    fi
    note "%$t: $st  sample: $vals"
done

section "Lists, diff and locks"
run prtget-list prt-get list
run prtget-listinst prt-get listinst
run prtget-listlocked prt-get listlocked
run prtget-listorphans prt-get listorphans
run prtget-dup prt-get dup
run prtget-diff prt-get diff
run prtget-diff-C env LC_ALL=C prt-get diff
run prtget-quickdiff prt-get quickdiff
run pkginfo-i pkginfo -i
run pkginfo-i-C env LC_ALL=C pkginfo -i

if succeeded printf-literaltab && exited_ok prtget-quickdiff; then
    awk -F'\t' '$4 == "diff" { print $1 }' "$LT" | sort > "$OUT/diff-from-printf.txt"
    tr ' ' '\n' < "$OUT/cmd/prtget-quickdiff.out" | sed '/^$/d' | sort \
        > "$OUT/diff-from-quickdiff.txt"
    mism=$(comm -3 "$OUT/diff-from-printf.txt" "$OUT/diff-from-quickdiff.txt" | wc -l)
    note "outdated: printf %i=diff vs quickdiff mismatches: $mism (see diff-from-*.txt)"
    cut -f1 "$LT" | sort | uniq -d > "$OUT/duplicate-names-from-printf.txt"
else
    note "outdated cross-check SKIPPED (printf: $(status printf-literaltab); quickdiff: $(status prtget-quickdiff))"
fi
if succeeded printf-literaltab; then
    cut -f1 "$LT" | sort | uniq -d > "$OUT/duplicate-names-from-printf.txt"
    note "duplicate port names in printf output: $(wc -l < "$OUT/duplicate-names-from-printf.txt")"
fi
note "listlocked: $(status prtget-listlocked), $(wc -l < "$OUT/cmd/prtget-listlocked.out") lines"

section "Cache"
run ls-cache ls -ln /var/lib/pkg/prt-get.cache
run printf-nocache prt-get printf '%n\t%v\t%i\n'
run printf-cache-flag prt-get --cache printf '%n\t%v\t%i\n'
run printf-prt-cache prt-cache printf '%n\t%v\t%i\n'
for c in printf-nocache printf-cache-flag printf-prt-cache; do
    note "$c: $(status "$c") $(cat "$OUT/cmd/$c.ms")ms first line: $(head -n 1 "$OUT/cmd/$c.out" 2>/dev/null)"
done

# ------------------------------------------------------------ sample ports --

if [ -z "$SAMPLE_INST" ]; then
    if grep -q '^glibc ' "$OUT/cmd/pkginfo-i.out" 2>/dev/null; then
        SAMPLE_INST=glibc
    else
        SAMPLE_INST=$(head -n 1 "$OUT/cmd/pkginfo-i.out" 2>/dev/null | cut -d' ' -f1)
    fi
fi
if [ -z "$SAMPLE_AVAIL" ] && succeeded printf-literaltab; then
    SAMPLE_AVAIL=$(awk -F'\t' '$4 == "no" && $8 != "" { print $1; exit }' "$LT")
    [ -z "$SAMPLE_AVAIL" ] &&
        SAMPLE_AVAIL=$(awk -F'\t' '$4 == "no" { print $1; exit }' "$LT")
fi

probe_port() {
    pp=$1
    tag=$2
    run "$tag-info" prt-get info "$pp"
    run "$tag-path" prt-get path "$pp"
    run "$tag-isinst" prt-get isinst "$pp"
    run "$tag-current" prt-get current "$pp"
    run "$tag-quickdep" prt-get quickdep "$pp"
    run "$tag-depends" prt-get depends "$pp"
    run "$tag-deptree" prt-get deptree "$pp"
    run "$tag-dependent" prt-get dependent "$pp"
    run "$tag-dependent-all" prt-get dependent --all "$pp"
    note "$tag ($pp): dependent $(status "$tag-dependent") $(wc -l < "$OUT/cmd/$tag-dependent.out") lines; dependent --all $(status "$tag-dependent-all") $(wc -l < "$OUT/cmd/$tag-dependent-all.out") lines"
    pdir=$(head -n 1 "$OUT/cmd/$tag-path.out" 2>/dev/null)
    if [ -n "$pdir" ] && [ -d "$pdir" ]; then
        run "$tag-portdir-ls" ls -lan "$pdir"
        for pf in Pkgfile README README.md .footprint .signature .md5sum pre-install post-install; do
            [ -f "$pdir/$pf" ] && grab "$pdir/$pf"
        done
    fi
}

section "Sample ports"
if [ -n "$SAMPLE_INST" ]; then
    note "installed sample: $SAMPLE_INST"
    probe_port "$SAMPLE_INST" inst
    run inst-pkginfo-l pkginfo -l "$SAMPLE_INST"
    run pkginfo-o-relative pkginfo -o usr/bin/prt-get
    run pkginfo-o-absolute pkginfo -o /usr/bin/prt-get
else
    note "no installed sample found"
fi
if [ -n "$SAMPLE_AVAIL" ]; then
    note "available sample: $SAMPLE_AVAIL"
    probe_port "$SAMPLE_AVAIL" avail
else
    note "no available (not installed) sample found"
fi

section "Error behaviour with a nonexistent port"
NX=octocrux-nonexistent-port-zz
for c in info path isinst current depends quickdep dependent; do
    run "nx-$c" prt-get "$c" "$NX"
    note "nx $c: rc=$(cat "$OUT/cmd/nx-$c.rc") out=$(head -n 1 "$OUT/cmd/nx-$c.out") err=$(head -n 1 "$OUT/cmd/nx-$c.err")"
done

# -------------------------------------------------------- build environment --

section "Build directories and mounts (real mounts, not fstab)"
grab /etc/fstab
cp /proc/self/mountinfo "$OUT/files/proc-self-mountinfo" 2>/dev/null
SAMPLE_DIR=$(head -n 1 "$OUT/cmd/inst-path.out" 2>/dev/null)

check_dir() {
    cl=$1
    cv=$2
    if [ -z "$cv" ]; then
        note "$cl dir: not set in pkgmk.conf (pkgmk default applies, see cmd/pkgmk-defaults.out)"
        [ -n "$SAMPLE_DIR" ] && mountinfo "$cl (sample port dir)" "$SAMPLE_DIR"
        return 0
    fi
    case $cv in
        /*) ;;
        *) note "$cl dir is relative: $cv"; return 0 ;;
    esac
    cp_=$(existing_parent "$cv")
    [ "$cp_" != "$cv" ] && note "$cl dir $cv does not exist now; checking $cp_"
    mountinfo "$cl" "$cp_"
}
check_dir work "$WORK"
check_dir source "$SRC"
check_dir package "$PKGD"
mountinfo "ports tree" /usr/ports

# Package file naming matters for a direct 'pkgadd -u' recovery path.
if [ -n "$PKGD" ] && [ -d "$PKGD" ]; then
    run package-dir-sample find "$PKGD" -maxdepth 1 -name '*.pkg.tar*'
    note "built packages in $PKGD: $(wc -l < "$OUT/cmd/package-dir-sample.out") ($(status package-dir-sample))"
elif [ -n "$SAMPLE_DIR" ] && [ -d "$SAMPLE_DIR" ]; then
    run package-dir-sample find "$SAMPLE_DIR" -maxdepth 1 -name '*.pkg.tar*'
    note "built packages in sample port dir: $(wc -l < "$OUT/cmd/package-dir-sample.out")"
fi

section "ccache / distcc"
run ccache-version ccache --version
run ccache-config ccache -p
run ccache-stats ccache -s
run ls-ccache-wrappers ls -lan /usr/lib/ccache
case ":$BPATH:" in
    *ccache*) note "pkgmk.conf PATH includes ccache: $BPATH" ;;
    *) note "pkgmk.conf PATH does not mention ccache" ;;
esac
case ":$BPATH:" in
    *distcc*) note "pkgmk.conf PATH includes distcc" ;;
esac
CCDIR=$(getvar CCACHE_DIR)
src_cc="pkgmk.conf"
if [ -z "$CCDIR" ] && command -v ccache >/dev/null 2>&1; then
    CCDIR=$(ccache -k cache_dir 2>/dev/null)
    src_cc="ccache config of the CURRENT user (root's may differ)"
fi
if [ -n "$CCDIR" ]; then
    note "ccache dir: $CCDIR (from $src_cc)"
    mountinfo ccache "$(existing_parent "$CCDIR")"
    if [ -d "$CCDIR" ]; then
        run ls-ccache-dir ls -ldn "$CCDIR"
        ccown=$(stat -c '%u' "$CCDIR")
        run ccache-foreign-owners find "$CCDIR" -maxdepth 2 ! -user "$ccown"
        note "entries not owned by the ccache dir owner (uid $ccown): $(wc -l < "$OUT/cmd/ccache-foreign-owners.out") (see cmd/ccache-foreign-owners.*)"
    fi
else
    note "no ccache dir determined"
fi

# --------------------------------------------------------------- privileges --

section "Privilege helpers"
for h in sudo doas su qt-sudo lxqt-sudo kdesu pkexec; do
    hp=$(command -v "$h" 2>/dev/null || true)
    note "$h: ${hp:-not found}"
done
run sudo-version sudo -V
run sudo-list-noninteractive sudo -n -l
run ls-doas-conf ls -ln /etc/doas.conf
grab /etc/doas.conf
run qt-sudo-help qt-sudo --help
grep -E '^(qt-sudo|sudo|opendoas|doas) ' "$OUT/cmd/pkginfo-i.out" 2>/dev/null |
    sed 's/^/  installed: /' | tee -a "$SUMMARY"

# ------------------------------------------------------- Qt / build tooling --

section "Qt and build tooling"
run pkgconfig-qt6core pkg-config --modversion Qt6Core
run pkgconfig-qt6widgets pkg-config --modversion Qt6Widgets
run pkgconfig-qtermwidget6 pkg-config --modversion qtermwidget6
run cmake-version cmake --version
run qmake6-version qmake6 -v
for c in pkgconfig-qt6core pkgconfig-qtermwidget6 cmake-version; do
    note "$c: $(status "$c") $(head -n 1 "$OUT/cmd/$c.out")"
done

# -------------------------------------------------------------- environment --

section "Session environment (selected variables)"
for v in PATH HOME SHELL LANG LC_ALL TERM DISPLAY WAYLAND_DISPLAY \
         XDG_SESSION_TYPE XDG_RUNTIME_DIR CCACHE_DIR; do
    eval "val=\${$v-<unset>}"
    # shellcheck disable=SC2154  # val is assigned by eval above
    printf '%s=%s\n' "$v" "$val"
done > "$OUT/env.txt"
note "see env.txt"

# ------------------------------------------------------------- dry run (opt) --

if [ "$WITH_DRYRUN" = 1 ]; then
    section "Dry run (opt-in)"
    if grep -q -e '--test' "$OUT/cmd/prtget-noargs.out" "$OUT/cmd/prtget-noargs.err" \
            "$OUT/cmd/prtget-help.out" "$OUT/cmd/prtget-help.err" 2>/dev/null &&
       [ -n "$SAMPLE_AVAIL" ]; then
        run dryrun-depinst prt-get --test depinst "$SAMPLE_AVAIL"
        note "dryrun depinst $SAMPLE_AVAIL: rc=$(cat "$OUT/cmd/dryrun-depinst.rc")"
    else
        note "dry run SKIPPED: --test not advertised by prt-get, or no available sample"
    fi
fi

# ------------------------------------------------------------------ wrap up --

{
    printf '\n## Exit codes and durations\n'
    for f in "$OUT"/cmd/*.rc; do
        n=$(basename "$f" .rc)
        printf '%-34s rc=%-4s %sms\n' "$n" "$(cat "$f")" "$(cat "$OUT/cmd/$n.ms" 2>/dev/null)"
    done
} >> "$SUMMARY"

tar -czf "$OUT.tar.gz" -C "$(dirname "$OUT")" "$(basename "$OUT")"
printf '\nDone.\n  directory: %s\n  archive:   %s.tar.gz\n' "$OUT" "$OUT"
printf 'Review SUMMARY.txt and redact anything private before sharing.\n'
