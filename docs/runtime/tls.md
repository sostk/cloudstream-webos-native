# TLS in the plugin runtime

Status 2026-10-19.

## How plugins reach HTTPS

CloudStream's HTTP client (OkHttp inside `cloudstream.jar`) runs on ART's libcore, whose JSSE provider in
this runtime is wolfSSL JNI (`wolfssljni-hostdex.jar` + `libwolfssljni.so` → `libwolfssl.so.46`, Alpine
packages). Trust anchors: `trust/cacerts`, from Alpine's `ca-certificates` (Mozilla). Certificate,
date and host name checks are on; the runtime never disables verification.

## The archive.org failure and the fix

archive.org (and the LibriVox and InternetArchive plugins that use it) failed every handshake with
`SSLHandshakeException: ASN parsing error, invalid input (-140)`. Cause, reproduced with a plain C
wolfSSL client (`evidence/runtime-host/tls/20261019-wolfssl-allow-0-serial.txt`): archive.org sends,
after a chain that verifies through "Go Daddy Root Certificate Authority - G2" (in the trust store), the
2004 self-signed "Go Daddy Class 2 Certification Authority" root, whose serial number is 0. RFC 5280
requires positive serials and wolfSSL's strict ASN.1 code rejects a zero serial in a peer's chain as a
parse error, which ends the handshake before an alternative path is considered. OpenSSL and Android's
Conscrypt accept the chain.

`runtime/native/build-wolfssl.sh` rebuilds the same wolfSSL version with Alpine's configuration for the
JNI variant plus one define, `WOLFSSL_ASN_ALLOW_0_SERIAL` ("allow a length=1, but zero value serial
number", wolfcrypt/src/asn.c). It changes parsing only: signatures, validity dates, host names and the
trust anchors are checked as before (expired, self-signed and wrong-host test sites are still refused,
on x86_64 and on aarch64 under qemu). The library exports the same 3194 symbols as Alpine's and needs
only libc. `runtime/make_dist.py` puts it in place of Alpine's `libwolfssl.so.46` (as it does for the
EGL stub) and refuses to build a dist without it.

Build order: `runtime/make-rootfs.sh` → `runtime/native/build-native.sh` → `runtime/native/build-wolfssl.sh`
→ `runtime/make_dist.py` → `runtime/make-aot.sh` → `make_dist.py --manifest`.

Licence: wolfSSL is GPL-3.0-or-later (or commercial); the app is GPL-3.0-or-later. The source is the
upstream release tarball, fetched by URL and checked against Alpine's SHA-512.

## Evidence

Host: InternetArchive search through the runtime service now returns 26 results in 1.3 s
(evidence file above). TV: not yet run with this library.

## Concurrent handshakes and the client session cache (2026-11-09)

Seen on the TV: CineVood's home page failed with `SSLPeerUnverifiedException: Hostname cinevood.net not
verified (no certificates)` while the site answered normally from a PC. Reproduced on the host runtime
(x86_64) with a probe of our own, no plugin code: N threads open TLS connections to one host at once through
one SSLContext (as OkHttp's fast fallback and plugins' parallel requests do).

| wolfJSSE client session cache | concurrent handshakes with peer certificates |
|---|---|
| default (size 10), cinevood.net / github.com | 12 / 36 and 10 / 36; the rest had none, some failed "Object has been freed" |
| size 0 | 36 / 36 (cinevood.net, github.com; twice each) |

Sequential reconnects resume sessions correctly (peer certificates present). wolfJSSE has no property for
the cache, and OkHttp creates an SSLContext per client, so `org.csweb.compat.TlsSessions` (installed first
thing by `Main.initApp`) places a provider before wolfJSSE that hands out wolfJSSE's own SSLContexts with
the client session cache size set to 0. Each handshake is then a full one (one more round trip per new
connection; OkHttp's connection pool still reuses connections). Protocols, cipher suites and certificate
checks are wolfJSSE's: with the wrapper, expired.badssl.com and self-signed.badssl.com are still refused.
Through CloudStream's `app.baseClient`, 12 parallel requests to cinevood.net all completed TLS.

## Known issue: closing some unread responses crashes wolfSSL (open)

On the host runtime, `GET https://thedaddy.li/` (about 1.3 MB, HTTP/1.1) followed by closing the response
without reading the body ends in SIGSEGV in `wolfSSL_read` (libwolfssljni → libwolfssl), with or without
the wrapper above; reading the body first and then closing works. Not reproduced with
speed.cloudflare.com downloads of 20 KB or 5 MB closed unread. CloudStream plugins normally read bodies
(`app.get(...).text`/`document`), which works. To investigate: OkHttp's discard-on-close of an HTTP/1.1
body (short read timeout, then socket close) against wolfJSSE's read after close.
