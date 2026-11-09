"""Stage the files headless Chromium needs into out/stage/ as plain files (no symlinks: the TV's USB
drive is FAT): the browser directory, its DT_NEEDED closure under the names it asks for, the NSS/p11-kit
modules NSS loads at run time, CA certificates, and the musl loader next to the browser (Chromium finds
its resources beside /proc/self/exe, which is the loader)."""
import os, re, shutil, subprocess
R = "out/rootfs-aarch64"
OUT = "out/stage/csweb-chromium-test"
LIBDIRS = [R + "/usr/lib", R + "/lib", R + "/usr/lib/chromium"]

def needed(path):
    out = subprocess.run(["readelf", "-d", path], capture_output=True, text=True).stdout
    return re.findall(r"\(NEEDED\).*\[(.+)\]", out)

def find(name):
    for d in LIBDIRS:
        p = os.path.join(d, name)
        if os.path.exists(p):
            return p
    return None

if os.path.exists("out/stage"):
    shutil.rmtree("out/stage")
lib = os.path.join(OUT, "lib")
os.makedirs(lib)
# Run-time loaded by NSS / p11-kit (not in DT_NEEDED).
extra = [n for n in os.listdir(R + "/usr/lib") if re.match(r"lib(nss|softokn|freebl|nssckbi|p11-kit)", n)]
roots = [R + "/usr/lib/chromium/chromium", R + "/usr/lib/chromium/chrome_crashpad_handler"] + \
        [R + "/usr/lib/" + n for n in extra]
if os.path.isdir(R + "/usr/lib/pkcs11"):
    roots += [R + "/usr/lib/pkcs11/" + n for n in os.listdir(R + "/usr/lib/pkcs11")]
seen, todo, missing = set(), list(roots), set()
while todo:
    p = todo.pop()
    if p in seen or not os.path.isfile(p):
        continue
    seen.add(p)
    for n in needed(os.path.realpath(p)):
        q = find(n)
        if q:
            todo.append(q)
            if not q.startswith(R + "/usr/lib/chromium/"):
                shutil.copyfile(os.path.realpath(q), os.path.join(lib, n))
        else:
            missing.add(n)
for n in extra:
    shutil.copyfile(os.path.realpath(R + "/usr/lib/" + n), os.path.join(lib, n))
if os.path.isdir(R + "/usr/lib/pkcs11"):
    os.makedirs(os.path.join(lib, "pkcs11"))
    for n in os.listdir(R + "/usr/lib/pkcs11"):
        shutil.copyfile(os.path.realpath(R + "/usr/lib/pkcs11/" + n), os.path.join(lib, "pkcs11", n))
browser = os.path.join(OUT, "chromium")
shutil.copytree(R + "/usr/lib/chromium", browser, symlinks=False)
shutil.copyfile(os.path.realpath(R + "/lib/ld-musl-aarch64.so.1"), os.path.join(browser, "ld-musl-aarch64.so.1"))
# CA certificates: NSS's built-in roots (libnssckbi) when p11-kit's trust module is absent; Alpine's
# chromium does not pull in ca-certificates, so copy the bundle only if the root has one.
ca = R + "/etc/ssl/certs/ca-certificates.crt"
if os.path.exists(ca):
    os.makedirs(os.path.join(OUT, "etc/ssl/certs"))
    shutil.copyfile(os.path.realpath(ca), os.path.join(OUT, "etc/ssl/certs/ca-certificates.crt"))
with open(os.path.join(OUT, "run.sh"), "w") as f:
    f.write('''#!/bin/sh
# Starts the bundled Chromium through its own musl loader. Also used as --browser-subprocess-path, so
# Chromium's helper processes start the same way.
D=$(cd "$(dirname "$0")" && pwd)
exec "$D/chromium/ld-musl-aarch64.so.1" --library-path "$D/lib:$D/chromium" "$D/chromium/chromium" "$@"
''')
total = sum(os.path.getsize(os.path.join(dp, f)) for dp, _, fs in os.walk(OUT) for f in fs)
print("missing:", sorted(missing))
print("staged files:", sum(len(fs) for _, _, fs in os.walk(OUT)), "size MiB: %.0f" % (total / 1048576))
