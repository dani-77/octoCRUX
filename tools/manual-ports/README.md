# Manual test ports

Ports for tests that need the real privilege helper (qt-sudo, sudo) and a real
`prt-get` → `pkgmk` tree, which the automated suite cannot run. They install
nothing useful. Copy one into a collection listed in `/etc/prt-get.conf` (for
example `/usr/ports/d77crux/`), run the test from OctoCRUX, then delete it.

## `octo-cancel-test`: Cancel during a silent step

`build()` stays silent for 180 s (phase 1), then prints one line per second for
180 s (phase 2). No sources are downloaded.

**Question it answers:** after a Cancel, does the root tree (`prt-get` → `pkgmk`
→ `bash` → `sleep`) stop even when nothing writes to the pipe? A Cancel during
the noisy configure of zsh stopped the tree in under a second, but left
`/usr/ports/work/zsh` behind. pkgmk's log had no `Interrupted.` line, although
pkgmk's `interrupted()` trap (SIGHUP, SIGINT, SIGQUIT, SIGTERM) removes the work
directory when `PKGMK_KEEP_WORK=no`. The hypothesis is that the processes died
of SIGPIPE when they next wrote, not of a relayed SIGTERM **[hypothesis]**. A
silent step tells the two apart.

1. Copy the directory to `/usr/ports/d77crux/octo-cancel-test/` (as root), and press Refresh in OctoCRUX.
2. Install `octo-cancel-test`. During phase 1 (the Output tab shows the
   "phase 1" line and then nothing), press **Cancel**.
3. Check:
   - `ps -ef | grep -E "prt-get|pkgmk|sleep"`: any `sleep 180` or `pkgmk` left?
   - `ls /usr/ports/work/`: is `octo-cancel-test` left behind?
   - `tail -3 /var/log/pkgbuild/octo-cancel-test.log`: is there `ERROR: Interrupted.`?
4. Repeat with Cancel during phase 2 (the "tick" lines), for comparison.
5. Delete `/usr/ports/d77crux/octo-cancel-test/` (and any leftover work directory).

Reading the result:
- Processes left after phase 1: a silent root build can outlive a Cancel.
- Everything stops, but the work directory stays and the log has no
  `Interrupted.`: pkgmk's trap died of SIGPIPE before its `rm -rf`.

**Result on the owner's T480s (2026-10-21) [verified]:** the second case, in
both phases. The tree stopped within about a second, even during `sleep 180`.
The work directory stayed. The cause was reproduced unprivileged: the trap's
first `echo` hits the pipe whose reader (prt-get) already died (OPEN_QUESTIONS
2.10.3). Since then OctoCRUX offers to remove the leftover after a Cancel;
accepting it should leave `/usr/ports/work/` without `octo-cancel-test`. **[owner-reported 2026-10-21, 0.1.1 build with qt-sudo]:** the offer removed the leftover `zsh` and `octo-cancel-test` directories; `/usr/ports/work/` was empty afterwards, with no prt-get or pkgmk left running **[verified]**.

Without `.signature` or `.md5sum`, pkgmk warns and creates new ones. If your
`pkgmk.conf` requires signatures, tick the signature exception (`-is`) in the
Information tab first.
