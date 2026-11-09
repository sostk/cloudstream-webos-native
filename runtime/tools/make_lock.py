#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Pin an architecture's Alpine packages: write runtime/inputs-<arch>.lock from rootfs-<arch>.manifest.

usage: make_lock.py <arch> <mirror URL> [<inputs dir>]
   e.g. make_lock.py armv7 https://github.com/sostk/cloudstream-webos-native/releases/download/runtime-inputs-armv7-1

For each package in runtime/rootfs-<arch>.manifest (written by an unpinned make-rootfs.sh run, which is
what tools/build-runtime.sh --arch <arch> does until this lock exists), the .apk is downloaded from Alpine
edge (main, community, then testing), hashed, and kept in <inputs dir>/alpine/<arch>/ (default
~/.cache/csweb-runtime-inputs). The lock has the same format as runtime/inputs.lock and is read by
tools/fetch-runtime-inputs.sh. Alpine edge drops old builds, so run this soon after the build it pins,
then upload the files to the mirror release named on the command line (docs/legacy-build.md).

A package that is not on Alpine edge but in the locally rebuilt ART directory ($CSWEB_ARMV7_ART_DIR,
default runtime/native/out/art-armv7: armv7's art_standalone r3) is taken from there, together with
the public key it was signed with (alpine/<arch>/keys/, which make-rootfs.sh trusts for pinned
builds). Those files exist only in the mirror, so their URL in the lock is the mirror's.

Only used for the legacy (armv7) runtime; runtime/inputs.lock (aarch64) is not touched.
"""
import hashlib
import os
import sys
import urllib.error
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
RUNTIME = os.path.dirname(HERE)
CDN = "https://dl-cdn.alpinelinux.org/alpine/edge"
REPOS = ("main", "community", "testing")


def fetch(url):
    try:
        with urllib.request.urlopen(url, timeout=120) as r:
            return r.read()
    except urllib.error.HTTPError as e:
        if e.code == 404:
            return None
        raise


def local_art(arch):
    """The rebuilt ART directory (build-art-armv7.sh output, or CI's art-armv7 artifact), or None."""
    if arch != "armv7":
        return None
    d = os.environ.get("CSWEB_ARMV7_ART_DIR") or os.path.join(RUNTIME, "native", "out", "art-armv7")
    return d if os.path.isdir(d) else None


def main(arch, mirror, inputs):
    if arch == "aarch64":
        sys.exit("make_lock.py: aarch64 is pinned in runtime/inputs.lock; this tool is for other arches")
    manifest = os.path.join(RUNTIME, f"rootfs-{arch}.manifest")
    if not os.path.exists(manifest):
        sys.exit(f"make_lock.py: no {manifest}: run tools/build-runtime.sh --arch {arch} first")
    lines = [
        "# SPDX-License-Identifier: GPL-3.0-or-later",
        f"# Alpine edge packages of the {arch} plugin runtime (legacy package, docs/legacy-build.md).",
        "# Written by runtime/tools/make_lock.py from runtime/rootfs-%s.manifest; read by tools/fetch-runtime-inputs.sh." % arch,
        "#   <sha256>  <path in the inputs mirror>  <upstream URL>",
        "# Files not on Alpine edge (the rebuilt ART and its signing key) have the mirror as their URL.",
        f"# mirror: {mirror}",
    ]
    header = len(lines)
    art = local_art(arch)

    def keep(path, data, url):
        out = os.path.join(inputs, path)
        os.makedirs(os.path.dirname(out), exist_ok=True)
        with open(out, "wb") as f:
            f.write(data)
        lines.append(f"{hashlib.sha256(data).hexdigest()}  {path}  {url}")

    rebuilt = False
    for pkg in open(manifest).read().split():
        name = f"{pkg}.apk"
        path = f"alpine/{arch}/{name}"
        local = os.path.join(art, name) if art else None
        if local and os.path.isfile(local):
            # Rebuilt locally (edge's own build of this version, if any, is a different file).
            keep(path, open(local, "rb").read(), f"{mirror}/{path.replace('/', '_')}")
            rebuilt = True
            print(f"{name}: rebuilt ({art})")
            continue
        for repo in REPOS:
            url = f"{CDN}/{repo}/{arch}/{name}"
            data = fetch(url)
            if data is not None:
                break
        else:
            sys.exit(f"make_lock.py: {name} is no longer on Alpine edge; rebuild unpinned and run again")
        keep(path, data, url)
        print(f"{name}: {repo}")
    if rebuilt:
        keys = sorted(k for k in os.listdir(os.path.join(art, "keys")) if k.endswith(".pub"))
        if not keys:
            sys.exit(f"make_lock.py: no public key in {art}/keys for the rebuilt packages")
        for k in keys:
            path = f"alpine/{arch}/keys/{k}"
            keep(path, open(os.path.join(art, "keys", k), "rb").read(), f"{mirror}/{path.replace('/', '_')}")
            print(f"{k}: key of the rebuilt packages")
    lock = os.path.join(RUNTIME, f"inputs-{arch}.lock")
    with open(lock, "w") as f:
        f.write("\n".join(lines) + "\n")
    print(f"{lock}: {len(lines) - header} files")


if __name__ == "__main__":
    if len(sys.argv) not in (3, 4) or not sys.argv[2].startswith("https://"):
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2].rstrip("/"),
         sys.argv[3] if len(sys.argv) == 4 else os.path.expanduser("~/.cache/csweb-runtime-inputs"))
