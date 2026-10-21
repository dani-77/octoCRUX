# CRUX port of OctoCRUX

`octocrux/Pkgfile` is the port for the `d77crux` collection. It builds a
release tarball from GitHub with CMake and installs:

| Path | What |
| :--- | :--- |
| `/usr/bin/octocrux` | the package manager |
| `/usr/bin/octocrux-notifier` | the tray notifier (start it yourself, or enable autostart, see the main README) |
| `/usr/share/applications/octocrux.desktop`, `octocrux-notifier.desktop` | menu entries |
| `/usr/share/icons/hicolor/{16x16,32x32,48x48,128x128,192x192,512x512}/apps/octocrux.png` | the logo, as menu and panel icons |
| `/usr/share/polkit-1/actions/org.octocrux.ports-sync.policy` | polkit action for exactly `/usr/bin/ports -u`; needs admin authentication by itself |

No documentation is installed (`-D OCTOCRUX_INSTALL_DOCS=OFF`, following the
`rm -r $PKG/usr/share/doc` convention of the collection), and no tests
(`-D BUILD_TESTING=OFF`). The per-user rule that waives the password for the
scheduled sync is **not** part of the package: the notifier installs it on
opt-in, under `/etc/polkit-1/rules.d/`.

## Dependencies

- `qt6-base` (Widgets and Network) and `qt-sudo` (graphical elevation).
- `prt-get`, `pkgutils`, `ports`, `cmake` and `sudo` are in `core`, and are not listed (CRUX convention).
- Optional: `polkit` and `consolekit` (or elogind), for the password-free scheduled sync through polkit. Without them, the notifier uses `sudo -n` or only reminds.

## Publishing a release (owner)

1. Tag the release on GitHub so the source URL exists: `git tag v0.1.0 && git push origin v0.1.0`.
   The archive's top directory is then `octoCRUX-0.1.0`, which the Pkgfile expects.
2. Copy `octocrux/` into `/usr/ports/d77crux/`.
3. In `/usr/ports/d77crux/octocrux`, as root: `pkgmk -d -uf` (download, build, write
   `.footprint`), then `pkgmk -us` (signature with the collection's key).
4. Install with `prt-get depinst octocrux`, or from OctoCRUX itself.

**If you turned on automatic sync with a build-tree notifier before installing
the package**, the Options dialog already installed
`/usr/share/polkit-1/actions/org.octocrux.ports-sync.policy`. That file belongs
to no package, so `pkgadd` reports a conflict. Remove it first
(`rm /usr/share/polkit-1/actions/org.octocrux.ports-sync.policy`; the package
installs the same file) or install with `pkgadd -f`. The rule in
`/etc/polkit-1/rules.d/` is not affected.

## Checked before committing

The Pkgfile was built **unprivileged** with the installed `pkgmk -cf <sandbox
conf>`, from a local tarball of the tree that has the same top directory and
the `renames` name, so there was no download and no `pkgadd`. The resulting
file list matches the table above. The `.footprint` generated that way lists
the build user's ownership, so it is not committed; generate the real one as
root (step 3).
