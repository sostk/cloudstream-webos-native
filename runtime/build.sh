#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Build the runtime's own DEX (Android compat layer + csloader) inside alpine:edge with OpenJDK 17 and D8.
#   docker run --rm -v <project>:/p alpine:edge sh /p/runtime/build.sh
# Output: /p/runtime/out/csweb-host.dex, /p/runtime/out/BUILD-INFO.txt
# Compile classpath: upstream CloudStream classes.jar (pinned in evidence/cloudstream-app/SHA256SUMS) and
# the Maven jars below; none of them are packaged by this step (the APK supplies them at run time).
set -eu
P=/p
OUT=$P/runtime/out
apk add -q openjdk17-jdk curl >/dev/null 2>&1
JAVA_HOME=/usr/lib/jvm/java-17-openjdk
M=https://repo1.maven.org/maven2
mkdir -p /tmp/lib /tmp/cls /tmp/stubcls "$OUT"
# Every download is checked against the SHA-256 recorded when the runtime was tested.
get() { curl -sfL -o "$2" "$3" && echo "$1  $2" | sha256sum -c - >/dev/null || { echo "build.sh: $3: bad download" >&2; exit 1; }; }
fetch() { get "$1" "/tmp/lib/$(basename "$2")" "$M/$2"; }
fetch 65d12d85a3b865c160db9147851712a64b10dadd68b22eea22a95bf8a8670dca org/jetbrains/kotlin/kotlin-stdlib/2.2.0/kotlin-stdlib-2.2.0.jar
fetch 5ca175b38df331fd64155b35cd8cae1251fa9ee369709b36d42e0a288ccce3fd org/jetbrains/kotlinx/kotlinx-coroutines-core-jvm/1.10.2/kotlinx-coroutines-core-jvm-1.10.2.jar
fetch 56cfbdc9e1736b5c56b43757f6cf546ee6d49393c79383c4e495c4f7047cb506 com/fasterxml/jackson/core/jackson-databind/2.13.1/jackson-databind-2.13.1.jar
fetch 8be6935cd8f9673ac684a589aaa1caecd57dee7c37ed1443d17924325799003d com/fasterxml/jackson/core/jackson-core/2.13.1/jackson-core-2.13.1.jar
fetch ab607939d288e22bff4cb97acc6b25e841d82681f2bc40ce67bb35cb0ffdb06d com/fasterxml/jackson/core/jackson-annotations/2.13.1/jackson-annotations-2.13.1.jar
R8=8.5.35
get 4733945987ee0a840fafc34080b135259e01678412e07212b23f706334290294 /tmp/r8.jar \
    https://dl.google.com/android/maven2/com/android/tools/r8/$R8/r8-$R8.jar
CP=$P/evidence/cloudstream-app/classes.jar:$(ls /tmp/lib/*.jar | tr '\n' ':')
# compile-only stubs first, then the shipped sources against them
CSLOADER_SRC=$P/runtime/csloader/src
if [ "${CSWEB_LEGACY_LOAD_TRACE:-0}" = 1 ]; then
    apk add -q python3 >/dev/null 2>&1
    CSLOADER_SRC=/tmp/csweb-legacy-csloader
    python3 "$P/tools/instrument-legacy-loader.py" "$P/runtime/csloader/src" "$CSLOADER_SRC"
fi
# The stubs may name compat types (fragment signatures use android.view.View): those come from the
# compat sources, which -implicit:none keeps out of the stub output.
javac -nowarn --release 8 -cp "$CP" -sourcepath $P/runtime/android-compat/src:$P/runtime/compile-stubs -implicit:none \
    -d /tmp/stubcls $(find $P/runtime/compile-stubs -name '*.java')
javac -Xlint:all,-options,-processing,-classfile --release 8 -cp "$CP:/tmp/stubcls" -d /tmp/cls \
    $(find $P/runtime/android-compat/src "$CSLOADER_SRC" -name '*.java')
# host tests of the compat layer on the JVM (no Android needed for these classes)
mkdir -p /tmp/testcls
javac -nowarn --release 8 -cp /tmp/cls:/tmp/stubcls -d /tmp/testcls $P/runtime/android-compat/test/*.java
java -Dconformance=$P/test-plugins/conformance/out/CswebConformance.cs3 -cp /tmp/cls:/tmp/testcls:/tmp/stubcls CompatTest
rm -f /tmp/dex/*; mkdir -p /tmp/dex
java -cp /tmp/r8.jar com.android.tools.r8.D8 --min-api 26 --lib "$JAVA_HOME" --classpath $P/evidence/cloudstream-app/classes.jar \
    $(for j in /tmp/lib/*.jar; do printf -- '--classpath %s ' "$j"; done) --classpath /tmp/stubcls \
    --output /tmp/dex $(find /tmp/cls -name '*.class')
cp /tmp/dex/classes.dex "$OUT/csweb-host.dex"
{
  echo "built $(date -u +%FT%TZ) in alpine:edge, $(java -version 2>&1 | head -1), D8 $R8"
  sha256sum /tmp/r8.jar /tmp/lib/*.jar $P/evidence/cloudstream-app/classes.jar | sed "s#$P/##"
  echo "csweb-host.dex $(sha256sum "$OUT/csweb-host.dex" | cut -d' ' -f1)"
} > "$OUT/BUILD-INFO.txt"
cat "$OUT/BUILD-INFO.txt"
