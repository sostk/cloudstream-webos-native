#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Assemble a relocatable plugin runtime from an Alpine root (runtime/make-rootfs.sh).

usage: make_dist.py <rootfs dir> <arch: aarch64|x86_64|armv7> <out dir, by convention runtime/dist/runtime-<arch>>
       make_dist.py --manifest <out dir>     rewrite MANIFEST.json after runtime/make-aot.sh

Layout of <out dir> (every file a regular file, no symlinks, so it can travel in an ipk):
  lib/ld-musl-<arch>.so.1            musl dynamic loader (the runtime is started through it; armv7: ld-musl-armhf.so.1)
  usr/bin/dalvikvm, usr/bin/dex2oat  ART executables (unmodified)
  usr/lib/..., usr/lib/art/...       DT_NEEDED closure of the above and of the JNI libraries,
                                     libEGL.so.1 replaced by runtime/native's stub, libwolfssl.so.46
                                     by runtime/native/build-wolfssl.sh's build (serial-0 roots parse)
  usr/lib/java/dex/art/*.jar, natives/   boot class path and its JNI libraries (unmodified)
  usr/share/icu/<ver>/icudt*.dat     ICU data (ICU_DATA)
  bin/dex2oat                        runtime/native relaunch helper (ART finds dex2oat on $PATH)
  lib/low4g.so                       aarch64: runtime/native/low4g.c, preloaded (ART low-4GB allocator fix)
  trust/cacerts                      Java trust store from Alpine's ca-certificates (Mozilla)
  trust/ca-bundle.pem                armv7 only: the same list as PEM, for the legacy package's libcurl
  csweb/csweb-host.dex               Android compat layer + csloader (runtime/build.sh)
  csweb/cloudstream.jar              CloudStream's pre-release APK minus Android-only parts (res/, lib/,
                                     assets/, resources.arsc, manifest, signatures): classes*.dex and the
                                     Java resources (META-INF/services, *.kotlin_builtins...), byte for byte,
                                     stored 4-byte aligned so ART maps the DEX instead of copying it
  licenses/                          aarch64 (runtime/licenses/) and armv7 (runtime/licenses-armv7/, when generated):
                                     NOTICES.md, packages.tsv, texts/ (runtime/tools/notices.py)
  MANIFEST.json                      every file with size and SHA-256, plus the Alpine package list
Added by runtime/make-aot.sh (optional): boot/<isa>/ boot image, csweb/oat/<isa>/ odex files, AOT.json.
"""
import hashlib
import json
import os
import shutil
import subprocess
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
PROJECT = os.path.dirname(HERE)
SEARCH = ["lib", "usr/lib", "usr/lib/art", "usr/lib/java/dex/art/natives"]
# musl names its loader after the ABI, not the arch: Alpine's armv7 is "armhf".
LOADER = {"aarch64": "ld-musl-aarch64.so.1", "x86_64": "ld-musl-x86_64.so.1", "armv7": "ld-musl-armhf.so.1"}
BOOT_JARS = ["core-oj", "core-libart", "okhttp", "bouncycastle", "apache-xml", "apachehttp", "wolfssljni",
             "core-junit", "hamcrest", "junit-runner"]  # parsed_options.cc default boot class path


def needed(path):
    out = subprocess.run(["readelf", "-dW", path], capture_output=True, text=True, check=True).stdout
    return [line.split("[", 1)[1].split("]", 1)[0] for line in out.splitlines() if "(NEEDED)" in line]


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


ANDROID_ONLY = ("res/", "lib/", "assets/", "resources.arsc", "AndroidManifest.xml")
# Android-only entries the runtime still needs: OkHttp's Android build reads the Public Suffix List as an
# asset (AssetManager, through the application context OkHttp.initialize gets), for DnsOverHttps and
# cookies; the runtime's application context reads assets from this jar (HeadlessContext.getAssets).
KEEP_ASSETS = ("assets/PublicSuffixDatabase.list",)


def is_signature(name):
    return name.startswith("META-INF/") and (name.endswith((".SF", ".RSA", ".EC", ".DSA")) or name == "META-INF/MANIFEST.MF")


def repack_classpath_jar(apk, jar):
    """Copy every class-loader-visible entry of the APK unchanged, stored and 4-byte aligned."""
    with zipfile.ZipFile(apk) as src, zipfile.ZipFile(jar, "w", zipfile.ZIP_STORED) as dst:
        for info in src.infolist():
            name = info.filename
            if name.endswith("/") or is_signature(name) or (name.startswith(ANDROID_ONLY) and name not in KEEP_ASSETS):
                continue
            data = src.read(info)
            zi = zipfile.ZipInfo(name, (1980, 1, 1, 0, 0, 0))
            header = dst.fp.tell() + 30 + len(name.encode())
            zi.extra = b"\0" * ((-header) % 4)  # pad the local header so the data starts 4-aligned
            dst.writestr(zi, data, compress_type=zipfile.ZIP_STORED)
        for zi in dst.infolist():
            if zi.filename.endswith(".dex"):
                assert (zi.header_offset + 30 + len(zi.filename.encode()) + len(zi.extra)) % 4 == 0, zi.filename


def main(rootfs, arch, out):
    if arch not in LOADER:
        sys.exit(f"unsupported arch {arch}")
    loader = LOADER[arch]
    native = os.path.join(HERE, "native", "out")
    overrides = {"libEGL.so.1": os.path.join(native, f"libEGL.so.1.{arch}"),
                 "libwolfssl.so.46": os.path.join(native, f"libwolfssl.so.46.{arch}")}
    for name, path in overrides.items():
        if not os.path.exists(path):
            sys.exit(f"missing {path}: run runtime/native/build-native.sh and build-wolfssl.sh first")
    if os.path.exists(out):
        shutil.rmtree(out)
    os.makedirs(out)

    def put(src, rel):
        dst = os.path.join(out, rel)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copyfile(os.path.realpath(src), dst)  # dereference: ipks carry no symlinks
        shutil.copymode(os.path.realpath(src), dst)
        return dst

    roots = [f"lib/{loader}", "usr/bin/dalvikvm", "usr/bin/dex2oat"]
    roots += [f"usr/lib/art/{n}" for n in sorted(os.listdir(os.path.join(rootfs, "usr/lib/art"))) if n.endswith(".so")]
    nat = "usr/lib/java/dex/art/natives"
    roots += [f"{nat}/{n}" for n in sorted(os.listdir(os.path.join(rootfs, nat))) if n.endswith(".so")]

    placed, stack, unresolved = {}, list(roots), []
    while stack:
        rel = stack.pop()
        if rel in placed:
            continue
        name = os.path.basename(rel)
        src = overrides.get(name, os.path.join(rootfs, rel))
        placed[rel] = put(src, rel)
        for so in needed(src):
            if so.startswith("libc.musl"):  # musl's libc is the loader itself
                so_rel = f"lib/{so}"
                if so_rel not in placed:
                    placed[so_rel] = put(os.path.join(rootfs, f"lib/{loader}"), so_rel)
                continue
            hit = next((f"{d}/{so}" for d in SEARCH if os.path.exists(os.path.join(rootfs, d, so)) or so in overrides), None)
            if hit is None:
                unresolved.append((rel, so))
            elif hit not in placed:
                stack.append(hit)
    if unresolved:
        sys.exit(f"unresolved libraries: {unresolved}")

    for jar in BOOT_JARS:
        put(os.path.join(rootfs, f"usr/lib/java/dex/art/{jar}-hostdex.jar"), f"usr/lib/java/dex/art/{jar}-hostdex.jar")
    icu = os.path.join(rootfs, "usr/share/icu")
    for ver in os.listdir(icu):
        for f in os.listdir(os.path.join(icu, ver)):
            if f.endswith(".dat"):
                put(os.path.join(icu, ver, f), f"usr/share/icu/{ver}/{f}")
        with open(os.path.join(out, "ICU_VERSION"), "w") as fh:
            fh.write(ver + "\n")
    put(os.path.join(native, f"relaunch.{arch}"), "bin/dex2oat")
    if arch == "aarch64":  # preloaded by runtime_launcher.cpp: ART low-4GB allocator fix (native/low4g.c)
        put(os.path.join(native, "low4g.so.aarch64"), "lib/low4g.so")
    put(os.path.join(HERE, "trust", "cacerts"), "trust/cacerts")
    if arch == "armv7":  # the legacy package's bundled libcurl verifies against it (runtime/make-trust-pem.sh)
        put(os.path.join(HERE, "trust", "ca-bundle.pem"), "trust/ca-bundle.pem")
        put(os.path.join(native, "libcsweb_legacy_math.so.armv7"),
            "usr/lib/java/dex/art/natives/libcsweb_legacy_math.so")
    put(os.path.join(HERE, "out", "csweb-host.dex"), "csweb/csweb-host.dex")

    apk = os.path.join(PROJECT, "evidence", "cloudstream-app", "app-prerelease-release.apk")
    jar = os.path.join(out, "csweb", "cloudstream.jar")
    repack_classpath_jar(apk, jar)

    # Licence notices (runtime/tools/notices.py; checked against this dist by `notices.py check`).
    # They describe the aarch64 runtime that ships in the TV package; the x86_64 one is for host runs. The
    # legacy package's armv7 runtime has its own (runtime/licenses-armv7/), copied once they are generated.
    lic = os.path.join(HERE, "licenses" if arch == "aarch64" else f"licenses-{arch}")
    if arch == "aarch64" or (arch == "armv7" and os.path.exists(os.path.join(lic, "packages.tsv"))):
        put(os.path.join(lic, "NOTICES.md"), "licenses/NOTICES.md")
        put(os.path.join(lic, "packages.tsv"), "licenses/packages.tsv")
        for origin in sorted(os.listdir(os.path.join(lic, "texts"))):
            for name in sorted(os.listdir(os.path.join(lic, "texts", origin))):
                put(os.path.join(lic, "texts", origin, name), f"licenses/texts/{origin}/{name}")

    write_manifest(out, {"arch": arch, "alpine_packages": open(os.path.join(HERE, f"rootfs-{arch}.manifest")).read().split(),
                         "cloudstream_apk_sha256": sha256(apk)})


def write_manifest(out, base):
    files = []
    for d, _, names in os.walk(out):
        for n in names:
            p = os.path.join(d, n)
            if os.path.relpath(p, out) != "MANIFEST.json":
                files.append({"path": os.path.relpath(p, out), "size": os.path.getsize(p), "sha256": sha256(p)})
    files.sort(key=lambda f: f["path"])
    manifest = dict(base, total_bytes=sum(f["size"] for f in files), files=files)
    aot = os.path.join(out, "AOT.json")
    manifest["aot"] = json.load(open(aot)) if os.path.exists(aot) else None
    with open(os.path.join(out, "MANIFEST.json"), "w") as f:
        json.dump(manifest, f, indent=1)
    print(f"{base['arch']}: {len(files)} files, {manifest['total_bytes'] / 2**20:.1f} MiB -> {out}")


def rewrite_manifest(out):
    old = json.load(open(os.path.join(out, "MANIFEST.json")))
    write_manifest(out, {k: old[k] for k in ("arch", "alpine_packages", "cloudstream_apk_sha256")})


if __name__ == "__main__":
    if len(sys.argv) == 3 and sys.argv[1] == "--manifest":
        rewrite_manifest(sys.argv[2])
    elif len(sys.argv) == 4:
        main(*sys.argv[1:])
    else:
        sys.exit(__doc__)
