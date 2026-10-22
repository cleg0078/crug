# Debian package templates

`scripts/build-deb.sh` renders these templates into the `DEBIAN/` control
directory of the binary package.  Nothing here is installed as-is; the
package payload is the same install tree `scripts/package.sh` tars
(`build/package/crug-<version>/opt/crug`).

| File | Package location | Notes |
| --- | --- | --- |
| `control.in` | `DEBIAN/control` | `@VERSION@`, `@ARCH@`, `@MAINTAINER@`, `@DEPENDS@` and `@INSTALLED_SIZE@` are filled in at build time |
| `copyright.in` | `/usr/share/doc/crug/copyright` | DEP-5; the licence text itself lives in `/usr/share/common-licenses/GPL-3` |
| `README.md` | not installed | this file |

`DEBIAN/md5sums` and `/usr/share/doc/crug/changelog.Debian.gz` are generated
by the script.  There is no maintainer script: the package only drops files
under `/opt/crug` and five links under `/usr`, so it needs no `postinst`,
`prerm` or `postrm`.

The installed layout keeps every GCC internal path under `/opt/crug`, which is
why the package cannot collide with the system `gcc`.  The system entry points
are:

```
/usr/bin/crugc                        -> /opt/crug/bin/crugc
/usr/include/crug                     -> /opt/crug/include/crug
/usr/lib/x86_64-linux-gnu/libcrug.a   -> /opt/crug/lib/libcrug.a
/usr/share/doc/crug/copyright
/usr/share/doc/crug/changelog.Debian.gz
```

Build it with `make package-deb` (or `scripts/build-deb.sh`); verify it with
`make check-DEB` (or `scripts/check-deb.sh`).
