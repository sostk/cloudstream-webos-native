#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Licence notices for the aarch64 plugin runtime that ships in the TV package (runtime/licenses/,
copied into runtime-aarch64/licenses/ by make_dist.py).

usage: notices.py check <rootfs dir> <dist dir>      fail unless packages.tsv matches that dist
       notices.py packages <rootfs dir> <dist dir>   write runtime/licenses/packages.tsv: the Alpine
                                                     packages whose files the dist ships (from apk's
                                                     database in the root), with version, licence
                                                     and aports commit
       notices.py render                             write runtime/licenses/NOTICES.md from
                                                     packages.tsv and the texts collected by
                                                     runtime/collect-licenses.sh

The texts are taken from each package's own source (Alpine's -doc packages rarely carry them), so
copyright lines are the projects' own.

CSWEB_LICENSES_DIR (default runtime/licenses) selects the notices directory, e.g. runtime/licenses-armv7
for the legacy runtime.
"""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
RUNTIME = os.path.dirname(HERE)
# The legacy (armv7) runtime keeps its own notices: CSWEB_LICENSES_DIR=runtime/licenses-armv7 (docs/legacy-build.md).
LICENSES = os.environ.get("CSWEB_LICENSES_DIR") or os.path.join(RUNTIME, "licenses")
TSV = os.path.join(LICENSES, "packages.tsv")

# Origins whose output ships although no file of theirs is copied as-is: the trust store is
# ca-certificates' Mozilla list (converted by java-cacerts' script, which itself does not ship);
# libwolfssl is rebuilt from wolfSSL's source.
DERIVED = {"ca-certificates": "trust/cacerts (Mozilla's CA list, converted to a Java trust store)",
           "wolfssl": "usr/lib/libwolfssl.so.46 (rebuilt from the same wolfSSL release, runtime/native/build-wolfssl.sh)"}


def read_db(rootfs):
    """apk's installed database: package records and which package owns each file."""
    pkgs, owner, cur, d = {}, {}, {}, None
    for line in open(os.path.join(rootfs, "lib/apk/db/installed"), encoding="utf-8", errors="replace"):
        line = line.rstrip("\n")
        if not line:
            if "P" in cur:
                pkgs[cur["P"]] = cur
            cur = {}
            continue
        key, val = line[:1], line[2:]
        if key == "F":
            d = val
        elif key == "R":
            owner[os.path.join(d, val)] = cur["P"]
        elif key not in cur:
            cur[key] = val
    if "P" in cur:
        pkgs[cur["P"]] = cur
    return pkgs, owner


def table(rootfs, dist):
    """packages.tsv's content for the dist built from this root."""
    pkgs, owner = read_db(rootfs)
    manifest = json.load(open(os.path.join(dist, "MANIFEST.json")))
    shipped = {}
    for f in manifest["files"]:
        p = owner.get(f["path"])
        if p:
            shipped.setdefault(p, []).append(f["path"])
    by_origin = {}
    for name, p in pkgs.items():
        if name in shipped or p.get("o") in DERIVED:
            by_origin.setdefault(p["o"], []).append(p)
    lines = ["# origin\taports commit\tpackages\tversion\tlicence\tupstream\tfiles shipped"]
    for origin in sorted(by_origin):
        ps = sorted(by_origin[origin], key=lambda p: p["P"])
        files = sum(len(shipped.get(p["P"], [])) for p in ps)
        lines.append("\t".join([origin, ps[0]["c"], ",".join(p["P"] for p in ps), ps[0]["V"], ps[0].get("L", ""),
                                ps[0].get("U", ""), str(files) if files else DERIVED.get(origin, "0")]))
    return "\n".join(lines) + "\n"


def packages(rootfs, dist):
    os.makedirs(LICENSES, exist_ok=True)
    with open(TSV, "w") as out:
        out.write(table(rootfs, dist))
    print(f"{TSV}: written")


def check(rootfs, dist):
    """Fails when the committed notices do not describe what this dist ships (other packages,
    versions or licences): run packages, collect-licenses.sh and render again."""
    want = table(rootfs, dist)
    have = open(TSV).read() if os.path.exists(TSV) else ""
    if want != have:
        import difflib
        sys.stdout.writelines(difflib.unified_diff(have.splitlines(True), want.splitlines(True), "committed", "this dist"))
        sys.exit("notices.py: runtime/licenses/ does not match this dist; regenerate it (see the module doc)")
    print("notices.py: runtime/licenses/ matches this dist")


def render():
    rows = [line.rstrip("\n").split("\t") for line in open(TSV) if not line.startswith("#")]
    texts = os.path.join(LICENSES, "texts")
    out = ["# Plugin runtime: third-party notices", "",
           "The plugin runtime (`runtime-<arch>/` in the package) is a relocatable copy of Alpine Linux's",
           "`art_standalone` (Android's ART, run outside Android) with the libraries it needs, plus parts",
           "of CloudStream and this project's own code. Each part keeps its own licence; none is modified",
           "except where stated. The app itself and this project's code are GPL-3.0-or-later (`../LICENSE`).",
           "",
           "## Alpine Linux packages", "",
           "Copied unchanged from the signed Alpine edge packages pinned in `runtime/inputs.lock`. The",
           "licence column is Alpine's (SPDX). Source: the package recipe at the aports commit (patches",
           "included) and the upstream source it names; licence texts from that source are in `texts/<origin>/`.",
           "",
           "| Origin | Version | Licence | Shipped | Recipe (aports) | Upstream |",
           "|---|---|---|---|---|---|"]
    for origin, commit, _pkgs, version, licence, upstream, files in rows:
        recipe = f"https://gitlab.alpinelinux.org/alpine/aports/-/commit/{commit}"
        have = os.path.isdir(os.path.join(texts, origin)) and os.listdir(os.path.join(texts, origin))
        shipped = (f"{files} file" + ("s" if files != "1" else "")) if files.isdigit() else files
        out.append(f"| {origin}{'' if have else ' (no licence file in source)'} | {version} | {licence} | {shipped} "
                   f"| [{commit[:10]}]({recipe}) | {upstream} |")
    out += ["",
            "`art_standalone`'s Java class library (`usr/lib/java/dex/art/core-oj-hostdex.jar`) derives from",
            "OpenJDK and is GPL-2.0-only WITH Classpath-exception-2.0 (Android's libcore), whatever the package's",
            "Apache-2.0 label says; its notices are among `texts/art_standalone/`.",
            "",
            "## Built by this project", "",
            "| Part | Source | Licence |",
            "|---|---|---|",
            "| `usr/lib/libwolfssl.so.46` | wolfSSL 5.9.4 release tarball (SHA-512 in `runtime/native/build-wolfssl.sh`), "
            "built with `-DWOLFSSL_ASN_ALLOW_0_SERIAL` and otherwise Alpine's options | GPL-3.0-only (`texts/wolfssl/`) |",
            "| `usr/lib/libEGL.so.1`, `bin/dex2oat` | `runtime/native/` of this project | GPL-3.0-or-later |",
            "| `csweb/csweb-host.dex` | `runtime/android-compat/`, `runtime/csloader/` of this project | GPL-3.0-or-later |",
            "| `boot/`, `csweb/oat/` | generated by ART's own dex2oat from the files above (`runtime/make-aot.sh`) | as their inputs |",
            "",
            "## CloudStream", "",
            "`csweb/cloudstream.jar` holds the class files (DEX) and Java resources of CloudStream's",
            "pre-release APK unchanged (SHA-256 in `runtime/inputs.lock`), without Android-only parts, except one",
            "asset OkHttp reads through Android's AssetManager: `assets/PublicSuffixDatabase.list`, the Public Suffix",
            "List (https://publicsuffix.org, MPL-2.0: https://mozilla.org/MPL/2.0/).",
            "CloudStream is GPL-3.0: https://github.com/recloudstream/cloudstream (source of the pinned build:",
            "commit eb962b4fb7b1387b8de42ec58000cee0a01fe741, with CloudStream's own LICENSE).",
            "",
            "The APK also contains the libraries CloudStream bundles (Kotlin, kotlinx, AndroidX, OkHttp,",
            "Jackson, jsoup, NewPipe Extractor, Rhino and others), each under its own licence. The APK carries",
            "licence files only for AndroidX and jsoup (kept in the jar under `META-INF/`); the full list of",
            "libraries and versions is in CloudStream's build files at that commit (`gradle/libs.versions.toml`,",
            "`app/build.gradle.kts`, `library/build.gradle.kts`).",
            ""]
    with open(os.path.join(LICENSES, "NOTICES.md"), "w") as f:
        f.write("\n".join(out))
    print(f"{os.path.join(LICENSES, 'NOTICES.md')}: {len(rows)} origins")


if __name__ == "__main__":
    if len(sys.argv) == 4 and sys.argv[1] in ("packages", "check"):
        (packages if sys.argv[1] == "packages" else check)(sys.argv[2], sys.argv[3])
    elif len(sys.argv) == 2 and sys.argv[1] == "render":
        render()
    else:
        sys.exit(__doc__)
